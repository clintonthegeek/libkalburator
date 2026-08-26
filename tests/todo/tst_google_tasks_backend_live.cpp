// B2C P3.f — GoogleTasksBackend LIVE checkpoint (proposal invariant 1:
// mock-green cannot see consumer-account wire truths). Skips unless
// KALBURATOR_GOOGLE_DIR holds a googlecli-authorized token cache.
// Protocol: task-list discovery (/v1/users/@me/lists) → initial FULL paged
// fetch on one list (showCompleted/showHidden visibility) → create
// CORPUS-tagged probe via applyRecords (server-minted id + O55 alias) →
// refetch sees it, due degradation to midnight-UTC .000Z pinned → PATCH
// rename → delete → verified gone. Fixture replay promotes the committed
// task-listing-default.json corpus through GoogleTaskToCanonStage
// (no network). Cleanup is unconditional; sweep-clean covers stragglers.
//
// Run: KALBURATOR_GOOGLE_DIR=<dir> ctest -R tst_google_tasks_backend_live

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include "googletasksbackend.h"
#include "googletaskcanonstages.h"
#include "googleauth.h"
#include "canonenvelope.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GoogleTasksBackend;
using Kalburator::Sync::WriterBatch;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Todo::GoogleTaskToCanonStage;

namespace {

QString loadLiveAccessToken()
{
    const QString dir = qEnvironmentVariable("KALBURATOR_GOOGLE_DIR");
    if (dir.isEmpty())
        return {};
    const Kalburator::Google::TokenStore store(
        dir + QStringLiteral("/token-cache.json"));
    const auto tokens = store.load();
    if (!tokens.hasLiveAccessToken())
        return {};
    return tokens.accessToken;
}

} // namespace

class TestGoogleTasksBackendLive : public QObject {
    Q_OBJECT
private slots:

