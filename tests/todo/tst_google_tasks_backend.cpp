// B2C P3.c — GoogleTasksBackend tests against the P3.b mock Google Tasks
// server. Pins the v1 contract: taskList discovery, FULL paged listings
// carrying showCompleted/showHidden (default listings omit completed and
// deleted rows; deleted rows are tombstoned), no sync-token machinery
// (refetch is full by design), O68 strip-at-create with minted ids aliased
// via O55, PATCH-in-place updates, idempotent deletes (404 ⇒ success),
// persistence resume with defensive union-merge over cached rich copies
// (O69 lesson).

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "googletasksbackend.h"
#include "mockgoogletasksserver.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GoogleTasksBackend;
using Kalburator::Sync::WriterBatch;
using Kalburator::Sync::WriteOperation;
using Kalburator::Tasks::MockGoogleTasksServer;

namespace {

QJsonObject wireTask(const QString &id, const QString &title)
{
    return QJsonObject{
        { QStringLiteral("id"), id },
        { QStringLiteral("title"), title },
        { QStringLiteral("status"), QStringLiteral("needsAction") },
        { QStringLiteral("updated"),
          QStringLiteral("2026-08-25T10:00:00.000Z") },
        { QStringLiteral("notes"),
          QStringLiteral("notes for %1").arg(title) } };
}

QJsonObject completedTask(const QString &id, const QString &title)
{
    QJsonObject t = wireTask(id, title);
    t.insert(QStringLiteral("status"), QStringLiteral("completed"));
    return t;
}

QJsonObject deletedTask(const QString &id, const QString &title)
{
    QJsonObject t = wireTask(id, title);
    t.insert(QStringLiteral("deleted"), true);
    return t;
}

WriterBatch singleCreate(const QString &requestedId, const QJsonObject &wire)
{
    WriterBatch batch;
    BackendRecord r;
    r.id = requestedId;
    r.type = QStringLiteral("todo");
    r.data = QJsonDocument(wire).toJson(QJsonDocument::Compact);
    batch.creates.append(r);
    return batch;
}

} // namespace

class TestGoogleTasksBackend : public QObject {
    Q_OBJECT

private slots:

