#include "mockgraphserver.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace Kalburator::Graph {

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

MockGraphServer::MockGraphServer(QObject *parent)
    : QObject(parent)
{
}

MockGraphServer::~MockGraphServer()
{
    stop();
}

bool MockGraphServer::start()
{
    if (m_server)
        return true;
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_port = m_server->serverPort();
    connect(m_server, &QTcpServer::newConnection,
            this, &MockGraphServer::onNewConnection);
    return true;
}

void MockGraphServer::stop()
{
    if (!m_server)
        return;
    m_server->close();
    delete m_server;
    m_server = nullptr;
}

quint16 MockGraphServer::port() const
{
    return m_port;
}

QString MockGraphServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
}

MockGraphServer::Collection *MockGraphServer::findCollection(const QString &path)
{
    for (auto &c : m_collections)
        if (c.path == path)
            return &c;
    return nullptr;
}

void MockGraphServer::addCollection(const QString &path, const QJsonArray &items)
{
    Collection c;
    c.path = path;
    c.items = items;
    m_collections.append(c);
}

void MockGraphServer::setCollectionItems(const QString &path, const QJsonArray &items)
{
    if (Collection *c = findCollection(path))
        c->items = items;
}

void MockGraphServer::addRoute(const QString &method, const QString &path,
                               const QJsonValue &body, int status)
{
    m_routes.append(Route{ method, path, body, status });
}

void MockGraphServer::queueDeltaChanges(const QString &collectionPath,
                                        const QString &deltaToken,
                                        const QJsonArray &changedItems)
{
    if (Collection *c = findCollection(collectionPath))
        c->deltaQueue.append({ deltaToken, changedItems });
}

void MockGraphServer::invalidateDeltaTokens(const QString &collectionPath)
{
    if (Collection *c = findCollection(collectionPath))
        c->lastDeltaToken.clear();
}

QList<MockGraphServer::RecordedRequest> MockGraphServer::requests() const
{
    return m_requests;
}

void MockGraphServer::clearRequests()
{
    m_requests.clear();
}

void MockGraphServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected,
                socket, &QTcpSocket::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            handleSocket(socket);
        });
    }
}

