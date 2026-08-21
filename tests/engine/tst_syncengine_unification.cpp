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

// Bug B fixture ids (see TstSyncEngineUnification::seedConflict).
constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";
constexpr auto kConflictUid     = "evt-conflict";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

/// Bug B: same, with an EXPLICIT lastModified. Bug B's staleness guard
/// (locked decision 3) compares each side's lastModified against what was
/// recorded when the conflict was detected, at second granularity — so a test
/// that wants "unchanged" or "changed" to mean something definite has to say
/// what the timestamps are rather than inherit whatever the iCal round-trip
/// stamps.
KCalendarCore::Event::Ptr makeEventAt(const QString &uid, const QString &summary,
                                      const QDateTime &lastModified)
{
    auto event = makeEvent(uid, summary);
    event->setLastModified(lastModified.toUTC());
    return event;
}

QString eventToIcal(const KCalendarCore::Incidence::Ptr &inc)
{
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(inc);
}

/// Bug B fixture: the one AskUser TwoWay mapping every Bug B test installs.
/// Shared so seedConflict() and restartEngine() cannot describe it differently
/// — a restart that quietly changed the mapping would make "the resolution
/// survived" mean nothing.
SyncMapping fixtureMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::AskUser;
    m.enabled        = true;
    return m;
}

/// The Unmonitored AskUser request every Bug B test issues.
SyncRequest unmonitoredRequestForFixtureMapping()
{
    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    req.behavior   = SyncEngine::SyncBehavior::Unmonitored;
    return req;
}

/// Conflict-resolution-repair Task 2: the injected resolver the production
/// Monitored path actually calls. SyncEngine::onWorkerConflictPauseRequested
/// asks ConflictManager for a resolution and, for CustomMerge only, for
/// lastMergedIcalData() — so a stub that answers both is the only way to drive
/// resumeAfterConflict(resolution, mergedIcal) with a non-empty mergedIcal
/// through the production callsite (INVARIANTS §6). ConflictManager takes
/// ownership.
class StubConflictResolver : public Kalburator::Sync::IConflictResolver
{
public:
    ConflictResolution resolveConflict(const ConflictInfo &conflict,
                                       QWidget *) override
    {
        ++calls;
        lastConflict = conflict;
        return resolution;
    }

    QString lastMergedIcalData() const override { return mergedIcal; }

    ConflictResolution resolution = ConflictResolution::SourceWins;
    QString            mergedIcal;
    int                calls = 0;
    ConflictInfo       lastConflict;
};

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
    void modifyDeleteConflictLeavesDeletedSideEmpty();
    void customMergeUsesCallerSuppliedMerge();
    void duplicateResolutionWritesASecondRecord();
    // Bug B (conflict-resolution-repair Task 3) — see the block comment above
    // unmonitoredResolutionReachesTheBackend().
    void unmonitoredResolutionReachesTheBackend();
    void resolutionSurvivesRestart();
    void appliedResolutionIsNotReapplied();
    void staleResolutionIsDiscarded();
    void deferredWorkflowResolutionIsApplied();
    void autoResolveWorkflowResolutionIsApplied();
    void storeLessHostStillAppliesResolution();