    void init()
    {
        m_server = std::make_unique<MockGoogleTasksServer>(this);
        QVERIFY(m_server->start());
        m_backend = new GoogleTasksBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("test-token"));
    }

    void cleanup()
    {
        m_backend = nullptr;   // parented: deleted with this
        m_server.reset();
    }

    // Discovery: GET /v1/users/@me/lists surfaces every task list as an
    // available collection (writable v1); the DiscoveredCalendar-style DTO
    // pins todo-only component support.
    void discoverySurfacesTaskListsAsCollections()
    {
        QJsonArray lists;
        lists.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("list-1") },
            { QStringLiteral("title"), QStringLiteral("Kalburator") } });
        lists.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("list-2") },
            { QStringLiteral("title"), QStringLiteral("Work") } });
        m_server->setTaskLists(lists);

        QStringList discovered;
        bool done = false;
        connect(m_backend, &GoogleTasksBackend::listDiscovered, this,
                [&](const QString &, const QString &listId) {
                    discovered.append(listId);
                });
        connect(m_backend, &GoogleTasksBackend::listsLoadFinished, this,
                [&](const QString &, bool ok) { done = ok; });
        m_backend->loadTaskLists(QStringLiteral("req"));
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

        QCOMPARE(discovered.size(), 2);
        QVERIFY(discovered.contains(QStringLiteral("list-1")));
        QVERIFY(discovered.contains(QStringLiteral("list-2")));

        const auto cols = m_backend->availableCollections();
        QCOMPARE(cols.size(), 2);
        for (const auto &c : cols) {
            QCOMPARE(c.type, QStringLiteral("todo"));
            QVERIFY(c.contentTypes.contains(QStringLiteral("VTODO")));
            QVERIFY(!c.readOnly);
        }

        bool sawKalburator = false;
        for (const auto &c : cols) {
            if (c.id == QStringLiteral("list-1")
                && c.name == QStringLiteral("Kalburator"))
                sawKalburator = true;
        }
        QVERIFY(sawKalburator);

        const auto d = m_backend->discoveredTaskList(QStringLiteral("list-1"));
        QCOMPARE(d.backendType, QStringLiteral("google-tasks"));
        QVERIFY(d.supportsVTodo);
        QVERIFY(!d.supportsVEvent);
        QVERIFY(d.writable);
        QCOMPARE(d.name, QStringLiteral("Kalburator"));
    }

    // Initial fetch walks every page; completed + deleted rows arrive ONLY
    // because the recorded listing carries showCompleted/showHidden; the
    // deleted row is tombstoned out of the reported set.
    void initialFetchAcrossPagesWithVisibilityFlagsAndTombstones()
    {
        QJsonArray tasks;
        for (int i = 1; i <= 150; ++i)
            tasks.append(wireTask(QStringLiteral("t%1").arg(i),
                                  QStringLiteral("Task %1").arg(i)));
        tasks.append(completedTask(QStringLiteral("t-done"),
                                   QStringLiteral("Done one")));
        tasks.append(deletedTask(QStringLiteral("t-dead"),
                                 QStringLiteral("Dead one")));
        m_server->setTasks(QStringLiteral("L1"), tasks);

        m_server->clearRequests();
        auto *op = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 151);
        bool sawCompleted = false;
        for (const auto &r : records) {
            QVERIFY2(r.id != QLatin1String("t-dead"),
                     "deleted rows must be tombstoned from the report");
            if (r.id == QLatin1String("t-done"))
                sawCompleted = true;
            QCOMPARE(r.type, QStringLiteral("todo"));
            QVERIFY(!r.contentHash.isEmpty());
        }
        QVERIFY(sawCompleted);
        // Sorted by id ("t-done" < "t1" lexically).
        QCOMPARE(records.first().id, QStringLiteral("t-done"));

        // Listing truth: every page request carried BOTH visibility flags,
        // and the walk followed nextPageToken past the first 100-item page.
        int listingRequests = 0;
        bool sawPageTokenFollowUp = false;
        for (const auto &req : m_server->requests()) {
            if (req.method != "GET"
                || !req.path.startsWith(
                    QStringLiteral("/v1/lists/L1/tasks")))
                continue;
            ++listingRequests;
            QVERIFY2(req.path.contains(
                         QStringLiteral("showCompleted=true")),
                     qPrintable(req.path));
            QVERIFY2(req.path.contains(QStringLiteral("showHidden=true")),
                     qPrintable(req.path));
            if (req.path.contains(QStringLiteral("pageToken=")))
                sawPageTokenFollowUp = true;
        }
        QCOMPARE(listingRequests, 2);
        QVERIFY(sawPageTokenFollowUp);
    }

    // Refetch reports the FULL set every time; the memo is single-shot.
    // There is no sync-token machinery to expire — a plain refetch is a
    // fresh full listing by design.
    void refetchReportsFullSetAndMemoIsSingleShot()
    {
        QJsonArray tasks;
        tasks.append(wireTask(QStringLiteral("t2"), QStringLiteral("Two")));
        tasks.append(wireTask(QStringLiteral("t1"), QStringLiteral("One")));
        m_server->setTasks(QStringLiteral("L1"), tasks);

        auto *op1 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().id, QStringLiteral("t1"));

        QVERIFY(!m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                 records, err));

        QJsonArray changed;
        changed.append(wireTask(QStringLiteral("t1"), QStringLiteral("One")));
        changed.append(wireTask(QStringLiteral("t3"), QStringLiteral("Three")));
        m_server->setTasks(QStringLiteral("L1"), changed);

        auto *op2 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().id, QStringLiteral("t1"));
        QCOMPARE(records.last().id, QStringLiteral("t3"));
    }

    // Create flow pins: POST /v1/lists/L1/tasks body has NO id/created/
    // updated keys (O68 strip seam); mock mints mocktask<N>, bridged
    // requested→stored via id aliases (O55); Bearer header pinned.
    void createStripsReadOnlyFieldsMintsIdAndAliases()
    {
        QJsonObject authored = wireTask(QString(), QStringLiteral("Created"));
        authored.insert(QStringLiteral("created"),
                        QStringLiteral("2026-01-01T00:00:00.000Z"));
        authored.insert(QStringLiteral("updated"),
                        QStringLiteral("2026-01-02T00:00:00.000Z"));
        authored.remove(QStringLiteral("id"));
        authored.insert(QStringLiteral("id"), QStringLiteral("local-noise"));

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            QStringLiteral("L1"),
            singleCreate(QStringLiteral("local-requested"), authored));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 1);

        const QString storedId = QStringLiteral("mocktask1");
        QCOMPARE(op->succeededUids().first(), storedId);
        QCOMPARE(op->idAliases().value(QStringLiteral("local-requested")),
                 storedId);
        QVERIFY(storedId != QStringLiteral("local-requested"));

        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(0).path, QStringLiteral("/v1/lists/L1/tasks"));

        const QJsonObject postedBody =
            QJsonDocument::fromJson(reqs.at(0).body).object();
        QVERIFY2(!postedBody.contains(QStringLiteral("id")),
                 "create must strip the client transport id (O68)");
        QVERIFY2(!postedBody.contains(QStringLiteral("created")),
                 "create must strip created (O68)");
        QVERIFY2(!postedBody.contains(QStringLiteral("updated")),
                 "create must strip updated (O68)");
        QCOMPARE(postedBody.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Created"));
        QCOMPARE(postedBody.value(QStringLiteral("notes")).toString(),
                 QStringLiteral("notes for Created"));

        QCOMPARE(reqs.at(0).authorizationHeader,
                 QByteArrayLiteral("Bearer test-token"));
    }

    // Update: PATCH in place under the existing transport id; merge keeps
    // untouched fields intact; updates never alias.
    void updatePatchesInPlace()
    {
        QJsonArray tasks;
        tasks.append(wireTask(QStringLiteral("t9"), QStringLiteral("Before")));
        m_server->setTasks(QStringLiteral("L1"), tasks);

        QJsonObject patch{
            { QStringLiteral("id"), QStringLiteral("t9") },
            { QStringLiteral("title"), QStringLiteral("After") },
            { QStringLiteral("notes"),
              QStringLiteral("rewritten notes") } };

        WriterBatch batch;
        BackendRecord r;
        r.id = QStringLiteral("t9");
        r.type = QStringLiteral("todo");
        r.data = QJsonDocument(patch).toJson(QJsonDocument::Compact);
        batch.updates.append(r);

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(QStringLiteral("L1"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids(),
                 QStringList{ QStringLiteral("t9") });
        QVERIFY(op->idAliases().isEmpty());   // updates never alias

        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("PATCH"));
        QCOMPARE(reqs.at(0).path,
                 QStringLiteral("/v1/lists/L1/tasks/t9"));

        // Merge-in-place: a later fetch shows patched fields alongside the
        // untouched status row.
        auto *fetch = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(fetch->isFinished(), 5000);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().displayName, QStringLiteral("After"));
        const QJsonObject wire =
            QJsonDocument::fromJson(records.first().data).object();
        QCOMPARE(wire.value(QStringLiteral("status")).toString(),
                 QStringLiteral("needsAction"));
        QCOMPARE(wire.value(QStringLiteral("notes")).toString(),
                 QStringLiteral("rewritten notes"));
    }

    // Delete: existing task settles success (204); absent task (404) also
    // settles success — Tasks deletes are idempotent.
    void deleteSemanticsSuccessAnd404AsSuccess()
    {
        QJsonArray tasks;
        tasks.append(wireTask(QStringLiteral("doomed"),
                              QStringLiteral("Doomed")));
        m_server->setTasks(QStringLiteral("L1"), tasks);

        WriterBatch batch;
        batch.deletes.append(QStringLiteral("doomed"));
        batch.deletes.append(QStringLiteral("ghost"));

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(QStringLiteral("L1"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QVERIFY(op->succeededUids().contains(QStringLiteral("doomed")));
        QVERIFY(op->succeededUids().contains(QStringLiteral("ghost")));
        QVERIFY(op->failedUids().isEmpty());

        int deletes = 0;
        for (const auto &req : m_server->requests()) {
            if (req.method == QByteArrayLiteral("DELETE")) {
                ++deletes;
                QVERIFY(req.path.startsWith(
                    QStringLiteral("/v1/lists/L1/tasks/")));
            }
        }
        QCOMPARE(deletes, 2);
    }

    // Persistence resume: a fresh backend over the same cacheDir reloads
    // the cache; a reduced listing projection merges OVER cached rich
    // copies instead of clobbering them (O69 lesson).
    void persistenceResumeAndUnionMergeOverReducedRows()
    {
        QTemporaryDir cacheDir;
        QVERIFY(cacheDir.isValid());

        QJsonObject rich = wireTask(QStringLiteral("t1"),
                                    QStringLiteral("Rich One"));
        rich.insert(QStringLiteral("due"),
                    QStringLiteral("2026-09-01T00:00:00.000Z"));
        QJsonArray tasks;
        tasks.append(rich);
        m_server->setTasks(QStringLiteral("L1"), tasks);

        m_backend->setCacheDir(cacheDir.path());
        auto *op1 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
        QVERIFY(QFile::exists(cacheDir.path()
                              + QStringLiteral("/google-tasks-state.json")));

        // "Restart": brand-new backend, same cacheDir, same live server.
        m_backend->deleteLater();
        m_backend = new GoogleTasksBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("token"));
        m_backend->setCacheDir(cacheDir.path());

        // Server now serves a SKELETON projection of t1 plus newcomer t2.
        QJsonObject skeleton{
            { QStringLiteral("id"), QStringLiteral("t1") },
            { QStringLiteral("title"), QStringLiteral("Renamed") },
            { QStringLiteral("status"), QStringLiteral("needsAction") } };
        QJsonArray projected;
        projected.append(skeleton);
        projected.append(wireTask(QStringLiteral("t2"),
                                  QStringLiteral("Newcomer")));
        m_server->setTasks(QStringLiteral("L1"), projected);

        auto *op2 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        for (const auto &r : records) {
            const QJsonObject wire =
                QJsonDocument::fromJson(r.data).object();
            if (r.id == QStringLiteral("t1")) {
                // Skeleton keys won; cached-only keys survived the merge.
                QCOMPARE(r.displayName, QStringLiteral("Renamed"));
                QCOMPARE(wire.value(QStringLiteral("notes")).toString(),
                         QStringLiteral("notes for Rich One"));
                QCOMPARE(wire.value(QStringLiteral("due")).toString(),
                         QStringLiteral("2026-09-01T00:00:00.000Z"));
            } else {
                QCOMPARE(r.id, QStringLiteral("t2"));
                QCOMPARE(r.displayName, QStringLiteral("Newcomer"));
            }
        }
    }

    // Fetch failure surfaces on the operation.
    void fetchFailureSurfacesOnTheOperation()
    {
        const QString failingRoute =
            QStringLiteral("/v1/lists/LX/tasks"
                           "?showCompleted=true&showHidden=true"
                           "&maxResults=100");
        m_server->addRoute(QByteArrayLiteral("GET"), failingRoute,
                           QByteArrayLiteral("{}"), 500);

        auto *op = m_backend->fetchItems(QStringLiteral("LX"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
        QVERIFY(!op->errorString().isEmpty());
    }

private:
    std::unique_ptr<MockGoogleTasksServer> m_server;
    GoogleTasksBackend *m_backend = nullptr;
};

QTEST_MAIN(TestGoogleTasksBackend)
#include "tst_google_tasks_backend.moc"
