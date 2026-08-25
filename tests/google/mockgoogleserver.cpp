#include "mockgoogleserver.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace Kalburator::Google {

// Per-socket partial-request buffers (browsers/clients may deliver a
// request across several readyRead ticks).
namespace {
thread_local QHash<QTcpSocket *, QByteArray> g_buffers;

QJsonObject errorBody(int code, const QString &reason, const QString &message)
{
    QJsonArray errors;
    errors.append(QJsonObject{ { QStringLiteral("reason"), reason },
                               { QStringLiteral("message"), message } });
    return QJsonObject{
        { QStringLiteral("error"),
          QJsonObject{ { QStringLiteral("code"), code },
                       { QStringLiteral("errors"), errors },
                       { QStringLiteral("message"), message } } }
    };
}
} // namespace

MockGoogleServer::MockGoogleServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this,
            &MockGoogleServer::handleConnection);
}

MockGoogleServer::~MockGoogleServer()
{
    stop();
}

bool MockGoogleServer::start()
{
    return m_server.listen(QHostAddress::LocalHost);
}

void MockGoogleServer::stop()
{
    if (m_server.isListening())
        m_server.close();
}

quint16 MockGoogleServer::port() const
{
    return m_server.serverPort();
}

QString MockGoogleServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port());
}

void MockGoogleServer::setCalendarList(const QJsonArray &calendars)
{
    m_calendarList = calendars;
}

void MockGoogleServer::setEvents(const QString &calendarId,
                                 const QJsonArray &events)
{
    m_events.insert(calendarId, events);
}

void MockGoogleServer::queueSyncChanges(const QString &calendarId,
                                        const QString &syncToken,
                                        const QJsonArray &changedItems)
{
    // The queued changes are delivered when the token AFTER this one is
    // presented? No — Google semantics: presenting token T delivers the
    // changes recorded SINCE T. We model: presenting the CURRENT live
    // token returns queued items and advances to a fresh live token.
    // So changes are keyed by the token they will be served WITH.
    const QString servingToken = m_currentSyncToken.value(calendarId);
    Q_UNUSED(syncToken);
    m_queuedChanges.insert(calendarId + QLatin1Char('|') + servingToken,
                           changedItems);
}

void MockGoogleServer::expireSyncTokens(const QString &calendarId)
{
    m_currentSyncToken.remove(calendarId);
    for (auto it = m_queuedChanges.begin(); it != m_queuedChanges.end();) {
        if (it.key().startsWith(calendarId + QLatin1Char('|')))
            it = m_queuedChanges.erase(it);
        else
            ++it;
    }
}

void MockGoogleServer::addRoute(const QByteArray &method, const QString &path,
                                const QByteArray &body, int status)
{
    m_routes.append({ method, path, body, status });
}

QList<MockGoogleServer::RecordedRequest> MockGoogleServer::requests() const
{
    return m_requests;
}

void MockGoogleServer::clearRequests()
{
    m_requests.clear();
}

void MockGoogleServer::handleConnection()
{
    while (QTcpSocket *sock = m_server.nextPendingConnection()) {
        g_buffers.insert(sock, {});
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            QByteArray &buf = g_buffers[sock];
            buf += sock->readAll();

            const int headerEnd = buf.indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
            if (lines.isEmpty())
                return;
            const QByteArray requestLine = lines.first().trimmed();
            const QList<QByteArray> parts = requestLine.split(' ');
            if (parts.size() < 2)
                return;
            const QByteArray method = parts.at(0);

            int contentLength = 0;
            QByteArray authorization;
            for (const QByteArray &raw : lines.mid(1)) {
                const QByteArray line = raw.trimmed();
                if (line.toLower().startsWith(QByteArray("content-length:")))
                    contentLength = line.mid(15).trimmed().toInt();
                if (line.toLower().startsWith(QByteArray("authorization:")))
                    authorization = line.mid(14).trimmed();
            }
            if (buf.size() < headerEnd + 4 + contentLength)
                return;   // body not complete yet

            RecordedRequest rec;
            rec.method = method;
            rec.body = buf.mid(headerEnd + 4, contentLength);
            rec.authorizationHeader = authorization;
            const QUrl url(QString::fromUtf8(parts.at(1)));
            rec.path = QUrl::fromPercentEncoding(
                url.toString(QUrl::FullyEncoded).toUtf8());
            m_requests.append(rec);
            buf.clear();
            g_buffers.remove(sock);

            const QByteArray response = respond(rec);
            sock->write(response);
            sock->flush();
            sock->disconnectFromHost();
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
    }
}

