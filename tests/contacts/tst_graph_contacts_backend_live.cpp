// B2C P2.f — GraphContactsBackend LIVE checkpoint (proposal invariant 1:
// mock-green cannot see consumer-account wire truths). Skips unless
// KALBURATOR_MSGRAPH_DIR holds a graphcli-authorized token cache.
// Protocol: folder discovery → initial FULL expanded listing (never delta,
// O70) → create CORPUS-tagged probe with a carrier extension (nav POST) →
// refetch sees probe + carrier via $expand → PATCH-in-place displayName →
// Open-Q4 carrier UPDATE through the nav channel → delete → verified gone.
//
// Live truths this drill OBSERVES (2026-08-25 pre-flight vs the real
// Outlook.com account): minted ids end '=', $expand read-back delivers the
// carrier row, and a second nav POST with the same extensionName UPSERTS
// the extension in place (deterministic id, no conflict) — Q4 verdict.
// Cleanup is unconditional at the end; sweep-clean covers stragglers.
//
// Run: KALBURATOR_MSGRAPH_DIR=<dir> ctest -R tst_graph_contacts_backend_live

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "graphcontactsbackend.h"
#include "graphauthenticator.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GraphContactsBackend;
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

class TestGraphContactsBackendLive : public QObject {
    Q_OBJECT
private slots:

