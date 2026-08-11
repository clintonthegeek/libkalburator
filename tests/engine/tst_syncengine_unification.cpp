// tst_syncengine_unification.cpp
//
// Plan 1 Task 1 (architectural-redress campaign) — protective integration
// tests pinning the SyncEngine + SyncEngineWorker unification contract
// before the decomposition in Tasks 2-6 begins.
//
// Per INVARIANTS §6 ("test the production callsite, not its synonym"),
// these tests drive SyncEngine through its public QFuture entry points
// and assert on observable side effects (target backend writes, future
// state, conflict signals). They are the contract the decomposition
// must preserve.
//
// The three scenarios each pin one structural property the unification
// rests on:
//
//   1. multiMappingSequentialCompletesInOrder
//      Exercises the queue path. Multiple enabled mappings dispatched
//      via runSync(SyncRequest). Per-mapping results land in
//      the order the engine iterated m_syncMappings. Falsifiability:
//      reversing the iteration order in advanceQueue makes the test
//      fail on the order assertion.
//
//   2. conflictPauseResumeRoundTrip
//      Exercises the worker's nested-yield pause and the engine's
//      resumeAfterConflictResolution slot. AskUser policy + seeded
//      baseline + Monitored behavior + ConflictManager (AutoResolve
//      workflow) makes the worker yield on conflict; the manager's
//      auto-resolution drives the engine to call
//      resumeAfterConflictResolution(SourceWins); worker continues
//      and writes the source-side value to target. Falsifiability:
//      stubbing the worker's pause-yield to immediately fall through
//      (skipping the yield + resume round trip) makes the conflict
//      signal still fire but the post-resume target write either
//      fails or applies the wrong side.
//
//   3. cancellationPropagates
//      future.cancel() during a monitored conflict pause: the
//      cancellation flag propagates from QFutureWatcher::canceled →
//      SyncEngine::onCancelObserved → SyncEngineWorker::observeCancel
//      → the yield-pause wake path (Task 20's
//      onCancelDuringConflictPause). The future settles with
//      isCanceled() == true and resultAt(0).cancelled == true.
//      Falsifiability: making observeCancel a no-op leaves the
//      worker yielded forever and the QTRY_VERIFY_WITH_TIMEOUT for
//      isFinished() trips.

#include <QtTest/QtTest>
#include <QFuture>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <memory>

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kCollectionId = "stub-collection";
constexpr int  kSyncTimeoutMs = 30000;

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

} // namespace

class TstSyncEngineUnification : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void multiMappingSequentialCompletesInOrder();
    void conflictPauseResumeRoundTrip();
    void cancellationPropagates();
    void unmonitoredConflictRecordsIcalData();

private:
    // Per-test fixture rebuilt in init() so each scenario starts clean.
    std::unique_ptr<QTemporaryDir>                       m_tmpDir;
    std::unique_ptr<BackendRegistry>                     m_registry;
    std::unique_ptr<StubSyncHost>                        m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore>  m_baselines;
    std::unique_ptr<SyncConflictStore>                   m_conflictStore;
    std::unique_ptr<ConflictManager>                     m_conflictManager;
    std::unique_ptr<SyncEngine>                          m_engine;

    // initTestCase wires the stock plugins into m_shape exactly once.
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstSyncEngineUnification::initTestCase()
{
    // Populate the shared ShapeRegistries bundle with stock plugin
    // registrations so the engine can resolve calendar/ical shapes
    // when dispatching sync work.
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstSyncEngineUnification::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_host     = std::make_unique<StubSyncHost>(m_registry.get());

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_baselines     = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore = std::make_unique<SyncConflictStore>(dbPath);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setCollection(m_host->stubCollection());
}

