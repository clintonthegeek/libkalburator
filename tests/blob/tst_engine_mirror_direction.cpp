/// G-phase Task 8 — failing test for ExecutionOverride mirror-direction semantics
///
/// Pins the expected behavior of
///   SyncEngine::runSyncFuture(mappingId, ExecutionOverride{Direction::MirrorAToB})
///   SyncEngine::runSyncFuture(mappingId, ExecutionOverride{Direction::MirrorBToA})
///
/// The stub added in Task 7 ignores the override and runs a normal two-way (or
/// first-sync mirror) sync, so these tests FAIL. Task 9 makes them pass by
/// wiring the override into dispatchBlobSync's direction-selection path.
///
/// Setup note: dispatchBlobSync routes based on nativeShapes() domain — any
/// non-calendar domain triggers the blob path. We define
/// IdentifiedBlobSyncBackend here: a SyncBackend subclass that advertises the
/// "memo" domain and delegates blob CRUD to an internal QHash store. This lets
/// us register backends via BackendRegistry (which wants SyncBackend*) while
/// still satisfying the IBlobBackend cast inside dispatchBlobSync.

#include <QtTest/QtTest>
#include <QHash>
#include <QList>
#include <QTemporaryDir>

#include "backendregistry.h"
#include "baselinestore.h"
#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "isynchost.h"
#include "isyncconfigstore.h"
#include "logicalcalendar.h"
#include "collectioninfo.h"
#include "pluginmanager.h"
#include "shape.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "synctypes.h"
#include "transformationregistry.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ExecutionOverride;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace {

// ---- Null ISyncConfigStore --------------------------------------------------

class NullConfigStore : public Kalburator::Sync::ISyncConfigStore
{
public:
    void addLogicalCalendar(const Kalburator::Sync::LogicalCalendar &) override {}
    void updateLogicalCalendar(const Kalburator::Sync::LogicalCalendar &) override {}
    void removeLogicalCalendar(const QString &) override {}
    Kalburator::Sync::LogicalCalendar logicalCalendar(const QString &) const override
        { return {}; }
    QVariantMap backendConfig(const QString &) const override { return {}; }
    bool hasSyncMappings() const override { return false; }
    QList<SyncMapping> syncMappings() const override { return {}; }
    void save() override {}
};

// ---- IdentifiedBlobSyncBackend ---------------------------------------------
//
// A SyncBackend subclass that:
//   - Returns DomainId{"memo"} in nativeShapes() so processSync routes to
//     dispatchBlobSync (not the calendar pipeline).
//   - Implements IBlobBackend using an internal QHash store.
//   - Stubs the calendar-domain methods (not used in blob-only tests).

class IdentifiedBlobSyncBackend : public SyncBackend
{
    Q_OBJECT
public:
    explicit IdentifiedBlobSyncBackend(const QString &id, QObject *p = nullptr)
        : SyncBackend(p), m_id(id) {}

