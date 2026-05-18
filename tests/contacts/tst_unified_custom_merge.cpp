// Phase N.1 — CustomMerge resolution in the unified dispatchSync path.
//
// Pins the engine's CustomMerge plumbing: when a mapping uses CustomMerge
// policy and the engine hits a BothModified conflict, the engine must
// call its m_unifiedMerger (acquired from the domain plugin) instead of
// falling through to ++conflictsDeferred.
//
// Uses blob/raw shape (same simplification as tst_unified_askuser_pause):
// RecordMergerBlob is the merger consulted. Per-domain merger quality
// (e.g., RecordMergerVCard property-level merge) is a separate concern.
//
// Slot 1 (auto-resolve): mapping.conflictPolicy = CustomMerge. Engine
// auto-resolves without yielding.
//
// Slot 2 (resume after AskUser): mapping.conflictPolicy = AskUser;
// manager in Deferred mode (no auto). Test explicitly resumes with
// CustomMerge, asserting the same plumbing.

#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <KCalendarCore/MemoryCalendar>

#include <memory>

#include "backendrecord.h"
#include "backendregistry.h"
#include "baselinestore.h"
#include "collectioninfo.h"
#include "conflictmanager.h"
#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "iblobbackend.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "pluginmanager.h"
#include "shape.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "synctypes.h"
#include "transformationregistry.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Storage::BaselineStore;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::ConflictInfo;
using Kalburator::Sync::ConflictManager;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackend;
using Kalburator::Sync::SyncConflictStore;
using Kalburator::Engine::SyncEngine;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::Shape;
using Kalburator::Shape::TransformationRegistry;

namespace {

constexpr int kSyncTimeoutMs = 30000;

constexpr const char *kSourceId   = "contacts-source";
constexpr const char *kTargetId   = "contacts-target";
constexpr const char *kColId      = "col-1";
constexpr const char *kMappingId  = "pause-test-mapping";
constexpr const char *kConflictId = "contact-conflict-rec";

// ──────────────────────────────────────────────────────────────────────────────
// RawBlobBackend — declares (contacts, raw) shape; stores BackendRecords in
// memory. Calendar pure-virtuals are no-op stubs.
// ──────────────────────────────────────────────────────────────────────────────
class RawBlobBackend final : public SyncBackend
{
    Q_OBJECT
public:
    explicit RawBlobBackend(const QString &id, QObject *parent = nullptr)
        : SyncBackend(parent), m_id(id)
    {
        CollectionInfo info;
        info.id   = QString::fromLatin1(kColId);
        info.name = QStringLiteral("Test Collection");
        info.type = QStringLiteral("contacts");
        m_collections.append(info);
    }

    QString backendType()  const override { return m_id; }
    QString backendId()    const override { return m_id; }
    QString displayName()  const override { return m_id; }
    bool    isAvailable()  const override { return true; }

    QList<Shape> nativeShapes() const override
    {
        // Use blob domain (identity pipeline) to avoid contacts-transcoding
        // setup in a test focused purely on the AskUser pause/resume mechanism.
        return { Shape{ DomainId{"blob"}, EncodingId{"raw"} } };
    }

    QList<CollectionInfo> availableCollections() override { return m_collections; }
    CollectionInfo collectionInfo(const QString &id) override
    {
        for (const auto &c : m_collections)
            if (c.id == id) return c;
        return {};
    }
    QString createCollection(const CollectionInfo &info) override
    {
        m_collections.append(info);
        return info.id;
    }

    QList<BackendRecord> loadRecords(const QString &) override { return m_records; }
    std::optional<BackendRecord> loadRecord(const QString &id) override
    {
        for (const auto &r : m_records)
            if (r.id == id) return r;
        return std::nullopt;
    }
    QString createRecord(const QString &, const BackendRecord &rec) override
    {
        m_records.append(rec);
        return rec.id;
    }
    bool updateRecord(const BackendRecord &rec) override
    {
        for (auto &r : m_records) {
            if (r.id == rec.id) { r = rec; return true; }
        }
        return false;
    }
    bool deleteRecord(const QString &id) override
    {
        for (int i = 0; i < m_records.size(); ++i)
            if (m_records[i].id == id) { m_records.removeAt(i); return true; }
        return false;
    }
    QList<BackendRecord> modifiedSince(const QString &, const QDateTime &) override { return {}; }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }
    bool supportsDeleteTracking() const override { return true; }

    // Calendar pure-virtual stubs — never reached for contacts domain.
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}

    // Test seam
    void seedRecord(const BackendRecord &rec) { m_records.append(rec); }
    QByteArray recordData(const QString &id) const
    {
        for (const auto &r : m_records)
            if (r.id == id) return r.data;
        return {};
    }

private:
    QString m_id;
    QList<CollectionInfo> m_collections;
    QList<BackendRecord>  m_records;
};

