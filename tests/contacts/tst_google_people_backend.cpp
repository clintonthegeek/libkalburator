// B2C P2.d — GooglePeopleBackend tests against the P2.b mock People
// server. Pins the v1 contract: paged connections walks carrying the
// shared personFields projection, sync-token incremental walks with 410
// self-heal (O42), full-set commits with single-shot memo, inline
// clientData carriers at create (live-Reversible channel), minted
// resourceNames bridged via id aliases (O55), mask-derived PATCH merges,
// idempotent deletes where 404 ⇒ success, and persistence resume with
// defensive union-merge over cached rich copies (O69 lesson).

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "googlepeoplebackend.h"
#include "mockpeopleserver.h"

using Kalburator::People::MockPeopleServer;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::GooglePeopleBackend;
using Kalburator::Sync::WriterBatch;
using Kalburator::Sync::WriteOperation;

namespace {

const QString kPersonFields = QStringLiteral(
    "names,nicknames,emailAddresses,phoneNumbers,addresses,urls,"
    "relations,externalIds,memberships,imClients,calendarUrls,interests,"
    "skills,occupations,locales,sipAddresses,birthdays,genders,"
    "biographies,photos,organizations,fileAses,clientData,metadata");

QJsonObject wirePerson(const QString &resourceName, const QString &name,
                       bool withCarrier = false)
{
    QJsonObject person{
        { QStringLiteral("resourceName"), resourceName },
        { QStringLiteral("names"),
          QJsonArray{ QJsonObject{
              { QStringLiteral("displayName"), name },
              { QStringLiteral("givenName"),
                name.split(QLatin1Char(' ')).value(0) } } } },
        { QStringLiteral("emailAddresses"),
          QJsonArray{ QJsonObject{
              { QStringLiteral("value"),
                QStringLiteral("%1@example.test")
                    .arg(name.split(QLatin1Char(' ')).value(0)
                             .toLower()) } } } } };
    if (withCarrier) {
        person.insert(QStringLiteral("clientData"),
                      QJsonArray{ QJsonObject{
                          { QStringLiteral("key"),
                            QStringLiteral("x-canon-gender") },
                          { QStringLiteral("value"),
                            QStringLiteral("unspecified") } } });
    }
    return person;
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

class TestGooglePeopleBackend : public QObject {
    Q_OBJECT

private slots:

    void init()
    {
        m_server = std::make_unique<MockPeopleServer>(this);
        QVERIFY(m_server->start());
        m_backend = new GooglePeopleBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("test-token"));
    }

    void cleanup()
    {
        m_backend = nullptr;   // parented: deleted with this
        m_server.reset();
    }

    // Single implicit collection surfaced as available.
    void singleImplicitConnectionsCollection()
    {
        const auto cols = m_backend->availableCollections();
        QCOMPARE(cols.size(), 1);
        QCOMPARE(cols.first().id,
                 GooglePeopleBackend::defaultCollectionId());
        QCOMPARE(cols.first().type, QStringLiteral("contacts"));
        QVERIFY(cols.first().contentTypes.contains(QStringLiteral("VCARD")));
        QVERIFY(!cols.first().readOnly);
    }

    // Initial fetch walks every page (pageSize honored), reports the whole
    // set sorted, and served clientData carriers ride through verbatim.
    void initialFetchAcrossPagesWithClientDataCarriers()
    {
        QJsonArray people;
        people.append(wirePerson(QStringLiteral("people/c1"),
                                 QStringLiteral("Alpha One"), true));
        for (int i = 2; i <= 205; ++i)
            people.append(wirePerson(QStringLiteral("people/c%1").arg(i),
                                     QStringLiteral("Person %1").arg(i)));
        m_server->setConnections(people);

        auto *op = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 205);
        QCOMPARE(records.first().id, QStringLiteral("people/c1"));
        QVERIFY(records.last().id.startsWith(QStringLiteral("people/c9")));
        bool sawC205 = false;
        for (const auto &r : records) {
            if (r.id == QStringLiteral("people/c205"))
                sawC205 = true;
        }
        QVERIFY(sawC205);
        QCOMPARE(records.first().displayName,
                 QStringLiteral("Alpha One"));

        {
            const QJsonObject wire =
                QJsonDocument::fromJson(records.first().data).object();
            QCOMPARE(wire.value(QStringLiteral("resourceName")).toString(),
                     QStringLiteral("people/c1"));
            const QJsonArray cd =
                wire.value(QStringLiteral("clientData")).toArray();
            QVERIFY2(cd.size() == 1
                         && cd.at(0).toObject()
                                    .value(QStringLiteral("key"))
                                    .toString()
                                == QLatin1String("x-canon-gender"),
                     "carrier rows must survive the listing untouched");
            QVERIFY(!records.first().contentHash.isEmpty());
        }

        // Pagination truth: the walk followed nextPageToken past the first
        // 200-item page.
        bool sawPageTokenFollowUp = false;
        int listingRequests = 0;
        for (const auto &req : m_server->requests()) {
            if (req.method != "GET"
                || !req.path.startsWith(
                    QStringLiteral("/v1/people/me/connections")))
                continue;
            ++listingRequests;
            if (req.path.contains(QStringLiteral("pageToken=")))
                sawPageTokenFollowUp = true;
        }
        QCOMPARE(listingRequests, 2);
        QVERIFY(sawPageTokenFollowUp);
    }

