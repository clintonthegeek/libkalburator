// Phase Ib.5 Task 3 — AskUser pause/resume in the unified dispatchSync path.
//
// Pins the behavior that was missing from the unified path: when two contacts
// backends have divergent records (conflict) and the mapping uses AskUser
// policy in Monitored mode, the engine must emit conflictDetected and apply
// the resolution from ConflictManager. The current (pre-Task 3) code silently
// defers AskUser conflicts in the unified path — so this test FAILS before the
// fix and PASSES after.
//
// Uses contacts domain (raw encoding on both sides) — no calendar code in loop.
// A BlobBaselineStore baseline is seeded so the engine sees a BothModified
// conflict (not BothCreated, which quick-path auto-resolves as SourceWins).

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
#include "iblobbackend.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "pluginmanager.h"
#include "shape.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "synctypes.h"

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
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::Shape;

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
    m.conflictPolicy = ConflictResolution::AskUser;
    m.enabled        = true;
    return m;
}

} // namespace

class TestUnifiedAskUserPause : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // Core test: monitored AskUser conflict in contacts domain must emit
    // conflictDetected (pause protocol fired) and complete with SourceWins.
    void monitored_contactsConflict_pausesAndResolvesWithSourceWins();

private:
    void seedConflictState(const QByteArray &baseline,
                           const QByteArray &sourceData,
                           const QByteArray &targetData);

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
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

void TestUnifiedAskUserPause::initTestCase()
{
    // Populate the injected bundle once; per-slot init() builds a
    // SyncEngine reading from this same m_shape.
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestUnifiedAskUserPause::init()
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

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setSyncMappings({ makeTwoWayMapping() });
}

void TestUnifiedAskUserPause::cleanup()
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

void TestUnifiedAskUserPause::seedConflictState(const QByteArray &baseline,
                                                  const QByteArray &sourceData,
                                                  const QByteArray &targetData)
{
    // Seed a BlobBaselineStore baseline so blobBatchDiff sees both sides
    // as "modified since baseline" — a BothModified conflict, not BothCreated.
    // (BothCreated in quick-path mode auto-resolves as SourceWins silently.)
    using Kalburator::Shape::CanonicalRecord;
    CanonicalRecord baselineRec;
    baselineRec.shape    = { DomainId{"blob"}, EncodingId{"raw"} };
    baselineRec.recordId = QString::fromLatin1(kConflictId);
    baselineRec.data     = baseline;
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId), baselineRec);

    m_source->seedRecord(makeRecord(QString::fromLatin1(kConflictId), sourceData));
    m_target->seedRecord(makeRecord(QString::fromLatin1(kConflictId), targetData));
}

void TestUnifiedAskUserPause::monitored_contactsConflict_pausesAndResolvesWithSourceWins()
{
    seedConflictState(
        "baseline-content",
        "source-modified-content",
        "target-modified-content"
    );

    // Spy on conflictDetected — the engine emits this signal when
    // onWorkerConflictPauseRequested fires (i.e., the pause protocol ran).
    // Before Task 3's fix, the unified contacts path never emits this signal
    // (blobBatchMergeWithPlugin silently defers AskUser conflicts), so the
    // test FAILS. After the fix, the worker yields and the engine fires the
    // signal via ConflictManager (AutoResolve / SourceWins).
    QSignalSpy conflictSpy(m_engine.get(), &SyncEngine::conflictDetected);
    QVERIFY(conflictSpy.isValid());

    auto future = m_engine->runSyncFuture(SyncEngine::SyncBehavior::Monitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(!future.isCanceled());

    // conflictDetected must have fired — proves the pause protocol executed.
    QVERIFY2(conflictSpy.count() >= 1,
             qPrintable(QStringLiteral("expected conflictDetected for AskUser contacts "
                                       "conflict in Monitored mode, got %1 signals")
                            .arg(conflictSpy.count())));

    // With AutoResolve = SourceWins, target must now hold source's content.
    QCOMPARE(m_target->recordData(QString::fromLatin1(kConflictId)),
             QByteArray("source-modified-content"));
}

QTEST_MAIN(TestUnifiedAskUserPause)
#include "tst_unified_askuser_pause.moc"
