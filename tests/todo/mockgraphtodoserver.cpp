#include "mockgraphtodoserver.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace Kalburator::Todo {

namespace {
const QString kExtensionPrefix =
    QStringLiteral("microsoft.graph.openTypeExtension.");
}

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

MockGraphTodoServer::MockGraphTodoServer(QObject *parent)
    : QObject(parent)
{
}

MockGraphTodoServer::~MockGraphTodoServer()
{
    stop();
}

bool MockGraphTodoServer::start()
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
            this, &MockGraphTodoServer::onNewConnection);
    return true;
}

void MockGraphTodoServer::stop()
{
    if (!m_server)
        return;
    m_server->close();
    delete m_server;
    m_server = nullptr;
}

quint16 MockGraphTodoServer::port() const
{
    return m_port;
}

QString MockGraphTodoServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
}

void MockGraphTodoServer::setTodoLists(const QJsonArray &lists)
{
    m_lists = lists;
}

MockGraphTodoServer::Collection *
MockGraphTodoServer::findCollection(const QString &path)
{
    for (auto &c : m_collections)
        if (c.path == path)
            return &c;
    return nullptr;
}

bool MockGraphTodoServer::findTask(const QString &id,
                                   Collection **collection, int *index)
{
    for (auto &c : m_collections) {
        for (int i = 0; i < c.items.size(); ++i) {
            if (c.items.at(i).toObject().value(QStringLiteral("id")).toString()
                    == id) {
                *collection = &c;
                *index = i;
                return true;
            }
        }
    }
    return false;
}

void MockGraphTodoServer::addCollection(const QString &path,
                                        const QJsonArray &items)
{
    Collection c;
    c.path = path;
    c.items = items;
    m_collections.append(c);
}

void MockGraphTodoServer::setCollectionItems(const QString &path,
                                             const QJsonArray &items)
{
    if (Collection *c = findCollection(path))
        c->items = items;
}

void MockGraphTodoServer::addRoute(const QString &method, const QString &path,
                                   const QJsonValue &body, int status)
{
    m_routes.append(Route{ method, path, body, status });
}

QList<MockGraphTodoServer::RecordedRequest> MockGraphTodoServer::requests() const
{
    return m_requests;
}

void MockGraphTodoServer::clearRequests()
{
    m_requests.clear();
}

// O60-adjacent trap: QUrlQuery's default PrettyDecoded formatting does NOT
// match %24-encoded keys against "$key" lookups, so resolve query params
// through the fully-decoded item list.
static QString queryParam(const QUrlQuery &query, const QString &key)
{
    const auto items = query.queryItems(QUrl::FullyDecoded);
    for (const auto &[k, v] : items) {
        if (k == key)
            return v;
    }
    return {};
}

// Returns false when the expand filter names an extension id with a wrong
// prefix (O66-correction wire truth ⇒ 500). An expand without a quoted
// filter id is valid and serves every extension row.
static bool resolveExpandFilterId(const QString &expand, QString *filterId)
{
    const int eq = expand.indexOf(QStringLiteral(" eq "));
    if (eq < 0) {
        *filterId = QString();
        return true;
    }
    const int q1 = expand.indexOf(QLatin1Char('\''), eq);
    const int q2 = q1 >= 0
        ? expand.indexOf(QLatin1Char('\''), q1 + 1) : -1;
    if (q2 < 0) {
        *filterId = QString();
        return true;
    }
    *filterId = expand.mid(q1 + 1, q2 - q1 - 1);
    return filterId->startsWith(kExtensionPrefix);
}