    // ---- SyncBackend identity ----
    QString backendType() const override { return QStringLiteral("memo"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override
    {
        // Phase Ia.5 Task 9: declare the memo plugin's canonical shape
        // (memo/text) so dispatchSync's plugin-aware path finds an edge.
        // The test still treats payloads as opaque bytes; "raw" was a
        // legacy placeholder from before dispatchSync consulted plugins.
        return { Kalburator::Shape::Shape{
            DomainId{QStringLiteral("memo")},
            EncodingId{QStringLiteral("text")} } };
    }
    QString resourceId() const override
        { return QStringLiteral("memo-test:") + m_id; }

    // ---- IBlobBackend identity ----
    QString backendId()  const override { return m_id; }
    QString displayName() const override { return m_id; }
    bool    isAvailable() const override { return true; }

    // ---- IBlobBackend collections ----
    QList<CollectionInfo> availableCollections() override
        { return m_collections.values(); }
    CollectionInfo collectionInfo(const QString &id) override
        { return m_collections.value(id); }
    QString createCollection(const CollectionInfo &info) override
    {
        m_collections.insert(info.id, info);
        return info.id;
    }

    // ---- IBlobBackend records ----
    QList<BackendRecord> loadRecords(const QString &colId) override
        { return m_records.value(colId).values(); }
    std::optional<BackendRecord> loadRecord(const QString &recId) override
    {
        for (const auto &col : m_records)
            if (col.contains(recId)) return col.value(recId);
        return std::nullopt;
    }
    QString createRecord(const QString &colId, const BackendRecord &rec) override
    {
        m_records[colId][rec.id] = rec;
        m_recToCol[rec.id] = colId;
        return rec.id;
    }
    bool updateRecord(const BackendRecord &rec) override
    {
        const QString colId = m_recToCol.value(rec.id);
        if (colId.isEmpty()) return false;
        m_records[colId][rec.id] = rec;
        return true;
    }
    bool deleteRecord(const QString &recId) override
    {
        const QString colId = m_recToCol.value(recId);
        if (colId.isEmpty()) return false;
        m_records[colId].remove(recId);
        m_recToCol.remove(recId);
        return true;
    }
    QList<BackendRecord> modifiedSince(const QString &, const QDateTime &) override
        { return {}; }
    QStringList deletedSince(const QString &, const QDateTime &) override
        { return {}; }
    bool supportsDeleteTracking() const override { return false; }

    // ---- Blob batch (no-ops) ----
    void beginBatch() override {}
    bool commitBatch() override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

    // ---- Calendar-domain stubs (not used in blob tests) ----
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &, KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Engine::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &,
        const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }

    // ---- Test helpers ----
    QHash<QString, BackendRecord> recordsIn(const QString &colId) const
        { return m_records.value(colId); }

    void clear()
    {
        m_records.clear();
        m_recToCol.clear();
    }

private:
    QString m_id;
    QHash<QString, CollectionInfo>                        m_collections;
    QHash<QString /*colId*/, QHash<QString, BackendRecord>> m_records;
    QHash<QString /*recId*/, QString /*colId*/>           m_recToCol;
};

// ---- MinimalSyncHost --------------------------------------------------------
//
// Routes backendById() through the BackendRegistry. All other ISyncHost
// methods use the default no-op implementations from the base class.

class MinimalSyncHost : public ISyncHost
{
public:
    explicit MinimalSyncHost(BackendRegistry *reg)
        : m_reg(reg) {}

    SyncBackend *backendById(const QString &id) override
        { return m_reg->backendInstance(id); }
    QHash<QString, SyncBackend*> backends() override
    {
        QHash<QString, SyncBackend*> result;
        for (const QString &id : m_reg->registeredInstanceIds())
            result.insert(id, m_reg->backendInstance(id));
        return result;
    }
    Kalburator::Sync::ISyncConfigStore *configStore() override
        { return &m_config; }

private:
    BackendRegistry *m_reg; // not owned
    NullConfigStore  m_config;
};

// ---- Record factory ---------------------------------------------------------

BackendRecord makeRecord(const QString &id, const QString &data)
{
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.displayName = id;
    r.data = data.toUtf8();
    r.contentHash = QStringLiteral("hash-of-%1").arg(data);
    r.lastModified = QDateTime::currentDateTimeUtc();
    return r;
}

CollectionInfo makeCollection(const QString &id)
{
    CollectionInfo c;
    c.id = id;
    c.name = id;
    c.type = QStringLiteral("memos");
    return c;
}

static constexpr int kTimeoutMs = 5000;

/// Seed a baseline into a BaselineStore using the v3 mapping-keyed API.
/// contentHash is stored as the canonical blob data (same encoding as
/// blobBatchDiff baseline persistence uses; BlobDomainAdapter::saveBaselines
/// was folded into blobBatchDiff in Phase Ia.5 Task 16).
void seedBaseline(Kalburator::Storage::BaselineStore &store,
                  const QString &mappingId,
                  const QString &recordId,
                  const QString &contentHash)
{
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = recordId;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("blob")},
        Kalburator::Shape::EncodingId{QStringLiteral("raw")}};
    rec.data = contentHash.toUtf8();
    store.setBaselineV3(mappingId, rec);
}

} // namespace

// ============================================================================