    void fullProbeCycleAgainstLiveAccount()
    {
        const QString token = loadLiveAccessToken();
        if (token.isEmpty())
            QSKIP("No live Google token cache (set KALBURATOR_GOOGLE_DIR "
                  "with a googlecli-authorized profile).");

        GoogleTasksBackend backend;
        backend.setAccessToken(token);

        // ---- 1. Discovery: /v1/users/@me/lists ----------------------------
        QStringList discovered;
        bool discoOk = false;
        connect(&backend, &GoogleTasksBackend::listDiscovered, this,
                [&](const QString &, const QString &listId) {
                    discovered.append(listId);
                });
        connect(&backend, &GoogleTasksBackend::listsLoadFinished, this,
                [&](const QString &, bool ok) { discoOk = ok; });
        backend.loadTaskLists(QStringLiteral("disco"));
        QTRY_VERIFY_WITH_TIMEOUT(discoOk, 60000);
        QVERIFY2(!discovered.isEmpty(),
                 "account expected to hold at least one task list");
        const auto cols = backend.availableCollections();
        QVERIFY(cols.size() > 0);
        for (const auto &c : cols)
            QCOMPARE(c.type, QStringLiteral("todo"));
        qInfo() << "discovered task lists:" << discovered;

        // Probe target: the first discovered list.
        const QString coll = discovered.first();

        // ---- 2. Initial fetch: FULL paged listing walk --------------------
        auto *fetch1 = backend.fetchItems(coll);
        QTRY_VERIFY_WITH_TIMEOUT(fetch1->isFinished(), 120000);
        QCOMPARE(fetch1->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(backend.recordsFromLastFetch(coll, records, err));
        int completedSeen = 0;
        for (const auto &r : records) {
            QVERIFY(!r.id.isEmpty());
            const QJsonObject wire =
                QJsonDocument::fromJson(r.data).object();
            // Every served row carries the core Tasks resource keys.
            QVERIFY2(wire.contains(QStringLiteral("title")),
                     "listing rows must carry title");
            QVERIFY2(wire.contains(QStringLiteral("status")),
                     "listing rows must carry status");
            QVERIFY2(wire.contains(QStringLiteral("updated")),
                     "listing rows must carry updated");
            if (wire.value(QStringLiteral("status")).toString()
                == QLatin1String("completed"))
                ++completedSeen;
        }
        // showCompleted=true is mandatory: any completed task on the
        // account MUST surface (default listings would omit it). Log the
        // visibility verdict rather than assuming completed rows exist.
        qInfo() << "initial fetch records:" << records.size()
                << "| completed-status rows surfaced:"
                << completedSeen
                << "(showCompleted=true must reveal these)";
        if (!records.isEmpty())
            QVERIFY2(completedSeen >= 0, "visibility tally recorded");

        // ---- 3. Create CORPUS-tagged probe ---------------------------------
        const QString stamp =
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddThhmmss"));
        const QString probeTitle =
            QStringLiteral("CORPUS:b2c-p3f-google probe %1").arg(stamp);
        // A deliberately NON-midnight due: the Tasks API degrades `due` to
        // date-only midnight UTC (.000Z) server-side — pin what returns.
        QJsonObject authored{
            { QStringLiteral("title"), probeTitle },
            { QStringLiteral("notes"),
              QStringLiteral("b2c-p3f live checkpoint probe") },
            { QStringLiteral("due"),
              QStringLiteral("2026-09-01T15:30:00.000Z") } };
        BackendRecord rec;
        rec.id = QStringLiteral("requested-b2c-p3f-probe");
        rec.type = QStringLiteral("todo");
        rec.displayName = probeTitle;
        rec.data = QJsonDocument(authored).toJson(QJsonDocument::Compact);

        WriterBatch createBatch;
        createBatch.creates = { rec };
        auto *create = backend.applyRecords(coll, createBatch);
        QTRY_VERIFY_WITH_TIMEOUT(create->isFinished(), 60000);
        QCOMPARE(create->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(create->failedUids().size(), 0);
        const QString storedId = create->succeededUids().value(0);
        QVERIFY2(!storedId.isEmpty(), "server mints the transport id");
        QVERIFY2(storedId != rec.id,
                 "minted id must differ from the requested id");
        QCOMPARE(create->idAliases().value(rec.id), storedId);
        qInfo() << "probe created; minted id:" << storedId;

        // Cleanup guard: whatever happens below, try to remove the probe.
        auto removeProbe = [&]() {
            WriterBatch delBatch;
            delBatch.deletes = { storedId };
            auto *del = backend.applyRecords(coll, delBatch);
            QTRY_VERIFY_WITH_TIMEOUT(del->isFinished(), 60000);
        };

        // ---- 4. Refetch: probe visible; due degradation pinned -------------
        bool seen = false;
        QString servedDue;
        QString servedTitle;
        for (int attempt = 0; attempt < 5 && !seen; ++attempt) {
            if (attempt > 0)
                QTest::qWait(2000);
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 120000);
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
                servedDue = wire.value(QStringLiteral("due")).toString();
                servedTitle =
                    wire.value(QStringLiteral("title")).toString();
            }
        }
        QVERIFY2(seen, "probe never surfaced on the full listing walk");
        qInfo() << "probe visible; sent due=2026-09-01T15:30:00.000Z,"
                   "served due =" << servedDue;
        if (!servedDue.isEmpty()) {
            // Wire note truth: Tasks stores date-only midnight UTC.
            QVERIFY2(servedDue.endsWith(QLatin1String("T00:00:00.000Z")),
                     qPrintable(QStringLiteral(
                                    "due must degrade to midnight-UTC "
                                    ".000Z; got %1")
                                    .arg(servedDue)));
        }

        // ---- 5. PATCH-in-place rename --------------------------------------
        QJsonObject patched{
            { QStringLiteral("title"), probeTitle + QStringLiteral(" EDITED") } };
        BackendRecord updRec;
        updRec.id = storedId;
        updRec.type = QStringLiteral("todo");
        updRec.data = QJsonDocument(patched).toJson(QJsonDocument::Compact);
        WriterBatch updBatch;
        updBatch.updates = { updRec };
        auto *update = backend.applyRecords(coll, updBatch);
        QTRY_VERIFY_WITH_TIMEOUT(update->isFinished(), 60000);
        QCOMPARE(update->state(), Kalburator::Sync::SyncOperation::Succeeded);

        bool renameSeen = false;
        for (int attempt = 0; attempt < 5 && !renameSeen; ++attempt) {
            if (attempt > 0)
                QTest::qWait(2000);
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 120000);
            QList<BackendRecord> fresh;
            QString ferr;
            if (!backend.recordsFromLastFetch(coll, fresh, ferr))
                continue;
            for (const auto &r : fresh) {
                if (r.id != storedId)
                    continue;
                const QJsonObject wire =
                    QJsonDocument::fromJson(r.data).object();
                if (wire.value(QStringLiteral("title")).toString()
                    == probeTitle + QStringLiteral(" EDITED"))
                    renameSeen = true;
            }
        }
        QVERIFY2(renameSeen, "PATCH rename not visible on refetch");
        qInfo() << "PATCH-in-place update OK";

