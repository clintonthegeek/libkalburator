#include "mockgoogletasksserver.h"

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

namespace Kalburator::Tasks {

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

MockGoogleTasksServer::MockGoogleTasksServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this,
            &MockGoogleTasksServer::handleConnection);
}

MockGoogleTasksServer::~MockGoogleTasksServer()
{
    stop();
}

bool MockGoogleTasksServer::start()
{
    return m_server.listen(QHostAddress::LocalHost);
}

void MockGoogleTasksServer::stop()
{
    if (m_server.isListening())
        m_server.close();
}

quint16 MockGoogleTasksServer::port() const
{
    return m_server.serverPort();
}

QString MockGoogleTasksServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port());
}

void MockGoogleTasksServer::setTaskLists(const QJsonArray &lists)
{
    m_taskLists = lists;
}

void MockGoogleTasksServer::setTasks(const QString &listId,
                                     const QJsonArray &tasks)
{
    m_tasks.insert(listId, tasks);
}

void MockGoogleTasksServer::addRoute(const QByteArray &method,
                                     const QString &path,
                                     const QByteArray &body, int status)
{
    m_routes.append({ method, path, body, status });
}

QList<MockGoogleTasksServer::RecordedRequest> MockGoogleTasksServer::requests() const
{
    return m_requests;
}

void MockGoogleTasksServer::clearRequests()
{
    m_requests.clear();
}

void MockGoogleTasksServer::handleConnection()
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

static QByteArray paginateItems(const QJsonArray &items,
                                const QString &pageToken, int maxResults)
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
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// Default Google wire behavior: completed and deleted rows are OMITTED
// unless the corresponding flags are presented.
static QJsonArray applyVisibilityFlags(const QJsonArray &tasks,
                                       bool showCompleted, bool showHidden)
{
    QJsonArray effective;
    for (const auto &row : tasks) {
        const QJsonObject task = row.toObject();
        const bool deleted = task.value(QStringLiteral("deleted")).toBool();
        const bool completed =
            task.value(QStringLiteral("status")).toString()
            == QLatin1String("completed");
        if (deleted && !showHidden)
            continue;
        if (completed && !showCompleted)
            continue;
        effective.append(task);
    }
    return effective;
}

int findTaskIndex(const QJsonArray &tasks, const QString &taskId)
{
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks.at(i).toObject().value(QStringLiteral("id")).toString()
            == taskId)
            return i;
    }
    return -1;
}

QByteArray MockGoogleTasksServer::makeReply(int status,
                                            const QByteArray &body) const
{
    const QByteArray reason =
        status == 200 ? "OK"
                      : status == 201 ? "Created"
                                      : status == 204 ? "No Content"
                                                      : status == 400
                                                          ? "Bad Request"
                                                          : status == 404
                                                              ? "Not Found"
                                                              : "Error";
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reason
        + "\r\nContent-Type: application/json\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    return head + body;
}

QByteArray MockGoogleTasksServer::respond(const RecordedRequest &req)
{
    // 1. Exact routes win.
    for (const Route &r : m_routes) {
        if (r.method == req.method && r.path == req.path)
            return makeReply(r.status, r.body);
    }

    const QString p = pathOnly(req.path);
    const bool showCompleted =
        queryValue(req.path, QStringLiteral("showCompleted"))
        == QLatin1String("true");
    const bool showHidden =
        queryValue(req.path, QStringLiteral("showHidden"))
        == QLatin1String("true");

    // 2. Task lists listing. Live truth 2026-08-26: the API addresses
    // users/@me (plain users/me 404s with an HTML page).
    if (req.method == "GET" && p == QLatin1String("/v1/users/@me/lists")) {
        return makeReply(200,
                         paginateItems(m_taskLists,
                                       queryValue(req.path,
                                                  QStringLiteral("pageToken")),
                                       queryValue(req.path,
                                                  QStringLiteral("maxResults"))
                                           .toInt()));
    }

    // 3. Task collections: /v1/lists/<listId>/tasks[/task-id].
    if (p.startsWith(QLatin1String("/v1/lists/"))
        && p.contains(QLatin1String("/tasks"))) {
        const QString listId = p.section(QLatin1Char('/'), 3, 3);
        const QString collectionPrefix =
            QStringLiteral("/v1/lists/%1/tasks").arg(listId);

        if (req.method == "GET" && p == collectionPrefix) {
            const QJsonArray effective = applyVisibilityFlags(
                m_tasks.value(listId), showCompleted, showHidden);
            return makeReply(
                200,
                paginateItems(effective,
                              queryValue(req.path,
                                         QStringLiteral("pageToken")),
                              queryValue(req.path,
                                         QStringLiteral("maxResults"))
                                  .toInt()));
        }

        const QString tail = p.mid(collectionPrefix.size());

        if (req.method == "POST" && tail.isEmpty()) {
            const QJsonObject posted =
                QJsonDocument::fromJson(req.body).object();
            // O68-family: Google REJECTS read-only created/updated AND a
            // client-supplied transport id — the server mints its own.
            if (posted.contains(QStringLiteral("created"))
                || posted.contains(QStringLiteral("updated"))
                || posted.contains(QStringLiteral("id"))) {
                return makeReply(
                    400,
                    QJsonDocument(errorBody(
                        400, QStringLiteral("invalid"),
                        QStringLiteral("Bad Request")))
                        .toJson(QJsonDocument::Compact));
            }
            QJsonObject created = posted;
            created.insert(QStringLiteral("id"),
                           QStringLiteral("mocktask%1").arg(++m_idCounter));
            if (!created.contains(QStringLiteral("status")))
                created.insert(QStringLiteral("status"),
                               QStringLiteral("needsAction"));
            QJsonArray items = m_tasks.value(listId);
            items.append(created);
            m_tasks.insert(listId, items);
            return makeReply(
                200, QJsonDocument(created).toJson(QJsonDocument::Compact));
        }

        if (!tail.isEmpty()) {
            const QString taskId = tail.mid(1);   // strip '/'
            QJsonArray items = m_tasks.value(listId);
            const int idx = findTaskIndex(items, taskId);

            if (req.method == "PATCH") {
                if (idx < 0) {
                    return makeReply(
                        404,
                        QJsonDocument(errorBody(
                            404, QStringLiteral("notFound"),
                            QStringLiteral("Task not found")))
                            .toJson(QJsonDocument::Compact));
                }
                QJsonObject merged = items.at(idx).toObject();
                const QJsonObject patch =
                    QJsonDocument::fromJson(req.body).object();
                for (auto it = patch.begin(); it != patch.end(); ++it)
                    merged.insert(it.key(), it.value());
                items.replace(idx, merged);
                m_tasks.insert(listId, items);
                return makeReply(
                    200,
                    QJsonDocument(merged).toJson(QJsonDocument::Compact));
            }

            if (req.method == "DELETE") {
                if (idx < 0) {
                    return makeReply(
                        404,
                        QJsonDocument(errorBody(
                            404, QStringLiteral("notFound"),
                            QStringLiteral("Task not found")))
                            .toJson(QJsonDocument::Compact));
                }
                items.removeAt(idx);
                m_tasks.insert(listId, items);
                return makeReply(204, {});
            }
        }
    }

    // 4. Unmatched ⇒ Google 404 shape.
    return makeReply(404,
                     QJsonDocument(errorBody(
                         404, QStringLiteral("notFound"),
                         QStringLiteral("Not Found")))
                         .toJson(QJsonDocument::Compact));
}

} // namespace Kalburator::Tasks
