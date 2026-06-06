/// v0.65 — ExecutionOverride::clobber semantics (WildPalms clobber-sync RFC,
/// docs/2026-06-05-libkalburator-clobber-sync-handoff.md).
///
/// Pins the engine contract for a clobber dispatch:
///   1. targetBackend->wipeCollection(targetCollectionId) is called once per
///      clobbered mapping, after the source fetch succeeds.
///   2. The baseline is NOT consulted (behaviorally pinned: a stale baseline
///      that would delete records from source if loaded must have no effect).
///   3. The mass-delete-guard hook is never invoked, even for a state whose
///      normal diff WOULD trip it.
///   4. All source records arrive at the (wiped) target; source is untouched.
///   5. A fresh baseline is written at end-of-sync.
///   6. `direction` is silently ignored when clobber == true.
///   7. The flag applies on multi-mapping subset dispatch — each mapping runs
///      the clobber semantics independently.
///
/// Harness modeled on tst_engine_mirror_direction.cpp (same backend fake,
/// host, and registry pattern).

#include <QtTest/QtTest>
#include <QHash>
#include <QList>
#include <QTemporaryDir>

#include "backendregistry.h"
#include "baselinestore.h"
#include "imassdeleteguard.h"
#include "isynchost.h"
#include "isyncconfigstore.h"
#include "logicalcalendar.h"
#include "collectioninfo.h"
#include "pluginmanager.h"
#include "shape.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ExecutionOverride;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
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

// ---- ClobberableBlobSyncBackend ----------------------------------------------
//
// Same shape as tst_engine_mirror_direction's IdentifiedBlobSyncBackend
// (note/canon domain, QHash store) plus wipeCollection instrumentation:
// counts calls, optionally injects failure, and otherwise delegates to the
// IBlobBackend default implementation (which this test thereby exercises
// through the engine).

class ClobberableBlobSyncBackend : public SyncBackend
{
    Q_OBJECT
public:
    explicit ClobberableBlobSyncBackend(const QString &id, QObject *p = nullptr)
        : SyncBackend(p), m_id(id) {}

    // ---- SyncBackend identity ----
    QString backendType() const override { return QStringLiteral("memo"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override
    {
        return { Kalburator::Shape::Shape{
            DomainId{QStringLiteral("note")},
            EncodingId{QStringLiteral("canon")} } };
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

    // ---- wipeCollection instrumentation (v0.65) ----
    bool wipeCollection(const QString &colId) override
    {
        wipedCollections.append(colId);
        if (failWipe) return false;
        // Exercise the library's default loadRecords+deleteRecord impl.
        return Kalburator::Sync::IBlobBackend::wipeCollection(colId);
    }
    QStringList wipedCollections;   ///< one entry per wipe call, in order
    bool failWipe = false;          ///< inject wipe failure

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
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &) override { return nullptr; }

    // ---- Test helpers ----
    QHash<QString, BackendRecord> recordsIn(const QString &colId) const
        { return m_records.value(colId); }

private:
    QString m_id;
    QHash<QString, CollectionInfo>                          m_collections;
    QHash<QString /*colId*/, QHash<QString, BackendRecord>> m_records;
    QHash<QString /*recId*/, QString /*colId*/>             m_recToCol;
};

// ---- MinimalSyncHost --------------------------------------------------------

class MinimalSyncHost : public ISyncHost
{
public:
    explicit MinimalSyncHost(BackendRegistry *reg) : m_reg(reg) {}

    SyncBackend *backendById(const QString &id) override
        { return static_cast<SyncBackend*>(m_reg->backendInstance(id)); }
    QHash<QString, SyncBackend*> backends() override
    {
        QHash<QString, SyncBackend*> result;
        for (const QString &id : m_reg->registeredInstanceIds())
            result.insert(id, static_cast<SyncBackend*>(m_reg->backendInstance(id)));
        return result;
    }
    Kalburator::Sync::ISyncConfigStore *configStore() override
        { return &m_config; }

private:
    BackendRegistry *m_reg; // not owned
    NullConfigStore  m_config;
};

// ---- Recording mass-delete guard ---------------------------------------------
//
// Records every confirmMassDelete invocation. Denies by default so the
// "scenario WOULD trip the guard" control phase leaves state untouched.

class RecordingMassDeleteGuard : public Kalburator::Conflict::IMassDeleteGuard
{
public:
    bool confirmMassDelete(const QString &, const QString &, int, int) override
    {
        ++calls;
        return false;
    }
    int calls = 0;
};

// ---- Factories ----------------------------------------------------------------

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

static constexpr int kTimeoutMs = 5000;

} // namespace

// ============================================================================

class TstEngineClobber : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        Kalburator::Sync::BackendRegistry pmRegistry;
        Kalburator::PluginManager pm(&pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void init();
    void cleanup();

    void clobber_single_mapping_wipes_skips_baseline_repushes();
    void clobber_multi_mapping_subset_runs_each_independently();
    void clobber_silences_mass_delete_guard();
    void clobber_ignores_direction();
    void clobber_wipe_failure_fails_mapping_in_isolation();

private:
    static constexpr const char *kSrcId  = "blob-src";
    static constexpr const char *kTgtId  = "blob-tgt";
    static constexpr const char *kCol1   = "col1";
    static constexpr const char *kCol2   = "col2";
    static constexpr const char *kMap1   = "clobber-map-1";
    static constexpr const char *kMap2   = "clobber-map-2";

    SyncMapping makeMapping(const char *mapId, const char *colId) const;
    QList<SyncResult> runRequest(const SyncRequest &request);

    QTemporaryDir                                       m_tmpDir;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<BackendRegistry>                    m_registry;
    std::unique_ptr<ClobberableBlobSyncBackend>         m_src;
    std::unique_ptr<ClobberableBlobSyncBackend>         m_tgt;
    std::unique_ptr<MinimalSyncHost>                    m_host;
    std::unique_ptr<SyncEngine>                         m_engine;
    int                                                 m_testCounter = 0;
    Kalburator::Shape::ShapeRegistries                  m_shape;
};

SyncMapping TstEngineClobber::makeMapping(const char *mapId, const char *colId) const
{
    SyncMapping mapping;
    mapping.id             = QString::fromLatin1(mapId);
    mapping.sourceBackend  = QString::fromLatin1(kSrcId);
    mapping.sourceCalendar = QString::fromLatin1(colId);
    mapping.targetBackend  = QString::fromLatin1(kTgtId);
    mapping.targetCalendar = QString::fromLatin1(colId);
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::SourceWins;
    mapping.enabled        = true;
    return mapping;
}

QList<SyncResult> TstEngineClobber::runRequest(const SyncRequest &request)
{
    QFuture<QList<SyncResult>> future = m_engine->runSync(request);
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs); }();
    return future.resultAt(0);
}

