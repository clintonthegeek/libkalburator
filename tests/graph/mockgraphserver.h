#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

class QTcpServer;
class QTcpSocket;

namespace Kalburator::Graph {

/// Minimal blocking HTTP client used to drive the mock server from tests
/// (same wire contract as the tools' helpers).
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

/// In-process mock of the Microsoft Graph wire surface that the EEE Phase-7
/// backend must survive (Stage D test bed). Qt6 Network only; CI-able.
///
/// Implemented Graph semantics (from the live corpus, FINDINGS O57):
/// - `$top`/`$skip` pagination over collections with absolute
///   `@odata.nextLink` URLs (masters-only listing is just data).
/// - `/delta` walks: initial call returns items + `@odata.deltaLink`;
///   presenting the token replays the queued change page and issues a fresh
///   deltaLink; an unknown/expired token yields HTTP 410 with
///   `error.code = "ResyncRequired"` (full-resync signal).
/// - Exact routes for single resources; unmatched item ids get the Graph
///   404 shape `{ "error": { "code": "ErrorItemNotFound", ... } }`.
///
/// All requests are recorded (method + path) for assertions.
class MockGraphServer : public QObject {
    Q_OBJECT
public:
    struct RecordedRequest {
        QByteArray method;
        QString path;      // path + query, URL-decoded
        QByteArray body;
    };

    explicit MockGraphServer(QObject *parent = nullptr);
    ~MockGraphServer() override;

    bool start();
    void stop();
    quint16 port() const;
    QString baseUrl() const;

    /// Serve GET <path> paginated over `items` ($top default 10, honoring
    /// an explicit $top query parameter).
    void addCollection(const QString &path, const QJsonArray &items);

    /// Replace a collection's items in place (simulates server-side change;
    /// next pagination walk sees the new data).
    void setCollectionItems(const QString &path, const QJsonArray &items);

    /// Exact-method single response. `body` is wrapped as-is.
    void addRoute(const QString &method, const QString &path,
                  const QJsonValue &body, int status = 200);

    /// Queue a delta change page: when a client presents deltaToken N they
    /// receive changes[N] plus a fresh deltaLink (or, past the last queued
    /// page, an empty change set + new link). Unknown tokens → 410 Resync.
    void queueDeltaChanges(const QString &collectionPath,
                           const QString &deltaToken,
                           const QJsonArray &changedItems);

    /// Forget the most recently issued delta token for a collection —
    /// presenting it afterwards yields 410 ResyncRequired (expiry drill).
    void invalidateDeltaTokens(const QString &collectionPath);

    QList<RecordedRequest> requests() const;
    void clearRequests();

private:
    void onNewConnection();
    void handleSocket(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int status,
                 const QByteArray &body,
                 const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});

    struct Collection {
        QString path;
        QJsonArray items;
        // ordered (token → changes) queue
        QVector<QPair<QString, QJsonArray>> deltaQueue;
        int deltaCounter = 0;   // mints successive tokens
        QString lastDeltaToken; // most recently issued token for this collection
    };

    Collection *findCollection(const QString &path);

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QList<RecordedRequest> m_requests;
    QList<Collection> m_collections;
    struct Route {
        QString method;
        QString path;
        QJsonValue body;
        int status;
    };
    QList<Route> m_routes;
};

} // namespace Kalburator::Graph