    // Every listing walk carries the shared personFields projection
    // constant (the promote stage's exact read surface).
    void personFieldsProjectionPin()
    {
        QJsonArray people;
        people.append(wirePerson(QStringLiteral("people/c1"),
                                 QStringLiteral("One")));
        m_server->setConnections(people);

        auto *op = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        bool sawProjection = false;
        for (const auto &req : m_server->requests()) {
            if (req.method == "GET"
                && req.path.startsWith(
                    QStringLiteral("/v1/people/me/connections")))
                sawProjection =
                    req.path.contains(QStringLiteral("personFields=")
                                      + kPersonFields);
        }
        QVERIFY2(sawProjection,
                 "listing must project exactly the promote-stage field set");
    }

    // Incremental walk: stored sync token rides as sync_token; queued
    // changes merge into the reported full set; an expired token yields
    // 410 and ONE fresh initial re-walk self-heals.
    void incrementalWalkQueuedChangesAndExpiredTokenSelfHeal()
    {
        QJsonArray people;
        people.append(wirePerson(QStringLiteral("people/c1"),
                                 QStringLiteral("One")));
        people.append(wirePerson(QStringLiteral("people/c2"),
                                 QStringLiteral("Two")));
        m_server->setConnections(people);

        auto *op1 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        // Queue changes against the CURRENT live token: rename c1, add c3.
        QJsonArray changes;
        changes.append(wirePerson(QStringLiteral("people/c1"),
                                  QStringLiteral("One Renamed")));
        changes.append(wirePerson(QStringLiteral("people/c3"),
                                  QStringLiteral("Three")));
        m_server->queueConnectionChanges(QString(), changes);

        auto *op2 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        // Full merged set: c1 renamed, c2 retained, c3 added.
        QCOMPARE(records.size(), 3);
        for (const auto &r : records) {
            if (r.id == QStringLiteral("people/c1"))
                QCOMPARE(r.displayName, QStringLiteral("One Renamed"));
            if (r.id == QStringLiteral("people/c3"))
                QCOMPARE(r.displayName, QStringLiteral("Three"));
        }

        bool sawSyncTokenParam = false;
        for (const auto &req : m_server->requests()) {
            if (req.method == "GET"
                && req.path.contains(QStringLiteral("sync_token=sync_1")))
                sawSyncTokenParam = true;
        }
        QVERIFY(sawSyncTokenParam);

        // Expire: the next incremental walk gets 410 and self-heals with
        // ONE fresh initial walk.
        m_server->expireSyncTokens();
        m_server->clearRequests();
        auto *op3 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op3->isFinished(), 5000);
        QCOMPARE(op3->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<MockPeopleServer::RecordedRequest> gets;
        for (const auto &req : m_server->requests()) {
            if (req.method == "GET")
                gets.append(req);
        }
        QCOMPARE(gets.size(), 2);
        QVERIFY(gets.at(0).path.contains(QStringLiteral("sync_token=")));
        QVERIFY2(!gets.at(1).path.contains(QStringLiteral("sync_token=")),
                 "self-heal must re-walk WITHOUT the expired token");

        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        // The fresh listing is authoritative: the server still holds c1
        // (rename was a client-side change) and c2.
        QCOMPARE(records.size(), 2);
        for (const auto &r : records) {
            if (r.id == QStringLiteral("people/c1"))
                QCOMPARE(r.displayName, QStringLiteral("One"));
        }
    }