void TstEngineClobber::init()
{
    QVERIFY(m_tmpDir.isValid());

    const QString dbPath = m_tmpDir.filePath(
        QStringLiteral("sync-%1.db").arg(m_testCounter++));
    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    QVERIFY(m_baselines->isOpen());

    m_registry = std::make_unique<BackendRegistry>();
    m_src = std::make_unique<ClobberableBlobSyncBackend>(QString::fromLatin1(kSrcId));
    m_tgt = std::make_unique<ClobberableBlobSyncBackend>(QString::fromLatin1(kTgtId));
    m_registry->registerBackendInstance(QString::fromLatin1(kSrcId), m_src.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTgtId), m_tgt.get());

    m_host   = std::make_unique<MinimalSyncHost>(m_registry.get());
    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());

    for (const char *col : { kCol1, kCol2 }) {
        m_src->createCollection(makeCollection(QString::fromLatin1(col)));
        m_tgt->createCollection(makeCollection(QString::fromLatin1(col)));
    }

    m_engine->setSyncMappings({ makeMapping(kMap1, kCol1),
                                makeMapping(kMap2, kCol2) });
}

void TstEngineClobber::cleanup()
{
    m_engine.reset();
    m_host.reset();
    m_tgt.reset();
    m_src.reset();
    m_registry.reset();
    m_baselines.reset();
}

// ---- single mapping ----------------------------------------------------------
//
// src = {a, b}    tgt = {b-old, c}
// Baseline deliberately poisoned: it records b@stale and c@h-c. If the engine
// consulted it after the wipe, the diff would read "b deleted on target" and
// DELETE b from source, and "c deleted on both". Source remaining untouched is
// therefore the behavioral proof that the baseline load was skipped.

