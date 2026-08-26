// B2C P3.d — GraphTodoTaskBackend tests against the P3.b mock Graph todo
// server. Pins the v1 contract: todo-list discovery, expanded FULL listings
// over every page (never delta, O69), carrier extensions visible via
// $expand plus the inline-create wire-lie pin, create strips extensions[]
// and nav-POSTs carriers (O73 UPSERT channel), '='-suffixed minted ids
// alias requested→stored verbatim (O55/O66(d)), the O66(b)
// recurrence-requires-dueDateTime fail-loud gate BEFORE any network call,
// PATCH-in-place updates, deletes with a 404-then-relist confirmation
// (O66(f)), persistence resume, and the defensive union-merge over cached
// rich copies (O69 lesson).

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "graphtodotaskbackend.h"
#include "mockgraphtodoserver.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GraphTodoTaskBackend;
using Kalburator::Sync::WriterBatch;
using Kalburator::Sync::WriteOperation;
using Kalburator::Todo::MockGraphTodoServer;

namespace {

const QString kCanonExtensionId = QStringLiteral(
    "microsoft.graph.openTypeExtension.kalburator.canon");

QJsonObject carrierRow(const QString &noteValue)
{
    return QJsonObject{
        { QStringLiteral("@odata.type"),
          QStringLiteral("microsoft.graph.openTypeExtension") },
        { QStringLiteral("extensionName"),
          QStringLiteral("kalburator.canon") },
        { QStringLiteral("x-canon-note"), noteValue } };
}

QJsonObject wireTask(const QString &id, const QString &subject,
                     bool withCarrier = false)
{
    QJsonObject task{
        { QStringLiteral("id"), id },
        { QStringLiteral("title"), subject },
        { QStringLiteral("importance"), QStringLiteral("normal") },
        { QStringLiteral("status"),
          QStringLiteral("notStarted") },
        { QStringLiteral("lastModifiedDateTime"),
          QStringLiteral("2026-08-25T10:00:00.000Z") } };
    if (withCarrier) {
        QJsonObject row = carrierRow(QStringLiteral("stored"));
        row.insert(QStringLiteral("id"), kCanonExtensionId);
        task.insert(QStringLiteral("extensions"),
                    QJsonArray{ row });
    }
    return task;
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

class TestGraphTodoBackend : public QObject {
    Q_OBJECT

private slots:

    void init()
    {
        m_server = std::make_unique<MockGraphTodoServer>(this);
        QVERIFY(m_server->start());
        m_backend = new GraphTodoTaskBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("test-token"));
    }

    void cleanup()
    {
        m_backend = nullptr;   // parented: deleted with this
        m_server.reset();
    }

    static QString tasksPath(const QString &listId)
    {
        return QStringLiteral("/v1.0/me/todo/lists/%1/tasks").arg(listId);
    }

    // Discovery: GET /me/todo/lists surfaces every list as an available
    // collection (writable v1); the DiscoveredCalendar-style DTO pins
    // todo-only component support (supportsVEvent=false).
    void discoverySurfacesTodoListsAsCollections()
    {
        QJsonArray lists;
        lists.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("list-1") },
            { QStringLiteral("displayName"), QStringLiteral("Kalburator") },
            { QStringLiteral("wellknownListName"),
              QStringLiteral("none") } });
        lists.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("list-2") },
            { QStringLiteral("displayName"), QStringLiteral("Work") },
            { QStringLiteral("wellknownListName"),
              QStringLiteral("default") } });
        m_server->setTodoLists(lists);

        QStringList discovered;
        bool done = false;
        connect(m_backend, &GraphTodoTaskBackend::listDiscovered, this,
                [&](const QString &, const QString &listId) {
                    discovered.append(listId);
                });
        connect(m_backend, &GraphTodoTaskBackend::listsLoadFinished, this,
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
            QVERIFY(m_backend->discoveredWritable(c.id));
        }
        bool sawKalburator = false;
        for (const auto &c : cols) {
            if (c.id == QStringLiteral("list-1")
                && c.name == QStringLiteral("Kalburator"))
                sawKalburator = true;
        }
        QVERIFY(sawKalburator);

        const auto d =
            m_backend->discoveredTaskList(QStringLiteral("list-1"));
        QCOMPARE(d.backendType, QStringLiteral("msgraph-todo"));
        QVERIFY(d.supportsVTodo);
        QVERIFY(!d.supportsVEvent);
        QVERIFY(d.writable);
        QCOMPARE(d.name, QStringLiteral("Kalburator"));
    }

    // Initial fetch walks every page of every fetched list; the $expand
    // filter reveals stored carrier rows; the inline-create WIRE-LIE is
    // pinned: a create whose RESPONSE echoes extensions[] does NOT make
    // carriers appear — only a nav POST persists them.
    void initialFetchAcrossListsAndPagesWithCarriersAndWireLiePin()
    {
        QJsonArray lists;
        lists.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("L1") },
            { QStringLiteral("displayName"), QStringLiteral("One") } });
        lists.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("L2") },
            { QStringLiteral("displayName"), QStringLiteral("Two") } });
        m_server->setTodoLists(lists);

        QJsonArray l1Items;
        for (int i = 1; i <= 15; ++i)
            l1Items.append(wireTask(QStringLiteral("t%1").arg(i),
                                    QStringLiteral("Task %1").arg(i)));
        m_server->addCollection(tasksPath(QStringLiteral("L1")), l1Items);

        QJsonArray l2Items;
        l2Items.append(wireTask(QStringLiteral("t-carrier"),
                                QStringLiteral("Carried"), true));
        m_server->addCollection(tasksPath(QStringLiteral("L2")), l2Items);

        m_server->clearRequests();
        auto *op1 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
        QCOMPARE(op1->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 15);
        for (const auto &r : records) {
            QCOMPARE(r.type, QStringLiteral("todo"));
            QVERIFY(!r.contentHash.isEmpty());
        }

        // Pagination truth: the walk followed @odata.nextLink past the
        // default 10-row page, and every page carried the $expand filter.
        int pageRequests = 0;
        int skipFollowUps = 0;
        for (const auto &req : m_server->requests()) {
            if (req.method != "GET"
                || !req.path.startsWith(tasksPath(QStringLiteral("L1"))))
                continue;
            ++pageRequests;
            QVERIFY2(req.path.contains(QStringLiteral("$expand")),
                     qPrintable(req.path));
            if (req.path.contains(QStringLiteral("$skip=")))
                ++skipFollowUps;
        }
        QVERIFY(pageRequests >= 2);
        QVERIFY(skipFollowUps >= 1);

        auto *op2 = m_backend->fetchItems(QStringLiteral("L2"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L2"),
                                                records, err));
        QCOMPARE(records.size(), 1);
        const QJsonObject wire =
            QJsonDocument::fromJson(records.first().data).object();
        const QJsonArray exts =
            wire.value(QStringLiteral("extensions")).toArray();
        QVERIFY2(exts.size() == 1
                     && exts.at(0).toObject()
                            .value(QStringLiteral("id"))
                            .toString() == kCanonExtensionId,
                 "expand must reveal the stored carrier row");

        // ---- inline-create WIRE-LIE pin ---------------------------------
        // A routed create whose response ECHOES extensions[] (the lie real
        // Graph tells) must not surface carriers: after a plain-body create
        // (no nav POST), the listing shows the created task with NO
        // extensions key.
        QJsonObject echoed = wireTask(QStringLiteral("AQMkWIRE="),
                                      QStringLiteral("Echoed"));
        echoed.insert(QStringLiteral("extensions"),
                      QJsonArray{ carrierRow(QStringLiteral("lied")) });
        m_server->addRoute(QByteArrayLiteral("POST"),
                           tasksPath(QStringLiteral("L1")), echoed, 201);

        // The server-side store keeps ONLY the extension-free copy.
        QJsonArray afterCreate = l1Items;
        afterCreate.append(
            wireTask(QStringLiteral("AQMkWIRE="), QStringLiteral("Echoed")));
        m_server->setCollectionItems(tasksPath(QStringLiteral("L1")),
                                     afterCreate);

        WriterBatch batch;
        BackendRecord r;
        r.id = QStringLiteral("local-wire");
        r.type = QStringLiteral("todo");
        r.data = QJsonDocument(
                     wireTask(QString(), QStringLiteral("Echoed")))
                     .toJson(QJsonDocument::Compact);
        batch.creates.append(r);

        auto *op3 = m_backend->applyRecords(QStringLiteral("L1"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op3->isFinished(), 5000);
        QCOMPARE(op3->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op3->idAliases().value(QStringLiteral("local-wire")),
                 QStringLiteral("AQMkWIRE="));

        auto *op4 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op4->isFinished(), 5000);
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        bool sawEchoedWithoutExtensions = false;
        for (const auto &rec : records) {
            if (rec.id != QLatin1String("AQMkWIRE="))
                continue;
            const QJsonObject w =
                QJsonDocument::fromJson(rec.data).object();
            sawEchoedWithoutExtensions =
                !w.contains(QStringLiteral("extensions"));
        }
        QVERIFY2(sawEchoedWithoutExtensions,
                 "the echoed create-response extensions[] must not "
                 "materialize as carriers");
    }

    // Refetch reports the FULL merged set every time (engine diff contract),
    // and the memo is single-shot.
    void refetchReportsFullSetAndMemoIsSingleShot()
    {
        QJsonArray items;
        items.append(wireTask(QStringLiteral("t2"), QStringLiteral("Two")));
        items.append(wireTask(QStringLiteral("t1"), QStringLiteral("One")));
        m_server->addCollection(tasksPath(QStringLiteral("L1")), items);

        auto *op1 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().id, QStringLiteral("t1"));

        // Single-shot memo: second call without a fresh fetch falls through.
        QVERIFY(!m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                 records, err));

        QJsonArray changed;
        changed.append(wireTask(QStringLiteral("t1"), QStringLiteral("One")));
        changed.append(wireTask(QStringLiteral("t3"), QStringLiteral("Three")));
        m_server->setCollectionItems(tasksPath(QStringLiteral("L1")),
                                     changed);

        auto *op2 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().id, QStringLiteral("t1"));
        QCOMPARE(records.last().id, QStringLiteral("t3"));
    }

    // Defensive union-merge (O69 lesson): a reduced/skeleton listing row is
    // enriched FROM the richer in-memory cache instead of clobbering it;
    // keys the row DOES carry win wholesale.
    void unionMergeEnrichesReducedListingRows()
    {
        QJsonArray items;
        items.append(wireTask(QStringLiteral("t1"), QStringLiteral("Rich"),
                              true));
        m_server->addCollection(tasksPath(QStringLiteral("L1")), items);

        auto *op1 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        // Server now serves a SKELETON projection of t1 (no importance,
        // no lastModifiedDateTime, no extensions).
        QJsonObject skeleton{
            { QStringLiteral("id"), QStringLiteral("t1") },
            { QStringLiteral("title"), QStringLiteral("Renamed") } };
        m_server->setCollectionItems(tasksPath(QStringLiteral("L1")),
                                     QJsonArray{ skeleton });

        auto *op2 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 1);
        const QJsonObject wire =
            QJsonDocument::fromJson(records.first().data).object();
        // Skeleton keys won...
        QCOMPARE(wire.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Renamed"));
        // ...cached-only keys survived the merge.
        QCOMPARE(wire.value(QStringLiteral("importance")).toString(),
                 QStringLiteral("normal"));
        QVERIFY(wire.contains(QStringLiteral("lastModifiedDateTime")));
        QVERIFY(wire.contains(QStringLiteral("extensions")));
    }

    // Create flow pins: POST body has NO extensions key; the stripped
    // carrier row rides the nav POST to …/tasks/{id}/extensions; the minted
    // '='-suffixed id aliases the requested id; '=' ids ride VERBATIM
    // (never %3D); Bearer Authorization header pinned.
    void createFlowStripsExtensionsNavPostsCarriersAndAliasesIds()
    {
        m_server->addCollection(tasksPath(QStringLiteral("L1")), {});

        QJsonObject authored =
            wireTask(QString(), QStringLiteral("Created"));
        authored.insert(QStringLiteral("extensions"),
                        QJsonArray{ carrierRow(QStringLiteral("authored")) });

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            QStringLiteral("L1"),
            singleCreate(QStringLiteral("local-requested"), authored));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 1);

        // Mock mints AQMkTEST000001= — '='-suffixed, ≠ requested id.
        const QString storedId = QStringLiteral("AQMkTEST000001=");
        QCOMPARE(op->succeededUids().first(), storedId);
        QCOMPARE(op->idAliases().value(QStringLiteral("local-requested")),
                 storedId);
        QVERIFY(storedId.endsWith(QLatin1Char('=')));
        QVERIFY(storedId != QStringLiteral("local-requested"));

        // Wire truth: plain POST first, then ONE nav carrier POST; the
        // plain body carries NO extensions key.
        const auto reqs = m_server->requests();
        QVERIFY(reqs.size() >= 2);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(0).path, tasksPath(QStringLiteral("L1")));
        const QJsonObject postedBody =
            QJsonDocument::fromJson(reqs.at(0).body).object();
        QVERIFY2(!postedBody.contains(QStringLiteral("extensions")),
                 "create must strip extensions[] before POST");
        QCOMPARE(postedBody.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Created"));

        QCOMPARE(reqs.at(1).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(1).path,
                 QStringLiteral("/v1.0/me/todo/lists/L1/tasks/%1/extensions")
                     .arg(storedId));
        QVERIFY2(!reqs.at(1).path.contains(QLatin1String("%3D")),
                 "'='-suffixed ids ride verbatim, never URL-encoded");
        const QJsonObject carrierBody =
            QJsonDocument::fromJson(reqs.at(1).body).object();
        QCOMPARE(carrierBody.value(QStringLiteral("extensionName")).toString(),
                 QStringLiteral("kalburator.canon"));
        QCOMPARE(carrierBody.value(QStringLiteral("x-canon-note")).toString(),
                 QStringLiteral("authored"));

        // Bearer auth on every request.
        for (const auto &req : reqs)
            QCOMPARE(req.authorizationHeader,
                     QByteArrayLiteral("Bearer test-token"));

        // The nav POST persisted the carrier: the next expand reveals it.
        auto *fetch = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(fetch->isFinished(), 5000);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("L1"),
                                                records, err));
        QCOMPARE(records.size(), 1);
        const QJsonArray exts =
            QJsonDocument::fromJson(records.first().data)
                .object()
                .value(QStringLiteral("extensions"))
                .toArray();
        QCOMPARE(exts.size(), 1);
        QCOMPARE(exts.at(0).toObject()
                     .value(QStringLiteral("extensionName"))
                     .toString(),
                 QStringLiteral("kalburator.canon"));
    }

    // O66(b) fail-loud rule: a create carrying a non-null `recurrence` but
    // no `dueDateTime` fails THAT record BEFORE any network call; the
    // terminal error names the rule; dates are never fabricated.
    void recurrenceWithoutDueFailsLoudBeforeAnyNetworkCall()
    {
        m_server->addCollection(tasksPath(QStringLiteral("L1")), {});
        m_server->clearRequests();

        QJsonObject authored =
            wireTask(QString(), QStringLiteral("Recurring"));
        authored.insert(QStringLiteral("recurrence"),
                        QJsonObject{
                            { QStringLiteral("pattern"),
                              QJsonObject{} },
                            { QStringLiteral("range"),
                              QJsonObject{} } });

        auto *op = m_backend->applyRecords(
            QStringLiteral("L1"),
            singleCreate(QStringLiteral("local-rec"), authored));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
        QVERIFY(op->failedUids().contains(QStringLiteral("local-rec")));
        QVERIFY(op->succeededUids().isEmpty());
        QVERIFY2(op->errorString().contains(
                     QLatin1String("recurrence requires dueDateTime")),
                 qPrintable(op->errorString()));
        QVERIFY2(op->errorString().contains(QLatin1String("O66(b)")),
                 qPrintable(op->errorString()));

        // No network call may have fired.
        QVERIFY2(m_server->requests().isEmpty(),
                 "the O66(b) gate must fire before any network traffic");

        // The same body WITH a dueDateTime passes the gate.
        QJsonArray items;
        items.append(wireTask(QStringLiteral("t1"), QStringLiteral("One")));
        m_server->setCollectionItems(tasksPath(QStringLiteral("L1")), items);

        QJsonObject okBody = authored;
        okBody.insert(QStringLiteral("dueDateTime"),
                      QJsonObject{
                          { QStringLiteral("dateTime"),
                            QStringLiteral("2026-09-01T00:00:00.000") },
                          { QStringLiteral("timeZone"),
                            QStringLiteral("UTC") } });
        WriterBatch batch;
        BackendRecord r;
        r.id = QStringLiteral("local-ok");
        r.type = QStringLiteral("todo");
        r.data = QJsonDocument(okBody).toJson(QJsonDocument::Compact);
        batch.creates.append(r);

        m_server->clearRequests();
        auto *opOk = m_backend->applyRecords(QStringLiteral("L1"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(opOk->isFinished(), 5000);
        QCOMPARE(opOk->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QVERIFY(m_server->requests().size() >= 1);
    }

    // Update: PATCH-in-place with extensions[] stripped (a PATCH-borne
    // extensions key ⇒ 500 server-side); the carrier change rides its own
    // nav POST; updates never alias.
    void updatePatchesInPlaceAndRoutesCarriersThroughNavChannel()
    {
        QJsonArray items;
        items.append(wireTask(QStringLiteral("srv-exist"),
                              QStringLiteral("Before")));
        m_server->addCollection(tasksPath(QStringLiteral("L1")), items);

        QJsonObject updated = wireTask(QStringLiteral("srv-exist"),
                                       QStringLiteral("After"));
        updated.insert(QStringLiteral("extensions"),
                       QJsonArray{ carrierRow(QStringLiteral("carried")) });

        WriterBatch batch;
        BackendRecord r;
        r.id = QStringLiteral("srv-exist");
        r.type = QStringLiteral("todo");
        r.data = QJsonDocument(updated).toJson(QJsonDocument::Compact);
        batch.updates.append(r);

        m_server->clearRequests();
        auto *op =
            m_backend->applyRecords(QStringLiteral("L1"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids(),
                 QStringList{ QStringLiteral("srv-exist") });
        QVERIFY(op->idAliases().isEmpty());   // updates never alias

        // PATCH succeeded ⇒ the body was extensions-free (the mock 500s a
        // PATCH-borne extensions key); the carrier rode its own nav POST.
        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 2);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("PATCH"));
        QCOMPARE(reqs.at(0).path,
                 tasksPath(QStringLiteral("L1")) + QStringLiteral("/srv-exist"));
        const QJsonObject patched =
            QJsonDocument::fromJson(reqs.at(0).body).object();
        QVERIFY2(!patched.contains(QStringLiteral("extensions")),
                 "PATCH must carry plain fields only");
        QCOMPARE(patched.value(QStringLiteral("title")).toString(),
                 QStringLiteral("After"));
        QCOMPARE(reqs.at(1).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(1).path,
                 QStringLiteral(
                     "/v1.0/me/todo/lists/L1/tasks/srv-exist/extensions"));
        QCOMPARE(QJsonDocument::fromJson(reqs.at(1).body)
                     .object()
                     .value(QStringLiteral("x-canon-note"))
                     .toString(),
                 QStringLiteral("carried"));
    }

    // Delete: 204 success; a flaky 404 triggers ONE confirming re-list —
    // gone ⇒ success, still present ⇒ fail loud (O66(f)).
    void deleteSemantics204AndFlaky404Confirmation()
    {
        QJsonArray items;
        items.append(wireTask(QStringLiteral("doomed"),
                              QStringLiteral("Doomed")));
        items.append(wireTask(QStringLiteral("stubborn"),
                              QStringLiteral("Stubborn")));
        m_server->addCollection(tasksPath(QStringLiteral("L1")), items);
        // Force stubborn's DELETE to 404 while it stays listed: exact routes
        // win over the generic handler in the mock.
        m_server->addRoute(
            QByteArrayLiteral("DELETE"),
            tasksPath(QStringLiteral("L1")) + QStringLiteral("/stubborn"),
            QJsonObject{}, 404);

        WriterBatch batch;
        batch.deletes.append(QStringLiteral("doomed"));
        batch.deletes.append(QStringLiteral("stubborn"));

        m_server->clearRequests();
        auto *op =
            m_backend->applyRecords(QStringLiteral("L1"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Terminal contract: some succeeded ⇒ op completes; stubborn fails
        // loud after the confirming re-list finds it still present.
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QVERIFY(op->succeededUids().contains(QStringLiteral("doomed")));
        QVERIFY(op->failedUids().contains(QStringLiteral("stubborn")));

        bool sawDelete404Relist = false;
        const auto reqs = m_server->requests();
        for (int i = 0; i < reqs.size(); ++i) {
            if (reqs.at(i).method == QByteArrayLiteral("DELETE")
                && reqs.at(i).path.endsWith(QStringLiteral("stubborn"))) {
                // The next GET must be the confirming re-list of the same
                // collection.
                for (int j = i + 1; j < reqs.size(); ++j) {
                    if (reqs.at(j).method == QByteArrayLiteral("GET")) {
                        sawDelete404Relist = reqs.at(j).path.startsWith(
                            tasksPath(QStringLiteral("L1")));
                        break;
                    }
                }
            }
        }
        QVERIFY2(sawDelete404Relist,
                 "a 404 delete must trigger one confirming re-list");
    }

    void deleteOfAbsentTaskSucceedsViaRelist()
    {
        QJsonArray items;
        items.append(wireTask(QStringLiteral("t1"), QStringLiteral("One")));
        m_server->addCollection(tasksPath(QStringLiteral("L1")), items);

        WriterBatch batch;
        batch.deletes.append(QStringLiteral("ghost"));

        auto *op =
            m_backend->applyRecords(QStringLiteral("L1"), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids(), QStringList{ QStringLiteral("ghost") });
        QVERIFY(op->failedUids().isEmpty());
    }

    // Persistence: a fresh backend over the same cacheDir resumes from the
    // persisted cache; a skeleton listing projection merges OVER the cached
    // rich copy instead of clobbering it (O69 lesson).
    void persistenceResumeAndUnionMergeOverCachedRichCopy()
    {
        QTemporaryDir cacheDir;
        QVERIFY(cacheDir.isValid());

        QJsonArray rich;
        rich.append(wireTask(QStringLiteral("t1"), QStringLiteral("Rich One"),
                             true));
        m_server->addCollection(tasksPath(QStringLiteral("L1")), rich);

        m_backend->setCacheDir(cacheDir.path());
        auto *op1 = m_backend->fetchItems(QStringLiteral("L1"));
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
        QVERIFY(QFile::exists(cacheDir.path()
                              + QStringLiteral("/msgraph-todo-state.json")));

        // "Restart": brand-new backend, same cacheDir, same live server.
        m_backend->deleteLater();
        m_backend = new GraphTodoTaskBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("token"));
        m_backend->setCacheDir(cacheDir.path());

        // Server now serves a SKELETON projection of t1 plus a newcomer t2.
        QJsonObject skeleton{
            { QStringLiteral("id"), QStringLiteral("t1") },
            { QStringLiteral("title"), QStringLiteral("Renamed") } };
        QJsonArray projected;
        projected.append(skeleton);
        projected.append(
            wireTask(QStringLiteral("t2"), QStringLiteral("Newcomer")));
        m_server->setCollectionItems(tasksPath(QStringLiteral("L1")),
                                     projected);

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
            if (r.id == QLatin1String("t1")) {
                // Skeleton keys won; cached-only keys survived the merge.
                QCOMPARE(wire.value(QStringLiteral("title")).toString(),
                         QStringLiteral("Renamed"));
                QCOMPARE(wire.value(QStringLiteral("importance")).toString(),
                         QStringLiteral("normal"));
                QVERIFY(wire.contains(QStringLiteral("extensions")));
            } else {
                QCOMPARE(r.id, QStringLiteral("t2"));
                QCOMPARE(wire.value(QStringLiteral("title")).toString(),
                         QStringLiteral("Newcomer"));
            }
        }
    }

    // Fetch failure surfaces on the operation (unmatched path ⇒ Graph 404
    // shape).
    void fetchFailureSurfacesOnTheOperation()
    {
        // No collection registered under LX → ErrorItemNotFound.
        auto *op = m_backend->fetchItems(QStringLiteral("LX"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
        QVERIFY2(op->errorString().contains(QLatin1String("ErrorItemNotFound")),
                 qPrintable(op->errorString()));
    }

private:
    std::unique_ptr<MockGraphTodoServer> m_server;
    GraphTodoTaskBackend *m_backend = nullptr;
};

QTEST_MAIN(TestGraphTodoBackend)
#include "tst_graph_todo_backend.moc"