void TstSyncEngineUnification::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_baselines.reset();
    m_host.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — Queue path: multiple mappings complete in iteration order.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::multiMappingSequentialCompletesInOrder()
{
    constexpr int kMappingCount = 3;

    // Build N independent source/target backend pairs, each with its
    // own calendar id, so the mappings don't share state. Register
    // backends with the fixture's BackendRegistry.
    std::vector<std::unique_ptr<MockBackend>> sources;
    std::vector<std::unique_ptr<MockBackend>> targets;
    QList<SyncMapping> mappings;
    QStringList expectedOrder;

    for (int i = 1; i <= kMappingCount; ++i) {
        const QString srcId = QStringLiteral("src-%1").arg(i);
        const QString tgtId = QStringLiteral("tgt-%1").arg(i);
        const QString calId = QStringLiteral("cal-%1").arg(i);
        const QString mapId = QStringLiteral("m%1").arg(i);

        sources.emplace_back(std::make_unique<MockBackend>());
        targets.emplace_back(std::make_unique<MockBackend>());

        m_registry->registerBackendInstance(srcId, sources.back().get());
        m_registry->registerBackendInstance(tgtId, targets.back().get());

        sources.back()->createCalendar(QString::fromLatin1(kCollectionId),
                                       calId,
                                       QStringLiteral("Calendar %1").arg(i));
        targets.back()->createCalendar(QString::fromLatin1(kCollectionId),
                                       calId,
                                       QStringLiteral("Calendar %1").arg(i));

        auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        hostCal->setId(calId);
        m_host->stubCollection()->addCalendarWithId(calId, hostCal);

        sources.back()->addIncidence(calId,
            makeEvent(QStringLiteral("evt-%1").arg(i),
                      QStringLiteral("Event %1").arg(i)));

        SyncMapping m;
        m.id              = mapId;
        m.sourceBackend   = srcId;
        m.sourceCalendar  = calId;
        m.targetBackend   = tgtId;
        m.targetCalendar  = calId;
        m.mode            = SyncMode::OneWayUpload;
        m.conflictPolicy  = ConflictResolution::SourceWins;
        m.enabled         = true;
        mappings.append(m);
        expectedOrder.append(mapId);
    }

    m_engine->setSyncMappings(mappings);

    // Spy on per-mapping syncStarted signals to capture dispatch
    // order independently of the queueResults aggregation, which
    // also reflects iteration order. Two independent observations
    // of the same property strengthen the falsifiability claim.
    QStringList startedOrder;
    QObject::connect(m_engine.get(), &SyncEngine::syncStarted,
                     this, [&startedOrder](const QString &mappingId) {
        startedOrder.append(mappingId);
    });

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_engine->runSync(req);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(!future.isCanceled());

    QCOMPARE(future.resultCount(), 1);
    const QList<SyncResult> resultList = future.resultAt(0);
    QCOMPARE(resultList.size(), kMappingCount);
    for (int i = 0; i < kMappingCount; ++i) {
        QVERIFY2(resultList[i].success,
                 qPrintable(QStringLiteral("mapping %1: %2")
                                .arg(i).arg(resultList[i].errorMessage)));
    }

    // The signal-order capture pins the queue iteration order — the
    // structural property the decomposition must preserve.
    QCOMPARE(startedOrder, expectedOrder);

    // Each target received exactly its mapping's event.
    for (int i = 0; i < kMappingCount; ++i) {
        const QString calId = QStringLiteral("cal-%1").arg(i + 1);
        const QStringList uids = targets[i]->allUids(calId);
        QCOMPARE(uids.size(), 1);
        QCOMPARE(uids.first(), QStringLiteral("evt-%1").arg(i + 1));
    }

    // Detach the mappings before the local backend unique_ptrs go out of
    // scope; otherwise the registry retains dangling pointers.
    m_engine->setSyncMappings({});
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — Conflict pause/resume round trip through the worker yield.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::conflictPauseResumeRoundTrip()
{
    constexpr auto kSourceBackendId = "source-mock";
    constexpr auto kTargetBackendId = "target-mock";
    constexpr auto kCalendarId      = "calendar-1";
    constexpr auto kMappingId       = "mapping-1";
    constexpr auto kConflictUid     = "evt-conflict";

    auto source = std::make_unique<MockBackend>();
    auto target = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        target.get());

    source->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));
    target->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                hostCal);

    // AskUser conflict policy is required for the worker to yield via
    // the m_yieldedForConflict pause; any direct policy resolves
    // silently. A baseline must also exist so the engine sees both
    // sides as "modified since baseline" (the quick-path downgrades
    // AskUser to SourceWins without baselines).
    SyncMapping mapping;
    mapping.id              = QString::fromLatin1(kMappingId);
    mapping.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    mapping.sourceCalendar  = QString::fromLatin1(kCalendarId);
    mapping.targetBackend   = QString::fromLatin1(kTargetBackendId);
    mapping.targetCalendar  = QString::fromLatin1(kCalendarId);
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::AskUser;
    mapping.enabled         = true;
    m_engine->setSyncMappings({ mapping });

    auto baselineEvent = makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Baseline"));
    const QString baselineIcal = eventToIcal(baselineEvent);
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               baselineIcal));

    source->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Source-Modified")));
    target->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Target-Modified")));

    // ConflictManager in AutoResolve(SourceWins) is the production
    // round-trip driver: onWorkerConflictPauseRequested calls
    // m_conflictManager->handleConflict(), then
    // resumeAfterConflictResolution(SourceWins) to unblock the
    // yielded worker. This pins the pause→resume contract end-to-end.
    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::SourceWins);
    m_engine->setConflictManager(m_conflictManager.get());

    QSignalSpy conflictSpy(m_engine.get(), &SyncEngine::conflictDetected);

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    req.behavior = SyncEngine::SyncBehavior::Monitored;
    auto future = m_engine->runSync(req);

    // The round trip:
    //   worker emits conflictPauseRequested → engine slot fires
    //   conflictDetected, calls ConflictManager (SourceWins), then
    //   resumeAfterConflictResolution → worker un-yields and finishes.
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!future.isCanceled(),
             "future was canceled — the round trip did not complete");

    // At least one conflictDetected fired (proves the worker yielded
    // and the engine processed the pause request).
    QVERIFY2(conflictSpy.count() >= 1,
             qPrintable(QStringLiteral("expected conflictDetected, got %1")
                            .arg(conflictSpy.count())));

    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0).first();
    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QVERIFY(!r.cancelled);

    // Post-resume side effect: SourceWins resolution propagated to
    // the target. Without the worker's yield-then-resume round trip
    // (e.g. if the worker skipped the pause entirely), the target
    // would either retain "Target-Modified" or end up empty.
    auto inc = target->incidence(QString::fromLatin1(kCalendarId),
                                  QString::fromLatin1(kConflictUid));
    QVERIFY(inc);
    QCOMPARE(inc->summary(), QStringLiteral("Source-Modified"));

    // Detach so backends don't outlive the local unique_ptrs.
    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test — Unmonitored AskUser conflicts persist both iCal serializations.