    // Refetch reports the FULL set every time; the memo is single-shot.
    void refetchReportsFullSetAndMemoIsSingleShot()
    {
        QJsonArray people;
        people.append(wirePerson(QStringLiteral("people/c2"),
                                 QStringLiteral("Two")));
        people.append(wirePerson(QStringLiteral("people/c1"),
                                 QStringLiteral("One")));
        m_server->setConnections(people);

        auto *op1 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().id, QStringLiteral("people/c1"));

        QVERIFY(!m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));

        QJsonArray changed;
        changed.append(wirePerson(QStringLiteral("people/c1"),
                                  QStringLiteral("One")));
        changed.append(wirePerson(QStringLiteral("people/c3"),
                                  QStringLiteral("Three")));
        m_server->setConnections(changed);
        // A plain refetch is INCREMENTAL (sync-token walk); force the
        // authoritative fresh listing by expiring the stored token.
        m_server->expireSyncTokens();

        auto *op2 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().id, QStringLiteral("people/c1"));
        QCOMPARE(records.last().id, QStringLiteral("people/c3"));
    }

    // Create flow pins: POST to :createContact carries clientData rows
    // INLINE (no strip-to-nav channel here), no etag/metadata; minted
    // resourceName settled + aliased; Bearer header pinned.
    void createPostsInlineCarriersMintsResourceNameAndAliases()
    {
        QJsonObject authored{
            { QStringLiteral("names"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("givenName"), QStringLiteral("Created") },
                  { QStringLiteral("familyName"), QStringLiteral("Person") } } } },
            { QStringLiteral("emailAddresses"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("value"),
                    QStringLiteral("created@example.test") } } } },
            { QStringLiteral("clientData"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("key"),
                    QStringLiteral("x-canon-gender") },
                  { QStringLiteral("value"), QStringLiteral("female") } } } },
            { QStringLiteral("etag"), QStringLiteral("etag-noise") },
            { QStringLiteral("metadata"), QJsonObject{} } };

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            GooglePeopleBackend::defaultCollectionId(),
            singleCreate(QStringLiteral("local-requested"), authored));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 1);

        // The mock mints people/c<N> — different from the requested id.
        const QString storedId = QStringLiteral("people/c1");
        QCOMPARE(op->idAliases().value(QStringLiteral("local-requested")),
                 storedId);

        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("POST"));
        QCOMPARE(reqs.at(0).path,
                 QStringLiteral("/v1/people/me:createContact"));

        const QJsonObject postedBody =
            QJsonDocument::fromJson(reqs.at(0).body).object();
        QVERIFY2(!postedBody.contains(QStringLiteral("etag")),
                 "create must strip etag defensively");
        QVERIFY2(!postedBody.contains(QStringLiteral("metadata")),
                 "create must strip metadata defensively");
        const QJsonArray cd =
            postedBody.value(QStringLiteral("clientData")).toArray();
        QCOMPARE(cd.size(), 1);
        QCOMPARE(cd.at(0).toObject().value(QStringLiteral("value")).toString(),
                 QStringLiteral("female"));
        QCOMPARE(postedBody.value(QStringLiteral("names"))
                     .toArray().at(0).toObject()
                     .value(QStringLiteral("familyName")).toString(),
                 QStringLiteral("Person"));

        QCOMPARE(reqs.at(0).authorizationHeader,
                 QByteArrayLiteral("Bearer test-token"));
    }

    // Update: PATCH :updateContact with updatePersonFields derived from the
    // patch body's own top-level keys; merge keeps unmasked fields intact.
    void updateDerivesMaskFromBodyKeysAndMergesInPlace()
    {
        QJsonObject seeded = wirePerson(QStringLiteral("people/c9"),
                                        QStringLiteral("Before"));
        seeded.insert(QStringLiteral("phoneNumbers"),
                      QJsonArray{ QJsonObject{
                          { QStringLiteral("value"),
                            QStringLiteral("+15550009") } } });
        QJsonArray people;
        people.append(seeded);
        m_server->setConnections(people);

        QJsonObject patch{
            { QStringLiteral("names"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("displayName"),
                    QStringLiteral("After") } } } },
            { QStringLiteral("emailAddresses"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("value"),
                    QStringLiteral("after@example.test") } } } } };

        WriterBatch batch;
        BackendRecord r;
        r.id = QStringLiteral("people/c9");
        r.type = QStringLiteral("contact");
        r.data = QJsonDocument(patch).toJson(QJsonDocument::Compact);
        batch.updates.append(r);

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            GooglePeopleBackend::defaultCollectionId(), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids(),
                 QStringList{ QStringLiteral("people/c9") });
        QVERIFY(op->idAliases().isEmpty());   // updates never alias

        const auto reqs = m_server->requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.at(0).method, QByteArrayLiteral("PATCH"));
        QVERIFY2(reqs.at(0).path.startsWith(
                     QStringLiteral("/v1/people/c9:updateContact"
                                    "?updatePersonFields=")),
                 qPrintable(reqs.at(0).path));

        // Mask derived from the body keys (order-free comparison).
        const int eq = reqs.at(0).path.indexOf(QLatin1Char('='));
        const QStringList maskFields =
            reqs.at(0).path.mid(eq + 1).split(QLatin1Char(','));
        QCOMPARE(maskFields.size(), 2);
        QVERIFY(maskFields.contains(QStringLiteral("names")));
        QVERIFY(maskFields.contains(QStringLiteral("emailAddresses")));

        // Merge-in-place: a later fetch shows patched fields alongside the
        // untouched phoneNumbers row.
        auto *fetch = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(fetch->isFinished(), 5000);
        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 1);
        const QJsonObject wire =
            QJsonDocument::fromJson(records.first().data).object();
        QCOMPARE(wire.value(QStringLiteral("names"))
                     .toArray().at(0).toObject()
                     .value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("After"));
        QCOMPARE(wire.value(QStringLiteral("phoneNumbers"))
                     .toArray().at(0).toObject()
                     .value(QStringLiteral("value")).toString(),
                 QStringLiteral("+15550009"));
    }

    // Delete: existing contact settles success; absent contact (404) also
    // settles success — People deletes are idempotent.
    void deleteSemanticsSuccessAnd404AsSuccess()
    {
        QJsonArray people;
        people.append(wirePerson(QStringLiteral("people/c7"),
                                 QStringLiteral("Doomed")));
        m_server->setConnections(people);

        WriterBatch batch;
        batch.deletes.append(QStringLiteral("people/c7"));
        batch.deletes.append(QStringLiteral("people/ghost"));

        m_server->clearRequests();
        auto *op = m_backend->applyRecords(
            GooglePeopleBackend::defaultCollectionId(), batch);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Succeeded);
        QVERIFY(op->succeededUids().contains(QStringLiteral("people/c7")));
        QVERIFY(op->succeededUids().contains(QStringLiteral("people/ghost")));
        QVERIFY(op->failedUids().isEmpty());

        int deletes = 0;
        for (const auto &req : m_server->requests()) {
            if (req.method == QByteArrayLiteral("DELETE")) {
                ++deletes;
                QVERIFY(req.path.endsWith(
                    QStringLiteral(":deleteContact")));
            }
        }
        QCOMPARE(deletes, 2);
    }

    // Persistence resume: a fresh backend over the same cacheDir reloads
    // cache + token; a 410-forced resync's skeleton projections merge OVER
    // cached rich copies instead of clobbering them (O69 lesson).
    void persistenceResumeAndUnionMergeOverSkeletonProjections()
    {
        QTemporaryDir cacheDir;
        QVERIFY(cacheDir.isValid());

        QJsonObject rich = wirePerson(QStringLiteral("people/c1"),
                                      QStringLiteral("Rich One"), true);
        rich.insert(QStringLiteral("phoneNumbers"),
                    QJsonArray{ QJsonObject{
                        { QStringLiteral("value"),
                          QStringLiteral("+15550001") } } });
        QJsonArray people;
        people.append(rich);
        m_server->setConnections(people);

        m_backend->setCacheDir(cacheDir.path());
        auto *op1 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
        QVERIFY(QFile::exists(cacheDir.path()
                              + QStringLiteral("/google-people-state.json")));

        // "Restart": brand-new backend, same cacheDir, same live server.
        m_backend->deleteLater();
        m_backend = new GooglePeopleBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("token"));
        m_backend->setCacheDir(cacheDir.path());

        // Expire the persisted token so the resumed walk hits 410 and
        // self-heals into a fresh full listing that now serves only a
        // SKELETON of c1 plus newcomer c2.
        m_server->expireSyncTokens();
        QJsonObject skeleton{
            { QStringLiteral("resourceName"), QStringLiteral("people/c1") },
            { QStringLiteral("names"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("displayName"),
                    QStringLiteral("Renamed") } } } } };
        QJsonArray projected;
        projected.append(skeleton);
        projected.append(wirePerson(QStringLiteral("people/c2"),
                                    QStringLiteral("Newcomer")));
        m_server->setConnections(projected);

        auto *op2 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
        QCOMPARE(op2->state(), Kalburator::Sync::SyncOperation::Succeeded);

        QList<BackendRecord> records;
        QString err;
        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 2);
        for (const auto &r : records) {
            const QJsonObject wire =
                QJsonDocument::fromJson(r.data).object();
            if (r.id == QStringLiteral("people/c1")) {
                // Skeleton keys won; cached-only keys survived the merge.
                QCOMPARE(wire.value(QStringLiteral("names"))
                             .toArray().at(0).toObject()
                             .value(QStringLiteral("displayName"))
                             .toString(),
                         QStringLiteral("Renamed"));
                QVERIFY(wire.contains(QStringLiteral("emailAddresses")));
                QCOMPARE(wire.value(QStringLiteral("phoneNumbers"))
                             .toArray().at(0).toObject()
                             .value(QStringLiteral("value")).toString(),
                         QStringLiteral("+15550001"));
                QVERIFY(wire.contains(QStringLiteral("clientData")));
            } else {
                QCOMPARE(r.id, QStringLiteral("people/c2"));
                QCOMPARE(r.displayName, QStringLiteral("Newcomer"));
            }
        }

        // A third backend resumes the REFRESHED token without a 410.
        m_backend->deleteLater();
        m_backend = new GooglePeopleBackend(this);
        m_backend->setBaseUrl(m_server->baseUrl());
        m_backend->setAccessToken(QStringLiteral("token"));
        m_backend->setCacheDir(cacheDir.path());
        m_server->clearRequests();

        auto *op3 = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op3->isFinished(), 5000);
        QCOMPARE(op3->state(), Kalburator::Sync::SyncOperation::Succeeded);
        for (const auto &req : m_server->requests()) {
            if (req.method == "GET")
                QVERIFY(req.path.contains(QStringLiteral("sync_token=")));
        }
        QVERIFY(m_backend->recordsFromLastFetch(
            GooglePeopleBackend::defaultCollectionId(), records, err));
        QCOMPARE(records.size(), 2);
    }

    // Fetch failure surfaces on the operation.
    void fetchFailureSurfacesOnTheOperation()
    {
        const QString failingRoute =
            QStringLiteral("/v1/people/me/connections"
                           "?pageSize=200&personFields=%1"
                           "&requestSyncToken=true").arg(kPersonFields);
        m_server->addRoute(QByteArrayLiteral("GET"), failingRoute,
                           QByteArrayLiteral("{}"), 500);

        auto *op = m_backend->fetchItems(
            GooglePeopleBackend::defaultCollectionId());
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
        QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
        QVERIFY(!op->errorString().isEmpty());
    }

private:
    std::unique_ptr<MockPeopleServer> m_server;
    GooglePeopleBackend *m_backend = nullptr;
};

QTEST_MAIN(TestGooglePeopleBackend)
#include "tst_google_people_backend.moc"
