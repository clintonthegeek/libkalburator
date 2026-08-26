#include "mockpeopleserver.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace Kalburator::People {

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

HttpResponse httpRequest(const QUrl &url,
                         const QByteArray &method,
                         const QList<QPair<QByteArray, QByteArray>> &headers,
                         const QByteArray &body)
{
    HttpResponse response;

    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setTransferTimeout(60'000);
    for (const auto &[name, value] : headers)
        request.setRawHeader(name, value);

    QNetworkReply *reply = nam.sendCustomRequest(request, method, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(120'000, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus == 0)
            response.error = reply->errorString();
        response.status = httpStatus;
    } else {
        response.status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    response.body = reply->readAll();
    reply->deleteLater();
    return response;
}

MockPeopleServer::MockPeopleServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this,
            &MockPeopleServer::handleConnection);
}

MockPeopleServer::~MockPeopleServer()
{
    stop();
}

bool MockPeopleServer::start()
{
    return m_server.listen(QHostAddress::LocalHost);
}

void MockPeopleServer::stop()
{
    if (m_server.isListening())
        m_server.close();
}

quint16 MockPeopleServer::port() const
{
    return m_server.serverPort();
}

QString MockPeopleServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port());
}

void MockPeopleServer::setConnections(const QJsonArray &people)
{
    m_connections = people;
}

void MockPeopleServer::queueConnectionChanges(const QString &syncToken,
                                              const QJsonArray &changedPeople)
{
    // Changes are keyed by the live token they will be served WITH
    // (presenting token T delivers changes recorded since T).
    Q_UNUSED(syncToken);
    m_queuedChanges.insert(m_currentSyncToken, changedPeople);
}

void MockPeopleServer::expireSyncTokens()
{
    m_currentSyncToken.clear();
    m_queuedChanges.clear();
}

void MockPeopleServer::addRoute(const QByteArray &method, const QString &path,
                                const QByteArray &body, int status)
{
    m_routes.append({ method, path, body, status });
}

QList<MockPeopleServer::RecordedRequest> MockPeopleServer::requests() const
{
    return m_requests;
}

void MockPeopleServer::clearRequests()
{
    m_requests.clear();
}

void MockPeopleServer::handleConnection()
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
                return;

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

static bool hasQueryItem(const QString &path, const QString &key)
{
    const int qmark = path.indexOf(QLatin1Char('?'));
    if (qmark < 0)
        return false;
    const QUrlQuery query(path.mid(qmark + 1));
    return query.hasQueryItem(key);
}

static QString pathOnly(const QString &path)
{
    const int qmark = path.indexOf(QLatin1Char('?'));
    return qmark < 0 ? path : path.left(qmark);
}