class TstEngineMirrorDirection : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        Kalburator::Sync::BackendRegistry pmRegistry;
        Kalburator::PluginManager pm(&pmRegistry);
        Kalburator::registerStockPlugins(pm);
    }
    void cleanupTestCase() {
        Kalburator::Shape::TransformationRegistry::instance().clear();
        Kalburator::Shape::DomainRegistry::instance().clear();
        Kalburator::Shape::DomainOperationsRegistry::instance().clear();
    }
    void init();
    void cleanup();

    // MirrorAToB: target becomes an exact copy of source (deletions applied)
    void mirrorAToB_targetBecomesExactCopyOfSource();

    // MirrorAToB: target is empty to start — records created in target
    void mirrorAToB_copiesSourceToEmptyTarget();

    // MirrorAToB: records matching on both sides are left alone
    void mirrorAToB_leavesMatchingRecordsAlone();

    // MirrorBToA: source becomes an exact copy of target (deletions applied)
    void mirrorBToA_sourceBecomesExactCopyOfTarget();

    // Regression: MirrorBToA must overwrite source-changed records with
    // target's (= baseline) version, not silently skip them.
    void mirrorBToA_overwritesSourceChangedRecord();

    // TwoWay: no changes on either side — everything is unchanged
    void twoWay_noChanges();

    // TwoWay: record modified on source only — propagates to target
    void twoWay_modifiedOnSourceOnly();

    // TwoWay: record modified on target only — propagates to source
    void twoWay_modifiedOnTargetOnly();

    // TwoWay: record deleted on source — deletion propagates to target
    void twoWay_deletedOnSource();

    // TwoWay: record deleted on target — deletion propagates to source
    void twoWay_deletedOnTarget();

    // TwoWay: new record on source only — created on target
    void twoWay_newOnSource();

    // TwoWay: new record on target only — created on source
    void twoWay_newOnTarget();

    // TwoWay: both sides modified same record — source wins (SourceWins policy)
    void twoWay_conflictSourceWins();

private:
    static constexpr const char *kSrcId  = "blob-src";
    static constexpr const char *kTgtId  = "blob-tgt";
    static constexpr const char *kColId  = "col1";
    static constexpr const char *kMapId  = "mirror-test";

    QTemporaryDir                              m_tmpDir;  ///< one dir per test
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<BackendRegistry>           m_registry;
    std::unique_ptr<IdentifiedBlobSyncBackend> m_src;
    std::unique_ptr<IdentifiedBlobSyncBackend> m_tgt;
    std::unique_ptr<MinimalSyncHost>           m_host;
    std::unique_ptr<SyncEngine>                m_engine;
    int                                        m_testCounter = 0;
};

void TstEngineMirrorDirection::init()
{
    QVERIFY(m_tmpDir.isValid());

    // Use a unique db file per test run so that baseline state never leaks
    // between test cases that share the same QTemporaryDir.
    const QString dbPath = m_tmpDir.filePath(
        QStringLiteral("sync-%1.db").arg(m_testCounter++));
    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    QVERIFY(m_baselines->isOpen());

    m_registry = std::make_unique<BackendRegistry>();
    m_src = std::make_unique<IdentifiedBlobSyncBackend>(QString::fromLatin1(kSrcId));
    m_tgt = std::make_unique<IdentifiedBlobSyncBackend>(QString::fromLatin1(kTgtId));

    m_registry->registerBackendInstance(QString::fromLatin1(kSrcId), m_src.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTgtId), m_tgt.get());

    m_host = std::make_unique<MinimalSyncHost>(m_registry.get());
    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setBaselineStore(m_baselines.get());

    // Seed the collections so dispatchBlobSync's fetch finds them.
    m_src->createCollection(makeCollection(QString::fromLatin1(kColId)));
    m_tgt->createCollection(makeCollection(QString::fromLatin1(kColId)));

    // Configure mapping (sourceCalendar / targetCalendar carry the collection ID
    // for blob-domain mappings — same convention as existing blob tests).
    SyncMapping mapping;
    mapping.id             = QString::fromLatin1(kMapId);
    mapping.sourceBackend  = QString::fromLatin1(kSrcId);
    mapping.sourceCalendar = QString::fromLatin1(kColId);
    mapping.targetBackend  = QString::fromLatin1(kTgtId);
    mapping.targetCalendar = QString::fromLatin1(kColId);
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::SourceWins;
    mapping.enabled        = true;

    m_engine->setSyncMappings({ mapping });
}

