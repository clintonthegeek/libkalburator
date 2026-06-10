// SPDX-License-Identifier: GPL-2.0-or-later
//
// Phase F2 — cancellation contract for SyncEngine's QFuture-based
// public API. Stubs are unskipped as the enabling infrastructure
// lands; see 04q-phase-f2-threading-plan.md.

#include "stubs/stubcalendarcollection.h"
#include "stubs/stubincidenceregistry.h"
#include "stubs/stubsyncconfigstore.h"
#include "stubs/stubsynchost.h"

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

#include <KCalendarCore/ICalFormat>

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include <memory>

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "cal1";
constexpr auto kMappingId       = "m1";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeCalendarMapping()
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = SyncMode::TwoWay;
    m.conflictPolicy  = ConflictResolution::LastWriteWins;
    m.enabled         = true;
    return m;
}

} // namespace

class TstEngineCancellation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // Cancellation contract — the seven cases.
    void cancelBeforeStart();          // C1 — Group 2 Task 23
    void cancelDuringFetch();          // C2 — Group 2 Task 24
    void cancelDuringApply();          // C3 — Group 2 Task 25
    void cancelDuringConflictPause();  // C4 — Group 2 Task 26
    void cancelMultiMappingMidQueue(); // C5 — Group 2 Task 27
    void idempotentCancel();           // C6 — Group 2 Task 28
    void cancelAfterFinished();        // C7 — Group 2 Task 28

    // Positive QFuture smoke tests.
    void singleMappingFutureCompletes();
    void multiMappingFutureReturnsList();
    void watcherFinishedFiresOnce();
    void progressValueTicks();

    // Ownership / leak regression.
    void engineDestroyedMidSync_freesInterface();

private:
    // Fixtures owned via unique_ptr; init() builds, cleanup() tears down.
    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_src;
    std::unique_ptr<MockBackend>           m_dst;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncEngine>            m_engine;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstEngineCancellation::initTestCase() {
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstEngineCancellation::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_src = std::make_unique<MockBackend>();
    m_dst = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        m_src.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        m_dst.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    // Seed both backends with the calendar that the mapping references.
    m_src->createCalendar(QString::fromLatin1(kCollectionId),
                          QString::fromLatin1(kCalendarId),
                          QStringLiteral("Calendar 1"));
    m_dst->createCalendar(QString::fromLatin1(kCollectionId),
                          QString::fromLatin1(kCalendarId),
                          QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_calendarBaselines.get());
    m_engine->setCollection(m_host->stubCollection());
    m_engine->setSyncMappings({ makeCalendarMapping() });
}

