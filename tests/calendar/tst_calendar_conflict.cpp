// tst_calendar_conflict.cpp
//
// Phase D.0 — Conflict detection (= 3-way merge in action). Two methods
// covering SyncEngine's two distinct conflict-handling code paths:
//
//   Unmonitored: ConflictDetected signal fires, sync continues, auto-
//                resolution applied per policy.
//   Monitored:   ConflictPauseRequested signal fires, sync yields,
//                test calls resumeAfterConflictResolution to continue.
//
// See: docs/phase0/04l-phase-d0-test-harness-design.md

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "syncconflictstore.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";
constexpr auto kConflictUid     = "evt-1";

constexpr int kSyncTimeoutMs = 30000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

QString eventToIcal(const KCalendarCore::Incidence::Ptr &inc)
{
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(inc);
}

SyncMapping makeTwoWayMapping()
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = SyncMode::TwoWay;
    // AskUser policy is required to make the engine emit conflict
    // signals. With any direct policy (SourceWins, etc.), SyncEngine
    // resolves the conflict silently via resolveConflictAutomatically
    // and no signal reaches the coordinator. The ConflictManager's
    // workflow mode (set in init()) handles the actual resolution.
    m.conflictPolicy  = ConflictResolution::AskUser;
    m.enabled         = true;
    return m;
}

} // namespace

class TestCalendarConflict : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void init();
    void cleanup();

    void unmonitored_sameUidDivergent_emitsConflictDetected();
    void monitored_sameUidDivergent_pausesUntilResume();

private:
    void seedDivergentConflictState();

    QStringList sourceUids() const { return m_source->allUids(QString::fromLatin1(kCalendarId)); }
    QStringList targetUids() const { return m_target->allUids(QString::fromLatin1(kCalendarId)); }

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>       m_coordinator;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestCalendarConflict::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>();
    m_target   = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        m_target.get());

    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    // AutoResolve workflow lets the test pin the resolution
    // programmatically without showing a UI dialog. handleConflict()
    // returns the configured policy directly.
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::SourceWins);

    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeTwoWayMapping() });
}

void TestCalendarConflict::cleanup()
{
    m_coordinator.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_calendarBaselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

// Set up a state where:
//   - Both backends have an incidence at uid `kConflictUid`, but with
//     divergent summaries.
//   - CalendarBaselineStore has a baseline recording the *original* (pre-divergence)
//     state — so the engine sees both sides as "modified since
//     baseline" and triggers a conflict.
void TestCalendarConflict::seedDivergentConflictState()
{
    auto baselineEvent = makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Baseline"));
    const QString baselineIcal = eventToIcal(baselineEvent);
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QString::fromLatin1(kConflictUid), baselineIcal));

    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QString::fromLatin1(kConflictUid),
                                     QStringLiteral("Source-Modified")));
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QString::fromLatin1(kConflictUid),
                                     QStringLiteral("Target-Modified")));
}

// ---- Tests ---------------------------------------------------------------

void TestCalendarConflict::unmonitored_sameUidDivergent_emitsConflictDetected()
{
    seedDivergentConflictState();

    QSignalSpy conflictSpy(m_coordinator.get(),
                           &SyncEngine::conflictDetected);

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_coordinator->runSync(req);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(!future.isCanceled());

    QVERIFY2(conflictSpy.count() >= 1,
             qPrintable(QStringLiteral("expected conflictDetected, got %1 signals")
                            .arg(conflictSpy.count())));

    // CONTRACT CHANGED by the conflict-resolution repair campaign, Task 3
    // (Bug B — docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
    // resolution-handoff.md §B). This used to assert the target was STILL
    // "Target-Modified" after the run, with the rationale "unmonitored mode
    // records conflicts and does not apply a resolution inline; backend-side
    // resolution happens later via ConflictManager". That "later" never
    // existed: the resolution this fixture's ConflictManager produces
    // (AutoResolve/SourceWins, applied in the post-mapping batch presentation)
    // wrote one SyncConflictStore column and touched no data, ever — so the
    // assertion was pinning the defect.
    //
    // It now pins the repair. The auto-policy's SourceWins reaches
    // SyncEngine::onConflictResolved, becomes a PendingConflictResolution, and
    // the automatic follow-up pass (locked decision 2) replays it through the
    // engine's normal write path within the same runSync — hence
    // "Source-Modified" here, without the caller doing anything extra. This is
    // also the explicit WorkflowMode::AutoResolve coverage locked decision 4
    // asks for.
    auto targetInc = m_target->incidence(QString::fromLatin1(kCalendarId),
                                          QString::fromLatin1(kConflictUid));
    QVERIFY(targetInc);
    QCOMPARE(targetInc->summary(), QStringLiteral("Source-Modified"));

    // Consume-once: the applied resolution's row is gone, so it cannot re-arm
    // against a future genuine conflict for the same record.
    QCOMPARE(m_conflictStore->unresolvedConflictCount(QString::fromLatin1(kMappingId)), 0);
    QVERIFY(m_conflictStore->resolvedConflicts(QString::fromLatin1(kMappingId)).isEmpty());
}

void TestCalendarConflict::monitored_sameUidDivergent_pausesUntilResume()
{
    seedDivergentConflictState();

    QSignalSpy conflictSpy(m_coordinator.get(),
                           &SyncEngine::conflictDetected);

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Monitored;
    auto future = m_coordinator->runSync(req);

    // In monitored mode, the worker emits conflictPauseRequested and
    // yields. SyncEngine's onWorkerConflictPauseRequested calls
    // ConflictManager::handleConflict() inline; with workflow mode
    // AutoResolve the manager returns SourceWins immediately, and the
    // coordinator calls resumeAfterConflictResolution to unblock the
    // worker. So the test sees: signal fires, then sync completes.
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!future.isCanceled(),
             "sync was canceled in monitored mode with AutoResolve");

    QVERIFY2(conflictSpy.count() >= 1,
             qPrintable(QStringLiteral("expected conflict signal, got %1")
                            .arg(conflictSpy.count())));

    auto targetInc = m_target->incidence(QString::fromLatin1(kCalendarId),
                                          QString::fromLatin1(kConflictUid));
    QVERIFY(targetInc);
    QCOMPARE(targetInc->summary(), QStringLiteral("Source-Modified"));
}

QTEST_MAIN(TestCalendarConflict)
#include "tst_calendar_conflict.moc"
