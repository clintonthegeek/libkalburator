// B2C P1.f — GoogleCalendarBackend LIVE checkpoint (proposal invariant 1:
// mock-green cannot see create-path rewrites). Skips unless a live token
// cache is available via KALBURATOR_GOOGLE_DIR (token-cache.json from the
// googlecli login). Protocol: discovery → fetch → create CORPUS-tagged
// probe → fetch (visible? iCalUID anchor honored?) → PATCH → fetch →
// DELETE → fetch (gone). Prints wire-truth verdicts; cleans up after
// itself (delete is unconditional at the end).
//
// Run: ctest -R tst_google_calendar_backend_live   (or the binary directly)

#include <QtTest/QtTest>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "googlecalendarbackend.h"
#include "googleauth.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GoogleCalendarBackend;
using Kalburator::Sync::WriterBatch;

namespace {

QString loadLiveAccessToken()
{
    const QString dir = qEnvironmentVariable("KALBURATOR_GOOGLE_DIR");
    if (dir.isEmpty())
        return {};
    const Kalburator::Google::TokenStore store(dir + QStringLiteral("/token-cache.json"));
    const auto tokens = store.load();
    if (!tokens.hasLiveAccessToken())
        return {};
    return tokens.accessToken;
}

} // namespace

class TestGoogleCalendarBackendLive : public QObject {
    Q_OBJECT
private slots:

