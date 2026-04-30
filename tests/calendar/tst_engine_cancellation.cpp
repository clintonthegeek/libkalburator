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
#include "calendarbaselinestore.h"
#include "calendardomainadapter.h"
#include "mockbackend.h"
#include "syncengine.h"
#include "synctypes.h"

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
    void cleanupTestCase();
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

private:
    // Fixtures owned via unique_ptr; init() builds, cleanup() tears down.
    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_src;
    std::unique_ptr<MockBackend>           m_dst;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<CalendarBaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncEngine>            m_engine;
};

void TstEngineCancellation::initTestCase() {}
void TstEngineCancellation::cleanupTestCase() {}

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
    m_calendarBaselines = std::make_unique<CalendarBaselineStore>(dbPath);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setCalendarBaselineStore(m_calendarBaselines.get());
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
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);

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

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);

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
    // domain adapter builds a SyncTransaction of the changes and
    // commits via BlockingQueuedConnection to the main thread). The
    // observable cancellation checkpoint that gates apply is the
    // per-record CancelOracle in CalendarDomainAdapter::
    // applyChangesToBackend (F2 Task 19) plus the post-target-fetch
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

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);

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
    QSKIP("Stub. Implemented in Group 2 Task 26 once the "
          "conflict-pause QEventLoop is wired to the cancellation "
          "channel.");
}

void TstEngineCancellation::cancelMultiMappingMidQueue()
{
    QSKIP("Stub. Implemented in Group 2 Task 27 once multi-mapping "
          "queue cancellation is in place.");
}

void TstEngineCancellation::idempotentCancel()
{
    QSKIP("Stub. Implemented in Group 2 Task 28.");
}

void TstEngineCancellation::cancelAfterFinished()
{
    QSKIP("Stub. Implemented in Group 2 Task 28.");
}

void TstEngineCancellation::singleMappingFutureCompletes()
{
    QSKIP("Stub. Implemented in Group 2 Task 29 (positive smoke).");
}

void TstEngineCancellation::multiMappingFutureReturnsList()
{
    QSKIP("Stub. Implemented in Group 2 Task 29 (positive smoke).");
}

void TstEngineCancellation::watcherFinishedFiresOnce()
{
    QSKIP("Stub. Implemented in Group 2 Task 29 (positive smoke).");
}

void TstEngineCancellation::progressValueTicks()
{
    QSKIP("Stub. Implemented in Group 2 Task 29 (positive smoke).");
}

QTEST_MAIN(TstEngineCancellation)
#include "tst_engine_cancellation.moc"