void TstEngineMirrorDirection::cleanup()
{
    m_engine.reset();
    m_host.reset();
    m_tgt.reset();
    m_src.reset();
    m_registry.reset();
    m_baselines.reset();
}

// ---- helpers used by multiple tests -----------------------------------------

/// Make a record with an explicit content hash (for baseline-aware two-way tests).
static BackendRecord hashedRecord(const QString &id, const QString &data,
                                  const QString &hash)
{
    BackendRecord r = makeRecord(id, data);
    r.contentHash = hash;
    return r;
}

/// Run the default (no-override) two-way sync on the shared engine/mapping and
/// return the result. Asserts that the future completes without cancellation.
static SyncResult runTwoWay(SyncEngine *engine, const QString &mappingId)
{
    QFuture<SyncResult> future = engine->runSyncFuture(
        mappingId, SyncEngine::SyncBehavior::Unmonitored);
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs); }();
    Q_ASSERT(!future.isCanceled());
    return future.resultAt(0);
}

// ---- MirrorAToB -------------------------------------------------------------
//
// src = {a, b}   tgt = {b (different payload), c}
//
// Expected after MirrorAToB:
//   tgt = {a, b}   (a added, b updated to src's version, c deleted)
//
// The Task-7 stub ignores the override and falls through to the normal
// two-way path, which will leave tgt with {a, b, c} (all three records,
// since neither side knows about each other's new records as baseline-less
// two-way treats them as new-on-each-side).

void TstEngineMirrorDirection::mirrorAToB_targetBecomesExactCopyOfSource()
{
    // Pre-populate
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("a"), QStringLiteral("payload-a")));
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("b"), QStringLiteral("src-payload-b")));
    m_tgt->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("b"), QStringLiteral("tgt-payload-b")));
    m_tgt->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("c"), QStringLiteral("payload-c")));

    ExecutionOverride ov;
    ov.direction = ExecutionOverride::Direction::MirrorAToB;

    QFuture<SyncResult> future = m_engine->runSyncFuture(
        QString::fromLatin1(kMapId), ov, SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Target must now be an exact mirror of source: records {a, b}.
    // Record "c" (target-only) must be deleted.
    // Record "a" (source-only) must be created in target.
    // Record "b" must have src's payload.
    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecs.size(), 2);
    QVERIFY2(tgtRecs.contains(QStringLiteral("a")),
             "Target should contain record 'a' (added from source)");
    QVERIFY2(tgtRecs.contains(QStringLiteral("b")),
             "Target should contain record 'b'");
    QVERIFY2(!tgtRecs.contains(QStringLiteral("c")),
             "Target should NOT contain record 'c' (target-only; mirror deletes it)");
    QCOMPARE(tgtRecs.value(QStringLiteral("b")).data,
             QByteArrayLiteral("src-payload-b"));

    // Source must be untouched.
    const auto srcRecs = m_src->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(srcRecs.size(), 2);
    QVERIFY(srcRecs.contains(QStringLiteral("a")));
    QVERIFY(srcRecs.contains(QStringLiteral("b")));
}

// ---- MirrorBToA -------------------------------------------------------------
//
// src = {a}   tgt = {b}
//
// Expected after MirrorBToA:
//   src = {b}   (a deleted, b created from target)
//
// The Task-7 stub leaves src with {a, b} (two-way propagation).

void TstEngineMirrorDirection::mirrorBToA_sourceBecomesExactCopyOfTarget()
{
    // Pre-populate
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("a"), QStringLiteral("payload-a")));
    m_tgt->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("b"), QStringLiteral("payload-b")));

    ExecutionOverride ov;
    ov.direction = ExecutionOverride::Direction::MirrorBToA;

    QFuture<SyncResult> future = m_engine->runSyncFuture(
        QString::fromLatin1(kMapId), ov, SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Source must now be an exact mirror of target: record {b} only.
    const auto srcRecs = m_src->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(srcRecs.size(), 1);
    QVERIFY2(!srcRecs.contains(QStringLiteral("a")),
             "Source should NOT contain record 'a' (source-only; mirror deletes it)");
    QVERIFY2(srcRecs.contains(QStringLiteral("b")),
             "Source should contain record 'b' (copied from target)");

    // Target must be untouched.
    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecs.size(), 1);
    QVERIFY(tgtRecs.contains(QStringLiteral("b")));
}

