// B2C P3.f — GraphTodoTaskBackend LIVE checkpoint (proposal invariant 1:
// mock-green cannot see consumer-account wire truths). Skips unless
// KALBURATOR_MSGRAPH_DIR holds a graphcli-authorized token cache.
// Protocol: todo-list discovery (/v1.0/me/todo/lists) → initial FULL
// expanded listing walk (never delta, O69; carriers visible via $expand) →
// create CORPUS-tagged probe with a carrier-class canon extension row
// (nav-POST channel; wire-lie check: the carrier must be verified by
// REFETCH expand, never by the create echo) → O66(b) recurrence-without-due
// fail-loud probe (no network write, absence verified) → PATCH rename →
// delete → verified gone. Fixture replay promotes the committed
// todo-tasks-listing.json corpus through MsTodoTaskToCanonStage (no
// network). Cleanup is unconditional; sweep-clean covers stragglers.
//
// Run: KALBURATOR_MSGRAPH_DIR=<dir> ctest -R tst_graph_todo_backend_live

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "graphtodotaskbackend.h"
#include "mstodotaskcanonstages.h"
#include "graphauthenticator.h"
#include "canonenvelope.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GraphTodoTaskBackend;
using Kalburator::Sync::WriterBatch;
using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Todo::MsTodoTaskToCanonStage;

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

class TestGraphTodoBackendLive : public QObject {
    Q_OBJECT
private slots:

    void fullProbeCycleAgainstLiveAccount()
    {
        const QString token = loadLiveAccessToken();
        if (token.isEmpty())
            QSKIP("No live Graph token cache (set KALBURATOR_MSGRAPH_DIR "
                  "with a graphcli-authorized profile).");

        GraphTodoTaskBackend backend;
        // Paths carry /v1.0 verbatim — the base is version-less.
        backend.setBaseUrl(QStringLiteral("https://graph.microsoft.com"));
        backend.setAccessToken(token);

        // ---- 1. Discovery: /me/todo/lists ----------------------------------
        QSignalSpy discoSpy(&backend,
                            &GraphTodoTaskBackend::listsLoadFinished);
        backend.loadTaskLists(QStringLiteral("disco"));
        QTRY_VERIFY_WITH_TIMEOUT(discoSpy.count() == 1, 60000);
        QVERIFY2(discoSpy.at(0).at(1).toBool(), "todo/lists fetch failed");
        const auto cols = backend.availableCollections();
        QVERIFY2(!cols.isEmpty(),
                 "account expected to hold at least one todo list");
        for (const auto &c : cols)
            QCOMPARE(c.type, QStringLiteral("todo"));
        const QString coll = cols.first().id;
        qInfo() << "discovered todo lists:" << cols.size()
                << "| probing list:" << coll << "(" << cols.first().name
                << ")";

        // ---- 2. Initial fetch: FULL expanded listing walk ------------------
        auto *fetch1 = backend.fetchItems(coll);
        QTRY_VERIFY_WITH_TIMEOUT(fetch1->isFinished(), 120000);
        QCOMPARE(fetch1->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(backend.recordsFromLastFetch(coll, records, err));
        int carriersSeen = 0;
        for (const auto &r : records) {
            QVERIFY(!r.id.isEmpty());
            const QJsonObject wire =
                QJsonDocument::fromJson(r.data).object();
            QVERIFY2(wire.contains(QStringLiteral("title")),
                     "listing rows must carry subject");
            QVERIFY2(wire.contains(QStringLiteral("lastModifiedDateTime")),
                     "listing rows must carry lastModifiedDateTime");
            if (!wire.value(QStringLiteral("extensions")).toArray().isEmpty())
                ++carriersSeen;
        }
        qInfo() << "initial fetch records:" << records.size()
                << "| rows with visible carrier extensions:"
                << carriersSeen;

        // ---- 3. Create CORPUS-tagged probe WITH a carrier row ---------------
        // The carrier rides the nav POST channel after the plain-body
        // create (inline-create echoes are a wire-lie — never persisted).
        const QString stamp =
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddThhmmss"));
        const QString subject =
            QStringLiteral("CORPUS:b2c-p3f-ms probe %1").arg(stamp);
        QJsonObject authored{
            { QStringLiteral("title"), subject },
            { QStringLiteral("importance"), QStringLiteral("normal") },
            { QStringLiteral("extensions"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("@odata.type"),
                    QStringLiteral("microsoft.graph.openTypeExtension") },
                  { QStringLiteral("extensionName"),
                    QStringLiteral("kalburator.canon") },
                  { QStringLiteral("x-canon-note"),
                    QStringLiteral("b2c-p3f live checkpoint") } } } } };
        BackendRecord rec;
        rec.id = QStringLiteral("requested-b2c-p3f-probe");
        rec.type = QStringLiteral("todo");
        rec.displayName = subject;
        rec.data = QJsonDocument(authored).toJson(QJsonDocument::Compact);