void TstEngineClobber::clobber_single_mapping_wipes_skips_baseline_repushes()
{
    m_src->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("a"), QStringLiteral("payload-a")));
    m_src->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("b"), QStringLiteral("src-payload-b")));
    m_tgt->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("b"), QStringLiteral("tgt-payload-b")));
    m_tgt->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("c"), QStringLiteral("payload-c")));

    seedBaseline(*m_baselines, QString::fromLatin1(kMap1),
                 QStringLiteral("b"), QStringLiteral("hash-of-stale-b"));
    seedBaseline(*m_baselines, QString::fromLatin1(kMap1),
                 QStringLiteral("c"), QStringLiteral("hash-of-payload-c"));

    SyncRequest request;
    request.mappingIds = { QString::fromLatin1(kMap1) };
    ExecutionOverride ov;
    ov.clobber = true;
    request.executionOverride = ov;

    const QList<SyncResult> results = runRequest(request);
    QCOMPARE(results.size(), 1);
    QVERIFY2(results.first().success,
             qUtf8Printable(results.first().errorMessage));

    // Wipe called exactly once, on the target, for the mapped collection.
    QCOMPARE(m_tgt->wipedCollections, QStringList{ QString::fromLatin1(kCol1) });
    QVERIFY2(m_src->wipedCollections.isEmpty(), "source must never be wiped");

    // Target is an exact copy of source; target-only "c" gone, "b" has the
    // SOURCE payload.
    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kCol1));
    QCOMPARE(tgtRecs.size(), 2);
    QVERIFY(tgtRecs.contains(QStringLiteral("a")));
    QCOMPARE(tgtRecs.value(QStringLiteral("b")).data,
             QByteArrayLiteral("src-payload-b"));
    QVERIFY(!tgtRecs.contains(QStringLiteral("c")));

    // Source untouched — the behavioral pin that the poisoned baseline was
    // NOT loaded (see comment above).
    const auto srcRecs = m_src->recordsIn(QString::fromLatin1(kCol1));
    QCOMPARE(srcRecs.size(), 2);
    QVERIFY(srcRecs.contains(QStringLiteral("a")));
    QVERIFY(srcRecs.contains(QStringLiteral("b")));

    // Fresh baseline written post-run for the pushed records.
    QStringList baselineIds;
    for (const auto &c : m_baselines->baselinesForMappingV3(QString::fromLatin1(kMap1)))
        baselineIds << c.recordId;
    QVERIFY2(baselineIds.contains(QStringLiteral("a")),
             "fresh baseline for 'a' must exist after clobber");
    QVERIFY2(baselineIds.contains(QStringLiteral("b")),
             "fresh baseline for 'b' must exist after clobber");
}

// ---- multi mapping -------------------------------------------------------------
//
// One SyncRequest naming BOTH mappings with clobber=true (the WildPalms
// multi-conduit UX). Each mapping must run the clobber semantics independently.

void TstEngineClobber::clobber_multi_mapping_subset_runs_each_independently()
{
    m_src->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("r1"), QStringLiteral("d1")));
    m_tgt->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("stale1"), QStringLiteral("s1")));
    m_src->createRecord(QString::fromLatin1(kCol2),
                        makeRecord(QStringLiteral("r2"), QStringLiteral("d2")));
    m_tgt->createRecord(QString::fromLatin1(kCol2),
                        makeRecord(QStringLiteral("stale2"), QStringLiteral("s2")));

    SyncRequest request;
    request.mappingIds = { QString::fromLatin1(kMap1), QString::fromLatin1(kMap2) };
    ExecutionOverride ov;
    ov.clobber = true;
    request.executionOverride = ov;

    const QList<SyncResult> results = runRequest(request);
    QCOMPARE(results.size(), 2);
    for (const auto &r : results)
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    // One wipe per mapping, in queue order.
    QCOMPARE(m_tgt->wipedCollections,
             (QStringList{ QString::fromLatin1(kCol1), QString::fromLatin1(kCol2) }));

    // Both collections clobbered to their source contents.
    const auto t1 = m_tgt->recordsIn(QString::fromLatin1(kCol1));
    QCOMPARE(t1.size(), 1);
    QVERIFY(t1.contains(QStringLiteral("r1")));
    const auto t2 = m_tgt->recordsIn(QString::fromLatin1(kCol2));
    QCOMPARE(t2.size(), 1);
    QVERIFY(t2.contains(QStringLiteral("r2")));
}

// ---- mass-delete guard ---------------------------------------------------------
//
// Control phase proves the scenario WOULD trip the guard on a normal sync
// (baseline = 12 records, source empty → 12 proposed deletes > absolute
// threshold of 10). The clobber phase must then complete WITHOUT the guard
// hook ever being invoked.