// ---- MirrorBToA overwrite regression ----------------------------------------
//
// Setup: both src and tgt originally held record "shared" with payload-v1
// (baseline = v1). Source subsequently modified "shared" to payload-v2.
// Target kept the original (v1). No other records exist.
//
// After MirrorBToA:
//   src "shared" must have payload-v1  (target's version = baseline version)
//   tgt must be untouched              (payload-v1, unchanged)
//
// Before the fix, the MirrorBToA Update arm was silently skipped, so src
// retained its local payload-v2 modification.

void TstEngineMirrorDirection::mirrorBToA_overwritesSourceChangedRecord()
{
    // Need a BaselineStore to seed the baseline so the diff classifies
    // src's modification as an Update (not a Create). Use a separate temp
    // engine so the baseline store is attached correctly.
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString dbPath = tmpDir.filePath(QStringLiteral(".kalburator-sync.db"));
    Kalburator::Storage::BaselineStore blobBaselines(dbPath);
    QVERIFY(blobBaselines.isOpen());

    // Seed baseline: "shared" at v1 (the original shared state).
    seedBaseline(blobBaselines, QString::fromLatin1(kMapId),
                 QStringLiteral("shared"),
                 QStringLiteral("hash-of-payload-v1"));

    // Build a fresh registry / backends / engine with the baseline store.
    BackendRegistry registry;
    IdentifiedBlobSyncBackend src(QString::fromLatin1(kSrcId));
    IdentifiedBlobSyncBackend tgt(QString::fromLatin1(kTgtId));
    registry.registerBackendInstance(QString::fromLatin1(kSrcId), &src);
    registry.registerBackendInstance(QString::fromLatin1(kTgtId), &tgt);

    MinimalSyncHost host(&registry);
    SyncEngine engine(&registry, &host);
    engine.setBaselineStore(&blobBaselines);

    src.createCollection(makeCollection(QString::fromLatin1(kColId)));
    tgt.createCollection(makeCollection(QString::fromLatin1(kColId)));

    SyncMapping mapping;
    mapping.id             = QString::fromLatin1(kMapId);
    mapping.sourceBackend  = QString::fromLatin1(kSrcId);
    mapping.sourceCalendar = QString::fromLatin1(kColId);
    mapping.targetBackend  = QString::fromLatin1(kTgtId);
    mapping.targetCalendar = QString::fromLatin1(kColId);
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::SourceWins;
    mapping.enabled        = true;
    engine.setSyncMappings({ mapping });

    // Src has the locally-modified version (v2); tgt retains the original (v1).
    src.createRecord(QString::fromLatin1(kColId),
                     makeRecord(QStringLiteral("shared"),
                                QStringLiteral("payload-v2")));
    tgt.createRecord(QString::fromLatin1(kColId),
                     makeRecord(QStringLiteral("shared"),
                                QStringLiteral("payload-v1")));

    ExecutionOverride ov;
    ov.direction = ExecutionOverride::Direction::MirrorBToA;

    QFuture<SyncResult> future = engine.runSyncFuture(
        QString::fromLatin1(kMapId), ov, SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Source must now hold target's (= baseline) version: payload-v1.
    const auto srcRecs = src.recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(srcRecs.size(), 1);
    QVERIFY2(srcRecs.contains(QStringLiteral("shared")),
             "Source must still have 'shared' record");
    QCOMPARE(srcRecs.value(QStringLiteral("shared")).data,
             QByteArrayLiteral("payload-v1"));

    // Target must be untouched: still payload-v1.
    const auto tgtRecs = tgt.recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecs.size(), 1);
    QCOMPARE(tgtRecs.value(QStringLiteral("shared")).data,
             QByteArrayLiteral("payload-v1"));
}