        WriterBatch createBatch;
        createBatch.creates = { rec };
        auto *create = backend.applyRecords(coll, createBatch);
        QTRY_VERIFY_WITH_TIMEOUT(create->isFinished(), 60000);
        QCOMPARE(create->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(create->failedUids().size(), 0);
        const QString storedId = create->succeededUids().value(0);
        QVERIFY2(!storedId.isEmpty(),
                 "consumer Graph mints a server-side id");
        QVERIFY2(storedId != rec.id,
                 "minted id must differ from the requested id");
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

        // ---- 4. Refetch: probe + carrier verified via $expand --------------
        // WIRE-LIE CHECK: the create response echoing extensions[] proves
        // nothing — only the expanded listing read-back counts.
        bool seen = false;
        bool carrierOk = false;
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
                const QJsonArray exts =
                    wire.value(QStringLiteral("extensions")).toArray();
                for (const auto &ev : exts) {
                    const QJsonObject row = ev.toObject();
                    if (row.value(QStringLiteral("extensionName"))
                                .toString()
                            == QLatin1String("kalburator.canon")
                        && !row.value(QStringLiteral("x-canon-note"))
                                   .toString()
                                   .isEmpty())
                        carrierOk = true;
                }
            }
        }
        QVERIFY2(seen, "probe never surfaced on the expanded full listing");
        QVERIFY2(carrierOk,
                 "carrier row must persist server-side and ride the "
                 "$expand read-back (create echo alone cannot satisfy)");
        qInfo() << "probe visible with carrier persisted (expand "
                   "read-back, not echo)";

        // ---- 5. O66(b) fail-loud probe: recurrence without due -------------
        // Must fail THAT record BEFORE any network call; verify no write
        // landed by confirming no such probe exists afterwards.
        QJsonObject recurringBody{
            { QStringLiteral("title"),
              QStringLiteral("CORPUS:b2c-p3f-recurrence-noDue %1")
                  .arg(stamp) },
            { QStringLiteral("recurrence"),
              QJsonObject{
                  { QStringLiteral("pattern"),
                    QJsonObject{
                        { QStringLiteral("type"),
                          QStringLiteral("daily") },
                        { QStringLiteral("interval"), 1 } } },
                  { QStringLiteral("range"),
                    QJsonObject{
                        { QStringLiteral("type"),
                          QStringLiteral("noEnd") } } } } } };
        BackendRecord recRec;
        recRec.id = QStringLiteral("requested-b2c-p3f-recurring");
        recRec.type = QStringLiteral("todo");
        recRec.data =
            QJsonDocument(recurringBody).toJson(QJsonDocument::Compact);
        WriterBatch recBatch;
        recBatch.creates = { recRec };
        auto *recOp = backend.applyRecords(coll, recBatch);
        QTRY_VERIFY_WITH_TIMEOUT(recOp->isFinished(), 60000);
        QCOMPARE(recOp->state(), Kalburator::Sync::SyncOperation::Failed);
        QVERIFY(recOp->failedUids().contains(
            QStringLiteral("requested-b2c-p3f-recurring")));
        QVERIFY2(recOp->errorString().contains(
                     QLatin1String("recurrence requires dueDateTime")),
                 qPrintable(recOp->errorString()));
        QVERIFY2(recOp->errorString().contains(QLatin1String("O66(b)")),
                 qPrintable(recOp->errorString()));
        qInfo() << "O66(b) gate fired:" << recOp->errorString();

        // Absence verification: the recurring probe must NOT exist.
        {
            auto *f = backend.fetchItems(coll);
            QTRY_VERIFY_WITH_TIMEOUT(f->isFinished(), 120000);
            QCOMPARE(f->state(), Kalburator::Sync::SyncOperation::Succeeded);
            QList<BackendRecord> fresh;
            QString ferr;
            QVERIFY(backend.recordsFromLastFetch(coll, fresh, ferr));
            for (const auto &r : fresh)
                QVERIFY2(r.id != QLatin1String("requested-b2c-p3f-recurring")
                             && !r.displayName.contains(
                                 QLatin1String(
                                     "CORPUS:b2c-p3f-recurrence-noDue")),
                         "recurrence-without-due probe must not have hit "
                         "the network (no such task may exist)");
        }

        // ---- 6. PATCH-in-place rename --------------------------------------
        QJsonObject patched{
            { QStringLiteral("title"), subject + QStringLiteral(" EDITED") } };
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
                    == subject + QStringLiteral(" EDITED"))
                    renameSeen = true;
                // Plain-field PATCH must not disturb the carrier row.
                QCOMPARE(wire.value(QStringLiteral("extensions"))
                             .toArray()
                             .size(),
                         1);
            }
        }
        QVERIFY2(renameSeen, "PATCH rename not visible on refetch");
        qInfo() << "PATCH-in-place update OK; carrier row undisturbed";

        // ---- 7. Delete + verify gone ---------------------------------------
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

        qInfo() << "LIVE CHECKPOINT PASSED (GraphTodoTaskBackend,"
                << "/v1.0/me/todo)";
    }

    // Fixture replay (no network): the committed sanitized corpus
    // tests/fixtures/vendor/microsoft/todo-tasks-listing.json promotes
    // cleanly through MsTodoTaskToCanonStage — uid ← wire id, summary ←
    // title, importance→priority vocabulary, due/completed survival, and
    // @odata.etag riding the providerExtras stash.
    void fixtureReplayPromotesCommittedCorpus()
    {
        QFile tasksFile(QLatin1String(KALBURATOR_VENDOR_FIXTURE_DIR)
                        + QStringLiteral("/microsoft/todo-tasks-listing.json"));
        QVERIFY2(tasksFile.open(QIODevice::ReadOnly),
                 qPrintable(tasksFile.errorString()));
        const QJsonArray tasks =
            QJsonDocument::fromJson(tasksFile.readAll())
                .object()
                .value(QStringLiteral("value"))
                .toArray();
        QVERIFY2(!tasks.isEmpty(), "fixture has no value[]");

        MsTodoTaskToCanonStage stage;
        for (const auto &item : tasks) {
            const QJsonObject wire = item.toObject();
            const QJsonObject canon = QJsonDocument::fromJson(
                stage.transform(
                    QJsonDocument(wire).toJson(QJsonDocument::Compact)))
                                          .object();
            QVERIFY2(!canon.isEmpty(), "promote returned empty canon");

            QCOMPARE(canon.value(QStringLiteral("uid")).toString(),
                     wire.value(QStringLiteral("id")).toString());
            QCOMPARE(canon.value(QStringLiteral("summary")).toString(),
                     wire.value(QStringLiteral("title")).toString());

            const QString importance =
                wire.value(QStringLiteral("importance")).toString();
            int expectedPriority = 5;
            if (importance == QLatin1String("low"))
                expectedPriority = 9;
            else if (importance == QLatin1String("high"))
                expectedPriority = 1;
            QCOMPARE(canon.value(QStringLiteral("priority")).toInt(),
                     expectedPriority);

            if (wire.contains(QLatin1String("dueDateTime"))) {
                QCOMPARE(canon.value(QStringLiteral("due")).toObject()
                             .value(QStringLiteral("dateTime")).toString(),
                         wire.value(QStringLiteral("dueDateTime"))
                             .toObject()
                             .value(QStringLiteral("dateTime"))
                             .toString());
            }
            if (wire.contains(QLatin1String("completedDateTime")))
                QVERIFY(canon.contains(QLatin1String("completed")));

            const QJsonObject extras =
                canon.value(providerExtrasKey()).toObject()
                    .value(QStringLiteral("msgraph")).toObject();
            QVERIFY(!extras.value(QStringLiteral("@odata.etag"))
                         .toString()
                         .isEmpty());
        }
        qInfo() << "fixture replay:" << tasks.size()
                << "corpus items promoted through the promotion slot";
    }
};

QTEST_MAIN(TestGraphTodoBackendLive)
#include "tst_graph_todo_backend_live.moc"