//
// docs/bugs/sync-conflict-store-duplicate-rows.md: the unmonitored-defer
// branch of unifiedHandleConflicts() used to construct its ConflictInfo
// without setting sourceIcalData/targetIcalData (unlike the sibling
// monitored-yield branch a few lines above it), so SyncConflictStore
// persisted every unmonitored conflict with empty local_ical/remote_ical.
// SyncEngine::onWorkerConflictDetected records directly via the wired
// SyncConflictStore even with no ConflictManager attached.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::unmonitoredConflictRecordsIcalData()
{
    constexpr auto kSourceBackendId = "source-mock";
    constexpr auto kTargetBackendId = "target-mock";
    constexpr auto kCalendarId      = "calendar-1";
    constexpr auto kMappingId       = "mapping-1";
    constexpr auto kConflictUid     = "evt-conflict";

    auto source = std::make_unique<MockBackend>();
    auto target = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        target.get());

    source->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));
    target->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                hostCal);

    SyncMapping mapping;
    mapping.id              = QString::fromLatin1(kMappingId);
    mapping.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    mapping.sourceCalendar  = QString::fromLatin1(kCalendarId);
    mapping.targetBackend   = QString::fromLatin1(kTargetBackendId);
    mapping.targetCalendar  = QString::fromLatin1(kCalendarId);
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::AskUser;
    mapping.enabled         = true;
    m_engine->setSyncMappings({ mapping });

    auto baselineEvent = makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Baseline"));
    const QString baselineIcal = eventToIcal(baselineEvent);
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               baselineIcal));

    source->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Source-Modified")));
    target->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Target-Modified")));

    // No ConflictManager attached — SyncBehavior::Unmonitored routes the
    // AskUser conflict through the "defer to next sync" branch, which
    // SyncEngine::onWorkerConflictDetected records directly via the
    // SyncConflictStore wired in init().
    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_engine->runSync(req);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!future.isCanceled(), "future was canceled");

    const QList<ConflictInfo> unresolved =
        m_conflictStore->unresolvedConflicts(QString::fromLatin1(kMappingId));
    QCOMPARE(unresolved.size(), 1);
    QVERIFY2(!unresolved.first().sourceIcalData.isEmpty(),
             "sourceIcalData was persisted empty");
    QVERIFY2(!unresolved.first().targetIcalData.isEmpty(),
             "targetIcalData was persisted empty");

    // Detach so backends don't outlive the local unique_ptrs.
    m_engine->setSyncMappings({});
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Cancellation propagation through the conflict-pause wake path.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::cancellationPropagates()
{
    constexpr auto kSourceBackendId = "source-mock";
    constexpr auto kTargetBackendId = "target-mock";
    constexpr auto kCalendarId      = "calendar-1";
    constexpr auto kMappingId       = "mapping-1";
    constexpr auto kConflictUid     = "evt-conflict";

    auto source = std::make_unique<MockBackend>();
    auto target = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        target.get());

    source->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));
    target->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                hostCal);

    SyncMapping mapping;
    mapping.id              = QString::fromLatin1(kMappingId);
    mapping.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    mapping.sourceCalendar  = QString::fromLatin1(kCalendarId);
    mapping.targetBackend   = QString::fromLatin1(kTargetBackendId);
    mapping.targetCalendar  = QString::fromLatin1(kCalendarId);
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::AskUser;
    mapping.enabled         = true;
    m_engine->setSyncMappings({ mapping });

    auto baselineEvent = makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Baseline"));
    const QString baselineIcal = eventToIcal(baselineEvent);
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               baselineIcal));

    source->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Source-Modified")));
    target->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Target-Modified")));

    // No ConflictManager attached: onWorkerConflictPauseRequested
    // will eventually post resumeAfterConflictResolution(Skip), but
    // we cancel before that lands at the worker so the cancellation
    // path wakes the yield via cancellationObserved →
    // onCancelDuringConflictPause (Task 20) rather than the Skip
    // resume. Either way the worker tears down via the cancellation
    // branch, which is exactly what this test pins.

    QSignalSpy conflictSpy(m_engine.get(), &SyncEngine::conflictDetected);

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    req.behavior = SyncEngine::SyncBehavior::Monitored;
    auto future = m_engine->runSync(req);

    // Wait for the conflict signal — proves the worker reached the
    // yield. Without observing the signal first, cancel() could race
    // ahead of processSync ever starting.
    QVERIFY2(conflictSpy.wait(5000),
             "expected conflictDetected before cancelling");

    future.cancel();

    // QFutureWatcher::canceled → onCancelObserved → queued observeCancel
    // → cancellationObserved → onCancelDuringConflictPause reaches the
    // worker thread asynchronously. QTRY_VERIFY_WITH_TIMEOUT spins the
    // event loop and is the synchronization point — no fixed-duration
    // qWait needed (avoids flakiness on slow CI).
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);

    QVERIFY(future.isCanceled());

    // resultAt(0) carries the cancelled SyncResult (F2 Task 23
    // contract: setAddResultsIfCanceledEnabled(true) + reportResult
    // before reportCanceled, read via resultCount + resultAt). The
    // QFuture<T>::results() / resultList accessor returns empty
    // after cancel in Qt6; resultAt(0) bypasses that quirk.
    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0).first();
    QVERIFY2(r.cancelled,
             "SyncResult.cancelled == false — cancellation did not propagate");

    // Detach so backends don't outlive the local unique_ptrs.
    m_engine->setSyncMappings({});
}

QTEST_MAIN(TstSyncEngineUnification)
#include "tst_syncengine_unification.moc"
