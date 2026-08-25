#pragma once

// B2C P1 — in-process mock of the Google Calendar API v3, the deliberate
// double for GoogleApiClient/GoogleCalendarBackend tests. Mirrors
// tests/graph/mockgraphserver mechanics (QTcpServer on localhost, request
// recording, exact-route table) but speaks GOOGLE wire conventions:
//
//   - pagination via items[] + nextPageToken (request param pageToken=)
//   - sync tokens: initial listing → nextSyncToken ("sync_N"); presenting
//     a valid token replays queued changes + fresh token; unknown/expired
//     token ⇒ 410 Gone {error:{errors:[{reason:"gone"}]}}
//   - deletions surface as status:"cancelled" items (no @removed)
//   - O67 create-path truths faked: events.insert REJECTS created/updated
//     (400), echoes client iCalUID, mints a fresh transport id, rewrites
//     organizer to the authenticated account
//
// Test-support library only — never part of the production target.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QVector>

namespace Kalburator::Google {

class MockGoogleServer : public QObject {
    Q_OBJECT
public:
    struct RecordedRequest {
        QByteArray method;
        QString path;      // path + query, URL-decoded
        QByteArray body;
        QByteArray authorizationHeader;
    };

    explicit MockGoogleServer(QObject *parent = nullptr);
    ~MockGoogleServer() override;

    bool start();
    void stop();
    quint16 port() const;
    QString baseUrl() const;

    /// calendarList entries served at /users/me/calendarList.
    void setCalendarList(const QJsonArray &calendars);
    /// Events seeded for a calendar id (the path segment after /calendars/).
    void setEvents(const QString &calendarId, const QJsonArray &events);
    /// Queue changed items delivered when `syncToken` is presented.
    void queueSyncChanges(const QString &calendarId, const QString &syncToken,
                          const QJsonArray &changedItems);
    /// Expire all outstanding tokens for a calendar (next replay ⇒ 410).
    void expireSyncTokens(const QString &calendarId);

    /// Exact-route override (checked before collections); status defaults 200.
    void addRoute(const QByteArray &method, const QString &path,
                  const QByteArray &body, int status = 200);

    QList<RecordedRequest> requests() const;
    void clearRequests();

private:
    void handleConnection();
    QByteArray respond(const RecordedRequest &req);

    QTcpServer m_server;
    QList<RecordedRequest> m_requests;
    QJsonArray m_calendarList;
    QHash<QString, QJsonArray> m_events;
    QHash<QString, QString> m_currentSyncToken;         // calId -> live token
    QHash<QString, QJsonArray> m_queuedChanges;         // "calId|token" -> items
    qint64 m_syncTokenCounter = 0;
    struct Route {
        QByteArray method;
        QString path;
        QByteArray body;
        int status;
    };
    QVector<Route> m_routes;
};

} // namespace Kalburator::Google