void TstEngineClobber::clobber_silences_mass_delete_guard()
{
    for (int i = 0; i < 12; ++i) {
        const QString id = QStringLiteral("rec-%1").arg(i);
        m_tgt->createRecord(QString::fromLatin1(kCol1),
                            makeRecord(id, QStringLiteral("data-%1").arg(i)));
        seedBaseline(*m_baselines, QString::fromLatin1(kMap1), id,
                     QStringLiteral("hash-of-data-%1").arg(i));
    }
    // Source intentionally empty.

    RecordingMassDeleteGuard guard;
    m_engine->setMassDeleteGuard(&guard);

    // Control: normal sync trips the guard (and is denied, leaving state put).
    SyncRequest normal;
    normal.mappingIds = { QString::fromLatin1(kMap1) };
    QList<SyncResult> results = runRequest(normal);
    QCOMPARE(results.size(), 1);
    QVERIFY2(guard.calls > 0,
             "control phase: a 12-delete diff must consult the guard");
    QCOMPARE(m_tgt->recordsIn(QString::fromLatin1(kCol1)).size(), 12);

    // Clobber: guard must never be consulted; wipe empties the target.
    guard.calls = 0;
    SyncRequest clobber;
    clobber.mappingIds = { QString::fromLatin1(kMap1) };
    ExecutionOverride ov;
    ov.clobber = true;
    clobber.executionOverride = ov;
    results = runRequest(clobber);
    QCOMPARE(results.size(), 1);
    QVERIFY2(results.first().success,
             qUtf8Printable(results.first().errorMessage));

    QCOMPARE(guard.calls, 0);
    QCOMPARE(m_tgt->wipedCollections, QStringList{ QString::fromLatin1(kCol1) });
    QCOMPARE(m_tgt->recordsIn(QString::fromLatin1(kCol1)).size(), 0);
}

// ---- direction ignored ----------------------------------------------------------
//
// clobber + MirrorBToA: honoring the direction would mirror the freshly-wiped
// (empty) target back over the source — destroying it. The spec says direction
// is silently ignored; effective direction is always source → target.

void TstEngineClobber::clobber_ignores_direction()
{
    m_src->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("keep"), QStringLiteral("payload")));
    m_tgt->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("old"), QStringLiteral("stale")));

    SyncRequest request;
    request.mappingIds = { QString::fromLatin1(kMap1) };
    ExecutionOverride ov;
    ov.clobber   = true;
    ov.direction = ExecutionOverride::Direction::MirrorBToA;  // must be ignored
    request.executionOverride = ov;

    const QList<SyncResult> results = runRequest(request);
    QCOMPARE(results.size(), 1);
    QVERIFY2(results.first().success,
             qUtf8Printable(results.first().errorMessage));

    // Source survived (direction ignored) and target mirrors source.
    const auto srcRecs = m_src->recordsIn(QString::fromLatin1(kCol1));
    QCOMPARE(srcRecs.size(), 1);
    QVERIFY(srcRecs.contains(QStringLiteral("keep")));
    const auto tgtRecs = m_tgt->recordsIn(QString::fromLatin1(kCol1));
    QCOMPARE(tgtRecs.size(), 1);
    QVERIFY(tgtRecs.contains(QStringLiteral("keep")));
}

// ---- wipe failure ---------------------------------------------------------------
//
// A failing wipe fails THAT mapping's SyncResult; the other mapping in the
// same request proceeds normally (per-mapping isolation).

void TstEngineClobber::clobber_wipe_failure_fails_mapping_in_isolation()
{
    m_src->createRecord(QString::fromLatin1(kCol1),
                        makeRecord(QStringLiteral("r1"), QStringLiteral("d1")));
    m_src->createRecord(QString::fromLatin1(kCol2),
                        makeRecord(QStringLiteral("r2"), QStringLiteral("d2")));

    m_tgt->failWipe = true;   // every wipe fails

    SyncRequest request;
    request.mappingIds = { QString::fromLatin1(kMap1), QString::fromLatin1(kMap2) };
    ExecutionOverride ov;
    ov.clobber = true;
    request.executionOverride = ov;

    const QList<SyncResult> results = runRequest(request);
    QCOMPARE(results.size(), 2);
    for (const auto &r : results) {
        QVERIFY2(!r.success, "wipe failure must fail the mapping");
        QVERIFY2(r.errorMessage.contains(QStringLiteral("wipeCollection")),
                 qUtf8Printable(r.errorMessage));
    }

    // Both mappings attempted their own wipe — isolation, not abort-on-first.
    QCOMPARE(m_tgt->wipedCollections.size(), 2);

    // No push happened into the failed targets.
    QVERIFY(!m_tgt->recordsIn(QString::fromLatin1(kCol1))
                 .contains(QStringLiteral("r1")));
    QVERIFY(!m_tgt->recordsIn(QString::fromLatin1(kCol2))
                 .contains(QStringLiteral("r2")));
}

QTEST_MAIN(TstEngineClobber)
#include "tst_engine_clobber.moc"
