// B2C P2.c — GraphContactsBackend tests against the P2.b mock Graph contacts
// server. Pins the v1 contract: expanded FULL listings (never delta, O70),
// carrier extensions visible via $expand, create strips extensions[] and
// nav-POSTs carriers (O66 correction), '='-suffixed minted ids alias
// requested→stored (O55/O66(d)), PATCH-in-place updates, idempotent deletes
// with a 404-then-relist confirmation (O66(f)), persistence resume, and the
// defensive union-merge over cached rich copies (O69 lesson).

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "graphcontactsbackend.h"
#include "mockgraphcontactsserver.h"

using Kalburator::Contacts::MockGraphContactsServer;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GraphContactsBackend;
using Kalburator::Sync::WriterBatch;
using Kalburator::Sync::WriteOperation;

namespace {

const QString kCanonExtensionId = QStringLiteral(
    "Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon");

QJsonObject wireContact(const QString &id, const QString &displayName,
                        bool withCarrier = false)
{
    QJsonObject contact{
        { QStringLiteral("id"), id },
        { QStringLiteral("displayName"), displayName },
        { QStringLiteral("givenName"), QStringLiteral("Given") },
        { QStringLiteral("surname"), displayName },
        { QStringLiteral("emailAddresses"),
          QJsonArray{ QJsonObject{
              { QStringLiteral("address"),
                QStringLiteral("%1@example.test")
                    .arg(displayName.toLower()) } } } } };
    if (withCarrier) {
        contact.insert(QStringLiteral("extensions"),
                       QJsonArray{ QJsonObject{
                           { QStringLiteral("id"), kCanonExtensionId },
                           { QStringLiteral("extensionName"),
                             QStringLiteral("kalburator.canon") },
                           { QStringLiteral("x-canon-gender"),
                             QStringLiteral("unspecified") } } });
    }
    return contact;
}

WriterBatch singleCreate(const QString &requestedId, const QJsonObject &wire)
{
    WriterBatch batch;
    BackendRecord r;
    r.id = requestedId;
    r.type = QStringLiteral("contact");
    r.data = QJsonDocument(wire).toJson(QJsonDocument::Compact);
    batch.creates.append(r);
    return batch;
}

} // namespace

class TestGraphContactsBackend : public QObject {
    Q_OBJECT

private slots:

    void init()
    {
        m_server = std::make_unique<MockGraphContactsServer>(this);
        QVERIFY(m_server->start());
        m_backend = new GraphContactsBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("test-token"));
    }

    void cleanup()
    {
        m_backend = nullptr;   // parented: deleted with this
        m_server.reset();
    }

    // Discovery: GET /me/contactFolders surfaces every folder as an
    // available collection (writable v1).
    void discoverySurfacesFoldersAsCollections()
    {
        QJsonArray folders;
        folders.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("fld-1") },
            { QStringLiteral("displayName"), QStringLiteral("Kalburator") } });
        folders.append(QJsonObject{
            { QStringLiteral("id"), QStringLiteral("fld-2") },
            { QStringLiteral("displayName"), QStringLiteral("Work") } });
        m_server->setContactFolders(folders);

        bool done = false;
        connect(m_backend, &GraphContactsBackend::foldersLoadFinished, this,
                [&](const QString &, bool ok) { done = ok; });
        m_backend->loadFolders(QStringLiteral("req"));
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

        const auto cols = m_backend->availableCollections();
        QCOMPARE(cols.size(), 2);
        for (const auto &c : cols) {
            QCOMPARE(c.type, QStringLiteral("contacts"));
            QVERIFY(c.contentTypes.contains(QStringLiteral("VCARD")));
            QVERIFY(!c.readOnly);
        }
        QVERIFY(m_backend->discoveredWritable(QStringLiteral("fld-1")));

        bool sawKalburator = false;
        for (const auto &c : cols) {
            if (c.id == QStringLiteral("fld-1")
                && c.name == QStringLiteral("Kalburator"))
                sawKalburator = true;
        }
        QVERIFY(sawKalburator);
    }

    // Initial fetch: records across default + folder collections, carrier
    // extensions delivered by the $expand walk.
    void initialFetchAcrossDefaultAndFolderCollectionsWithCarriers()
    {
        QJsonArray defaultItems;
        defaultItems.append(
            wireContact(QStringLiteral("cid-def"), QStringLiteral("Default"),
                        true));
        m_server->addCollection(QStringLiteral("/v1.0/me/contacts"),
                                defaultItems);

        QJsonArray folderItems;
        folderItems.append(
            wireContact(QStringLiteral("cid-fld"), QStringLiteral("Foldered"),
                        true));
        m_server->addCollection(
            QStringLiteral("/v1.0/me/contactFolders/fld-1/contacts"),
            folderItems);

        auto *opDefault =
            m_backend->fetchItems(GraphContactsBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(opDefault->isFinished(), 5000);
        QCOMPARE(opDefault->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GraphContactsBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 1);
        {
            const QJsonObject wire =
                QJsonDocument::fromJson(records.first().data).object();
            QCOMPARE(wire.value(QStringLiteral("id")).toString(),
                     QStringLiteral("cid-def"));
            const QJsonArray exts =
                wire.value(QStringLiteral("extensions")).toArray();
            QVERIFY2(exts.size() == 1
                         && exts.at(0).toObject()
                                    .value(QStringLiteral("id"))
                                    .toString() == kCanonExtensionId,
                     "expand must reveal the stored carrier row");
            QVERIFY(!records.first().contentHash.isEmpty());
        }

        auto *opFolder = m_backend->fetchItems(QStringLiteral("fld-1"));
        QTRY_VERIFY_WITH_TIMEOUT(opFolder->isFinished(), 5000);
        QCOMPARE(opFolder->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QVERIFY(m_backend->recordsFromLastFetch(QStringLiteral("fld-1"),
                                                records, err));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().id, QStringLiteral("cid-fld"));

        // Both walks carried the expand filter on the right paths.
        bool sawDefaultExpand = false;
        bool sawFolderExpand = false;
        for (const auto &req : m_server->requests()) {
            if (req.method != "GET")
                continue;
            if (req.path.startsWith(QStringLiteral("/v1.0/me/contacts?")))
                sawDefaultExpand = req.path.contains(QStringLiteral("$expand"))
                    && req.path.contains(
                        QStringLiteral("kalburator.canon"));
            if (req.path.startsWith(
                    QStringLiteral("/v1.0/me/contactFolders/fld-1/contacts?")))
                sawFolderExpand = req.path.contains(QStringLiteral("$expand"));
        }
        QVERIFY2(sawDefaultExpand, "default fetch must use $expand");
        QVERIFY2(sawFolderExpand, "folder fetch must use $expand");
    }

    // Refetch reports the FULL merged set every time (engine diff contract),
    // and the memo is single-shot.
    void refetchReportsFullSetAndMemoIsSingleShot()
    {
        QJsonArray items;
        items.append(wireContact(QStringLiteral("cid-2"),
                                 QStringLiteral("Two")));
        items.append(wireContact(QStringLiteral("cid-1"),
                                 QStringLiteral("One")));
        m_server->addCollection(QStringLiteral("/v1.0/me/contacts"), items);

        auto *op1 = m_backend->fetchItems(
            GraphContactsBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GraphContactsBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 2);
        // Sorted by id.
        QCOMPARE(records.first().id, QStringLiteral("cid-1"));

        // Single-shot memo: second call without a fresh fetch falls through.
        QVERIFY(!m_backend->recordsFromLastFetch(
            GraphContactsBackend::defaultCollectionId(), records, err));

        // Server-side change; the refetch still reports the WHOLE set.
        QJsonArray changed;
        changed.append(wireContact(QStringLiteral("cid-1"),
                                   QStringLiteral("One")));
        changed.append(wireContact(QStringLiteral("cid-3"),
                                   QStringLiteral("Three")));
        m_server->setCollectionItems(QStringLiteral("/v1.0/me/contacts"),
                                     changed);

        auto *op2 = m_backend->fetchItems(
            GraphContactsBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QVERIFY(m_backend->recordsFromLastFetch(
            GraphContactsBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().id, QStringLiteral("cid-1"));
        QCOMPARE(records.last().id, QStringLiteral("cid-3"));
    }

    // Create flow pins: POST body has NO extensions key; stripped carrier row
    // rides the nav POST to /extensions; minted '='-suffixed id aliases the
    // requested id; Bearer Authorization header pinned.
    void createFlowStripsExtensionsNavPostsCarriersAndAliasesIds()
    {
        // The mock's collection-level POST needs a registered collection.
        m_server->addCollection(QStringLiteral("/v1.0/me/contacts"), {});

        QJsonObject authored = wireContact(QString(),
                                           QStringLiteral("Created"));
        authored.insert(QStringLiteral("extensions"),
                        QJsonArray{ QJsonObject{
                            { QStringLiteral("@odata.type"),
                              QStringLiteral(
                                  "microsoft.graph.openTypeExtension") },
                            { QStringLiteral("extensionName"),
                              QStringLiteral("kalburator.canon") },
                            { QStringLiteral("x-canon-gender"),
                              QStringLiteral("female") } } });

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            GraphContactsBackend::defaultCollectionId(),
            singleCreate(QStringLiteral("local-requested"), authored));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 1);

        // Mock mints AQMkTEST000001= — '='-suffixed, ≠ requested id.
        const QString storedId = QStringLiteral("AQMkTEST000001=");
        QCOMPARE(op->idAliases().value(QStringLiteral("local-requested")),
                 storedId);
        QVERIFY(storedId.endsWith(QLatin1Char('=')));
        QVERIFY(storedId != QStringLiteral("local-requested"));

        // Wire truth: plain POST first, then ONE nav carrier POST; the plain
        // body carries NO extensions key.
        const auto reqs = m_server->requests();
        QVERIFY(reqs.size() >= 2);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(0).path, QStringLiteral("/v1.0/me/contacts"));
        const QJsonObject postedBody =
            QJsonDocument::fromJson(reqs.at(0).body).object();
        QVERIFY2(!postedBody.contains(QStringLiteral("extensions")),
                 "create must strip extensions[] before POST");
        QCOMPARE(postedBody.value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("Created"));

        QCOMPARE(reqs.at(1).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(1).path,
                 QStringLiteral("/v1.0/me/contacts/%1/extensions").arg(storedId));
        const QJsonObject carrierBody =
            QJsonDocument::fromJson(reqs.at(1).body).object();
        QCOMPARE(carrierBody.value(QStringLiteral("extensionName")).toString(),
                 QStringLiteral("kalburator.canon"));
        QCOMPARE(carrierBody.value(QStringLiteral("x-canon-gender")).toString(),
                 QStringLiteral("female"));

        // Bearer auth on every request.
        for (const auto &req : reqs)
            QCOMPARE(req.authorizationHeader,
                     QByteArrayLiteral("Bearer test-token"));
    }

    // Update: PATCH-in-place with extensions[] stripped (a PATCH-borne
    // extensions key ⇒ 500 server-side), carrier change routed as nav POST.
    void updatePatchesInPlaceAndRoutesCarriersThroughNavChannel()
    {
        QJsonArray items;
        items.append(wireContact(QStringLiteral("srv-exist"),
                                 QStringLiteral("Before")));
        m_server->addCollection(QStringLiteral("/v1.0/me/contacts"), items);

        QJsonObject updated = wireContact(QStringLiteral("srv-exist"),
                                          QStringLiteral("After"));
        updated.insert(QStringLiteral("extensions"),
                       QJsonArray{ QJsonObject{
                           { QStringLiteral("@odata.type"),
                             QStringLiteral(
                                 "microsoft.graph.openTypeExtension") },
                           { QStringLiteral("extensionName"),
                             QStringLiteral("kalburator.canon") },
                           { QStringLiteral("x-canon-note"),
                             QStringLiteral("carried") } } });

        WriterBatch batch;
        BackendRecord r;
        r.id = QStringLiteral("srv-exist");
        r.type = QStringLiteral("contact");
        r.data = QJsonDocument(updated).toJson(QJsonDocument::Compact);
        batch.updates.append(r);

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            GraphContactsBackend::defaultCollectionId(), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids(), QStringList{ QStringLiteral("srv-exist") });
        QVERIFY(op->idAliases().isEmpty());   // updates never alias

        // PATCH succeeded ⇒ the body was extensions-free (the mock 500s a
        // PATCH-borne extensions key); the carrier rode its own nav POST.
        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 2);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("PATCH"));
        QCOMPARE(reqs.at(0).path,
                 QStringLiteral("/v1.0/me/contacts/srv-exist"));
        const QJsonObject patched =
            QJsonDocument::fromJson(reqs.at(0).body).object();
        QVERIFY2(!patched.contains(QStringLiteral("extensions")),
                 "PATCH must carry plain fields only");
        QCOMPARE(patched.value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("After"));
        QCOMPARE(reqs.at(1).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(1).path,
                 QStringLiteral("/v1.0/me/contacts/srv-exist/extensions"));
        QCOMPARE(QJsonDocument::fromJson(reqs.at(1).body)
                     .object()
                     .value(QStringLiteral("x-canon-note"))
                     .toString(),
                 QStringLiteral("carried"));
    }

    // Delete: 204 success; 404-then-relist ⇒ success when gone, failure when
    // still present (O66(f) flaky-delete semantics, no silent best-effort).
    void deleteSemantics204AndFlaky404Confirmation()
    {
        QJsonArray items;
        items.append(wireContact(QStringLiteral("doomed"),
                                 QStringLiteral("Doomed")));
        items.append(wireContact(QStringLiteral("stubborn"),
                                 QStringLiteral("Stubborn")));
        m_server->addCollection(QStringLiteral("/v1.0/me/contacts"), items);
        // Force stubborn's DELETE to 404 while it stays listed: exact routes
        // win over the generic handler in the mock.
        m_server->addRoute(QStringLiteral("DELETE"),
                           QStringLiteral("/v1.0/me/contacts/stubborn"),
                           QJsonObject{}, 404);

        WriterBatch batch;
        batch.deletes.append(QStringLiteral("doomed"));
        batch.deletes.append(QStringLiteral("stubborn"));

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            GraphContactsBackend::defaultCollectionId(), batch);
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
                        sawDelete404Relist = reqs.at(j).path
                                                     .startsWith(QStringLiteral(
                                                         "/v1.0/me/contacts"));
                        break;
                    }
                }
            }
        }
        QVERIFY2(sawDelete404Relist,
                 "a 404 delete must trigger one confirming re-list");
    }

    void deleteOfAbsentContactSucceedsViaRelist()
    {
        QJsonArray items;
        items.append(wireContact(QStringLiteral("cid-1"),
                                 QStringLiteral("One")));
        m_server->addCollection(QStringLiteral("/v1.0/me/contacts"), items);

        WriterBatch batch;
        batch.deletes.append(QStringLiteral("ghost"));

        auto *op = m_backend->applyRecords(
            GraphContactsBackend::defaultCollectionId(), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids(), QStringList{ QStringLiteral("ghost") });
        QVERIFY(op->failedUids().isEmpty());
    }

    // Persistence: a fresh backend over the same cacheDir resumes from the
    // persisted cache; a skeleton listing projection merges OVER the cached
    // rich copy instead of clobbering it (O69 lesson).
    void persistenceResumeAndDefensiveUnionMergeOverCachedRichCopy()
    {
        QTemporaryDir cacheDir;
        QVERIFY(cacheDir.isValid());

        QJsonArray rich;
        rich.append(wireContact(QStringLiteral("cid-1"),
                                QStringLiteral("Rich One"), true));
        m_server->addCollection(QStringLiteral("/v1.0/me/contacts"), rich);

        m_backend->setCacheDir(cacheDir.path());
        auto *op1 = m_backend->fetchItems(
            GraphContactsBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
        QVERIFY(QFile::exists(cacheDir.path()
                              + QStringLiteral(
                                  "/msgraph-contacts-state.json")));

        // "Restart": brand-new backend, same cacheDir, same live server.
        m_backend->deleteLater();
        m_backend = new GraphContactsBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("token"));
        m_backend->setCacheDir(cacheDir.path());

        // Server now serves a SKELETON projection of cid-1 plus a new cid-2.
        QJsonObject skeleton{
            { QStringLiteral("id"), QStringLiteral("cid-1") },
            { QStringLiteral("displayName"), QStringLiteral("Renamed") } };
        QJsonArray projected;
        projected.append(skeleton);
        projected.append(wireContact(QStringLiteral("cid-2"),
                                     QStringLiteral("Newcomer")));
        m_server->setCollectionItems(QStringLiteral("/v1.0/me/contacts"),
                                     projected);

        auto *op2 = m_backend->fetchItems(
            GraphContactsBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GraphContactsBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 2);
        for (const auto &r : records) {
            const QJsonObject wire =
                QJsonDocument::fromJson(r.data).object();
            if (r.id == QStringLiteral("cid-1")) {
                // Skeleton keys won; cached-only keys survived the merge.
                QCOMPARE(wire.value(QStringLiteral("displayName")).toString(),
                         QStringLiteral("Renamed"));
                QCOMPARE(wire.value(QStringLiteral("givenName")).toString(),
                         QStringLiteral("Given"));
                QVERIFY(wire.contains(QStringLiteral("emailAddresses")));
                QVERIFY(wire.contains(QStringLiteral("extensions")));
            } else {
                QCOMPARE(r.id, QStringLiteral("cid-2"));
                QCOMPARE(wire.value(QStringLiteral("surname")).toString(),
                         QStringLiteral("Newcomer"));
            }
        }
    }

    // Fetch failure surfaces on the operation (unmatched path ⇒ Graph 404
    // shape).
    void fetchFailureSurfacesOnTheOperation()
    {
        // No collection registered under fld-missing → ErrorItemNotFound.
        auto *op = m_backend->fetchItems(QStringLiteral("fld-missing"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
        QVERIFY2(op->errorString().contains(QLatin1String("ErrorItemNotFound")),
                 qPrintable(op->errorString()));
    }

private:
    std::unique_ptr<MockGraphContactsServer> m_server;
    GraphContactsBackend *m_backend = nullptr;
};

QTEST_MAIN(TestGraphContactsBackend)
#include "tst_graph_contacts_backend.moc"