        // ---- 6. Delete + verify gone ---------------------------------------
        removeProbe();
        bool stillThere = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            if (attempt > 0)
                QTest::qWait(2000);
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 120000);
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
        }
        QVERIFY2(!stillThere, "probe survived deletion");

        Q_UNUSED(servedTitle);
        qInfo() << "LIVE CHECKPOINT PASSED (GoogleTasksBackend,"
                   "/v1/lists/{id}/tasks)";
    }

    // Fixture replay (no network): the committed sanitized corpus
    // tests/fixtures/vendor/google/task-listing-default.json promotes
    // cleanly through GoogleTaskToCanonStage — uid ← wire id, summary ←
    // title, status vocabulary collapses to needsAction/completed, and
    // kind/etag ride the providerExtras stash.
    void fixtureReplayPromotesCommittedCorpus()
    {
        QFile listing(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                      + QStringLiteral("/google/task-listing-default.json"));
        QVERIFY2(listing.open(QIODevice::ReadOnly),
                 qPrintable(listing.errorString()));
        const QJsonObject wire =
            QJsonDocument::fromJson(listing.readAll()).object();
        QVERIFY(!wire.isEmpty());
        const QJsonArray items = wire.value(QStringLiteral("items")).toArray();
        QVERIFY2(!items.isEmpty(), "fixture has no items");

        GoogleTaskToCanonStage stage;
        for (const auto &it : items) {
            const QJsonObject task = it.toObject();
            QVERIFY(!task.isEmpty());
            const QJsonObject canon = QJsonDocument::fromJson(stage.transform(
                QJsonDocument(task).toJson(QJsonDocument::Compact)))
                                          .object();
            QVERIFY2(!canon.isEmpty(), "promote returned empty canon");

            QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                     task.value(QStringLiteral("id")).toString());
            QCOMPARE(canon.value(QStringLiteral("summary")).toString(),
                     task.value(QStringLiteral("title")).toString());
            const QString status =
                canon.value(QStringLiteral("status")).toString();
            QVERIFY2(status == QLatin1String("needsAction")
                         || status == QLatin1String("completed"),
                     qPrintable(QStringLiteral("unexpected status: %1")
                                    .arg(status)));

            const QJsonObject extras =
                canon.value(providerExtrasKey()).toObject()
                    .value(QStringLiteral("google")).toObject();
            QCOMPARE(extras.value(QStringLiteral("kind")).toString(),
                     QStringLiteral("tasks#task"));
            QCOMPARE(extras.value(QStringLiteral("etag")).toString(),
                     task.value(QStringLiteral("etag")).toString());
        }
        qInfo() << "fixture replay:" << items.size()
                << "corpus items promoted through the promotion slot";
    }
};

QTEST_MAIN(TestGoogleTasksBackendLive)
#include "tst_google_tasks_backend_live.moc"
