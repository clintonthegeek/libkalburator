// tst_engine_fetch_progress_relay.cpp
//
// Always-visible sync progress (status bar) — Task 2: scoped per-item
// progress relay in SyncEngineWorker::dispatchSync.
//
// MockBackend::setEmitFetchProgress(true) (Task 1 fixture) makes a source
// backend emit fetchProgressChanged(calendarId, current, total) once per
// seeded item during fetchItems(). Before this task, nothing wired that
// backend signal to SyncEngineWorker::fetchProgress (which IS already
// forwarded to SyncEngine::fetchProgress at construction time), so it fired
// into the void. This test pins that a real sync run now observes
// SyncEngine::fetchProgress firing, terminating at (current=3, total=3) for
// 3 seeded source items.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <optional>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
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
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-fetch-progress-relay";

// Parallel-sync Task 3: fixture ids for the overlap tests below. Both the
// "source" and "target" calendars live on the SAME MockBackend instance
// (registered under both backend ids) so a single high-water counter
// observes ops from both sides of one mapping.
constexpr auto kOverlapCollectionId    = "overlap-collection";
constexpr auto kOverlapSourceCalendar  = "overlap-source-cal";
constexpr auto kOverlapTargetCalendar  = "overlap-target-cal";
constexpr auto kOverlapMappingId       = "mapping-overlap-test";

constexpr int kSyncTimeoutMs = 30000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    event->setLastModified(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeMapping(SyncMode mode)
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = mode;
    m.conflictPolicy  = ConflictResolution::SourceWins;
    m.enabled         = true;
    return m;
}

} // namespace

class TestEngineFetchProgressRelay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void fetchProgressChanged_isRelayedToEngineFetchProgress();

    // Parallel-sync Task 3.
    void testSourceAndTargetFetchesOverlap();
    void testClobberKeepsFetchesSequential();

private:
    bool runOneSync(const QString &mappingId = QString::fromLatin1(kMappingId),
                     std::optional<ExecutionOverride> override = std::nullopt);
    void setupCoordinator(const QList<SyncMapping> &mappings);

    // Parallel-sync Task 3: builds a mapping whose source AND target
    // collections live on ONE MockBackend instance (m_source, registered
    // under both backend ids), so m_source's single high-water counter
    // observes ops from both sides. Sequential fetches never exceed 1;
    // overlapped fetches reach 2. Deterministic — no timing. (The
    // per-collection FIFO does not interfere: these are two DISTINCT
    // collections, which the FIFO explicitly allows to run concurrently —
    // see MockBackend::maxConcurrentOpsOn's doc comment.)
    void buildMappingWithBothSidesOnOneBackend();

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_coordinator;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestEngineFetchProgressRelay::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestEngineFetchProgressRelay::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>(QString::fromLatin1(kSourceBackendId));
    m_target   = std::make_unique<MockBackend>(QString::fromLatin1(kTargetBackendId));
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

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_baselines     = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
}

void TestEngineFetchProgressRelay::cleanup()
{
    m_coordinator.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_baselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TestEngineFetchProgressRelay::setupCoordinator(const QList<SyncMapping> &mappings)
{
    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_baselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings(mappings);
}

bool TestEngineFetchProgressRelay::runOneSync(const QString &mappingId,
                                              std::optional<ExecutionOverride> override)
{
    SyncRequest req;
    req.mappingIds = { mappingId };
    req.executionOverride = override;
    auto future = m_coordinator->runSync(req);
    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    if (!future.isFinished()) {
        qWarning() << "runSync did not finish within" << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSync was canceled unexpectedly";
        return false;
    }
    const QList<SyncResult> results = future.resultAt(0);
    for (const auto &r : results) {
        if (!r.success) {
            qWarning() << "sync reported failure:" << r.errorMessage;
            return false;
        }
    }
    return true;
}

void TestEngineFetchProgressRelay::fetchProgressChanged_isRelayedToEngineFetchProgress()
{
    // Seed 3 items on the source and enable the per-item fetch-progress fixture.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"), QStringLiteral("Alpha")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-2"), QStringLiteral("Beta")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-3"), QStringLiteral("Gamma")));
    m_source->setEmitFetchProgress(true);

    setupCoordinator({ makeMapping(SyncMode::TwoWay) });

    QSignalSpy progressSpy(m_coordinator.get(), &SyncEngine::fetchProgress);

    QVERIFY(runOneSync());

    QVERIFY2(!progressSpy.isEmpty(),
             "SyncEngine::fetchProgress never fired — per-item relay missing");
    const auto last = progressSpy.constLast();          // (calendarId, current, total)
    QCOMPARE(last.at(1).toInt(), 3);                     // current
    QCOMPARE(last.at(2).toInt(), 3);                     // total
}

void TestEngineFetchProgressRelay::buildMappingWithBothSidesOnOneBackend()
{
    // Register m_source under BOTH backend ids, so a mapping whose source
    // and target backend ids differ still routes both sides to the SAME
    // MockBackend instance. Two distinct calendar ids on it stand in for
    // the source/target collections — the per-collection FIFO
    // (SyncBackendBase::enqueueOperation) only serializes ops WITHIN one
    // collection, so these two are free to run concurrently.
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId), m_source.get());

    m_source->createCalendar(QString::fromLatin1(kOverlapCollectionId),
                             QString::fromLatin1(kOverlapSourceCalendar),
                             QStringLiteral("Overlap Source"));
    m_source->createCalendar(QString::fromLatin1(kOverlapCollectionId),
                             QString::fromLatin1(kOverlapTargetCalendar),
                             QStringLiteral("Overlap Target"));

    SyncMapping m;
    m.id             = QString::fromLatin1(kOverlapMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kOverlapSourceCalendar);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kOverlapTargetCalendar);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;

    setupCoordinator({ m });
}

void TestEngineFetchProgressRelay::testSourceAndTargetFetchesOverlap()
{
    // Source and target collections live on ONE MockBackend so a single
    // high-water counter observes both fetches. Sequential fetches never
    // exceed 1; overlapped fetches reach 2. Deterministic — no timing.
    buildMappingWithBothSidesOnOneBackend();
    m_source->setOperationDelay(100);
    m_source->resetConcurrencyStats();

    QVERIFY(runOneSync(QString::fromLatin1(kOverlapMappingId)));

    QCOMPARE(m_source->maxConcurrentOps(), 2);
}

void TestEngineFetchProgressRelay::testClobberKeepsFetchesSequential()
{
    // The clobber wipe sits BETWEEN the two fetches deliberately, so a
    // target is never destroyed when the source cannot be read. Under
    // clobber the overlap must NOT be taken.
    buildMappingWithBothSidesOnOneBackend();
    m_source->setOperationDelay(100);
    m_source->resetConcurrencyStats();

    ExecutionOverride ov;
    ov.clobber = true;

    QVERIFY(runOneSync(QString::fromLatin1(kOverlapMappingId), ov));

    QCOMPARE(m_source->maxConcurrentOps(), 1);
}

QTEST_MAIN(TestEngineFetchProgressRelay)
#include "tst_engine_fetch_progress_relay.moc"