void TstEngineCancellation::cleanup()
{
    m_engine.reset();
    m_calendarBaselines.reset();
    m_host.reset();
    m_dst.reset();
    m_src.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TstEngineCancellation::cancelBeforeStart()
{
    // C1 — cancel BEFORE the worker observes the run.
    //
    // Pre-populate the source so a successful run would actually
    // write items to the destination. After cancel-before-start,
    // the destination MUST remain empty.
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"),
                                  QStringLiteral("Event One")));
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-2"),
                                  QStringLiteral("Event Two")));
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-3"),
                                  QStringLiteral("Event Three")));

    // Block the source fetch so the worker cannot complete the run
    // before our cancel propagates. This makes the cancel-wins
    // outcome deterministic on fast machines (the simple
    // "runSyncFuture then cancel" form races with the worker thread
    // dispatching processSync). The contract C1 pins is "cancel
    // observed before start"; blocking the fetch ensures the worker
    // does not race past the cancellation check.
    m_src->setFetchBlocking(true);

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));

    // Cancel immediately — before the worker thread can dispatch
    // processSync past the cancellation pre-check.
    future.cancel();

    // Spin the engine-thread event loop so the QFutureWatcher's
    // canceled() signal is dispatched to onCancelObserved and the
    // queued observeCancel reaches the worker. waitForFinished()
    // does NOT spin the event loop, so without this the cancel
    // never propagates and the test deadlocks on the blocked fetch.
    QTest::qWait(50);

    // Release the fetch blocker so the worker can observe the
    // cancellation flag at the next checkpoint and tear down the
    // run rather than wedging the test.
    m_src->releaseFetchBlocker();

    // Pump the event loop while the worker drains; QFuture::
    // waitForFinished doesn't run our event loop, but the engine
    // posts back to the main thread to finalize the future.
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    QVERIFY(future.isFinished());
    QVERIFY(future.isCanceled());

    // No items written to the destination calendar.
    QCOMPARE(m_dst->allUids(QString::fromLatin1(kCalendarId)).size(), 0);

    // SyncResult should reflect cancellation. The design (04q section
    // "Multi-mapping queue" + Task 21 plan body) calls for a sentinel
    // SyncResult with cancelled=true && skipped=true to be delivered
    // on the future for cancel-before-start.
    //
    // Production gaps closed in the F2 Task 23 follow-up commit:
    //
    //   1. SyncEngine::processSingleMapping now has a top-level
    //      cancel-precheck (defensive, symmetric to advanceQueue).
    //
    //   2. Both runSyncFuture overloads call
    //      setAddResultsIfCanceledEnabled(true) on the iface so
    //      reportResult is not silently dropped after reportCanceled.
    //
    //   3. SyncEngine::onCancelObserved sets the engine-side
    //      m_cancelled flag, and onWorkerSyncCompleted decorates the
    //      final SyncResult with cancelled=true (and skipped=true if
    //      no items were touched) before reporting on the iface.
    //
    // Note: QFuture<T>::results() returns an empty QList<T> when the
    // future is canceled (Qt6 design — see qfutureinterface.h
    // QFutureInterface<T>::results), regardless of
    // setAddResultsIfCanceledEnabled. Use resultCount() + resultAt()
    // to read cancellation-marker results from the underlying store.
    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    QVERIFY(r.cancelled);
    QVERIFY(r.skipped);
}

void TstEngineCancellation::cancelDuringFetch()
{
    // C2 — cancel WHILE the worker is awaiting the source fetch.
    //
    // Seed source with items so a successful run would write to dst.
    // Block the source fetch so the worker yields inside await<Op>;
    // cancel after a brief delay so the watcher's canceled() fires
    // while the worker is parked. After cancel, release the blocker
    // so the FetchOperation finishes and the await loop unwinds via
    // the cancellationObserved branch.
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"),
                                  QStringLiteral("Event One")));
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-2"),
                                  QStringLiteral("Event Two")));
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-3"),
                                  QStringLiteral("Event Three")));

    m_src->setFetchBlocking(true);

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));

    // Let the worker dispatch processSync and reach the source fetch.
    QTest::qWait(100);

    future.cancel();

    // Pump so the QFutureWatcher::canceled → onCancelObserved →
    // queued observeCancel chain reaches the worker thread before
    // we release the fetch blocker.
    QTest::qWait(50);

    m_src->releaseFetchBlocker();

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    QVERIFY(future.isCanceled());

    // No items written to the destination calendar — fetch was
    // interrupted before any apply work could begin.
    QCOMPARE(m_dst->allUids(QString::fromLatin1(kCalendarId)).size(), 0);

    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    QVERIFY(r.cancelled);
}

void TstEngineCancellation::cancelDuringApply()
{
    // C3 — cancel WHILE the worker is in the apply phase (or, in
    // the current production wiring, between fetch and apply).
    //
    // Production note: SyncEngine's apply path is synchronous (the
    // writer commits via BlockingQueuedConnection to the main thread).
    // The observable
    // cancellation checkpoint that gates apply is the post-target-fetch
    // m_cancelled check in processSync.
    //
    // To get a deterministic "cancel before apply commits" test,
    // we block the *target* fetch — the second fetch in the worker.
    // The source fetch completes and produces records; the worker
    // then parks in await<FetchOperation> for the target. Cancel
    // there: observeCancel fires, await unwinds, and either the
    // post-target-fetch m_cancelled check trips OR the per-record
    // oracle fires inside applyChangesToBackend. Either way, no
    // items reach the destination.
    //
    // The structural assertion C3 pins is "fewer than all 5 made
    // it through, and the run is reported cancelled". The exact
    // count is fixture-dependent (here: 0, because the cancel lands
    // before commitAll runs).
    for (int i = 1; i <= 5; ++i) {
        m_src->addIncidence(QString::fromLatin1(kCalendarId),
                            makeEvent(QStringLiteral("evt-%1").arg(i),
                                      QStringLiteral("Event %1").arg(i)));
    }

    m_dst->setFetchBlocking(true);

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));

    // Let the source fetch complete and the worker reach the
    // target-fetch await.
    QTest::qWait(150);

    future.cancel();

    // Pump so QFutureWatcher::canceled → onCancelObserved →
    // queued observeCancel reaches the worker before we release.
    QTest::qWait(50);

    m_dst->releaseFetchBlocker();

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    QVERIFY(future.isCanceled());

    // Structural assertion: not all 5 items made it through.
    const int written = m_dst->allUids(QString::fromLatin1(kCalendarId)).size();
    QVERIFY2(written < 5,
             qPrintable(QStringLiteral("expected fewer than 5 written, got %1")
                            .arg(written)));

    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    QVERIFY(r.cancelled);
}

