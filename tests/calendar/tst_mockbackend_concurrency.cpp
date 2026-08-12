// tests/calendar/tst_mockbackend_concurrency.cpp
// Parallel-sync Task 6 — MockBackend concurrency high-water-mark
// instrumentation (maxConcurrentOps() / maxConcurrentOpsOn() /
// resetConcurrencyStats()). Task 8's scheduler tests need to PROVE that
// two mappings ran at once (and that two sharing an endpoint did not)
// deterministically, rather than inferring it from wall-clock timing,
// which would be a flaky race. This file pins the instrumentation itself.

#include <QtTest>
#include <QSignalSpy>

#include "mockbackend.h"
#include "syncoperation.h"

using namespace Kalburator::Sync;

class TstMockBackendConcurrency : public QObject
{
    Q_OBJECT
private slots:
    void testConcurrentFetchesOnDistinctCollectionsAreObserved();
    void testTheSameCollectionNeverRunsTwoOpsAtOnce();
    void testResetConcurrencyStatsClearsCounters();
};

void TstMockBackendConcurrency::testConcurrentFetchesOnDistinctCollectionsAreObserved()
{
    MockBackend backend;
    backend.setOperationDelay(100);
    backend.setCalendarData(QStringLiteral("a"), {});
    backend.setCalendarData(QStringLiteral("b"), {});
    backend.resetConcurrencyStats();

    auto *opA = backend.fetchItems(QStringLiteral("a"));
    auto *opB = backend.fetchItems(QStringLiteral("b"));
    QTRY_VERIFY_WITH_TIMEOUT(opA->isFinished() && opB->isFinished(), 5000);

    QCOMPARE(backend.maxConcurrentOps(), 2);
    opA->deleteLater();
    opB->deleteLater();
}

void TstMockBackendConcurrency::testTheSameCollectionNeverRunsTwoOpsAtOnce()
{
    // The per-collection FIFO in SyncBackendBase::enqueueOperation is what
    // guarantees this. Pinning it here makes the instrumentation itself
    // trustworthy before Task 8 relies on it.
    MockBackend backend;
    backend.setOperationDelay(100);
    backend.setCalendarData(QStringLiteral("a"), {});
    backend.resetConcurrencyStats();

    auto *op1 = backend.fetchItems(QStringLiteral("a"));
    auto *op2 = backend.fetchItems(QStringLiteral("a"));
    QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished() && op2->isFinished(), 5000);

    QCOMPARE(backend.maxConcurrentOpsOn(QStringLiteral("a")), 1);
    op1->deleteLater();
    op2->deleteLater();
}

void TstMockBackendConcurrency::testResetConcurrencyStatsClearsCounters()
{
    MockBackend backend;
    backend.setOperationDelay(50);
    backend.setCalendarData(QStringLiteral("a"), {});
    backend.resetConcurrencyStats();

    auto *op1 = backend.fetchItems(QStringLiteral("a"));
    QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 5000);
    QCOMPARE(backend.maxConcurrentOps(), 1);
    op1->deleteLater();

    backend.resetConcurrencyStats();
    QCOMPARE(backend.maxConcurrentOps(), 0);
    QCOMPARE(backend.maxConcurrentOpsOn(QStringLiteral("a")), 0);

    auto *op2 = backend.fetchItems(QStringLiteral("a"));
    QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 5000);
    QCOMPARE(backend.maxConcurrentOps(), 1);
    op2->deleteLater();
}

QTEST_MAIN(TstMockBackendConcurrency)
#include "tst_mockbackend_concurrency.moc"