private:
    /// Bug B tests: the two-MockBackend three-way-conflict fixture, which six
    /// of the tests below need verbatim. The four tests written before this
    /// helper existed keep their inline copies — rewriting green tests to
    /// prove a new one is not a trade worth making.
    ///
    /// Registers both backends under kSourceBackendId/kTargetBackendId,
    /// creates kCalendarId on both plus the host mirror, installs one AskUser
    /// TwoWay mapping (kMappingId), seeds a "Baseline" baseline for
    /// kConflictUid, and puts a divergent copy on each side — i.e. exactly the
    /// state that makes the engine emit a genuine BothModified conflict.
    ///
    /// lastModified is stamped EXPLICITLY and distinctly on each side because
    /// Bug B's staleness guard compares those values across runs; leaving them
    /// to whatever the iCal round-trip invents would make the guard's tests
    /// depend on wall-clock timing within a second.
    struct ConflictFixture {
        std::unique_ptr<MockBackend> source;
        std::unique_ptr<MockBackend> target;
    };
    ConflictFixture seedConflict(const QString &sourceSummary,
                                 const QString &targetSummary,
                                 const QDateTime &sourceModified,
                                 const QDateTime &targetModified);

    /// Rebuild SyncEngine over the SAME stores, dropping every piece of
    /// in-process state — the honest way to test "the resolution survived a
    /// restart", since a resolution that only lives in m_pendingResolutions
    /// would sail through a test that kept the old engine.
    void restartEngine();

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

    // Parallel-sync Task 10 (N=4 sweep): this test's whole point is the
    // concurrency-1 iteration-order contract documented above — pin it
    // explicitly rather than relying on the default, so
    // KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS sweeping the rest of the
    // suite at N=4 can't silently reorder these three disjoint mappings'
    // dispatch and trip the order assertion below on a property this test
    // was never trying to exercise.
    m_engine->setMaxConcurrentMappings(1);

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
    // structural property the decomposition must preserve at
    // concurrency 1 (the production default, and what
    // setMaxConcurrentMappings(1) above requests). Parallel-sync Task 10's
    // KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS sweep overrides the host's
    // requested cap unconditionally (a `static` in resolveEffectiveCap,
    // memoized for the binary's whole process — setting it back per-test
    // has no effect once any earlier test in this binary has read it), so
    // this ordering claim cannot hold under the sweep: three disjoint
    // mappings dispatch together and interleave by completion, not
    // submission order. That is a real, permanent property of N>1
    // concurrency, not a bug — skip the assertion the sweep cannot honour
    // rather than weaken what this test proves at N=1.
    if (!qEnvironmentVariableIsSet("KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS"))
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
    const ConflictInfo stored = unresolved.first();

    // Bug A (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
    // resolution-handoff.md): !isEmpty() is NOT enough. dispatchSync promotes
    // both fetched record lists to canonical Shape JSON before diffing, so
    // for months these fields carried non-empty canonical JSON — passing the
    // old assertions while PlanStan's dialog parsed nothing out of them.
    // Parse the payload as what the field contract claims it is, and check
    // the content survived, so a fix that parses-but-mangles still fails.
    KCalendarCore::ICalFormat fmt;

    auto srcCal = QSharedPointer<KCalendarCore::MemoryCalendar>::create(
        QTimeZone::systemTimeZone());
    QVERIFY2(fmt.fromString(srcCal, stored.sourceIcalData),
             qPrintable(QStringLiteral("sourceIcalData did not parse as iCal: %1")
                            .arg(stored.sourceIcalData)));
    QCOMPARE(srcCal->incidences().size(), 1);
    QCOMPARE(srcCal->incidences().first()->summary(),
             QStringLiteral("Source-Modified"));

    auto tgtCal = QSharedPointer<KCalendarCore::MemoryCalendar>::create(
        QTimeZone::systemTimeZone());
    QVERIFY2(fmt.fromString(tgtCal, stored.targetIcalData),
             qPrintable(QStringLiteral("targetIcalData did not parse as iCal: %1")
                            .arg(stored.targetIcalData)));
    QCOMPARE(tgtCal->incidences().size(), 1);
    QCOMPARE(tgtCal->incidences().first()->summary(),
             QStringLiteral("Target-Modified"));

    // baselineIcalData: wired through the same demotion as the two sides, but
    // it can only ever be non-empty once the engine actually carries baseline
    // BYTES. It does not: perRecordDiff builds EngineDiffOp::baselineRecord as
    // a hash-only shell (perrecorddiff.cpp's baselineShell) because baselines
    // have been per-side hashes since Phase B4. So the honest pin today is
    // "empty, and specifically not canonical JSON leaking through". See
    // FINDINGS O48 — when baseline bytes come back, this assertion is the one
    // that must flip to a parse+summary check for "Baseline".
    QVERIFY2(stored.baselineIcalData.isEmpty(),
             qPrintable(QStringLiteral("baselineIcalData unexpectedly populated: %1")
                            .arg(stored.baselineIcalData)));

    // Bug A, additive fields: the live ConflictInfo names the encoding each
    // payload is in. Read them off the SyncResult, not the store — they are
    // transport-only (SyncConflictStore has no columns for them, and adding
    // any would be a schema migration nobody asked for).
    const QList<SyncResult> results = future.resultAt(0);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().unresolvedConflicts.size(), 1);
    const ConflictInfo live = results.first().unresolvedConflicts.first();
    QCOMPARE(live.sourceEncoding, QStringLiteral("ical"));
    QCOMPARE(live.targetEncoding, QStringLiteral("ical"));
    QVERIFY2(stored.sourceEncoding.isEmpty(),
             "sourceEncoding is documented transport-only but came back from the store");

    // Detach so backends don't outlive the local unique_ptrs.
    m_engine->setSyncMappings({});
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — Bug A's empty-side guard: a ModifyDelete conflict has no record
// on the deleted side, so there are no bytes to demote there.
//
// Source keeps (and modifies) the record; the target no longer has it, with a
// baseline present for both — perRecordDiff's (hasS && !hasT && hasB) arm,
// which emits makeConflict(sRec, {}, bRec). The engine must leave
// targetIcalData empty rather than running the canon→native pipeline over
// zero bytes (which would either throw or manufacture an empty-shell record
// the UI would render as a real, blank version of the item).
//
// Honest note on falsifiability (INVARIANTS §5): this test was run with
// demoteToNative's isEmpty() short-circuit removed and stayed GREEN — today's
// canon→ical edge happens to return empty for empty input. So the guard is
// defensive, not load-bearing against the current edge set, and what this
// test actually pins is the *contract* PlanStan depends on ("the deleted side
// comes back empty, never an empty-shell record"), independent of what any
// future edge chooses to do with zero bytes. The source-side parse assertion
// below IS falsifiable: bypassing the demotion turns it red with canonical
// JSON, same as unmonitoredConflictRecordsIcalData.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::modifyDeleteConflictLeavesDeletedSideEmpty()
{
    constexpr auto kSourceBackendId = "source-mock";
    constexpr auto kTargetBackendId = "target-mock";
    constexpr auto kCalendarId      = "calendar-1";
    constexpr auto kMappingId       = "mapping-1";
    constexpr auto kConflictUid     = "evt-modify-delete";

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

    // Baseline says both sides once had this record. It must be a per-side
    // HASH baseline (setBaselineHashesV4), not a v3 canonical-bytes row:
    // baselineHashesForMappingV4() skips legacy non-"blob" rows, so a
    // calendarTestRec baseline would leave the diff with no baseline at all
    // and the (hasS && !hasT && !hasB) arm would emit a plain Create.
    // Deliberately stale hashes so the source reads as modified since.
    m_baselines->setBaselineHashesV4(QString::fromLatin1(kMappingId),
                                     QString::fromLatin1(kConflictUid),
                                     QStringLiteral("stale-source-hash"),
                                     QStringLiteral("stale-target-hash"));

    // ...the source modified it, the target deleted it. Nothing is added to
    // the target backend: its absence IS the delete.
    source->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Source-Modified")));

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_engine->runSync(req);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!future.isCanceled(), "future was canceled");

    const QList<SyncResult> results = future.resultAt(0);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().unresolvedConflicts.size(), 1);
    const ConflictInfo info = results.first().unresolvedConflicts.first();
    QCOMPARE(info.type, ConflictType::ModifyDelete);

    // Present side: real, parseable native iCal.
    KCalendarCore::ICalFormat fmt;
    auto srcCal = QSharedPointer<KCalendarCore::MemoryCalendar>::create(
        QTimeZone::systemTimeZone());
    QVERIFY2(fmt.fromString(srcCal, info.sourceIcalData),
             qPrintable(QStringLiteral("sourceIcalData did not parse as iCal: %1")
                            .arg(info.sourceIcalData)));
    QCOMPARE(srcCal->incidences().size(), 1);
    QCOMPARE(srcCal->incidences().first()->summary(),
             QStringLiteral("Source-Modified"));

    // Deleted side: empty, not an empty-shell VCALENDAR.
    QVERIFY2(info.targetIcalData.isEmpty(),
             qPrintable(QStringLiteral("targetIcalData should be empty for the "
                                       "deleted side, got: %1")
                            .arg(info.targetIcalData)));

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