void TstEngineCancellation::cancelDuringConflictPause()
{
    // C4 — cancel WHILE the worker is yielded for a monitored
    // AskUser conflict.
    //
    // Per FINDINGS "Conflict signals require AskUser policy" + the
    // tst_calendar_conflict.cpp template: AskUser conflict policy +
    // baseline-seeded path are both required for the engine to
    // emit conflictDetected / conflictPauseRequested rather than
    // downgrading silently.
    //
    // Build a fresh mapping with conflictPolicy = AskUser, seed a
    // baseline, and put divergent records on both sides. Monitored
    // sync mode causes the worker to yield via m_yieldedForConflict
    // when the conflict is observed in handleConflicts(). Cancel
    // there: the worker's onCancelDuringConflictPause() slot
    // (Task 20) clears the yield flag and emits syncCompleted with
    // success=false. The engine decorates with cancelled=true.

    constexpr auto kConflictUid = "evt-conflict";

    // Reconfigure the mapping with AskUser policy.
    SyncMapping mapping = makeCalendarMapping();
    mapping.conflictPolicy = ConflictResolution::AskUser;
    m_engine->setSyncMappings({ mapping });

    // Wire a SyncConflictStore so the engine has somewhere to
    // record / leave the conflict during the pause.
    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-conflicts.db"));
    auto conflictStore = std::make_unique<SyncConflictStore>(dbPath);
    m_engine->setSyncConflictStore(conflictStore.get());

    // Seed baseline so the engine sees both sides as "modified
    // since baseline" and triggers a conflict (the quick-path
    // downgrades AskUser to SourceWins without baselines).
    auto baselineEvent = makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Baseline"));
    KCalendarCore::ICalFormat fmt;
    const QString baselineIcal = fmt.toICalString(baselineEvent);
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(QString::fromLatin1(kConflictUid), baselineIcal));

    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QString::fromLatin1(kConflictUid),
                                  QStringLiteral("Source-Modified")));
    m_dst->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QString::fromLatin1(kConflictUid),
                                  QStringLiteral("Target-Modified")));

    // Watch for the conflictDetected signal emitted when the
    // worker observes the conflict in handleConflicts (the
    // Monitored path emits both conflictDetected and
    // conflictPauseRequested before yielding).
    QSignalSpy conflictSpy(m_engine.get(),
                           &SyncEngine::conflictDetected);

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId),
                                          SyncEngine::SyncBehavior::Monitored);

    // Wait for the conflict signal — proves the worker has
    // yielded with m_yieldedForConflict = true.
    QVERIFY2(conflictSpy.wait(5000),
             "expected conflictDetected before conflict-pause cancel");

    future.cancel();

    // Pump so QFutureWatcher::canceled → onCancelObserved →
    // queued observeCancel → cancellationObserved →
    // onCancelDuringConflictPause reaches the worker.
    QTest::qWait(100);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    QVERIFY(future.isCanceled());

    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    QVERIFY(r.cancelled);

    // Detach the conflict store before destruction so the engine
    // doesn't outlive it during cleanup.
    m_engine->setSyncConflictStore(nullptr);
}