static QByteArray paginateConnections(const QJsonArray &people,
                                      const QString &pageToken, int pageSize,
                                      const QString &nextSyncToken)
{
    QJsonObject out;
    int start = 0;
    if (!pageToken.isEmpty()) {
        bool ok = false;
        start = pageToken.toInt(&ok);
        if (!ok || start < 0 || start > people.size())
            start = people.size();
    }
    const int count =
        pageSize > 0 ? qMin(pageSize, people.size() - start)
                     : people.size() - start;
    QJsonArray pageItems;
    for (int i = start; i < start + count && i < people.size(); ++i)
        pageItems.append(people.at(i));
    out.insert(QStringLiteral("connections"), pageItems);
    const int nextStart = start + count;
    if (nextStart < people.size())
        out.insert(QStringLiteral("nextPageToken"), QString::number(nextStart));
    if (!nextSyncToken.isEmpty())
        out.insert(QStringLiteral("nextSyncToken"), nextSyncToken);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// Project each person to ONLY the requested fields (plus the always-on
// resourceName and etag/metadata when present).
static void projectPersons(QJsonArray &people, const QString &personFields)
{
    QStringList wanted;
    for (const auto &f : personFields.split(QLatin1Char(','), Qt::SkipEmptyParts))
        wanted << f.trimmed();

    for (int i = 0; i < people.size(); ++i) {
        const QJsonObject src = people.at(i).toObject();
        QJsonObject out;
        out.insert(QStringLiteral("resourceName"),
                   src.value(QStringLiteral("resourceName")));
        if (src.contains(QStringLiteral("etag")))
            out.insert(QStringLiteral("etag"),
                       src.value(QStringLiteral("etag")));
        if (src.contains(QStringLiteral("metadata")))
            out.insert(QStringLiteral("metadata"),
                       src.value(QStringLiteral("metadata")));
        for (const QString &field : wanted) {
            if (src.contains(field))
                out.insert(field, src.value(field));
        }
        people.replace(i, out);
    }
}

QByteArray MockPeopleServer::makeReply(int status, const QByteArray &body) const
{
    const QByteArray reason =
        status == 200 ? "OK"
                      : status == 201 ? "Created"
                                      : status == 204 ? "No Content"
                                                      : status == 400
                                                          ? "Bad Request"
                                                          : status == 403
                                                              ? "Forbidden"
                                                              : status == 404
                                                                  ? "Not Found"
                                                                  : status == 410
                                                                      ? "Gone"
                                                                      : status == 500
                                                                          ? "Internal Server Error"
                                                                          : "Error";
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reason
        + "\r\nContent-Type: application/json\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    return head + body;
}

int findPersonIndex(const QJsonArray &connections,
                    const QString &fullResourceName)
{
    for (int i = 0; i < connections.size(); ++i) {
        if (connections.at(i).toObject()
                .value(QStringLiteral("resourceName")).toString()
            == fullResourceName)
            return i;
    }
    return -1;
}

QByteArray MockPeopleServer::respond(const RecordedRequest &req)
{
    // 1. Exact routes win.
    for (const Route &r : m_routes) {
        if (r.method == req.method && r.path == req.path)
            return makeReply(r.status, r.body);
    }

    const QString p = pathOnly(req.path);

    // 2. Connections listing.
    if (req.method == "GET" && p == QLatin1String("/v1/people/me/connections")) {
        const QString syncToken = queryValue(req.path, QStringLiteral("sync_token"));
        const bool wantSyncToken =
            queryValue(req.path, QStringLiteral("requestSyncToken"))
            == QLatin1String("true");

        QJsonArray effective;
        QString nextSyncToken;
        if (syncToken.isEmpty()) {
            effective = m_connections;
            if (wantSyncToken) {
                nextSyncToken =
                    QStringLiteral("sync_%1").arg(++m_syncTokenCounter);
                m_currentSyncToken = nextSyncToken;
            }
        } else if (m_currentSyncToken == syncToken) {
            effective = m_queuedChanges.take(syncToken);
            nextSyncToken = QStringLiteral("sync_%1").arg(++m_syncTokenCounter);
            m_currentSyncToken = nextSyncToken;
        } else {
            return makeReply(
                410, QJsonDocument(errorBody(
                         410, QStringLiteral("gone"),
                         QStringLiteral("Sync token no longer valid")))
                        .toJson(QJsonDocument::Compact));
        }

        const QString personFields =
            queryValue(req.path, QStringLiteral("personFields"));
        if (hasQueryItem(req.path, QStringLiteral("personFields")))
            projectPersons(effective, personFields);

        return makeReply(
            200, paginateConnections(effective,
                                     queryValue(req.path,
                                                QStringLiteral("pageToken")),
                                     queryValue(req.path,
                                                QStringLiteral("pageSize"))
                                         .toInt(),
                                     nextSyncToken));
    }

    // 3. createContact: mint resourceName, store, echo body verbatim +
    //    resourceName (clientData rows pass through untouched).
    if (req.method == "POST" && p == QLatin1String("/v1/people/me:createContact")) {
        QJsonObject created = QJsonDocument::fromJson(req.body).object();
        created.insert(QStringLiteral("resourceName"),
                       QStringLiteral("people/c%1").arg(++m_personCounter));
        m_connections.append(created);
        return makeReply(200,
                         QJsonDocument(created).toJson(QJsonDocument::Compact));
    }

    // 4. updateContact: merge ONLY the masked top-level fields.
    if (req.method == "PATCH" && p.startsWith(QLatin1String("/v1/people/"))
        && p.endsWith(QLatin1String(":updateContact"))) {
        const QString rn = p.mid(int(qstrlen("/v1/people/")),
                                p.size() - int(qstrlen("/v1/people/"))
                                    - int(qstrlen(":updateContact")));
        const int idx = findPersonIndex(m_connections,
                                        QStringLiteral("people/") + rn);
        if (idx < 0)
            return makeReply(404,
                             QJsonDocument(errorBody(
                                 404, QStringLiteral("notFound"),
                                 QStringLiteral("Person not found")))
                                 .toJson(QJsonDocument::Compact));

        const QJsonObject patch =
            QJsonDocument::fromJson(req.body).object();
        QJsonObject merged = m_connections.at(idx).toObject();
        const QString mask =
            queryValue(req.path, QStringLiteral("updatePersonFields"));
        for (const auto &field : mask.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString name = field.trimmed();
            if (patch.contains(name))
                merged.insert(name, patch.value(name));
        }
        m_connections.replace(idx, merged);
        return makeReply(200,
                         QJsonDocument(merged).toJson(QJsonDocument::Compact));
    }

    // 5. deleteContact.
    if (req.method == "DELETE" && p.startsWith(QLatin1String("/v1/people/"))
        && p.endsWith(QLatin1String(":deleteContact"))) {
        const QString rn = p.mid(int(qstrlen("/v1/people/")),
                                 p.size() - int(qstrlen("/v1/people/"))
                                     - int(qstrlen(":deleteContact")));
        const int idx = findPersonIndex(m_connections,
                                        QStringLiteral("people/") + rn);
        if (idx < 0)
            return makeReply(404,
                             QJsonDocument(errorBody(
                                 404, QStringLiteral("notFound"),
                                 QStringLiteral("Person not found")))
                                 .toJson(QJsonDocument::Compact));
        m_connections.removeAt(idx);
        return makeReply(200, {});
    }

    // 6. Unmatched ⇒ Google 404 shape.
    return makeReply(404,
                     QJsonDocument(errorBody(
                         404, QStringLiteral("notFound"),
                         QStringLiteral("Not Found")))
                         .toJson(QJsonDocument::Compact));
}

} // namespace Kalburator::People