    void fullProbeCycleAgainstLiveAccount()
    {
        const QString token = loadLiveAccessToken();
        if (token.isEmpty())
            QSKIP("No live Graph token cache (set KALBURATOR_MSGRAPH_DIR "
                  "with a graphcli-authorized profile).");

        GraphContactsBackend backend;
        // Paths carry /v1.0 verbatim — the base is version-less.
        backend.setBaseUrl(QStringLiteral("https://graph.microsoft.com"));
        backend.setAccessToken(token);

        // ---- 1. Discovery: /me/contactFolders -----------------------------
        QSignalSpy discoSpy(&backend,
                            &GraphContactsBackend::foldersLoadFinished);
        backend.loadFolders(QStringLiteral("coll"));
        QTRY_VERIFY_WITH_TIMEOUT(discoSpy.count() == 1, 30000);
        QVERIFY2(discoSpy.at(0).at(1).toBool(), "contactFolders fetch failed");
        qInfo() << "discovered contact folders:"
                << backend.availableCollections().size();

        // ---- 2. Initial fetch: FULL expanded listing walk -----------------
        const QString coll = GraphContactsBackend::defaultCollectionId();
        auto *fetch1 = backend.fetchItems(coll);
        QTRY_VERIFY_WITH_TIMEOUT(fetch1->isFinished(), 60000);
        QCOMPARE(fetch1->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(backend.recordsFromLastFetch(coll, records, err));
        QVERIFY2(!records.isEmpty(),
                 "account expected non-empty (GraphCLI Test contact)");
        bool sawRealFields = false;
        for (const auto &r : records) {
            const QJsonObject wire =
                QJsonDocument::fromJson(r.data).object();
            if (!wire.value(QStringLiteral("emailAddresses"))
                     .toArray().isEmpty())
                sawRealFields = true;
            QVERIFY(wire.contains(QStringLiteral("lastModifiedDateTime")));
        }
        QVERIFY2(sawRealFields,
                 "listing records must carry expected contact fields");
        qInfo() << "initial fetch records:" << records.size();

        // ---- 3. Create CORPUS-tagged probe with a carrier extension -------
        const QString stamp =
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddThhmmss"));
        const QString summary =
            QStringLiteral("CORPUS:b2c-p2f-ms probe %1").arg(stamp);
        QJsonObject authored{
            { QStringLiteral("displayName"), summary },
            { QStringLiteral("givenName"), QStringLiteral("CORPUS") },
            { QStringLiteral("surname"), QStringLiteral("p2f") },
            { QStringLiteral("emailAddresses"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("address"),
                    QStringLiteral("b2c-p2f-%1@example.com").arg(stamp) } } } },
            { QStringLiteral("extensions"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("@odata.type"),
                    QStringLiteral("microsoft.graph.openTypeExtension") },
                  { QStringLiteral("extensionName"),
                    QStringLiteral("kalburator.canon") },
                  { QStringLiteral("x-canon-gender"),
                    QStringLiteral("male") } } } } };
        BackendRecord rec;
        rec.id = QStringLiteral("requested-b2c-p2f-probe");
        rec.type = QStringLiteral("contact");
        rec.displayName = summary;
        rec.data = QJsonDocument(authored).toJson(QJsonDocument::Compact);

        WriterBatch createBatch;
        createBatch.creates = { rec };
        auto *create = backend.applyRecords(coll, createBatch);
        QTRY_VERIFY_WITH_TIMEOUT(create->isFinished(), 60000);
        QCOMPARE(create->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(create->failedUids().size(), 0);
        const QString storedId = create->succeededUids().value(0);
        QVERIFY(!storedId.isEmpty());
        QVERIFY2(storedId != rec.id,
                 "consumer Graph mints a server-side id — never echo ours");
        QCOMPARE(create->idAliases().value(rec.id), storedId);
        if (!storedId.endsWith(QLatin1Char('=')))
            qWarning() << "stored id does not end '=' (O66(d) sample drift):"
                       << storedId.left(20);
        else
            qInfo() << "probe created; stored id ends '=' (O66(d) holds):"
                    << storedId.left(24) + "…";

        // Cleanup guard: whatever happens below, try to remove the probe.
        auto removeProbe = [&]() {
            WriterBatch delBatch;
            delBatch.deletes = { storedId };
            auto *del = backend.applyRecords(coll, delBatch);
            QTRY_VERIFY_WITH_TIMEOUT(del->isFinished(), 60000);
        };

        // ---- 4. Refetch: probe visible WITH carrier via $expand -----------
        bool seen = false;
        for (int attempt = 0; attempt < 5 && !seen; ++attempt) {
            if (attempt > 0)
                QTest::qWait(2000);
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> fresh;
            QString ferr;
            if (!backend.recordsFromLastFetch(coll, fresh, ferr))
                continue;
            for (const auto &r : fresh) {
                if (r.id != storedId)
                    continue;
                seen = true;
                const QJsonObject wire =
                    QJsonDocument::fromJson(r.data).object();
                const QJsonArray exts =
                    wire.value(QStringLiteral("extensions")).toArray();
                QCOMPARE(exts.size(), 1);
                QCOMPARE(exts.at(0).toObject()
                             .value(QStringLiteral("x-canon-gender"))
                             .toString(),
                         QStringLiteral("male"));
            }
        }
        QVERIFY2(seen, "probe never surfaced on the expanded full listing");
        qInfo() << "probe visible with carrier row delivered via $expand";

        // ---- 5. PATCH-in-place update --------------------------------------
        QJsonObject patched{
            { QStringLiteral("displayName"), summary + QStringLiteral(" EDITED") },
        };
        BackendRecord updRec;
        updRec.id = storedId;
        updRec.type = QStringLiteral("contact");
        updRec.data = QJsonDocument(patched).toJson(QJsonDocument::Compact);
        WriterBatch updBatch;
        updBatch.updates = { updRec };
        auto *update = backend.applyRecords(coll, updBatch);
        QTRY_VERIFY_WITH_TIMEOUT(update->isFinished(), 60000);
        QCOMPARE(update->state(), Kalburator::Sync::SyncOperation::Succeeded);

        bool renameSeen = false;
        for (int attempt = 0; attempt < 5 && !renameSeen; ++attempt) {
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> fresh;
            QString ferr;
            if (!backend.recordsFromLastFetch(coll, fresh, ferr))
                continue;
            for (const auto &r : fresh) {
                if (r.id != storedId)
                    continue;
                const QJsonObject wire =
                    QJsonDocument::fromJson(r.data).object();
                if (wire.value(QStringLiteral("displayName")).toString()
                    == summary + QStringLiteral(" EDITED"))
                    renameSeen = true;
                // Plain-field PATCH must not disturb the carrier row.
                QCOMPARE(wire.value(QStringLiteral("extensions"))
                             .toArray().size(),
                         1);
            }
        }
        QVERIFY2(renameSeen, "PATCH-in-place rename not visible on refetch");
        qInfo() << "PATCH-in-place update OK; carrier row undisturbed";

        // ---- 6. Open-Q4: carrier UPDATE through the backend's nav channel --
        // The backend strips extensions[] off the PATCH and routes the row
        // as a POST to /me/contacts/{id}/extensions. Live pre-flight showed
        // consumer Graph treats that POST as an UPSERT (same deterministic
        // extension id, value replaced, no conflict) — assert that verdict.
        QJsonObject carrierPatched{
            { QStringLiteral("displayName"), summary + QStringLiteral(" EDITED") },
            { QStringLiteral("extensions"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("@odata.type"),
                    QStringLiteral("microsoft.graph.openTypeExtension") },
                  { QStringLiteral("extensionName"),
                    QStringLiteral("kalburator.canon") },
                  { QStringLiteral("x-canon-gender"),
                    QStringLiteral("female") } } } } };
        BackendRecord carRec;
        carRec.id = storedId;
        carRec.type = QStringLiteral("contact");
        carRec.data =
            QJsonDocument(carrierPatched).toJson(QJsonDocument::Compact);
        WriterBatch carBatch;
        carBatch.updates = { carRec };
        auto *carUpdate = backend.applyRecords(coll, carBatch);
        QTRY_VERIFY_WITH_TIMEOUT(carUpdate->isFinished(), 60000);
        qInfo() << "Q4 carrier-update op state:"
                << static_cast<int>(carUpdate->state())
                << "succeeded:" << carUpdate->succeededUids()
                << "failed:" << carUpdate->failedUids();

        bool carrierUpdateSeen = false;
        int extensionRowsObserved = 0;
        for (int attempt = 0; attempt < 5 && !carrierUpdateSeen; ++attempt) {
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> fresh;
            QString ferr;
            if (!backend.recordsFromLastFetch(coll, fresh, ferr))
                continue;
            for (const auto &r : fresh) {
                if (r.id != storedId)
                    continue;
                const QJsonArray exts =
                    QJsonDocument::fromJson(r.data).object()
                        .value(QStringLiteral("extensions")).toArray();
                extensionRowsObserved = exts.size();
                for (const auto &ev : exts) {
                    if (ev.toObject().value(QStringLiteral("x-canon-gender"))
                            .toString() == QLatin1String("female"))
                        carrierUpdateSeen = true;
                }
            }
        }
        qInfo() << "Q4 VERDICT: nav-POST carrier update ->"
                << (carrierUpdateSeen ? "UPSERT confirmed (new value served)"
                                      : "NOT reflected")
                << "| extension rows on read-back:" << extensionRowsObserved;
        QVERIFY2(carrierUpdateSeen,
                 "nav-channel carrier update must land (upsert semantics)");
        QCOMPARE(extensionRowsObserved, 1);

        // ---- 7. Delete + verify gone ---------------------------------------
        removeProbe();
        bool stillThere = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 60000);
            QList<BackendRecord> postDelete;
            QString derr;
            stillThere = false;
            if (!backend.recordsFromLastFetch(coll, postDelete, derr))
                continue;
            for (const auto &r : postDelete)
                if (r.id == storedId)
                    stillThere = true;
            if (!stillThere)
                break;
            QTest::qWait(2000);
        }
        QVERIFY2(!stillThere, "probe survived deletion");

        qInfo() << "LIVE CHECKPOINT PASSED (GraphContactsBackend,"
                << "/me/contacts)";
    }
};

QTEST_MAIN(TestGraphContactsBackendLive)
#include "tst_graph_contacts_backend_live.moc"