// Expand ON: keep only matching extension rows; expand OFF: strip the
// extensions[] key entirely.
static QJsonObject projectItem(const QJsonObject &item, bool hasExpand,
                               const QString &filterId)
{
    QJsonObject out = item;
    if (!hasExpand) {
        out.remove(QStringLiteral("extensions"));
        return out;
    }
    if (!out.contains(QStringLiteral("extensions")))
        return out;
    QJsonArray rows;
    const QJsonArray src = out.value(QStringLiteral("extensions")).toArray();
    for (const auto &row : src) {
        if (filterId.isEmpty()
            || row.toObject().value(QStringLiteral("id")).toString() == filterId)
            rows.append(row);
    }
    if (rows.isEmpty())
        out.remove(QStringLiteral("extensions"));
    else
        out.insert(QStringLiteral("extensions"), rows);
    return out;
}

void MockGraphTodoServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected,
                socket, &QTcpSocket::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            handleSocket(socket);
        });
    }
}

void MockGraphTodoServer::handleSocket(QTcpSocket *socket)
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

    const QByteArray body = buf.mid(headerEnd + 4, contentLength);
    const QUrl url(QString::fromUtf8(parts.at(1)));
    buf.clear();
    RecordedRequest rec;
    rec.method = method;
    rec.path = QUrl::fromPercentEncoding(
        url.toString(QUrl::FullyEncoded).toUtf8());
    rec.body = body;
    rec.authorizationHeader = authorization;
    m_requests.append(rec);
    buffers.remove(socket);

    const QString path = url.path();
    const QUrlQuery query(url.query());
    const QString httpMethod = QString::fromUtf8(method);

    // ---- exact routes win -------------------------------------------------
    for (const Route &r : std::as_const(m_routes)) {
        if (r.method == httpMethod && r.path == path) {
            respond(socket, r.status,
                    QJsonDocument(r.body.toObject().isEmpty()
                                      && r.body.isArray()
                                  ? QJsonDocument(r.body.toArray())
                                  : QJsonDocument(r.body.toObject())).toJson(
                                      QJsonDocument::Indented));
            return;
        }
    }

    // ---- todo lists --------------------------------------------------------
    if (httpMethod == QStringLiteral("GET")
        && path == QStringLiteral("/v1.0/me/todo/lists")) {
        QJsonObject out;
        out.insert(QStringLiteral("@odata.context"),
                   baseUrl() + QStringLiteral("/$metadata#me/todo/lists"));
        out.insert(QStringLiteral("value"), m_lists);
        respond(socket, 200, QJsonDocument(out).toJson(QJsonDocument::Indented));
        return;
    }

    // ---- nav carrier protocol: POST .../tasks/{id}/extensions --------------
    // O73: the carrier nav POST is an UPSERT keyed on extensionName — same
    // name replaces values and keeps the deterministic full-prefix id.
    if (httpMethod == QStringLiteral("POST")
        && path.startsWith(QStringLiteral("/v1.0/me/todo/lists/"))
        && path.endsWith(QStringLiteral("/extensions"))) {
        static const QString suffix = QStringLiteral("/extensions");
        QString rest = path.mid(
            int(QStringLiteral("/v1.0/me/todo/lists/").size()));
        rest.chop(suffix.size());
        const int sep = rest.lastIndexOf(QLatin1Char('/'));
        if (sep < 0) {
            respondNotFound(socket);
            return;
        }
        const QString ownerId = rest.mid(sep + 1);
        Collection *c = nullptr;
        int idx = -1;
        if (!findTask(ownerId, &c, &idx)) {
            respondNotFound(socket);
            return;
        }
        QJsonObject row = QJsonDocument::fromJson(body).object();
        const QString extensionName =
            row.value(QStringLiteral("extensionName")).toString();
        row.insert(QStringLiteral("id"),
                   kExtensionPrefix + extensionName);
        QJsonObject stored = c->items.at(idx).toObject();
        QJsonArray exts = stored.value(QStringLiteral("extensions")).toArray();
        bool replaced = false;
        for (int i = 0; i < exts.size(); ++i) {
            const QJsonObject existing = exts.at(i).toObject();
            if (existing.value(QStringLiteral("extensionName")).toString()
                == extensionName) {
                exts.replace(i, row);
                replaced = true;
                break;
            }
        }
        if (!replaced)
            exts.append(row);
        stored.insert(QStringLiteral("extensions"), exts);
        c->items.replace(idx, stored);
        respond(socket, 201, QJsonDocument(row).toJson(QJsonDocument::Indented));
        return;
    }

    // ---- resolve task collection / item paths ------------------------------
    QString collectionPath;
    QString itemId;
    if (path.startsWith(QStringLiteral("/v1.0/me/todo/lists/"))) {
        const QString rest =
            path.mid(int(QStringLiteral("/v1.0/me/todo/lists/").size()));
        const int marker = rest.indexOf(QStringLiteral("/tasks"));
        if (marker > 0) {
            collectionPath = QStringLiteral("/v1.0/me/todo/lists/%1/tasks")
                                 .arg(rest.left(marker));
            itemId = rest.mid(marker + int(QStringLiteral("/tasks").size()));
            if (itemId.startsWith(QLatin1Char('/')))
                itemId.remove(0, 1);
        }
    }

    if (!collectionPath.isEmpty()) {
        Collection *c = findCollection(collectionPath);

        // Item-level operations.
        if (!itemId.isEmpty()) {
            Collection *owner = nullptr;
            int idx = -1;
            const bool found = findTask(itemId, &owner, &idx);

            if (httpMethod == QStringLiteral("GET")) {
                if (!found) {
                    respondNotFound(socket);
                    return;
                }
                QString filterId;
                const QString expandValue =
                    queryParam(query, QStringLiteral("$expand"));
                const bool hasExpand = !expandValue.isEmpty();
                if (hasExpand
                    && !resolveExpandFilterId(expandValue, &filterId)) {
                    respond(socket, 500,
                            QByteArrayLiteral("{\"error\":{\"code\":"
                                              "\"ErrorInternalServerError\"}}"));
                    return;
                }
                const QJsonObject record = projectItem(
                    owner->items.at(idx).toObject(), hasExpand, filterId);
                respond(socket, 200,
                        QJsonDocument(record).toJson(QJsonDocument::Indented));
                return;
            }
            if (httpMethod == QStringLiteral("PATCH")) {
                if (!found) {
                    respondNotFound(socket);
                    return;
                }
                const QJsonObject patch =
                    QJsonDocument::fromJson(body).object();
                if (patch.contains(QStringLiteral("extensions"))) {
                    respond(socket, 500,
                            QByteArrayLiteral("{\"error\":{\"code\":"
                                              "\"ErrorInternalServerError\"}}"));
                    return;
                }
                QJsonObject merged = owner->items.at(idx).toObject();
                for (auto it = patch.begin(); it != patch.end(); ++it)
                    merged.insert(it.key(), it.value());
                owner->items.replace(idx, merged);
                respond(socket, 200,
                        QJsonDocument(merged).toJson(QJsonDocument::Indented));
                return;
            }
            if (httpMethod == QStringLiteral("DELETE")) {
                if (!found) {
                    respondNotFound(socket);
                    return;
                }
                owner->items.removeAt(idx);
                respond(socket, 204, {});
                return;
            }
        }

        // Collection-level operations.
        if (c && httpMethod == QStringLiteral("POST")) {
            QJsonObject created = QJsonDocument::fromJson(body).object();
            // Inline-create WIRE-LIE: extensions[] sent on the create body
            // are ECHOED but never persisted — carriers must travel via the
            // nav POST afterwards.
            QJsonArray echoedExts;
            const bool hadExtensions =
                created.contains(QStringLiteral("extensions"));
            if (hadExtensions) {
                echoedExts = created.value(QStringLiteral("extensions"))
                                 .toArray();
                created.remove(QStringLiteral("extensions"));
            }
            ++m_idCounter;
            created.insert(
                QStringLiteral("id"),
                QStringLiteral("AQMkTEST%1=")
                    .arg(m_idCounter, 6, 10, QLatin1Char('0')));
            c->items.append(created);
            if (hadExtensions)
                created.insert(QStringLiteral("extensions"), echoedExts);
            respond(socket, 201,
                    QJsonDocument(created).toJson(QJsonDocument::Indented));
            return;
        }
        if (c && httpMethod == QStringLiteral("GET")) {
            QString filterId;
            const QString expandValue =
                queryParam(query, QStringLiteral("$expand"));
            const bool hasExpand = !expandValue.isEmpty();
            if (hasExpand
                && !resolveExpandFilterId(expandValue, &filterId)) {
                respond(socket, 500,
                        QByteArrayLiteral("{\"error\":{\"code\":"
                                          "\"ErrorInternalServerError\"}}"));
                return;
            }

            int top = 10;
            if (!queryParam(query, QStringLiteral("$top")).isEmpty())
                top = qMax(1, queryParam(query, QStringLiteral("$top"))
                                    .toInt());
            int skip = 0;
            if (!queryParam(query, QStringLiteral("$skip")).isEmpty())
                skip = qMax(0, queryParam(query, QStringLiteral("$skip"))
                                     .toInt());

            QJsonArray page;
            for (int i = skip; i < c->items.size() && page.size() < top; ++i)
                page.append(projectItem(c->items.at(i).toObject(), hasExpand,
                                        filterId));
            const bool hasMore = skip + page.size() < c->items.size();

            QJsonObject out;
            out.insert(QStringLiteral("@odata.context"),
                       baseUrl()
                           + QStringLiteral("/$metadata#me/todo/tasks"));
            out.insert(QStringLiteral("value"), page);
            if (hasMore) {
                QUrlQuery nextQuery(query);
                nextQuery.removeQueryItem(QStringLiteral("$skip"));
                nextQuery.addQueryItem(
                    QStringLiteral("$skip"),
                    QString::number(skip + page.size()));
                const QString qs = nextQuery.toString(QUrl::FullyEncoded);
                out.insert(QStringLiteral("@odata.nextLink"),
                           baseUrl() + path
                               + (qs.isEmpty() ? QString() : "?" + qs));
            }
            respond(socket, 200,
                    QJsonDocument(out).toJson(QJsonDocument::Indented));
            return;
        }
    }

    // ---- unmatched → Graph 404 shape ---------------------------------------
    respondNotFound(socket);
}

