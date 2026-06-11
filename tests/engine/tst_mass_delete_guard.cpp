// tst_mass_delete_guard.cpp
//
// T3 — mass-delete guard engine integration test.
//
// Verifies that SyncEngine consults the registered IMassDeleteGuard before
// propagating a batch of deletes that exceeds the threshold (>10 absolute
// OR >25% of baseline). Tests cover:
//   1. guardNotCalledBelowThreshold — no deletes proposed, guard not called.
//   2. guardCalledAboveAbsoluteThreshold — 20 deletes, guard fires with correct args.
//   3. guardCalledAboveRelativeThreshold — 9 deletes from 30, 30% > 25%, guard fires.
//   4. guardDenySkipsDeletesKeepsCreatesUpdates — guard returns false, deletes skipped,
//      creates still applied.
//   5. noGuardRegisteredAllowsAllDeletes — default: no guard, deletes proceed.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
#include "imassdeleteguard.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "syncconflictstore.h"
#include "synctypes.h"

#include "stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";
constexpr int  kSyncTimeoutMs   = 30000;

SyncMapping makeTwoWayMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

class StubGuard : public Kalburator::Conflict::IMassDeleteGuard {
public:
    int     invocations   = 0;
    QString lastMappingId;
    QString lastTargetBackend;
    int     lastProposed  = -1;
    int     lastBaseline  = -1;
    bool    nextReturn    = true;

    bool confirmMassDelete(const QString &mappingId,
                           const QString &targetBackendId,
                           int proposedDeletes,
                           int baselineCount) override {
        ++invocations;
        lastMappingId     = mappingId;
        lastTargetBackend = targetBackendId;
        lastProposed      = proposedDeletes;
        lastBaseline      = baselineCount;
        return nextReturn;
    }
};

} // namespace

class TstMassDeleteGuard : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        // Populate the injected bundle once; per-slot init() builds a
        // SyncEngine reading from this same m_shape.
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void init();
    void cleanup();

    void guardNotCalledBelowThreshold();
    void guardCalledAboveAbsoluteThreshold();
    void guardCalledAboveRelativeThreshold();
    void guardDenySkipsDeletesKeepsCreatesUpdates();
    void noGuardRegisteredAllowsAllDeletes();

private:
    void seedEvents(MockBackend *backend, int count, int startIndex = 1);
    bool runOneSync();

    Kalburator::Shape::ShapeRegistries     m_shape;
    Kalburator::Sync::BackendRegistry      m_pmRegistry;
    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_engine;
    StubGuard                              m_guard;
    SyncResult                             m_lastResult;
};

void TstMassDeleteGuard::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>();
    m_target   = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId), m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId), m_target.get());

    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId), hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".sync.db"));
    m_baselines     = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setCollection(m_host->stubCollection());

    m_guard      = StubGuard{};
    m_lastResult = SyncResult{};
}

void TstMassDeleteGuard::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_baselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TstMassDeleteGuard::seedEvents(MockBackend *backend, int count, int startIndex)
{
    for (int i = startIndex; i < startIndex + count; ++i) {
        auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        e->setUid(QStringLiteral("uid-%1").arg(i));
        e->setSummary(QStringLiteral("Event %1").arg(i));
        e->setDtStart(QDateTime::currentDateTimeUtc());
        backend->addIncidence(QString::fromLatin1(kCalendarId), e);
    }
}

bool TstMassDeleteGuard::runOneSync()
{
    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_engine->runSync(req);
    if (!QTest::qWaitFor([&] { return future.isFinished(); }, kSyncTimeoutMs)) return false;
    if (future.isCanceled()) return false;
    const auto results = future.resultAt(0);
    if (results.isEmpty()) return false;
    m_lastResult = results.last();
    return true;
}

// ---- Tests --------------------------------------------------------------

