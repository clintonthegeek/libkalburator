/// Regression test for the WildPalms dispatchSync `backendById` RFC
/// (docs/2026-06-06-libkalburator-dispatchsync-backendbyid-regression.md,
/// WildPalms repo).
///
/// Post-Plan-3, backends may be registered whose dynamic type derives only
/// the neutral `SyncBackendBase` (WildPalms' canonical hub
/// `GenericSqliteBackend`, CardDAV contact backends, plugin backends). A
/// type-correct `ISyncHost::backendById` implementation — one that
/// `dynamic_cast`s the registry result to the calendar-typed `SyncBackend*`
/// the interface demands — returns nullptr for those backends. The engine's
/// dispatch path must therefore fetch backends from `BackendRegistry`
/// (`SyncBackendBase*`), not route through `ISyncHost::backendById`.
///
/// Before the v0.66 fix, this test fails with
/// "dispatchSync: backend not found": the engine's six
/// `m_controller->backendById()` sites only worked in-tree because every
/// other engine test registers calendar-derived (`SyncBackend`) stubs.
///
/// The harness mirrors tst_engine_mirror_direction with two deliberate
/// differences:
///   - `BaseOnlyBlobBackend` derives `SyncBackendBase` (NOT `SyncBackend`)
///     — no calendar-domain virtuals exist on it at all.
///   - `TypeCorrectSyncHost::backendById` uses `dynamic_cast` (WildPalms'
///     exact implementation), so it returns nullptr for these backends.

#include <QtTest/QtTest>
#include <QHash>
#include <QList>
#include <QTemporaryDir>

#include "backendregistry.h"
#include "baselinestore.h"
#include "isynchost.h"
#include "isyncconfigstore.h"
#include "logicalcalendar.h"
#include "collectioninfo.h"
#include "pluginmanager.h"
#include "shape.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncbackendbase.h"
#include "syncengine.h"
#include "synctypes.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncBackendBase;
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

// ---- BaseOnlyBlobBackend ----------------------------------------------------
//
// The point of this class: it derives the NEUTRAL `SyncBackendBase` only.
// `dynamic_cast<SyncBackend*>` on it returns nullptr — exactly like
// WildPalms' hub / CardDAV / plugin backends post-Plan-3.

class BaseOnlyBlobBackend : public SyncBackendBase
{
    Q_OBJECT
public:
    explicit BaseOnlyBlobBackend(const QString &id, QObject *p = nullptr)
        : SyncBackendBase(p), m_id(id) {}

