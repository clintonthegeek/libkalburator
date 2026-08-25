// B2C P1 — GoogleApiClient transport tests against the mock Google server.
// Pins: pageToken aggregation, nextSyncToken surfacing, sync-token replay
// semantics, 410 Gone surfacing as typed error, Bearer header injection,
// O67 create-path rejection (created/updated ⇒ 400).

#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "googleapiclient.h"
#include "mockgoogleserver.h"

using Kalburator::Google::GoogleApiClient;
using Kalburator::Google::GoogleError;
using Kalburator::Google::MockGoogleServer;

namespace {

QJsonObject makeItem(int i)
{
    return QJsonObject{
        { QStringLiteral("id"), QStringLiteral("evt-%1").arg(i) },
        { QStringLiteral("summary"), QStringLiteral("Event %1").arg(i) }
    };
}

} // namespace

class TestGoogleApiClient : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        m_server = new MockGoogleServer(this);
        QVERIFY(m_server->start());
        m_client = new GoogleApiClient(this);
        m_client->setBaseUrl(m_server->baseUrl());
        m_client->setAccessToken(QStringLiteral("test-token"));
    }

    void cleanup()
    {
        delete m_client;
        delete m_server;
        m_client = nullptr;
        m_server = nullptr;
    }

    // Aggregates all pages via nextPageToken; surfaces nextSyncToken.
    void fetchCollectionAggregatesPagesAndSurfacesSyncToken()
    {
        QJsonArray events;
        for (int i = 0; i < 12; ++i)
            events.append(makeItem(i));
        m_server->setEvents(QStringLiteral("primary"), events);

        const auto result = m_client->fetchCollectionSync(
            QStringLiteral("/calendars/primary/events?maxResults=10"));
        QVERIFY2(result.error.ok(), qPrintable(result.error.message));
        QCOMPARE(result.items->size(), 12);
        QCOMPARE(result.items->at(0).toObject().value("id").toString(),
                 QStringLiteral("evt-0"));
        QCOMPARE(result.items->at(11).toObject().value("id").toString(),
                 QStringLiteral("evt-11"));
        QVERIFY(!result.nextSyncToken.isEmpty());
        // 12 items at maxResults=10 ⇒ exactly two requests.
        QCOMPARE(m_server->requests().size(), 2);
    }

    // Presenting the surfaced sync token replays queued changes + fresh
    // token; unknown tokens yield typed 410 Gone.
    void syncTokenReplayThenGone()
    {
        QJsonArray events;
        events.append(makeItem(1));
        m_server->setEvents(QStringLiteral("primary"), events);

        auto first = m_client->fetchCollectionSync(
            QStringLiteral("/calendars/primary/events"));
        QVERIFY2(first.error.ok(), qPrintable(first.error.message));
        QVERIFY(!first.nextSyncToken.isEmpty());

        m_server->queueSyncChanges(QStringLiteral("primary"),
                                   first.nextSyncToken,
                                   { makeItem(2), makeItem(3) });
        auto second = m_client->fetchCollectionSync(
            QStringLiteral("/calendars/primary/events?syncToken=")
                + first.nextSyncToken);
        QVERIFY2(second.error.ok(), qPrintable(second.error.message));
        QCOMPARE(second.items->size(), 2);
        QVERIFY(!second.nextSyncToken.isEmpty());

        m_server->expireSyncTokens(QStringLiteral("primary"));
        auto third = m_client->fetchCollectionSync(
            QStringLiteral("/calendars/primary/events?syncToken=")
                + second.nextSyncToken);
        QVERIFY(!third.error.ok());
        QVERIFY(third.error.isGone());
        QCOMPARE(third.error.httpStatus, 410);
        QCOMPARE(third.error.reason, QStringLiteral("gone"));
    }

    // Bearer header on every request (mock records it).
    void bearerHeaderInjectedOnEveryRequest()
    {
        m_server->setCalendarList({ makeItem(1) });
        const auto result = m_client->fetchCollectionSync(
            QStringLiteral("/users/me/calendarList"));
        QVERIFY2(result.error.ok(), qPrintable(result.error.message));
        QCOMPARE(m_server->requests().size(), 1);
        QCOMPARE(m_server->requests().first().authorizationHeader,
                 QByteArray("Bearer test-token"));
    }

    // O67(b)(1): insert with read-only created/updated must be rejected —
    // the backend write path strips these before POST (pin the wire truth).
    void insertRejectsReadOnlyTimestamps()
    {
        QJsonObject body{
            { QStringLiteral("summary"), QStringLiteral("x") },
            { QStringLiteral("created"),
              QStringLiteral("2026-01-01T00:00:00Z") }
        };
        int status = 0;
        bool networkError = true;
        m_client->rawRequest(
            "POST", "/calendars/primary/events",
            QJsonDocument(body).toJson(QJsonDocument::Compact),
            [&](int s, const QByteArray &, bool ne) {
                status = s;
                networkError = ne;
            });
        QTRY_COMPARE_WITH_TIMEOUT(status, 400, 5000);
        QVERIFY(!networkError);
    }

private:
    MockGoogleServer *m_server = nullptr;
    GoogleApiClient *m_client = nullptr;
};

QTEST_MAIN(TestGoogleApiClient)
#include "tst_google_api_client.moc"