// ─────────────────────────────────────────────────────────────────────────────
// Test 6 — Bug C: CustomMerge must write the CALLER'S merge, not the
// auto-merger's.
//
// docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-
// handoff.md §C. resumeAfterConflict(resolution, mergedIcal) has always been
// handed the dialog's hand-merged payload — SyncEngine::
// onWorkerConflictPauseRequested fetches it via
// ConflictManager::lastMergedIcalData() specifically for CustomMerge — and the
// CustomMerge case ignored the parameter entirely, running
// m_unifiedMerger->merge() instead. Every Custom Merge a user performed was
// silently discarded.
//
// The stub resolver returns a merge whose summary matches NEITHER side, so the
// assertion cannot pass by accident: the auto-merger can only ever produce
// "Source-Modified" or "Target-Modified" from these two inputs.
//
// Falsifiability (INVARIANTS §5): with the Bug C block removed from
// applyConflictResolution (auto-merge unconditionally), both backends come back
// "Source-Modified" and both QCOMPAREs go red.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::customMergeUsesCallerSuppliedMerge()
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
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               eventToIcal(baselineEvent)));

    source->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Source-Modified")));
    target->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Target-Modified")));

    // The user's hand merge: native iCal, same uid, a summary neither side has.
    // Native is what the dialog produces (it parses ConflictInfo::sourceIcalData,
    // which Task 1 made genuine native iCal), so this is the real payload shape
    // the engine has to promote to canonical before writing.
    auto mergedEvent = makeEvent(QString::fromLatin1(kConflictUid),
                                 QStringLiteral("User-Merged"));

    auto *resolver = new StubConflictResolver;
    resolver->resolution = ConflictResolution::CustomMerge;
    resolver->mergedIcal = eventToIcal(mergedEvent);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_conflictManager->setConflictResolver(resolver);   // takes ownership
    m_engine->setConflictManager(m_conflictManager.get());

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    req.behavior = SyncEngine::SyncBehavior::Monitored;
    auto future = m_engine->runSync(req);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!future.isCanceled(), "future was canceled");
    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0).first();
    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(resolver->calls, 1);

    // CustomMerge writes the merged record to BOTH sides.
    auto tgtInc = target->incidence(QString::fromLatin1(kCalendarId),
                                    QString::fromLatin1(kConflictUid));
    QVERIFY(tgtInc);
    QCOMPARE(tgtInc->summary(), QStringLiteral("User-Merged"));

    auto srcInc = source->incidence(QString::fromLatin1(kCalendarId),
                                    QString::fromLatin1(kConflictUid));
    QVERIFY(srcInc);
    QCOMPARE(srcInc->summary(), QStringLiteral("User-Merged"));

    // Detach so backends don't outlive the local unique_ptrs.
    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7 — Bug D: "Keep Both" must actually produce a second record.
