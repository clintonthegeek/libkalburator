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
    // via future.results() for cancel-before-start.
    //
    // Two production gaps surfaced by this C1 test land as a follow-
    // up task (NOT Task 23 — per Task 23's "DO NOT modify production
    // code just to make C1 pass; surface as a CONCERN" directive):
    //
    //   1. SyncEngine::processSingleMapping has no top-level
    //      cancel-precheck. Task 21's plan body specified one that
    //      emits {cancelled=true, skipped=true} via the iface; the
    //      landed Task 21 commit (35c1881) did not include it.
    //
    //   2. QFutureInterface::reportResult silently drops results
    //      once the Canceled flag is set, unless
    //      setAddResultsIfCanceledEnabled(true) is called on the
    //      iface. runSyncFuture does not call it, so even if the
    //      engine populated a sentinel SyncResult after cancel(),
    //      future.results() would still be empty for the consumer.
    //
    // The QEXPECT_FAIL block below pins the contract: when those
    // gaps close, the XFAIL flips to XPASS and forces a refactor of
    // this test (which is exactly the desired signal).
    QEXPECT_FAIL("",
        "F2 follow-up: SyncEngine lacks cancel-precheck + "
        "setAddResultsIfCanceledEnabled, so the sentinel SyncResult "
        "with cancelled=true && skipped=true never reaches "
        "future.results() on cancel-before-start.",
        Abort);
    const auto results = future.results();
    QVERIFY(!results.isEmpty());
    const SyncResult &r = results.first();
    QVERIFY(r.cancelled);
    QVERIFY(r.skipped);
}

void TstEngineCancellation::cancelDuringFetch()
{
    QSKIP("Stub. Implemented in Group 2 Task 24 once await<Op> + "
          "MockBackend blockable fetch are in place.");
}

void TstEngineCancellation::cancelDuringApply()
{
    QSKIP("Stub. Implemented in Group 2 Task 25 once per-record "
          "cancellation check in the apply phase is wired.");
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