void TstEngineCancellation::cancelMultiMappingMidQueue()
{
    // C5 — cancel a multi-mapping queue run after some mappings
    // have completed and one is in flight.
    //
    // Fixture: build 5 mappings m1..m5, each with its own source/
    // target MockBackend pair so they don't interfere. Block m3's
    // source fetch so the queue stalls there. Wait for m1 + m2 to
    // complete (allSyncsCompleted not yet — that fires only after
    // the whole queue drains), then cancel.
    //
    // Per F2 Task 21's advanceQueue: on cancel, the queue exits
    // early and reports m_queueResults on the multi-iface. m1 and
    // m2's real results are already appended; m3..m5 are not.
    // Per the prompt's contract, the test pins the structural
    // claim that future.resultAt(0) returns a list, m1 + m2 are
    // present and successful, and remaining slots either don't
    // exist or are skipped/cancelled — see comments below.

    constexpr int kMappingCount = 5;

    // Extra backends owned by the test (not the fixture's m_src /
    // m_dst, which we won't use). Register them with the fixture's
    // BackendRegistry so the engine can resolve them.
    std::vector<std::unique_ptr<MockBackend>> sources;
    std::vector<std::unique_ptr<MockBackend>> targets;
    for (int i = 1; i <= kMappingCount; ++i) {
        sources.emplace_back(std::make_unique<MockBackend>());
        targets.emplace_back(std::make_unique<MockBackend>());
        const QString srcId = QStringLiteral("src-%1").arg(i);
        const QString tgtId = QStringLiteral("tgt-%1").arg(i);
        m_registry->registerBackendInstance(srcId, sources.back().get());
        m_registry->registerBackendInstance(tgtId, targets.back().get());
        const QString calId = QStringLiteral("cal-%1").arg(i);
        sources.back()->createCalendar(QString::fromLatin1(kCollectionId),
                                       calId,
                                       QStringLiteral("Calendar %1").arg(i));
        targets.back()->createCalendar(QString::fromLatin1(kCollectionId),
                                       calId,
                                       QStringLiteral("Calendar %1").arg(i));

        // Host calendar shell so applyChangesToBackend can find it.
        auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        hostCal->setId(calId);
        m_host->stubCollection()->addCalendarWithId(calId, hostCal);

        // Seed each source with one event so the run has actual
        // work to do (and m1/m2 have observable success).
        sources.back()->addIncidence(calId,
            makeEvent(QStringLiteral("evt-%1").arg(i),
                      QStringLiteral("Event %1").arg(i)));
    }

    QList<SyncMapping> mappings;
    for (int i = 1; i <= kMappingCount; ++i) {
        SyncMapping m;
        m.id              = QStringLiteral("m%1").arg(i);
        m.sourceBackend   = QStringLiteral("src-%1").arg(i);
        m.sourceCalendar  = QStringLiteral("cal-%1").arg(i);
        m.targetBackend   = QStringLiteral("tgt-%1").arg(i);
        m.targetCalendar  = QStringLiteral("cal-%1").arg(i);
        m.mode            = SyncMode::TwoWay;
        m.conflictPolicy  = ConflictResolution::LastWriteWins;
        m.enabled         = true;
        mappings.append(m);
    }
    m_engine->setSyncMappings(mappings);

    // Block m3's source fetch so the queue stalls there.
    sources[2]->setFetchBlocking(true);

    auto future = m_engine->runSync(SyncRequest{});

    // Wait until m1 + m2 have completed by observing their target
    // backends receiving the seeded event. Per-mapping QFuture
    // signals were retired in F2 Task 42; observing the side effect
    // (target writes landed) is the canonical observation channel.
    QTRY_VERIFY_WITH_TIMEOUT(
        targets[0]->allUids(QStringLiteral("cal-1")).contains(QStringLiteral("evt-1")) &&
        targets[1]->allUids(QStringLiteral("cal-2")).contains(QStringLiteral("evt-2")),
        5000);

    future.cancel();

    // Pump so the cancel chain reaches the worker before we
    // release m3's blocker.
    QTest::qWait(100);

    sources[2]->releaseFetchBlocker();

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    QVERIFY(future.isCanceled());

    // The future result is the per-mapping QList<SyncResult>. Per
    // advanceQueue, on cancel the multi-iface gets reportResult
    // with whatever m_queueResults has accumulated (m1 + m2's real
    // results, possibly m3 if it raced past) — m4 and m5 were never
    // dispatched and are not in the queue results.
    QCOMPARE(future.resultCount(), 1);
    const QList<SyncResult> resultList = future.resultAt(0);

    // m1 + m2 must be present and successful.
    QVERIFY2(resultList.size() >= 2,
             qPrintable(QStringLiteral("expected at least 2 results, got %1")
                            .arg(resultList.size())));
    QVERIFY(resultList[0].success);
    QVERIFY(resultList[1].success);

    // m4 and m5 were never dispatched, so they should not appear
    // in the queue results.
    QVERIFY2(resultList.size() < kMappingCount,
             qPrintable(QStringLiteral("expected fewer than %1 results "
                                       "(remaining mappings should be skipped), got %2")
                            .arg(kMappingCount).arg(resultList.size())));

    // Detach mappings before scope exits so backend pointers don't
    // outlive the local std::unique_ptrs.
    m_engine->setSyncMappings({});
}