static QString queryValue(const QString &path, const QString &key)
{
    const int qmark = path.indexOf(QLatin1Char('?'));
    if (qmark < 0)
        return {};
    const QUrlQuery query(path.mid(qmark + 1));
    return query.queryItemValue(key);
}

static QString pathOnly(const QString &path)
{
    const int qmark = path.indexOf(QLatin1Char('?'));
    return qmark < 0 ? path : path.left(qmark);
}

static QByteArray paginate(const QJsonArray &items, const QString &pageToken,
                           int maxResults, const QString &nextSyncToken)
{
    QJsonObject out;
    int start = 0;
    if (!pageToken.isEmpty()) {
        bool ok = false;
        start = pageToken.toInt(&ok);
        if (!ok || start < 0 || start > items.size())
            start = items.size();
    }
    const int count = maxResults > 0
        ? qMin(maxResults, items.size() - start)
        : items.size() - start;
    QJsonArray pageItems;
    for (int i = start; i < start + count && i < items.size(); ++i)
        pageItems.append(items.at(i));
    out.insert(QStringLiteral("items"), pageItems);
    const int nextStart = start + count;
    if (nextStart < items.size())
        out.insert(QStringLiteral("nextPageToken"), QString::number(nextStart));
    if (!nextSyncToken.isEmpty())
        out.insert(QStringLiteral("nextSyncToken"), nextSyncToken);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QByteArray MockGoogleServer::respond(const RecordedRequest &req)
{
    // 1. Exact routes win.
    for (const Route &r : m_routes) {
        if (r.method == req.method && r.path == req.path) {
            QByteArray head = "HTTP/1.1 " + QByteArray::number(r.status)
                + " OK\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(r.body.size()) + "\r\nConnection: close\r\n\r\n";
            return head + r.body;
        }
    }

    const QString p = pathOnly(req.path);

    // 2. calendarList.
    if (req.method == "GET" && p == QLatin1String("/users/me/calendarList")) {
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
               "Content-Length: "
            + QByteArray::number(paginate(m_calendarList,
                                          queryValue(req.path, "pageToken"),
                                          queryValue(req.path, "maxResults").toInt(),
                                          {}).size())
            + "\r\nConnection: close\r\n\r\n"
            + paginate(m_calendarList, queryValue(req.path, "pageToken"),
                       queryValue(req.path, "maxResults").toInt(), {});
    }

    // 3. Events collections: /calendars/<id>/events.
    if (p.startsWith(QLatin1String("/calendars/"))
        && p.endsWith(QLatin1String("/events"))) {
        const QString calId = p.section(QLatin1Char('/'), 2, 2);
        const QJsonArray items = m_events.value(calId);

        if (req.method != "GET") {
            // CRUD on single events: .../events/<id>; creates POST the collection.
            const QString tail = p.mid(QString("/calendars/%1/events").arg(calId).size());
            if (!tail.isEmpty()) {
                const QString eventId = tail.mid(1);   // strip '/'
                if (req.method == "PATCH") {
                    for (int i = 0; i < items.size(); ++i) {
                        if (items.at(i).toObject().value("id").toString() == eventId) {
                            QJsonObject updated = items.at(i).toObject();
                            const QJsonObject patch =
                                QJsonDocument::fromJson(req.body).object();
                            for (auto it = patch.begin(); it != patch.end(); ++it)
                                updated.insert(it.key(), it.value());
                            const QByteArray body =
                                QJsonDocument(updated).toJson(QJsonDocument::Compact);
                            return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                   "Content-Length: "
                                + QByteArray::number(body.size())
                                + "\r\nConnection: close\r\n\r\n" + body;
                        }
                    }
                    const QByteArray body = QJsonDocument(errorBody(
                        404, QStringLiteral("notFound"),
                        QStringLiteral("Event not found"))).toJson(QJsonDocument::Compact);
                    return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
                           "Content-Length: "
                        + QByteArray::number(body.size())
                        + "\r\nConnection: close\r\n\r\n" + body;
                }
                if (req.method == "DELETE") {
                    bool found = false;
                    for (int i = 0; i < items.size(); ++i) {
                        if (items.at(i).toObject().value("id").toString() == eventId)
                            found = true;
                    }
                    if (!found) {
                        // Idempotent delete semantics: Google answers 410 Gone.
                        return "HTTP/1.1 410 Gone\r\nContent-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
                    }
                    return "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
                }
            }
            if (req.method == "POST") {
                const QJsonObject posted =
                    QJsonDocument::fromJson(req.body).object();
                // O67(b)(1): Google REJECTS read-only created/updated.
                if (posted.contains(QStringLiteral("created"))
                    || posted.contains(QStringLiteral("updated"))) {
                    const QByteArray body = QJsonDocument(errorBody(
                        400, QStringLiteral("invalid"),
                        QStringLiteral("Bad Request"))).toJson(QJsonDocument::Compact);
                    return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n"
                           "Content-Length: "
                        + QByteArray::number(body.size())
                        + "\r\nConnection: close\r\n\r\n" + body;
                }
                // Mint transport id, honor client iCalUID (O67(b)(4)),
                // rewrite organizer to the authenticated account (O67(b)(2)).
                QJsonObject created = posted;
                static qint64 counter = 424242;
                created.insert(QStringLiteral("id"),
                               QStringLiteral("mockevt%1").arg(++counter));
                created.insert(QStringLiteral("status"),
                               QStringLiteral("confirmed"));
                if (created.contains(QStringLiteral("organizer"))) {
                    created.insert(QStringLiteral("organizer"),
                                   QJsonObject{
                                       { QStringLiteral("email"),
                                         QStringLiteral("tester@example.com") },
                                       { QStringLiteral("self"), true } });
                }
                const QByteArray body =
                    QJsonDocument(created).toJson(QJsonDocument::Compact);
                return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                       "Content-Length: "
                    + QByteArray::number(body.size())
                    + "\r\nConnection: close\r\n\r\n" + body;
            }
        }

        // GET listing with sync-token semantics.
        const QString syncToken = queryValue(req.path, "syncToken");
        QString nextSyncToken;
        QJsonArray effective = items;
        if (syncToken.isEmpty()) {
            // Initial full listing.
            nextSyncToken = QStringLiteral("sync_%1").arg(++m_syncTokenCounter);
            m_currentSyncToken.insert(calId, nextSyncToken);
        } else if (m_currentSyncToken.value(calId) == syncToken) {
            // Valid incremental replay: ONLY changed items are delivered
            // (real Google semantics — never the full set again).
            effective = m_queuedChanges.take(calId + QLatin1Char('|') + syncToken);
            nextSyncToken = QStringLiteral("sync_%1").arg(++m_syncTokenCounter);
            m_currentSyncToken.insert(calId, nextSyncToken);
        } else {
            // Unknown/expired token ⇒ 410 Gone full-resync signal.
            const QByteArray body = QJsonDocument(errorBody(
                410, QStringLiteral("gone"),
                QStringLiteral("Sync token no longer valid"))).toJson(
                QJsonDocument::Compact);
            return "HTTP/1.1 410 Gone\r\nContent-Type: application/json\r\n"
                   "Content-Length: "
                + QByteArray::number(body.size())
                + "\r\nConnection: close\r\n\r\n" + body;
        }
        const QByteArray body = paginate(effective,
                                         queryValue(req.path, "pageToken"),
                                         queryValue(req.path, "maxResults").toInt(),
                                         nextSyncToken);
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
               "Content-Length: "
            + QByteArray::number(body.size())
            + "\r\nConnection: close\r\n\r\n" + body;
    }

    // 4. Unmatched ⇒ Google 404 shape.
    const QByteArray body = QJsonDocument(errorBody(
        404, QStringLiteral("notFound"),
        QStringLiteral("Not Found"))).toJson(QJsonDocument::Compact);
    return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
           "Content-Length: "
        + QByteArray::number(body.size())
        + "\r\nConnection: close\r\n\r\n" + body;
}

} // namespace Kalburator::Google
