#pragma once

// B2C P3.b — in-process mock of the Microsoft Graph TODO surface (todoTask),
// the deliberate double for todo-transport tests. Mirrors
// tests/contacts/mockgraphcontactsserver mechanics (QTcpServer on localhost,
// request recording incl. Authorization, exact-route table, $top/$skip
// pagination with absolute @odata.nextLink) plus the todo wire truths:
//
//   - lists served at GET /v1.0/me/todo/lists; tasks keyed per list under
//     /v1.0/me/todo/lists/{id}/tasks
//   - $expand=extensions($filter=Id eq 'Microsoft.OutlookServices.OpenTypeExtension.*')
//     (%27-quoted variants too) reveals stored extension carriers; WITHOUT
//     the expand, extensions[] keys are stripped from served records
//   - inline-create WIRE-LIE faked: a POST whose body carries extensions[]
//     gets them ECHOED in the response but they are NOT persisted
//   - nav carrier protocol: POST /v1.0/me/todo/lists/{id}/tasks/{id}/extensions
//     is an UPSERT keyed on extensionName — same name replaces the row's
//     values and keeps the deterministic full-prefix id (O73); 201 either way
//   - a PATCH whose body carries an extensions key ⇒ 500; a WRONG
//     extension-id prefix in the expand filter ⇒ 500
//   - creates mint a server-style transport id ending in '='
//   - DELETE removes (204); unmatched ids get
//     {error:{code:"ErrorItemNotFound"}} (re-list confirmation drills)
//
// Test-support library only — never part of the production target.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

class QTcpServer;
class QTcpSocket;

namespace Kalburator::Todo {

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

class MockGraphTodoServer : public QObject {
    Q_OBJECT
public:
    struct RecordedRequest {
        QByteArray method;
        QString path;      // path + query, URL-decoded
        QByteArray body;
        QByteArray authorizationHeader;
    };

    explicit MockGraphTodoServer(QObject *parent = nullptr);
    ~MockGraphTodoServer() override;

    bool start();
    void stop();
    quint16 port() const;
    QString baseUrl() const;

    /// Todo lists served at GET /v1.0/me/todo/lists.
    void setTodoLists(const QJsonArray &lists);

    /// Serve GET <path> paginated over `items` ($top default 10, honoring
    /// an explicit $top/$skip query pair). `path` is a task collection
    /// (/v1.0/me/todo/lists/{id}/tasks); POSTs to it create tasks with a
    /// minted '='-terminated transport id.
    void addCollection(const QString &path, const QJsonArray &items);

    /// Replace a collection's items in place.
    void setCollectionItems(const QString &path, const QJsonArray &items);

    /// Exact-method single response. `body` is wrapped as-is.
    void addRoute(const QString &method, const QString &path,
                  const QJsonValue &body, int status = 200);

    QList<RecordedRequest> requests() const;
    void clearRequests();

private:
    void onNewConnection();
    void handleSocket(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int status,
                 const QByteArray &body,
                 const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});
    void respondNotFound(QTcpSocket *socket);

    struct Collection {
        QString path;
        QJsonArray items;
    };

    Collection *findCollection(const QString &path);
    bool findTask(const QString &id, Collection **collection, int *index);

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QList<RecordedRequest> m_requests;
    QJsonArray m_lists;
    QList<Collection> m_collections;
    qint64 m_idCounter = 0;
    struct Route {
        QString method;
        QString path;
        QJsonValue body;
        int status;
    };
    QList<Route> m_routes;
};

} // namespace Kalburator::Todo