void TstMassDeleteGuard::guardNotCalledBelowThreshold()
{
    // Seed both sides with 5 events; first sync establishes baseline.
    // No deletes are proposed → guard must NOT be called.
    seedEvents(m_source.get(), 5);
    seedEvents(m_target.get(), 5);
    m_engine->setMassDeleteGuard(&m_guard);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    QCOMPARE(m_guard.invocations, 0);
}

void TstMassDeleteGuard::guardCalledAboveAbsoluteThreshold()
{
    // First sync: seed 20 events both sides, establish baseline of 20.
    seedEvents(m_source.get(), 20);
    seedEvents(m_target.get(), 20);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    QVERIFY(m_lastResult.success);

    // Second sync: remove all 20 source events. Engine proposes 20 deletes
    // against target. 20 > 10 absolute → guard fires.
    for (int i = 1; i <= 20; ++i)
        m_source->removeItem(QString::fromLatin1(kCalendarId),
                             QStringLiteral("uid-%1").arg(i));
    m_guard.nextReturn = true;
    m_engine->setMassDeleteGuard(&m_guard);
    QVERIFY(runOneSync());
    QCOMPARE(m_guard.invocations, 1);
    QCOMPARE(m_guard.lastMappingId, QString::fromLatin1(kMappingId));
    QCOMPARE(m_guard.lastTargetBackend, QString::fromLatin1(kTargetBackendId));
    QCOMPARE(m_guard.lastProposed, 20);
    QCOMPARE(m_guard.lastBaseline, 20);
}

void TstMassDeleteGuard::guardCalledAboveRelativeThreshold()
{
    // First sync: seed 30 both sides, baseline = 30.
    seedEvents(m_source.get(), 30);
    seedEvents(m_target.get(), 30);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    QVERIFY(m_lastResult.success);

    // Second sync: remove 9 source events. 9 < 10 absolute, but
    // 9/30 = 30% > 25% relative → guard fires.
    for (int i = 1; i <= 9; ++i)
        m_source->removeItem(QString::fromLatin1(kCalendarId),
                             QStringLiteral("uid-%1").arg(i));
    m_engine->setMassDeleteGuard(&m_guard);
    QVERIFY(runOneSync());
    QCOMPARE(m_guard.invocations, 1);
    QCOMPARE(m_guard.lastProposed, 9);
    QCOMPARE(m_guard.lastBaseline, 30);
}

void TstMassDeleteGuard::guardDenySkipsDeletesKeepsCreatesUpdates()
{
    // First sync: 20 both sides.
    seedEvents(m_source.get(), 20);
    seedEvents(m_target.get(), 20);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    QVERIFY(m_lastResult.success);

    // Second sync: remove all 20 source events AND add 2 new source events.
    for (int i = 1; i <= 20; ++i)
        m_source->removeItem(QString::fromLatin1(kCalendarId),
                             QStringLiteral("uid-%1").arg(i));
    seedEvents(m_source.get(), 2, 100);

    m_guard.nextReturn = false;
    m_engine->setMassDeleteGuard(&m_guard);
    QVERIFY(runOneSync());
    QCOMPARE(m_guard.invocations, 1);
    // Target still has the original 20 (deletes skipped)
    // and gained 2 new events from source (creates applied).
    const auto tgtUids = m_target->allUids(QString::fromLatin1(kCalendarId));
    QCOMPARE(tgtUids.size(), 22);
}

void TstMassDeleteGuard::noGuardRegisteredAllowsAllDeletes()
{
    seedEvents(m_source.get(), 20);
    seedEvents(m_target.get(), 20);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    QVERIFY(m_lastResult.success);

    for (int i = 1; i <= 20; ++i)
        m_source->removeItem(QString::fromLatin1(kCalendarId),
                             QStringLiteral("uid-%1").arg(i));
    // No setMassDeleteGuard call — default behaviour: deletes proceed.
    QVERIFY(runOneSync());
    QCOMPARE(m_target->allUids(QString::fromLatin1(kCalendarId)).size(), 0);
}

QTEST_GUILESS_MAIN(TstMassDeleteGuard)
#include "tst_mass_delete_guard.moc"