// ---- MirrorAToB: copies to empty target -------------------------------------
//
// src = {r1, r2}   tgt = {}
//
// After MirrorAToB: tgt = {r1, r2}   (two records created)

void TstEngineMirrorDirection::mirrorAToB_copiesSourceToEmptyTarget()
{
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("r1"), QStringLiteral("data-a")));
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("r2"), QStringLiteral("data-b")));

    ExecutionOverride ov;
    ov.direction = ExecutionOverride::Direction::MirrorAToB;

    QFuture<SyncResult> future = m_engine->runSyncFuture(
        QString::fromLatin1(kMapId), ov, SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecs.size(), 2);
    QVERIFY(tgtRecs.contains(QStringLiteral("r1")));
    QVERIFY(tgtRecs.contains(QStringLiteral("r2")));

    // Source is untouched.
    QCOMPARE(m_src->recordsIn(QString::fromLatin1(kColId)).size(), 2);
}

// ---- MirrorAToB: matching records left alone --------------------------------
//
// src = {r1 payload-x}   tgt = {r1 payload-x}  (same hash)
//
// After MirrorAToB: tgt unchanged.  The record is not re-written.

void TstEngineMirrorDirection::mirrorAToB_leavesMatchingRecordsAlone()
{
    const BackendRecord rec = hashedRecord(
        QStringLiteral("r1"), QStringLiteral("same-payload"), QStringLiteral("h-same"));
    m_src->createRecord(QString::fromLatin1(kColId), rec);
    m_tgt->createRecord(QString::fromLatin1(kColId), rec);

    ExecutionOverride ov;
    ov.direction = ExecutionOverride::Direction::MirrorAToB;

    QFuture<SyncResult> future = m_engine->runSyncFuture(
        QString::fromLatin1(kMapId), ov, SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Target still has exactly one record with the original hash.
    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecs.size(), 1);
    QCOMPARE(tgtRecs.value(QStringLiteral("r1")).contentHash,
             QStringLiteral("h-same"));
}

// ---- TwoWay: no changes -----------------------------------------------------
//
// Both sides hold the same record at the baseline hash.
// After sync: both sides are unchanged.

void TstEngineMirrorDirection::twoWay_noChanges()
{
    const QString recId = QStringLiteral("r1");
    const QString hash  = QStringLiteral("h1");

    const BackendRecord rec = hashedRecord(recId, QStringLiteral("payload"), hash);
    m_src->createRecord(QString::fromLatin1(kColId), rec);
    m_tgt->createRecord(QString::fromLatin1(kColId), rec);

    seedBaseline(*m_baselines, QString::fromLatin1(kMapId), recId, hash);

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Neither side should have been modified.
    QCOMPARE(m_src->recordsIn(QString::fromLatin1(kColId)).size(), 1);
    QCOMPARE(m_tgt->recordsIn(QString::fromLatin1(kColId)).size(), 1);
    // Note: dispatchBlobSync does not yet populate SyncResult.{source,target}Stats;
    // we assert on backend state only.
}

// ---- TwoWay: modified on source only ----------------------------------------
//
// src = r1 @ v2   tgt = r1 @ v1   baseline = v1
// After sync: tgt updated to v2.

void TstEngineMirrorDirection::twoWay_modifiedOnSourceOnly()
{
    const QString recId = QStringLiteral("r1");

    m_src->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v2"), QStringLiteral("h-v2")));
    m_tgt->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v1"), QStringLiteral("h-v1")));

    seedBaseline(*m_baselines, QString::fromLatin1(kMapId), recId,
                 QStringLiteral("h-v1"));

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecs.size(), 1);
    QCOMPARE(tgtRecs.value(recId).contentHash, QStringLiteral("h-v2"));
}

// ---- TwoWay: modified on target only ----------------------------------------
//
// src = r1 @ v1   tgt = r1 @ v2   baseline = v1
// After sync: src updated to v2.

