#pragma once

// B2C P2.b — in-process mock of the Microsoft Graph CONTACTS surface, the
// deliberate double for contacts-transport tests. Mirrors
// tests/graph/mockgraphserver mechanics (QTcpServer on localhost, request
// recording incl. Authorization, exact-route table, $top/$skip pagination
// with absolute @odata.nextLink) plus the O66-correction contacts truths:
//
//   - folders served at GET /v1.0/me/contactFolders; contacts keyed per
//     folder under /v1.0/me/contactFolders/{id}/contacts and the default
//     /v1.0/me/contacts
//   - $expand=extensions($filter=Id eq 'Microsoft.OutlookServices.OpenTypeExtension.*')
//     (%27-quoted variants too) reveals stored extension carriers; WITHOUT
//     the expand, extensions[] keys are stripped from served records
//   - nav carrier protocol: POST /v1.0/me/contacts/{id}/extensions appends
//     an extension row to the stored record and echoes it (201); a PATCH
//     whose body carries an extensions key ⇒ 500; a WRONG extension-id
//     prefix in the expand filter ⇒ 500
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

namespace Kalburator::Contacts {

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

class MockGraphContactsServer : public QObject {
    Q_OBJECT
public:
    struct RecordedRequest {
        QByteArray method;
        QString path;      // path + query, URL-decoded
        QByteArray body;
        QByteArray authorizationHeader;
    };

    explicit MockGraphContactsServer(QObject *parent = nullptr);
    ~MockGraphContactsServer() override;

    bool start();
    void stop();
    quint16 port() const;
    QString baseUrl() const;

    /// Folders served at GET /v1.0/me/contactFolders.
    void setContactFolders(const QJsonArray &folders);

    /// Serve GET <path> paginated over `items` ($top default 10, honoring
    /// an explicit $top query parameter). `path` is a contacts collection
    /// (/v1.0/me/contacts or /v1.0/me/contactFolders/{id}/contacts); POSTs
    /// to it create contacts with a minted '='-terminated id.
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
    bool findContact(const QString &id, Collection **collection, int *index);

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QList<RecordedRequest> m_requests;
    QJsonArray m_folders;
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

} // namespace Kalburator::Contacts