    // ---- SyncBackendBase identity ----
    QString backendType() const override { return QStringLiteral("base-only"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override
    {
        // note/canon: routes processSync to the blob/plugin path the same
        // way tst_engine_mirror_direction's stub does.
        return { Kalburator::Shape::Shape{
            DomainId{QStringLiteral("note")},
            EncodingId{QStringLiteral("canon")} } };
    }
    QString resourceId() const override
        { return QStringLiteral("base-only-test:") + m_id; }

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

    // ---- Test helpers ----
    QHash<QString, BackendRecord> recordsIn(const QString &colId) const
        { return m_records.value(colId); }

private:
    QString m_id;
    QHash<QString, CollectionInfo>                           m_collections;
    QHash<QString /*colId*/, QHash<QString, BackendRecord>>  m_records;
    QHash<QString /*recId*/, QString /*colId*/>              m_recToCol;
};

// ---- TypeCorrectSyncHost ----------------------------------------------------
//
// WildPalms' PalmSyncHost::backendById, verbatim semantics: the interface
// forces a SyncBackend* return, and the only type-correct way to produce
// one from a registry whose entries are SyncBackendBase* is dynamic_cast —
// which yields nullptr for base-only backends. (The other in-tree stubs
// use an unchecked static_cast, which is why this hole had no coverage.)

class TypeCorrectSyncHost : public ISyncHost
{
public:
    explicit TypeCorrectSyncHost(BackendRegistry *reg)
        : m_reg(reg) {}

    SyncBackend *backendById(const QString &id) override
        { return dynamic_cast<SyncBackend*>(m_reg->backendInstance(id)); }
    QHash<QString, SyncBackend*> backends() override
    {
        QHash<QString, SyncBackend*> result;
        for (const QString &id : m_reg->registeredInstanceIds()) {
            if (auto *sb = dynamic_cast<SyncBackend*>(m_reg->backendInstance(id)))
                result.insert(id, sb);
        }
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

} // namespace

// ============================================================================

class TstEngineBaseOnlyBackend : public QObject
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

    // The RFC's headline symptom: a two-way sync between two
    // SyncBackendBase-only backends must succeed, not bail with
    // "dispatchSync: backend not found".
    void twoWay_baseOnlyBackends_succeeds();

    // First-sync path (dispatchFirstSync site): empty target seeded from
    // source through the base-only pointers.
    void firstSync_baseOnlyBackends_seedsTarget();

private:
    static constexpr const char *kSrcId  = "base-src";
    static constexpr const char *kTgtId  = "base-tgt";
    static constexpr const char *kColId  = "col1";
    static constexpr const char *kMapId  = "base-only-test";

    QTemporaryDir                                        m_tmpDir;
    std::unique_ptr<Kalburator::Storage::BaselineStore>  m_baselines;
    std::unique_ptr<BackendRegistry>                     m_registry;
    std::unique_ptr<BaseOnlyBlobBackend>                 m_src;
    std::unique_ptr<BaseOnlyBlobBackend>                 m_tgt;
    std::unique_ptr<TypeCorrectSyncHost>                 m_host;
    std::unique_ptr<SyncEngine>                          m_engine;
    int                                                  m_testCounter = 0;
    Kalburator::Shape::ShapeRegistries                   m_shape;
};

void TstEngineBaseOnlyBackend::init()
{
    QVERIFY(m_tmpDir.isValid());

    const QString dbPath = m_tmpDir.filePath(
        QStringLiteral("sync-%1.db").arg(m_testCounter++));
    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    QVERIFY(m_baselines->isOpen());

    m_registry = std::make_unique<BackendRegistry>();
    m_src = std::make_unique<BaseOnlyBlobBackend>(QString::fromLatin1(kSrcId));
    m_tgt = std::make_unique<BaseOnlyBlobBackend>(QString::fromLatin1(kTgtId));

    m_registry->registerBackendInstance(QString::fromLatin1(kSrcId), m_src.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTgtId), m_tgt.get());

    // Sanity: the host's type-correct cast must yield nullptr for these
    // backends — this is the precondition that distinguishes this harness
    // from every other engine test.
    m_host = std::make_unique<TypeCorrectSyncHost>(m_registry.get());
    QVERIFY(m_host->backendById(QString::fromLatin1(kSrcId)) == nullptr);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());

    m_src->createCollection(makeCollection(QString::fromLatin1(kColId)));
    m_tgt->createCollection(makeCollection(QString::fromLatin1(kColId)));

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

void TstEngineBaseOnlyBackend::cleanup()
{
    m_engine.reset();
    m_host.reset();
    m_tgt.reset();
    m_src.reset();
    m_registry.reset();
    m_baselines.reset();
}

void TstEngineBaseOnlyBackend::twoWay_baseOnlyBackends_succeeds()
{
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("a"), QStringLiteral("payload-a")));

    QFuture<SyncResult> future = m_engine->runSyncFuture(
        QString::fromLatin1(kMapId), SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    // The record must have reached the target through the neutral pointers.
    const auto tgtRecords = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecords.size(), 1);
    QVERIFY(tgtRecords.contains(QStringLiteral("a")));
}

void TstEngineBaseOnlyBackend::firstSync_baseOnlyBackends_seedsTarget()
{
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("a"), QStringLiteral("payload-a")));
    m_src->createRecord(QString::fromLatin1(kColId),
                        makeRecord(QStringLiteral("b"), QStringLiteral("payload-b")));

    QFuture<SyncResult> future = m_engine->runSyncFuture(
        QString::fromLatin1(kMapId), SyncEngine::SyncBehavior::Unmonitored);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0);
    QVERIFY2(result.success, qUtf8Printable(result.errorMessage));

    const auto tgtRecords = m_tgt->recordsIn(QString::fromLatin1(kColId));
    QCOMPARE(tgtRecords.size(), 2);
    QVERIFY(tgtRecords.contains(QStringLiteral("a")));
    QVERIFY(tgtRecords.contains(QStringLiteral("b")));
}

QTEST_MAIN(TstEngineBaseOnlyBackend)
#include "tst_engine_baseonly_backend.moc"