void TstEngineMirrorDirection::twoWay_modifiedOnTargetOnly()
{
    const QString recId = QStringLiteral("r1");

    m_src->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v1"), QStringLiteral("h-v1")));
    m_tgt->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v2"), QStringLiteral("h-v2")));

    seedBaseline(*m_baselines, QString::fromLatin1(kMapId), recId,
                 QStringLiteral("h-v1"));

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const auto srcRecs = m_src->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(srcRecs.size(), 1);
    QCOMPARE(srcRecs.value(recId).contentHash, QStringLiteral("h-v2"));
}

// ---- TwoWay: deleted on source ----------------------------------------------
//
// src = {}   tgt = {r1 @ v1}   baseline = v1
// After sync: tgt = {}

void TstEngineMirrorDirection::twoWay_deletedOnSource()
{
    const QString recId = QStringLiteral("r1");

    m_tgt->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v1"), QStringLiteral("h-v1")));

    seedBaseline(*m_baselines, QString::fromLatin1(kMapId), recId,
                 QStringLiteral("h-v1"));

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QCOMPARE(m_tgt->recordsIn(QString::fromLatin1(kColId)).size(), 0);
}

// ---- TwoWay: deleted on target ----------------------------------------------
//
// src = {r1 @ v1}   tgt = {}   baseline = v1
// After sync: src = {}

void TstEngineMirrorDirection::twoWay_deletedOnTarget()
{
    const QString recId = QStringLiteral("r1");

    m_src->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v1"), QStringLiteral("h-v1")));

    seedBaseline(*m_baselines, QString::fromLatin1(kMapId), recId,
                 QStringLiteral("h-v1"));

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QCOMPARE(m_src->recordsIn(QString::fromLatin1(kColId)).size(), 0);
}

// ---- TwoWay: new record on source -------------------------------------------
//
// src = {r1}   tgt = {}   no baseline for r1 (first-sync)
// After sync: tgt = {r1}

void TstEngineMirrorDirection::twoWay_newOnSource()
{
    const QString recId = QStringLiteral("r1");

    m_src->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v1"), QStringLiteral("h-v1")));

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QCOMPARE(m_tgt->recordsIn(QString::fromLatin1(kColId)).size(), 1);
    QVERIFY(m_tgt->recordsIn(QString::fromLatin1(kColId)).contains(recId));
}

// ---- TwoWay: new record on target -------------------------------------------
//
// src = {}   tgt = {r1}   no baseline for r1 (first-sync)
// After sync: src = {r1}

void TstEngineMirrorDirection::twoWay_newOnTarget()
{
    const QString recId = QStringLiteral("r1");

    m_tgt->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("v1"), QStringLiteral("h-v1")));

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    QCOMPARE(m_src->recordsIn(QString::fromLatin1(kColId)).size(), 1);
    QVERIFY(m_src->recordsIn(QString::fromLatin1(kColId)).contains(recId));
}

// ---- TwoWay: conflict resolved by SourceWins policy -------------------------
//
// Both sides modified the same record since the shared baseline.
// The mapping has conflictPolicy = SourceWins, so the source version must
// win and be written to the target.
//
// Note: the blob path always uses SourceWins internally (policy hardcoded
// in dispatchBlobSync); this test pins that behavior.

void TstEngineMirrorDirection::twoWay_conflictSourceWins()
{
    const QString recId = QStringLiteral("r1");

    m_src->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("src-v2"), QStringLiteral("h-src-v2")));
    m_tgt->createRecord(QString::fromLatin1(kColId),
                        hashedRecord(recId, QStringLiteral("tgt-v2"), QStringLiteral("h-tgt-v2")));

    // Baseline = v1: both sides have since modified the record independently.
    seedBaseline(*m_baselines, QString::fromLatin1(kMapId), recId,
                 QStringLiteral("h-v1"));

    const SyncResult result = runTwoWay(m_engine.get(), QString::fromLatin1(kMapId));
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // Source wins: target must hold the source version.
    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecs.size(), 1);
    QCOMPARE(tgtRecs.value(recId).contentHash, QStringLiteral("h-src-v2"));

    // Source must be untouched.
    const auto srcRecs = m_src->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(srcRecs.size(), 1);
    QCOMPARE(srcRecs.value(recId).contentHash, QStringLiteral("h-src-v2"));
}

QTEST_MAIN(TstEngineMirrorDirection)
#include "tst_engine_mirror_direction.moc"