//
// docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-
// handoff.md §D / PlanStan's docs/bugs/sync-dialog-keepboth-duplicate-not-
// created.md. The Duplicate case gave the clone a fresh BackendRecord::id and
// then tried to match the new id into the payload with
// data.replace("UID:"+oldId, "UID:"+newId) — an iCal spelling applied to
// CANONICAL Shape JSON, whose envelope spells the uid `"uid": "…"`. The replace
// never matched, so the clone demoted back to iCal carrying the ORIGINAL UID
// and the backend's uid-keyed store collapsed the pair into one record.
//
// Falsifiability (INVARIANTS §5): restoring the byte-replace leaves the target
// with a single uid and the size QCOMPARE goes red.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::duplicateResolutionWritesASecondRecord()
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
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               eventToIcal(baselineEvent)));

    source->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Source-Modified")));
    target->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Target-Modified")));

    auto *resolver = new StubConflictResolver;
    resolver->resolution = ConflictResolution::Duplicate;

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_conflictManager->setConflictResolver(resolver);   // takes ownership
    m_engine->setConflictManager(m_conflictManager.get());

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    req.behavior = SyncEngine::SyncBehavior::Monitored;
    auto future = m_engine->runSync(req);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!future.isCanceled(), "future was canceled");
    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0).first();
    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(resolver->calls, 1);

    // Keep Both: the target ends up with the source's version under the
    // original uid PLUS a clone of its own version under a fresh uid. Two
    // records, two distinct uids — the whole point of the resolution.
    QStringList tgtUids = target->allUids(QString::fromLatin1(kCalendarId));
    tgtUids.sort();
    QVERIFY2(tgtUids.size() == 2,
             qPrintable(QStringLiteral("target should hold 2 records after "
                                       "Duplicate, holds %1: %2")
                            .arg(tgtUids.size()).arg(tgtUids.join(QLatin1Char(',')))));
    QVERIFY(tgtUids.contains(QString::fromLatin1(kConflictUid)));

    const QString cloneUid = tgtUids.at(0) == QString::fromLatin1(kConflictUid)
                                 ? tgtUids.at(1) : tgtUids.at(0);
    QVERIFY2(cloneUid != QString::fromLatin1(kConflictUid),
             "the clone reused the original uid");

    // The original uid still resolves to the original (source-side) content...
    auto original = target->incidence(QString::fromLatin1(kCalendarId),
                                      QString::fromLatin1(kConflictUid));
    QVERIFY(original);
    QCOMPARE(original->summary(), QStringLiteral("Source-Modified"));

    // ...and the clone carries the target-side version that would otherwise
    // have been overwritten. Its UID property must match its new id, not the
    // original — that is exactly what the byte-replace failed to do.
    auto clone = target->incidence(QString::fromLatin1(kCalendarId), cloneUid);
    QVERIFY(clone);
    QCOMPARE(clone->summary(), QStringLiteral("Target-Modified"));
    QCOMPARE(clone->uid(), cloneUid);

    // The clone is pushed back to the source too, so both sides converge on
    // the same pair.
    QStringList srcUids = source->allUids(QString::fromLatin1(kCalendarId));
    srcUids.sort();
    QCOMPARE(srcUids.size(), 2);
    QVERIFY(srcUids.contains(cloneUid));

    // Detach so backends don't outlive the local unique_ptrs.
    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ═════════════════════════════════════════════════════════════════════════════