// ──────────────────────────────────────────────────────────────────────────────
// StubSyncHost
// ──────────────────────────────────────────────────────────────────────────────
class StubSyncHost final : public ISyncHost
{
public:
    explicit StubSyncHost(BackendRegistry *reg) : m_registry(reg) {}

    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? m_registry->backendInstance(id) : nullptr;
    }
    QHash<QString, SyncBackend *> backends() override
    {
        QHash<QString, SyncBackend *> out;
        if (!m_registry) return out;
        for (const auto &id : m_registry->registeredInstanceIds())
            out.insert(id, m_registry->backendInstance(id));
        return out;
    }
    ISyncConfigStore *configStore() override { return nullptr; }
    void syncStarted(const QString &, const LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}

private:
    BackendRegistry *m_registry = nullptr;
};

BackendRecord makeRecord(const QString &id, const QByteArray &data)
{
    BackendRecord rec;
    rec.id          = id;
    rec.type        = QStringLiteral("contact");
    rec.displayName = id;
    rec.data        = data;
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
    rec.isDeleted    = false;
    return rec;
}

SyncMapping makeTwoWayMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceId);
    m.sourceCalendar = QString::fromLatin1(kColId);
    m.targetBackend  = QString::fromLatin1(kTargetId);
    m.targetCalendar = QString::fromLatin1(kColId);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::CustomMerge;
    m.enabled        = true;
    return m;
}

} // namespace

class TestUnifiedCustomMerge : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void customMerge_autoResolves_callsPluginMerger();
    // Slot 2 (customMerge_resumeAfterAskUser_callsPluginMerger) is
    // added in Task 5.

private:
    void seedConflictState(const QByteArray &baseline,
                           const QByteArray &sourceData,
                           const QByteArray &targetData);

    QTemporaryDir                      m_tmpDir;
    std::unique_ptr<BackendRegistry>   m_registry;
    std::unique_ptr<RawBlobBackend>    m_source;
    std::unique_ptr<RawBlobBackend>    m_target;
    std::unique_ptr<StubSyncHost>      m_host;
    std::unique_ptr<BaselineStore> m_baselines;
    std::unique_ptr<SyncConflictStore> m_conflictStore;
    std::unique_ptr<ConflictManager>   m_conflictManager;
    std::unique_ptr<SyncEngine>        m_engine;
};

void TestUnifiedCustomMerge::initTestCase()
{
    Kalburator::PluginManager pm;
    Kalburator::registerStockPlugins(pm);
}

void TestUnifiedCustomMerge::cleanupTestCase()
{
    TransformationRegistry::instance().clear();
    DomainRegistry::instance().clear();
    Kalburator::Shape::DomainOperationsRegistry::instance().clear();
    Kalburator::Sync::BackendRegistry::instance().clear();
}

void TestUnifiedCustomMerge::init()
{
    QVERIFY(m_tmpDir.isValid());
    const QString dbPath = m_tmpDir.filePath(QStringLiteral("kalb-sync.db"));

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<RawBlobBackend>(QString::fromLatin1(kSourceId));
    m_target   = std::make_unique<RawBlobBackend>(QString::fromLatin1(kTargetId));
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceId), m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetId), m_target.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    m_baselines     = std::make_unique<BaselineStore>(dbPath);
    m_conflictStore = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::SourceWins);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setSyncMappings({ makeTwoWayMapping() });
}

void TestUnifiedCustomMerge::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_baselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
}

void TestUnifiedCustomMerge::seedConflictState(const QByteArray &baseline,
                                                const QByteArray &sourceData,
                                                const QByteArray &targetData)
{
    using Kalburator::Shape::CanonicalRecord;
    CanonicalRecord baselineRec;
    baselineRec.shape    = { DomainId{"blob"}, EncodingId{"raw"} };
    baselineRec.recordId = QString::fromLatin1(kConflictId);
    baselineRec.data     = baseline;
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId), baselineRec);

    m_source->seedRecord(makeRecord(QString::fromLatin1(kConflictId), sourceData));
    m_target->seedRecord(makeRecord(QString::fromLatin1(kConflictId), targetData));
}

void TestUnifiedCustomMerge::customMerge_autoResolves_callsPluginMerger()
{
    seedConflictState(
        "baseline-content",
        "source-modified-content",
        "target-modified-content"
    );

    // Sanity-check: AskUser was the previous default; we want CustomMerge.
    // The auto-resolve branch must fire, not the AskUser yield path.
    auto future = m_engine->runSyncFuture(SyncEngine::SyncBehavior::Monitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(!future.isCanceled());

    const auto results = future.resultAt(0);
    QCOMPARE(results.size(), 1);
    const SyncResult &r = results.first();

    // Plumbing assertion: CustomMerge resolved the conflict; nothing deferred.
    QVERIFY(r.unresolvedConflicts.isEmpty());

    // RecordMergerBlob with autoResolve == None defaults to source data.
    // Both sides should converge on the merger's output.
    QCOMPARE(m_target->recordData(QString::fromLatin1(kConflictId)),
             QByteArray("source-modified-content"));
    QCOMPARE(m_source->recordData(QString::fromLatin1(kConflictId)),
             QByteArray("source-modified-content"));
}

QTEST_MAIN(TestUnifiedCustomMerge)
#include "tst_unified_custom_merge.moc"