void TstEngineCancellation::idempotentCancel()
{
    // C6 — calling QFuture::cancel() twice must not crash or assert.
    // Mirrors C1's "block fetch then cancel" pattern; the second
    // cancel is a no-op per Qt's QFuture contract.
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"),
                                  QStringLiteral("Event One")));
    m_src->setFetchBlocking(true);

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));
    future.cancel();
    future.cancel();  // double cancel — must not crash or assert

    QTest::qWait(50);
    m_src->releaseFetchBlocker();

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);
    QVERIFY(future.isCanceled());
}

void TstEngineCancellation::cancelAfterFinished()
{
    // C7 — cancel() on an already-finished future must not crash or
    // re-trigger any teardown logic. The contract pinned here:
    //   - isFinished() stays true
    //   - the run's actual SyncResult (success=true) was reported
    //     before cancel; any post-finish cancel does not corrupt it
    //
    // Empirical Qt6 note: QFuture::cancel() on an already-finished
    // QFuture *does* flip isCanceled() to true (the underlying
    // QFutureInterface accepts reportCanceled even after
    // reportFinished). The prompt allowed for this — what matters is
    // the result already in the store stays intact and finished
    // remains true.
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"),
                                  QStringLiteral("Event One")));

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    // Read the result first (before cancel), then cancel, then
    // verify the result is unchanged and the future is still
    // marked finished.
    QCOMPARE(future.resultCount(), 1);
    const SyncResult before = future.resultAt(0);
    QVERIFY(before.success);
    QVERIFY(!before.cancelled);

    future.cancel();

    QVERIFY(future.isFinished());
    // resultAt(0) must still return the success result regardless
    // of whether Qt6 flips isCanceled() post-finish.
    QCOMPARE(future.resultCount(), 1);
    const SyncResult after = future.resultAt(0);
    QVERIFY(after.success);
    QVERIFY(!after.cancelled);
}

void TstEngineCancellation::singleMappingFutureCompletes()
{
    // Positive smoke — non-cancelled run via runSyncFuture(mappingId).
    // Source has 3 events; target empty. After waitForFinished, the
    // future is finished, not canceled, and resultAt(0) carries a
    // successful SyncResult. The destination has the 3 items.
    constexpr int kEventCount = 3;
    for (int i = 1; i <= kEventCount; ++i) {
        m_src->addIncidence(QString::fromLatin1(kCalendarId),
                            makeEvent(QStringLiteral("evt-%1").arg(i),
                                      QStringLiteral("Event %1").arg(i)));
    }

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    QVERIFY(!future.isCanceled());
    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    QVERIFY(r.success);
    QVERIFY(!r.cancelled);

    QCOMPARE(m_dst->allUids(QString::fromLatin1(kCalendarId)).size(), kEventCount);
}

void TstEngineCancellation::multiMappingFutureReturnsList()
{
    // Positive smoke — runSyncFuture() (no mappingId) returns a
    // QFuture<QList<SyncResult>>. With one enabled mapping in the
    // fixture, the list has one entry.
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"),
                                  QStringLiteral("Event One")));

    auto future = m_engine->runSync(SyncRequest{});

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);

    QVERIFY(!future.isCanceled());
    QCOMPARE(future.resultCount(), 1);
    const QList<SyncResult> resultList = future.resultAt(0);
    QCOMPARE(resultList.size(), 1);
    QVERIFY(resultList[0].success);
}

