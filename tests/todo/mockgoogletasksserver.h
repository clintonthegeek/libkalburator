#pragma once

// B2C P3.b — in-process mock of the Google TASKS API, the deliberate double
// for todo-transport tests. Mirrors tests/google/mockgoogleserver mechanics
// (QTcpServer on localhost, request recording incl. Authorization,
// exact-route table) but speaks the TASKS wire conventions:
//
//   - task lists served at GET /v1/users/me/lists; per-list task
//     collections at /v1/lists/{listId}/tasks — both paginate via items[]
//     + nextPageToken honoring maxResults/pageToken
//   - DEFAULT listing OMITS completed (status:"completed") and deleted
//     ("deleted":true) rows; showCompleted=true / showHidden=true reveal
//     them (suites pin that a backend must pass the flags)
//   - O68-family create truths faked: tasks.insert REJECTS a body carrying
//     top-level id, created, or updated (400, reason "invalid"); the server
//     mints a deterministic transport id
//   - PATCH merges in place; unmatched ids ⇒ 404; DELETE removes (204),
//     unmatched ⇒ 404 {error:{code:404,...}}
//
// Test-support library only — never part of the production target.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QTcpServer>
#include <QVector>

class QTcpSocket;

namespace Kalburator::Tasks {

/// Minimal blocking HTTP client used to drive the mock server from tests.
struct HttpResponse {
    int status = 0;
    QByteArray body;
    QString error;

    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

HttpResponse httpRequest(const QUrl &url,
                         const QByteArray &method,
                         const QList<QPair<QByteArray, QByteArray>> &headers = {},
                         const QByteArray &body = {});

class MockGoogleTasksServer : public QObject {
    Q_OBJECT
public:
    struct RecordedRequest {
        QByteArray method;
        QString path;      // path + query, URL-decoded
        QByteArray body;
        QByteArray authorizationHeader;
    };

    explicit MockGoogleTasksServer(QObject *parent = nullptr);
    ~MockGoogleTasksServer() override;

    bool start();
    void stop();
    quint16 port() const;
    QString baseUrl() const;

    /// Task lists served at /v1/users/me/lists.
    void setTaskLists(const QJsonArray &lists);
    /// Tasks seeded for a list id (the path segment after /v1/lists/).
    void setTasks(const QString &listId, const QJsonArray &tasks);

    /// Exact-route override (checked before built-ins); status defaults 200.
    void addRoute(const QByteArray &method, const QString &path,
                  const QByteArray &body, int status = 200);

    QList<RecordedRequest> requests() const;
    void clearRequests();

private:
    void handleConnection();
    QByteArray respond(const RecordedRequest &req);
    QByteArray makeReply(int status, const QByteArray &body) const;

    QTcpServer m_server;
    QList<RecordedRequest> m_requests;
    QJsonArray m_taskLists;
    QHash<QString, QJsonArray> m_tasks;
    qint64 m_idCounter = 0;
    struct Route {
        QByteArray method;
        QString path;
        QByteArray body;
        int status;
    };
    QVector<Route> m_routes;
};

} // namespace Kalburator::Tasks