void MockGraphServer::handleSocket(QTcpSocket *socket)
{
    // Accumulate until the full request (headers + Content-Length body) is in.
    static thread_local QHash<QTcpSocket *, QByteArray> buffers;
    QByteArray &buf = buffers[socket];
    buf += socket->readAll();

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
    for (const QByteArray &raw : lines.mid(1)) {
        const QByteArray line = raw.trimmed();
        if (line.toLower().startsWith(QByteArray("content-length:")))
            contentLength = line.mid(15).trimmed().toInt();
    }
    if (buf.size() < headerEnd + 4 + contentLength)
        return;   // body not complete yet

    const QByteArray body = buf.mid(headerEnd + 4, contentLength);
    const QUrl url(QString::fromUtf8(parts.at(1)));
    // Consume exactly this request (no pipelining support): a follow-up
    // readyRead must never re-parse the same bytes.
    buf.clear();
    RecordedRequest rec;
    rec.method = method;
    rec.path = QUrl::fromPercentEncoding(
        url.toString(QUrl::FullyEncoded).toUtf8());
    rec.body = body;
    m_requests.append(rec);
    buffers.remove(socket);

    const QString path = url.path();
    const QUrlQuery query(url.query());

    // ---- exact routes win -------------------------------------------------
    for (const Route &r : std::as_const(m_routes)) {
        if (r.method == QString::fromUtf8(method) && r.path == path) {
            respond(socket, r.status,
                    QJsonDocument(r.body.toObject().isEmpty()
                                      && r.body.isArray()
                                  ? QJsonDocument(r.body.toArray())
                                  : QJsonDocument(r.body.toObject())).toJson(
                                      QJsonDocument::Indented));
            return;
        }
    }

    // ---- collections: pagination + delta ----------------------------------
    // Delta requests address "<collection>/delta"; resolve back to the
    // collection key for both forms.
    QString basePath = path;
    if (basePath.endsWith(QStringLiteral("/delta")))
        basePath.chop(int(QStringLiteral("/delta").size()));
    if (Collection *c = findCollection(basePath)) {
        const bool isDelta = path.endsWith(QStringLiteral("/delta"))
            || query.hasQueryItem(QStringLiteral("$deltatoken"));

        if (isDelta) {
            const QString token =
                query.queryItemValue(QStringLiteral("$deltatoken"));
            if (!token.isEmpty()) {
                bool known = (token == c->lastDeltaToken);
                QJsonArray changes;
                int queuedIndex = -1;
                for (int i = 0; i < c->deltaQueue.size(); ++i) {
                    if (c->deltaQueue.at(i).first == token) {
                        known = true;
                        queuedIndex = i;
                        changes = c->deltaQueue.at(i).second;
                        break;
                    }
                }
                if (!known) {
                    QJsonObject err;
                    err.insert("error", QJsonObject{
                        { "code", QStringLiteral("ResyncRequired") },
                        { "message", QStringLiteral("The provided delta token is no longer valid.") } });
                    respond(socket, 410,
                            QJsonDocument(err).toJson(QJsonDocument::Indented));
                    return;
                }
                ++c->deltaCounter;
                const QString nextToken = QStringLiteral("delta_%1").arg(c->deltaCounter);
                c->lastDeltaToken = nextToken;
                // Consume the served page so replays don't repeat it
                // (-1 ⇒ token matched only via lastDeltaToken; nothing queued).
                if (queuedIndex >= 0)
                    c->deltaQueue.removeAt(queuedIndex);

                QJsonObject out;
                out.insert("@odata.context", baseUrl()
                    + QStringLiteral("/$metadata#me/calendar/events"));
                out.insert("value", changes);
                if (!changes.isEmpty())
                    out.insert("@odata.nextLink", baseUrl() + path
                        + QStringLiteral("?$deltatoken=") + nextToken);
                else
                    out.insert("@odata.deltaLink", baseUrl() + path
                        + QStringLiteral("?$deltatoken=") + nextToken);
                respond(socket, 200, QJsonDocument(out).toJson(QJsonDocument::Indented));
                return;
            }

            // Initial delta walk: everything, plus a deltaLink.
            ++c->deltaCounter;
            const QString nextToken = QStringLiteral("delta_%1").arg(c->deltaCounter);
            c->lastDeltaToken = nextToken;
            QJsonObject out;
            out.insert("@odata.context", baseUrl()
                + QStringLiteral("/$metadata#me/calendar/events"));
            out.insert("value", c->items);
            out.insert("@odata.deltaLink", baseUrl() + path
                + QStringLiteral("?$deltatoken=") + nextToken);
            respond(socket, 200, QJsonDocument(out).toJson(QJsonDocument::Indented));
            return;
        }

        // Plain pagination walk.
        int top = 10;
        if (query.hasQueryItem(QStringLiteral("$top")))
            top = qMax(1, query.queryItemValue(QStringLiteral("$top")).toInt());
        int skip = 0;
        if (query.hasQueryItem(QStringLiteral("$skip")))
            skip = qMax(0, query.queryItemValue(QStringLiteral("$skip")).toInt());

        QJsonArray page;
        for (int i = skip; i < c->items.size() && page.size() < top; ++i)
            page.append(c->items.at(i));
        const bool hasMore = skip + page.size() < c->items.size();

        QJsonObject out;
        out.insert("@odata.context", baseUrl()
            + QStringLiteral("/$metadata#me/calendar/events"));
        out.insert("value", page);
        if (hasMore) {
            QUrlQuery nextQuery(query);
            nextQuery.removeQueryItem(QStringLiteral("$skip"));
            nextQuery.addQueryItem(QStringLiteral("$skip"),
                                   QString::number(skip + page.size()));
            const QString qs = nextQuery.toString(QUrl::FullyEncoded);
            out.insert("@odata.nextLink",
                       baseUrl() + path
                           + (qs.isEmpty() ? QString() : "?" + qs));
        }
        respond(socket, 200, QJsonDocument(out).toJson(QJsonDocument::Indented));
        return;
    }

    // ---- unmatched → Graph 404 shape ---------------------------------------
    QJsonObject err;
    err.insert("error", QJsonObject{
        { "code", QStringLiteral("ErrorItemNotFound") },
        { "message", QStringLiteral("The specified object was not found in the store.") } });
    respond(socket, 404, QJsonDocument(err).toJson(QJsonDocument::Indented));
}

void MockGraphServer::respond(QTcpSocket *socket, int status,
                              const QByteArray &body,
                              const QList<QPair<QByteArray, QByteArray>> &extraHeaders)
{
    QByteArray head;
    head += "HTTP/1.1 " + QByteArray::number(status)
        + (status == 200 ? " OK" : status == 404 ? " Not Found"
                                                 : status == 410 ? " Gone" : " Error")
        + "\r\n";
    head += "Content-Type: application/json\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const auto &[name, value] : extraHeaders)
        head += name + ": " + value + "\r\n";
    head += "Connection: close\r\n\r\n";
    socket->write(head + body);
    socket->disconnectFromHost();
}

} // namespace Kalburator::Graph
