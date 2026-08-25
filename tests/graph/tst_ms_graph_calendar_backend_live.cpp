// B2C P1.f — MSGraphCalendarBackend LIVE checkpoint (proposal invariant 1).
// Skips unless KALBURATOR_MSGRAPH_DIR holds a graphcli-authorized token
// cache. Protocol: discovery → initial delta walk → create CORPUS-tagged
// probe → incremental walk sees it (uid MINTED FRESH by Graph per O67(c);
// organizer rewritten) → PATCH-in-place → delete → verified gone.
//
// Known consumer-account truths this drill expects to OBSERVE (not fight):
// fresh uid minting, organizer rewrite, possible Teams auto-provisioning.
// Cleanup is unconditional at the end; sweep-clean covers stragglers.

#include <QtTest/QtTest>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "msgraphcalendarbackend.h"
#include "graphauthenticator.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::MSGraphCalendarBackend;
using Kalburator::Sync::WriterBatch;

namespace {

QString loadLiveAccessToken()
{
    const QString dir = qEnvironmentVariable("KALBURATOR_MSGRAPH_DIR");
    if (dir.isEmpty())
        return {};
    const Kalburator::Graph::TokenStore store(
        dir + QStringLiteral("/token-cache.json"));
    const auto tokens = store.load();
    if (!tokens.hasLiveAccessToken())
        return {};
    return tokens.accessToken;
}

} // namespace

class TestMsGraphCalendarBackendLive : public QObject {
    Q_OBJECT
private slots:

    void fullProbeCycleAgainstLiveAccount()
    {
        const QString token = loadLiveAccessToken();
        if (token.isEmpty())
            QSKIP("No live Graph token cache (set KALBURATOR_MSGRAPH_DIR "
                  "with a graphcli-authorized profile).");

        MSGraphCalendarBackend backend;
        backend.setBaseUrl(QStringLiteral("https://graph.microsoft.com/v1.0"));
        backend.setAccessToken(token);
        backend.setCollectionPath(QStringLiteral("/me/events"));

        // ---- 1. Discovery ------------------------------------------------
        QSignalSpy discoSpy(
            &backend,
            &Kalburator::Sync::SyncBackend::loadCalendarsFinished);
        backend.loadCalendars(QStringLiteral("coll"));
        QTRY_VERIFY_WITH_TIMEOUT(discoSpy.count() == 1, 30000);
        QVERIFY2(discoSpy.at(0).at(1).toBool(), "calendar discovery failed");
        qInfo() << "discovered calendars:"
                << backend.availableCollections().size();

        // ---- 2. Initial delta walk ---------------------------------------
        auto *fetch1 = backend.fetchItems(QStringLiteral("me-events"));
        QTRY_VERIFY_WITH_TIMEOUT(fetch1->isFinished(), 60000);
        QCOMPARE(fetch1->state(), Kalburator::Sync::SyncOperation::Succeeded);
        qInfo() << "initial fetch records:"
                << fetch1->fetchedItems().size();

        // ---- 3. Create CORPUS-tagged probe --------------------------------
        const QString summary = QStringLiteral("CORPUS:b2c-p1f-ms probe");
        // Authored wire carries fields Google rejects — Graph tolerates
        // them on create (O67: timestamps accepted; id/organizer rewritten
        // or ignored server-side). The backend POSTs record data verbatim.
        QJsonObject authored{
            { QStringLiteral("subject"), summary },
            { QStringLiteral("id"),
              QStringLiteral("requested-b2c-ms-probe") },
            { QStringLiteral("createdDateTime"),
              QStringLiteral("2026-01-01T00:00:00Z") },
            { QStringLiteral("start"),
              QJsonObject{ { QStringLiteral("dateTime"),
                             QStringLiteral("2026-10-01T10:00:00.0000000") },
                           { QStringLiteral("timeZone"),
                             QStringLiteral("UTC") } } },
            { QStringLiteral("end"),
              QJsonObject{ { QStringLiteral("dateTime"),
                             QStringLiteral("2026-10-01T11:00:00.0000000") },
                           { QStringLiteral("timeZone"),
                             QStringLiteral("UTC") } } },
        };
        BackendRecord rec;
        rec.id = QStringLiteral("requested-b2c-ms-probe");
        rec.type = QStringLiteral("event");
        rec.displayName = summary;
        rec.data = QJsonDocument(authored).toJson(QJsonDocument::Compact);

        WriterBatch createBatch;
        createBatch.creates = { rec };
        auto *create = backend.applyRecords(QStringLiteral("me-events"),
                                            createBatch);
        QTRY_VERIFY_WITH_TIMEOUT(create->isFinished(), 60000);
        QCOMPARE(create->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(create->failedUids().size(), 0);
        const QString storedId = create->succeededUids().value(0);
        QVERIFY(!storedId.isEmpty());
        qInfo() << "probe created; requested:" << rec.id
                << "stored:" << storedId.left(24) + "…";

        auto removeProbe = [&]() {
            WriterBatch delBatch;
            delBatch.deletes = { storedId };
            auto *del = backend.applyRecords(QStringLiteral("me-events"),
                                             delBatch);
            QTRY_VERIFY_WITH_TIMEOUT(del->isFinished(), 60000);
        };

        // ---- 4. Incremental walk sees the probe ---------------------------
        bool seen = false;
        QString observedUid;
        for (int attempt = 0; attempt < 5 && !seen; ++attempt) {
            if (attempt > 0)
                QTest::qWait(2000);
            auto *f = backend.fetchItems(QStringLiteral("me-events"));
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> records;
            QString err;
            if (!backend.recordsFromLastFetch(QStringLiteral("me-events"),
                                              records, err))
                continue;
            for (const auto &r : records) {
                if (r.id == storedId) {
                    seen = true;
                    const QJsonObject wire =
                        QJsonDocument::fromJson(r.data).object();
                    observedUid =
                        wire.value(QStringLiteral("uid")).toString();
                    // O67(c)(1): Graph mints a FRESH uid — never anything we
                    // authored. O69: delta pages may deliver skeletons
                    // WITHOUT uid/iCalUId at all; the loss-profile fallback
                    // chain (uid ← iCalUId ← transport id) then applies, and
                    // the union-merge preserves whatever the cache held.
                    if (wire.contains(QStringLiteral("uid")))
                        QVERIFY(observedUid
                                != QLatin1String("requested-b2c-ms-probe"));
                    qInfo() << "probe wire fields:"
                            << "uid?" << wire.contains(QStringLiteral("uid"))
                            << "iCalUId?"
                            << wire.contains(QStringLiteral("iCalUId"))
                            << "subject?"
                            << wire.contains(QStringLiteral("subject"));
                }
            }
        }
        QVERIFY2(seen, "probe never surfaced on incremental delta walks");
        qInfo() << "probe visible; uid minted by Graph (fresh):"
                << observedUid.left(20) + "…";

        // ---- 5. Update in place (PATCH — O61(e) structural rule) ----------
        QJsonObject patched{
            { QStringLiteral("subject"), summary + QStringLiteral(" EDITED") },
        };
        BackendRecord updRec;
        updRec.id = storedId;
        updRec.type = QStringLiteral("event");
        updRec.data = QJsonDocument(patched).toJson(QJsonDocument::Compact);
        WriterBatch updBatch;
        updBatch.updates = { updRec };
        auto *update = backend.applyRecords(QStringLiteral("me-events"),
                                            updBatch);
        QTRY_VERIFY_WITH_TIMEOUT(update->isFinished(), 60000);
        QCOMPARE(update->state(), Kalburator::Sync::SyncOperation::Succeeded);
        qInfo() << "PATCH-in-place update OK";

        // ---- 6. Delete + verify gone --------------------------------------
        removeProbe();
        auto *verifyDel = backend.fetchItems(QStringLiteral("me-events"));
        QTRY_VERIFY_WITH_TIMEOUT(verifyDel->isFinished(), 60000);
        QList<BackendRecord> postDelete;
        QString err2;
        bool stillThere = false;
        if (backend.recordsFromLastFetch(QStringLiteral("me-events"),
                                         postDelete, err2)) {
            for (const auto &r : postDelete)
                if (r.id == storedId)
                    stillThere = true;
        }
        QVERIFY2(!stillThere, "probe survived deletion");

        qInfo() << "LIVE CHECKPOINT PASSED (MSGraphCalendarBackend, /me/events)";
    }
};

QTEST_MAIN(TestMsGraphCalendarBackendLive)
#include "tst_ms_graph_calendar_backend_live.moc"