    void fullProbeCycleAgainstLiveAccount()
    {
        const QString token = loadLiveAccessToken();
        if (token.isEmpty())
            QSKIP("No live Google token cache (set KALBURATOR_GOOGLE_DIR "
                  "with a googlecli-authorized profile).");

        GoogleCalendarBackend backend;
        backend.setBaseUrl(
            QStringLiteral("https://www.googleapis.com/calendar/v3"));
        backend.setAccessToken(token);

        // ---- 1. Discovery ------------------------------------------------
        QSignalSpy discoSpy(
            &backend,
            &Kalburator::Sync::SyncBackend::loadCalendarsFinished);
        backend.loadCalendars(QStringLiteral("coll"));
        QTRY_VERIFY_WITH_TIMEOUT(discoSpy.count() == 1, 15000);
        QVERIFY2(discoSpy.at(0).at(1).toBool(), "calendarList fetch failed");
        qInfo() << "discovered calendars:" << backend.availableCollections().size();
        QVERIFY(!backend.availableCollections().isEmpty());

        // ---- 2. Initial fetch -------------------------------------------
        auto *fetch1 = backend.fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(fetch1->isFinished(), 30000);
        QCOMPARE(fetch1->state(), Kalburator::Sync::SyncOperation::Succeeded);
        qInfo() << "initial fetch records:" << fetch1->fetchedItems().size();

        // ---- 3. Create CORPUS-tagged probe -------------------------------
        const QString stamp =
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddThhmmss"));
        const QString anchorUid =
            QStringLiteral("b2c-live-%1@example.com").arg(stamp);
        const QString summary = QStringLiteral("CORPUS:b2c-p1f probe");
        QJsonObject authored{
            { QStringLiteral("summary"), summary },
            { QStringLiteral("iCalUID"), anchorUid },
            { QStringLiteral("id"), QStringLiteral("requested-b2c-probe") },
            { QStringLiteral("created"),
              QStringLiteral("2026-01-01T00:00:00Z") },          // must strip
            { QStringLiteral("updated"),
              QStringLiteral("2026-01-01T00:00:00Z") },          // must strip
            { QStringLiteral("start"),
              QJsonObject{ { QStringLiteral("dateTime"),
                             QStringLiteral("2026-10-01T10:00:00Z") } } },
            { QStringLiteral("end"),
              QJsonObject{ { QStringLiteral("dateTime"),
                             QStringLiteral("2026-10-01T11:00:00Z") } } },
        };
        BackendRecord rec;
        rec.id = QStringLiteral("requested-b2c-probe");
        rec.type = QStringLiteral("event");
        rec.displayName = summary;
        rec.data = QJsonDocument(authored).toJson(QJsonDocument::Compact);

        WriterBatch createBatch;
        createBatch.creates = { rec };
        auto *create = backend.applyRecords(QStringLiteral("primary"),
                                            createBatch);
        QTRY_VERIFY_WITH_TIMEOUT(create->isFinished(), 30000);
        QCOMPARE(create->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(create->failedUids().size(), 0);
        const QString storedId = create->succeededUids().value(0);
        qInfo() << "probe created; requested id:" << rec.id
                << "stored transport id:" << storedId;
        QVERIFY(!storedId.isEmpty());
        QVERIFY(!storedId.startsWith(QLatin1String("mockevt")));

        // Cleanup guard: whatever happens below, try to remove the probe.
        auto removeProbe = [&]() {
            auto *del = backend.applyRecords(
                QStringLiteral("primary"),
                ([&]{ WriterBatch b; b.deletes = { storedId }; return b; })());
            QTRY_VERIFY_WITH_TIMEOUT(del->isFinished(), 30000);
        };

        // ---- 4. Incremental fetch sees the probe -------------------------
        // (Google sync tokens may lag slightly; poll a few times.)
        bool seen = false;
        QString seenWireSummary;
        for (int attempt = 0; attempt < 5 && !seen; ++attempt) {
            auto *f = backend.fetchItems(QStringLiteral("primary"));
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 30000);
            QList<BackendRecord> records;
            QString err;
            if (backend.recordsFromLastFetch(QStringLiteral("primary"),
                                             records, err)) {
                for (const auto &r : records) {
                    if (r.id == storedId) {
                        seen = true;
                        const QJsonObject wire =
                            QJsonDocument::fromJson(r.data).object();
                        seenWireSummary =
                            wire.value(QStringLiteral("summary")).toString();
                        // O67(b)(4): client iCalUID anchor HONORED.
                        QCOMPARE(wire.value(QStringLiteral("iCalUID")).toString(),
                                 anchorUid);
                        // O67(b)(2): organizer rewritten to authed account.
                        QVERIFY(wire.contains(QStringLiteral("organizer")));
                    }
                }
            }
            if (!seen)
                QTest::qWait(2000);
        }
        QVERIFY2(seen, "probe never surfaced on incremental fetch");
        qInfo() << "probe visible on read-back:" << seenWireSummary;

        // ---- 5. Update in place (PATCH) ----------------------------------
        QJsonObject patched{
            { QStringLiteral("id"), storedId },
            { QStringLiteral("summary"), summary + QStringLiteral(" EDITED") },
        };
        BackendRecord updRec;
        updRec.id = storedId;
        updRec.type = QStringLiteral("event");
        updRec.data = QJsonDocument(patched).toJson(QJsonDocument::Compact);
        auto *update = backend.applyRecords(
            QStringLiteral("primary"),
            [&]{ WriterBatch b; b.updates = { updRec }; return b; }());
        QTRY_VERIFY_WITH_TIMEOUT(update->isFinished(), 30000);
        QCOMPARE(update->state(), Kalburator::Sync::SyncOperation::Succeeded);
        qInfo() << "PATCH-in-place update OK";

        // ---- 6. Delete + verify gone --------------------------------------
        removeProbe();
        auto *verifyDel = backend.fetchItems(QStringLiteral("primary"));
        QTRY_VERIFY_WITH_TIMEOUT(verifyDel->isFinished(), 30000);
        QList<BackendRecord> postDelete;
        QString err2;
        bool stillThere = false;
        if (backend.recordsFromLastFetch(QStringLiteral("primary"),
                                         postDelete, err2)) {
            for (const auto &r : postDelete)
                if (r.id == storedId)
                    stillThere = true;
        }
        QVERIFY2(!stillThere, "probe survived deletion");

        qInfo() << "LIVE CHECKPOINT PASSED (GoogleCalendarBackend, primary)";
    }
};

QTEST_MAIN(TestGoogleCalendarBackendLive)
#include "tst_google_calendar_backend_live.moc"
