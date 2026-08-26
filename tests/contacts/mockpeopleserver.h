#pragma once

// B2C P2.b — in-process mock of the Google People API v1, the deliberate
// double for contacts-transport tests. Mirrors tests/google/mockgoogleserver
// mechanics (QTcpServer on localhost, request recording incl. Authorization,
// exact-route table) but speaks the PEOPLE wire conventions:
//
//   - GET /v1/people/me/connections paginates connections[] +
//     nextPageToken (pageSize/pageToken); personFields projects each person
//     to ONLY the requested fields (plus resourceName/etag/metadata)
//   - sync tokens: requestSyncToken=true on the initial listing →
//     nextSyncToken ("sync_N"); presenting a valid sync_token replays
//     queued changed people + fresh token; unknown/expired token ⇒ 410
//     {error:{code:410,...}}
//   - POST /v1/people:createContact mints resourceName "people/c<N>"
//     and echoes the body (clientData rows verbatim — live-Reversible);
//   - PATCH /v1/people/{rn}:updateContact?updatePersonFields= merges only
//     the masked top-level fields; DELETE :deleteContact → 200 empty.
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

namespace Kalburator::People {

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

class MockPeopleServer : public QObject {
    Q_OBJECT
public:
    struct RecordedRequest {
        QByteArray method;
        QString path;      // path + query, URL-decoded
        QByteArray body;
        QByteArray authorizationHeader;
    };

    explicit MockPeopleServer(QObject *parent = nullptr);
    ~MockPeopleServer() override;

    bool start();
    void stop();
    quint16 port() const;
    QString baseUrl() const;

    /// People served at /v1/people/me/connections.
    void setConnections(const QJsonArray &people);
    /// Queue changed people delivered when the CURRENT live sync token is
    /// presented (Google semantics: presenting token T delivers the changes
    /// recorded since T).
    void queueConnectionChanges(const QString &syncToken,
                                const QJsonArray &changedPeople);
    /// Expire the outstanding sync token (next replay ⇒ 410).
    void expireSyncTokens();

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
    QJsonArray m_connections;
    QString m_currentSyncToken;
    QHash<QString, QJsonArray> m_queuedChanges;   // live token -> items
    qint64 m_syncTokenCounter = 0;
    qint64 m_personCounter = 0;
    struct Route {
        QByteArray method;
        QString path;
        QByteArray body;
        int status;
    };
    QVector<Route> m_routes;
};

} // namespace Kalburator::People