// Bug B — a resolution chosen in SyncBehavior::Unmonitored must actually
// reach the data.
//
// docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-
// handoff.md §B. Unmonitored is the ONLY mode a real PlanStan sync run ever
// uses. Its AskUser branch did no data write of any kind: it deferred the
// conflict, the run finished, SyncEngine batch-presented the conflict to
// ConflictManager, and a real (non-Skip) answer wrote ONE COLUMN in
// SyncConflictStore and stopped there. Nothing applied it, nothing advanced a
// baseline, and the identical conflict re-detected on every subsequent sync,
// forever. The only code that turns a ConflictResolution into a write
// (resumeAfterConflict, now applyConflictResolution) was wired exclusively to
// the Monitored mid-run yield, which this mode never takes.
//
// Task 3 makes ConflictManager::conflictResolved the one channel a user's
// choice travels on, stores it as a PendingConflictResolution keyed by
// (mapping, record), hands it to the NEXT dispatchSync on the worker Request,
// and replays it in the diff walk's AskUser branch through the same helper the
// Monitored resume uses — no second write mechanism (INVARIANTS §1).
// ═════════════════════════════════════════════════════════════════════════════

TstSyncEngineUnification::ConflictFixture
TstSyncEngineUnification::seedConflict(const QString &sourceSummary,
                                       const QString &targetSummary,
                                       const QDateTime &sourceModified,
                                       const QDateTime &targetModified)
{
    ConflictFixture fx;
    fx.source = std::make_unique<MockBackend>();
    fx.target = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        fx.source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        fx.target.get());

    fx.source->createCalendar(QString::fromLatin1(kCollectionId),
                              QString::fromLatin1(kCalendarId),
                              QStringLiteral("Calendar 1"));
    fx.target->createCalendar(QString::fromLatin1(kCollectionId),
                              QString::fromLatin1(kCalendarId),
                              QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                hostCal);

    m_engine->setSyncMappings({ fixtureMapping() });

    // AskUser + a seeded baseline is what makes this a three-way conflict
    // rather than a quick-path SourceWins downgrade.
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               eventToIcal(makeEvent(
                                                   QString::fromLatin1(kConflictUid),
                                                   QStringLiteral("Baseline")))));

    fx.source->addIncidence(QString::fromLatin1(kCalendarId),
                            makeEventAt(QString::fromLatin1(kConflictUid),
                                        sourceSummary, sourceModified));
    fx.target->addIncidence(QString::fromLatin1(kCalendarId),
                            makeEventAt(QString::fromLatin1(kConflictUid),
                                        targetSummary, targetModified));
    return fx;
}