void MockGraphTodoServer::respondNotFound(QTcpSocket *socket)
{
    QJsonObject err;
    err.insert(QStringLiteral("error"),
               QJsonObject{
                   { QStringLiteral("code"),
                     QStringLiteral("ErrorItemNotFound") },
                   { QStringLiteral("message"),
                     QStringLiteral("The specified object was not found in the store.") } });
    respond(socket, 404, QJsonDocument(err).toJson(QJsonDocument::Indented));
}

void MockGraphTodoServer::respond(QTcpSocket *socket, int status,
                                  const QByteArray &body,
                                  const QList<QPair<QByteArray, QByteArray>>
                                      &extraHeaders)
{
    QByteArray head;
    head += "HTTP/1.1 " + QByteArray::number(status)
        + (status == 200 ? " OK"
                         : status == 201 ? " Created"
                                         : status == 204 ? " No Content"
                                                         : status == 400
                                                             ? " Bad Request"
                                                             : status == 404
                                                                 ? " Not Found"
                                                                 : status == 500
                                                                     ? " Internal Server Error"
                                                                     : " Error")
        + "\r\n";
    head += "Content-Type: application/json\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const auto &[name, value] : extraHeaders)
        head += name + ": " + value + "\r\n";
    head += "Connection: close\r\n\r\n";
    socket->write(head + body);
    socket->disconnectFromHost();
}

} // namespace Kalburator::Todo