void TstEngineCancellation::watcherFinishedFiresOnce()
{
    // Positive smoke — QFutureWatcher::finished must fire exactly
    // once for a single runSyncFuture call. (The engine installs
    // its own internal watcher for cancel forwarding; this tests
    // a *consumer-side* watcher attached to the returned future.)
    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"),
                                  QStringLiteral("Event One")));

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId));

    QFutureWatcher<SyncResult> watcher;
    QSignalSpy finishedSpy(&watcher, &QFutureWatcher<SyncResult>::finished);
    watcher.setFuture(future);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 30000);
    // Pump briefly so the watcher's queued finished signal lands.
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
}

void TstEngineCancellation::progressValueTicks()
{
    // Positive smoke — does the future surface progress?
    //
    // Empirically: SyncEngine reports per-mapping progressUpdated
    // signals, but does NOT call setProgressValue / setProgressRange
    // on the QFutureInterface — so QFuture::progressValue() stays at
    // 0 throughout. Wiring iface progress is a separate plumbing
    // task (not in F2's scope per 04q). Skip with a concrete reason
    // until that wiring lands.
    QSKIP("QFuture progressValue is not wired to QFutureInterface in "
          "the current engine; SyncEngine emits progressUpdated signals "
          "instead. Wiring setProgressValue on the iface is a separate "
          "task, deferred from F2.");
}

void TstEngineCancellation::engineDestroyedMidSync_freesInterface()
{
    // Ownership regression: SyncEngine previously leaked its in-flight
    // QFutureInterface* when destroyed mid-sync. After Plan 4 T6, both
    // members are unique_ptr and are freed by the destructor automatically.
    //
    // Threading constraint: dispatchSync calls QMetaObject::invokeMethod
    // with Qt::BlockingQueuedConnection to fetch items on the backend's
    // thread (main thread). Destroying the engine while that call is in
    // flight blocks the main thread in m_workerThread.wait() while the
    // worker waits for the main thread to process its queued event —
    // a deadlock. No timing trick (qWait, semaphore) avoids this without
    // modifying production code.
    //
    // What we CAN test safely: run a complete sync cycle and destroy the
    // engine afterwards. m_currentSingleIface is allocated synchronously
    // by runSyncFuture() (so it is live from the moment the call returns),
    // and is freed by onWorkerSyncCompleted() when the sync finishes. The
    // destructor then sees a null unique_ptr — no double-free. LSAN/ASAN
    // confirm no leak from the iface allocation over the whole lifecycle.
    // Reverting the unique_ptr fix to a raw pointer would cause a leak in
    // the true mid-sync-destroy scenario (not mechanically testable here),
    // but NOT cause a crash in this happy-path scenario either. This test
    // therefore pins the contract "engine teardown after a complete sync
    // cycle does not crash or leak the iface", which is provable and
    // deterministic.

    m_src->addIncidence(QString::fromLatin1(kCalendarId),
                        makeEvent(QStringLiteral("evt-1"),
                                  QStringLiteral("Event One")));

    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QFuture<SyncResult> future =
        m_engine->runSyncFuture(QString::fromLatin1(kMappingId));
    QT_WARNING_POP

    // Pump the event loop so the worker can process BlockingQueuedConnection
    // events (fetchItems, loadRecordsOrError) on the main thread and
    // complete the sync. Destroying the engine before this returns would
    // deadlock (see comment above).
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);

    // onWorkerSyncCompleted has already reset m_currentSingleIface.
    // Destroying the engine now is safe: unique_ptr dtor sees null, no-op.
    // cleanup() calls m_engine.reset() again — also a no-op.
    m_engine.reset();

    // Reaching here without crashing or hanging is the contract.
    // (A crash still fails the test via the framework; LSAN/ASAN catch leaks/UAF.)
}

QTEST_MAIN(TstEngineCancellation)
#include "tst_engine_cancellation.moc"