void TstSyncEngineUnification::restartEngine()
{
    m_engine.reset();
    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setCollection(m_host->stubCollection());
    // Same mapping, re-installed: the backends and their contents are
    // untouched by the restart (they are the "server" and "disk"), only the
    // engine's in-process state is gone. Re-running seedConflict() here would
    // register a SECOND pair of MockBackends over the first and leave the
    // registry holding dangling pointers.
    m_engine->setSyncMappings({ fixtureMapping() });
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8 — THE headline case. Unmonitored conflict, the user picks "keep the
// source version", and the target backend must actually end up holding it.
//
// Falsifiability (INVARIANTS §5): shown RED against the pre-Task-3 code —
// exactly the handoff's prediction, "it will still show the original
// conflicting pair, unchanged, and a fresh AskUser conflict for the very same
// id": target keeps "Target-Modified" and the conflict is still unresolved in
// the store. Also red if the injection point in unifiedHandleConflicts is
// removed, or if consumeAppliedResolutions runs before the apply.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::unmonitoredResolutionReachesTheBackend()
{
    const QDateTime srcMod = QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC);
    const QDateTime tgtMod = QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC);
    auto fx = seedConflict(QStringLiteral("Source-Modified"),
                           QStringLiteral("Target-Modified"), srcMod, tgtMod);

    auto *resolver = new StubConflictResolver;
    resolver->resolution = ConflictResolution::SourceWins;

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_conflictManager->setConflictResolver(resolver);   // takes ownership
    m_engine->setConflictManager(m_conflictManager.get());

    auto future = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!future.isCanceled(), "future was canceled");

    // The dialog was shown exactly once: the follow-up pass APPLIES the answer,
    // it does not ask again. (Asking twice for one conflict is the user-visible
    // shape of the bug this test exists to prevent regressing into.)
    QCOMPARE(resolver->calls, 1);

    // The point of the whole campaign.
    auto tgtInc = fx.target->incidence(QString::fromLatin1(kCalendarId),
                                       QString::fromLatin1(kConflictUid));
    QVERIFY(tgtInc);
    QCOMPARE(tgtInc->summary(), QStringLiteral("Source-Modified"));

    // ...and no fresh AskUser conflict is left standing for the same id.
    const SyncResult last = future.resultAt(0).last();
    QVERIFY2(last.unresolvedConflicts.isEmpty(),
             qPrintable(QStringLiteral("%1 conflict(s) still unresolved after the "
                                       "resolution was applied")
                            .arg(last.unresolvedConflicts.size())));
    QCOMPARE(last.appliedConflictIds.size(), 1);
    QCOMPARE(m_conflictStore->unresolvedConflictCount(QString::fromLatin1(kMappingId)), 0);

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9 — restart durability. PlanStan's live database is, right now, full of
// rows that are resolved but were never applied: that IS the defect, persisted.
// A resolution chosen in one process must land in the next one.
//
// Deliberately does NOT go through ConflictManager for the resolution: it
// writes the store the way ConflictManager's store-only paths always did
// (resolveConflict and nothing else), then throws the whole engine away, so
// the only thing that can make this pass is rehydration from SQLite.
//
// Falsifiability: shown RED with rehydratePendingResolutions() stubbed to
// return immediately — the second run re-detects the conflict and writes
// nothing.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::resolutionSurvivesRestart()
{
    const QDateTime srcMod = QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC);
    const QDateTime tgtMod = QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC);
    auto fx = seedConflict(QStringLiteral("Source-Modified"),
                           QStringLiteral("Target-Modified"), srcMod, tgtMod);

    // Run 1: no ConflictManager at all, so the conflict is merely recorded.
    auto first = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), kSyncTimeoutMs);

    const QList<ConflictInfo> unresolved =
        m_conflictStore->unresolvedConflicts(QString::fromLatin1(kMappingId));
    QCOMPARE(unresolved.size(), 1);
    const QString conflictId = unresolved.first().conflictId;
    QVERIFY2(!conflictId.isEmpty(),
             "SyncEngine::onWorkerConflictDetected discarded the recorded conflict id");

    // Sanity: the store must round-trip the two lastModified values, or the
    // staleness guard in run 2 would reject every rehydrated resolution and
    // this test would be green for the wrong reason in reverse.
    QCOMPARE(unresolved.first().sourceModified.toSecsSinceEpoch(),
             srcMod.toSecsSinceEpoch());
    QCOMPARE(unresolved.first().targetModified.toSecsSinceEpoch(),
             tgtMod.toSecsSinceEpoch());

    // The user answers the dialog. This one line is EVERYTHING that used to
    // happen when they did.
    m_conflictStore->resolveConflict(conflictId, ConflictResolution::SourceWins);

    // Restart: engine gone and rebuilt, backends and stores untouched.
    restartEngine();

    auto second = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), kSyncTimeoutMs);
    QVERIFY2(!second.isCanceled(), "future was canceled");

    auto tgtInc = fx.target->incidence(QString::fromLatin1(kCalendarId),
                                       QString::fromLatin1(kConflictUid));
    QVERIFY(tgtInc);
    QCOMPARE(tgtInc->summary(), QStringLiteral("Source-Modified"));
    QVERIFY(m_conflictStore->resolvedConflicts(QString::fromLatin1(kMappingId)).isEmpty());

    m_engine->setSyncMappings({});
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10 — consume-once. A resolution must apply EXACTLY once. A row left
// behind would silently auto-apply to a future GENUINE conflict for the same
// record, which is a worse data-loss bug than the one being fixed.
//
// The third conflict is re-seeded with the SAME contents and the SAME
// lastModified values as the first, so the staleness guard cannot fire — this
// isolates consumption from staleness. The resolver answers Skip the second
// time, so the ONLY thing that could write "Source-Modified" to the target is
// the first resolution re-applying.
//
// Falsifiability: shown RED with the consumeAppliedResolutions() call removed
// from onWorkerSyncCompleted — the retained SourceWins fires again and the
// target loses "Target-Modified-Again" without anyone choosing that.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::appliedResolutionIsNotReapplied()
{
    const QDateTime srcMod = QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC);
    const QDateTime tgtMod = QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC);
    auto fx = seedConflict(QStringLiteral("Source-Modified"),
                           QStringLiteral("Target-Modified"), srcMod, tgtMod);

    auto *resolver = new StubConflictResolver;
    resolver->resolution = ConflictResolution::SourceWins;

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_conflictManager->setConflictResolver(resolver);   // takes ownership
    m_engine->setConflictManager(m_conflictManager.get());

    auto first = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), kSyncTimeoutMs);
    QCOMPARE(fx.target->incidence(QString::fromLatin1(kCalendarId),
                                  QString::fromLatin1(kConflictUid))->summary(),
             QStringLiteral("Source-Modified"));
    QVERIFY(m_conflictStore->resolvedConflicts(QString::fromLatin1(kMappingId)).isEmpty());

    // A NEW, genuine conflict for the same record — same timestamps as before,
    // so nothing about it looks stale.
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               eventToIcal(makeEvent(
                                                   QString::fromLatin1(kConflictUid),
                                                   QStringLiteral("Baseline")))));
    fx.source->addIncidence(QString::fromLatin1(kCalendarId),
                            makeEventAt(QString::fromLatin1(kConflictUid),
                                        QStringLiteral("Source-Modified"), srcMod));
    fx.target->addIncidence(QString::fromLatin1(kCalendarId),
                            makeEventAt(QString::fromLatin1(kConflictUid),
                                        QStringLiteral("Target-Modified-Again"), tgtMod));

    // This time the user declines to answer.
    resolver->resolution = ConflictResolution::Skip;
    resolver->calls = 0;

    auto second = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), kSyncTimeoutMs);

    // Presented afresh (so the conflict really was re-detected)...
    QCOMPARE(resolver->calls, 1);
    // ...and NOT silently resolved by the previous run's answer.
    auto tgtInc = fx.target->incidence(QString::fromLatin1(kCalendarId),
                                       QString::fromLatin1(kConflictUid));
    QVERIFY(tgtInc);
    QCOMPARE(tgtInc->summary(), QStringLiteral("Target-Modified-Again"));
    QVERIFY(second.resultAt(0).last().appliedConflictIds.isEmpty());

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 11 — staleness (locked decision 3). A "Keep Local" answered against one
// version of a record must never clobber an edit made after the dialog was
// answered. On a lastModified mismatch the stored resolution is discarded and
// the conflict presented again.
//
// Falsifiability: shown RED with the sameModifiedInstant() checks removed from
// the injection point — the stale SourceWins applies and the target's later
// edit ("Target-Edited-Later") is destroyed.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::staleResolutionIsDiscarded()
{
    const QDateTime srcMod = QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC);
    const QDateTime tgtMod = QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC);
    auto fx = seedConflict(QStringLiteral("Source-Modified"),
                           QStringLiteral("Target-Modified"), srcMod, tgtMod);

    // Run 1, no manager: the conflict is recorded, then "answered" the way
    // ConflictManager's store-only paths always answered.
    auto first = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), kSyncTimeoutMs);
    const QList<ConflictInfo> unresolved =
        m_conflictStore->unresolvedConflicts(QString::fromLatin1(kMappingId));
    QCOMPARE(unresolved.size(), 1);
    const QString conflictId = unresolved.first().conflictId;
    m_conflictStore->resolveConflict(conflictId, ConflictResolution::SourceWins);

    // Someone edits the target AFTER the dialog was answered.
    fx.target->addIncidence(QString::fromLatin1(kCalendarId),
                            makeEventAt(QString::fromLatin1(kConflictUid),
                                        QStringLiteral("Target-Edited-Later"),
                                        tgtMod.addSecs(600)));

    restartEngine();

    auto *resolver = new StubConflictResolver;
    resolver->resolution = ConflictResolution::Skip;   // decline again
    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_conflictManager->setConflictResolver(resolver);
    m_engine->setConflictManager(m_conflictManager.get());

    auto second = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), kSyncTimeoutMs);

    const SyncResult r = second.resultAt(0).last();
    QCOMPARE(r.staleConflictIds, QStringList{ conflictId });
    QVERIFY(r.appliedConflictIds.isEmpty());
    // The conflict was presented FRESH rather than silently resolved...
    QCOMPARE(resolver->calls, 1);
    QCOMPARE(r.unresolvedConflicts.size(), 1);
    // ...and the later edit survived.
    auto tgtInc = fx.target->incidence(QString::fromLatin1(kCalendarId),
                                       QString::fromLatin1(kConflictUid));
    QVERIFY(tgtInc);
    QCOMPARE(tgtInc->summary(), QStringLiteral("Target-Edited-Later"));

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 12 — WorkflowMode::Deferred (locked decision 4: explicit coverage, not
// coverage inherited from the Immediate path). Deferred queues the conflict for
// the dock; the host later calls ConflictManager::applyResolution(), whose own
// doc comment has always claimed "SyncEngine reads the resolution and applies
// data modifications" — a sentence that described an intent nobody wired up.
// It is true as of Task 3, and this pins it.
//
// Falsifiability: shown RED pre-Task-3 (applyResolution wrote a column, the
// target kept "Target-Modified"), and red again if SyncEngine stops connecting
// to ConflictManager::conflictResolved.
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::deferredWorkflowResolutionIsApplied()
{
    const QDateTime srcMod = QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC);
    const QDateTime tgtMod = QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC);
    auto fx = seedConflict(QStringLiteral("Source-Modified"),
                           QStringLiteral("Target-Modified"), srcMod, tgtMod);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);
    m_engine->setConflictManager(m_conflictManager.get());

    QString queuedId;
    QObject::connect(m_conflictManager.get(), &ConflictManager::conflictQueued,
                     this, [&queuedId](const ConflictInfo &c) { queuedId = c.conflictId; });

    auto first = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(first.isFinished(), kSyncTimeoutMs);

    QVERIFY2(!queuedId.isEmpty(), "Deferred workflow never queued the conflict");
    // Deferred means deferred: nothing was applied by the run itself.
    QCOMPARE(fx.target->incidence(QString::fromLatin1(kCalendarId),
                                  QString::fromLatin1(kConflictUid))->summary(),
             QStringLiteral("Target-Modified"));

    // The user answers it in the dock, later.
    QVERIFY(m_conflictManager->applyResolution(queuedId, ConflictResolution::SourceWins));

    auto second = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(second.isFinished(), kSyncTimeoutMs);

    auto tgtInc = fx.target->incidence(QString::fromLatin1(kCalendarId),
                                       QString::fromLatin1(kConflictUid));
    QVERIFY(tgtInc);
    QCOMPARE(tgtInc->summary(), QStringLiteral("Source-Modified"));

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 13 — WorkflowMode::AutoResolve (locked decision 4). The handoff
// suspected applyAutoPolicy() had the identical "record and immediately
// resolve, write nothing" shape as showImmediateDialog(); it did. It already
// emitted conflictResolved unconditionally, so making SyncEngine listen fixed
// it with no change to that method's own logic.
//
// Falsifiability: shown RED pre-Task-3 — target keeps "Target-Modified".
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::autoResolveWorkflowResolutionIsApplied()
{
    const QDateTime srcMod = QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC);
    const QDateTime tgtMod = QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC);
    auto fx = seedConflict(QStringLiteral("Source-Modified"),
                           QStringLiteral("Target-Modified"), srcMod, tgtMod);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    // LastWriteWins, not a fixed side, so the policy has to actually consult
    // the two lastModified values the fixture stamps: the TARGET is newer.
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::LastWriteWins);
    m_engine->setConflictManager(m_conflictManager.get());

    auto future = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);

    auto srcInc = fx.source->incidence(QString::fromLatin1(kCalendarId),
                                       QString::fromLatin1(kConflictUid));
    QVERIFY(srcInc);
    QCOMPARE(srcInc->summary(), QStringLiteral("Target-Modified"));
    QCOMPARE(future.resultAt(0).last().appliedConflictIds.size(), 1);

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 14 — a host with no SyncConflictStore. The in-process channel
// (ConflictManager -> conflictResolved -> SyncEngine -> next dispatchSync) must
// work on its own; persistence only buys surviving a restart.
//
// This is why showImmediateDialog() now emits conflictResolved unconditionally
// instead of only `if (m_syncStore && !conflictId.isEmpty())`: with that guard,
// a store-less host got no signal, so no application, ever.
//
// Falsifiability: shown RED with the emit put back behind the m_syncStore
// guard (no signal, target unchanged), and red without the synthesized
// conflict id in onWorkerConflictDetected (empty id, resolution unmappable).
// ─────────────────────────────────────────────────────────────────────────────
void TstSyncEngineUnification::storeLessHostStillAppliesResolution()
{
    m_engine->setSyncConflictStore(nullptr);

    const QDateTime srcMod = QDateTime::fromSecsSinceEpoch(1700000100, QTimeZone::UTC);
    const QDateTime tgtMod = QDateTime::fromSecsSinceEpoch(1700000200, QTimeZone::UTC);
    auto fx = seedConflict(QStringLiteral("Source-Modified"),
                           QStringLiteral("Target-Modified"), srcMod, tgtMod);

    auto *resolver = new StubConflictResolver;
    resolver->resolution = ConflictResolution::SourceWins;

    m_conflictManager = std::make_unique<ConflictManager>();
    // Deliberately NO setSyncConflictStore on the manager either.
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_conflictManager->setConflictResolver(resolver);
    m_engine->setConflictManager(m_conflictManager.get());

    auto future = m_engine->runSync(unmonitoredRequestForFixtureMapping());
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);

    QCOMPARE(resolver->calls, 1);
    auto tgtInc = fx.target->incidence(QString::fromLatin1(kCalendarId),
                                       QString::fromLatin1(kConflictUid));
    QVERIFY(tgtInc);
    QCOMPARE(tgtInc->summary(), QStringLiteral("Source-Modified"));

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

QTEST_MAIN(TstSyncEngineUnification)
#include "tst_syncengine_unification.moc"
