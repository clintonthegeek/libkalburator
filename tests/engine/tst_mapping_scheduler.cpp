/// G.6 Task 44 — MappingScheduler unit tests.
///
/// Tests the resource-aware FIFO scheduler in isolation using a dispatch
/// callback. No SyncEngine involvement — the scheduler is a standalone class.

#include <QtTest/QtTest>
#include <QStringList>

#include "mappingscheduler.h"

using namespace Kalburator::Sync;

class TstMappingScheduler : public QObject
{
    Q_OBJECT
private slots:
    void twoDisjointResources_bothDispatchedInOrder();
    void twoSameResource_secondWaitsForFirst();
    void fiveMixedResources_orderPreservedWithinGroup();
    void cancelTouchingResource_removesFromQueue();
    void cancelTouchingResource_doesNotAffectOthers();
    void emptyQueue_nothingDispatched();
    void onCompletedWithoutActive_isNoop();
};

/// Helper: build a simple one-entry resource map.
static QHash<QString, QSet<QString>> res(const QString &id, QSet<QString> resources)
{
    QHash<QString, QSet<QString>> m;
    m[id] = std::move(resources);
    return m;
}

/// Merge two resource maps.
static QHash<QString, QSet<QString>> merge(QHash<QString, QSet<QString>> a,
                                            const QHash<QString, QSet<QString>> &b)
{
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        a[it.key()] = it.value();
    return a;
}

// ---------------------------------------------------------------------------

void TstMappingScheduler::twoDisjointResources_bothDispatchedInOrder()
{
    // m1 uses res-a, m2 uses res-b. Disjoint. v1 is sequential, so m1 runs
    // first (FIFO), m2 runs after m1 completes.
    MappingScheduler sched;
    QStringList dispatched;

    const auto resourceMap = merge(res(QStringLiteral("m1"), {QStringLiteral("res-a")}),
                                   res(QStringLiteral("m2"), {QStringLiteral("res-b")}));

    sched.enqueue({QStringLiteral("m1"), QStringLiteral("m2")}, resourceMap,
                  [&](const QString &id) { dispatched << id; });

    // m1 dispatched immediately.
    QCOMPARE(dispatched, QStringList{QStringLiteral("m1")});
    QCOMPARE(sched.active(), QStringLiteral("m1"));
    QVERIFY(sched.hasQueued());

    sched.onCompleted(QStringLiteral("m1"));

    // m2 dispatched after m1 completes.
    QCOMPARE(dispatched, (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
    QCOMPARE(sched.active(), QStringLiteral("m2"));
    QVERIFY(!sched.hasQueued());

    sched.onCompleted(QStringLiteral("m2"));
    QVERIFY(!sched.hasActive());
}

void TstMappingScheduler::twoSameResource_secondWaitsForFirst()
{
    // Both m1 and m2 use res-a. m2 must wait for m1.
    MappingScheduler sched;
    QStringList dispatched;

    const auto resourceMap = merge(res(QStringLiteral("m1"), {QStringLiteral("res-a")}),
                                   res(QStringLiteral("m2"), {QStringLiteral("res-a")}));

    sched.enqueue({QStringLiteral("m1"), QStringLiteral("m2")}, resourceMap,
                  [&](const QString &id) { dispatched << id; });

    // Only m1 dispatched; m2 is still queued.
    QCOMPARE(dispatched, QStringList{QStringLiteral("m1")});
    QVERIFY(sched.hasQueued());

    sched.onCompleted(QStringLiteral("m1"));

    // m2 dispatched after m1 done.
    QCOMPARE(dispatched, (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
    QVERIFY(!sched.hasQueued());

    sched.onCompleted(QStringLiteral("m2"));
    QVERIFY(!sched.hasActive());
}

void TstMappingScheduler::fiveMixedResources_orderPreservedWithinGroup()
{
    // Five mappings: m1(res-a), m2(res-b), m3(res-a), m4(res-c), m5(res-a).
    // v1 sequential — FIFO dispatch across all resources.
    // Expected dispatch order: m1, m2, m3, m4, m5.
    MappingScheduler sched;
    QStringList dispatched;

    QHash<QString, QSet<QString>> resourceMap;
    resourceMap[QStringLiteral("m1")] = {QStringLiteral("res-a")};
    resourceMap[QStringLiteral("m2")] = {QStringLiteral("res-b")};
    resourceMap[QStringLiteral("m3")] = {QStringLiteral("res-a")};
    resourceMap[QStringLiteral("m4")] = {QStringLiteral("res-c")};
    resourceMap[QStringLiteral("m5")] = {QStringLiteral("res-a")};

    const QStringList ids{QStringLiteral("m1"), QStringLiteral("m2"),
                          QStringLiteral("m3"), QStringLiteral("m4"),
                          QStringLiteral("m5")};

    sched.enqueue(ids, resourceMap, [&](const QString &id) { dispatched << id; });

    // m1 dispatched first.
    QCOMPARE(dispatched, QStringList{QStringLiteral("m1")});

    const QStringList expected{QStringLiteral("m1"), QStringLiteral("m2"),
                               QStringLiteral("m3"), QStringLiteral("m4"),
                               QStringLiteral("m5")};

    // Complete each in order; verify the next is dispatched.
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(sched.active(), expected[i]);
        sched.onCompleted(expected[i]);
        if (i + 1 < 5)
            QCOMPARE(dispatched.size(), i + 2);
    }

    QCOMPARE(dispatched, expected);
    QVERIFY(!sched.hasActive());
}

void TstMappingScheduler::cancelTouchingResource_removesFromQueue()
{
    // m1(res-a), m2(res-b), m3(res-a). Cancel res-a while m1 is active.
    // Queue contains m2 and m3. After cancel: queue should have only m2.
    MappingScheduler sched;
    QStringList dispatched;

    QHash<QString, QSet<QString>> resourceMap;
    resourceMap[QStringLiteral("m1")] = {QStringLiteral("res-a")};
    resourceMap[QStringLiteral("m2")] = {QStringLiteral("res-b")};
    resourceMap[QStringLiteral("m3")] = {QStringLiteral("res-a")};

    sched.enqueue({QStringLiteral("m1"), QStringLiteral("m2"), QStringLiteral("m3")},
                  resourceMap,
                  [&](const QString &id) { dispatched << id; });

    QCOMPARE(sched.active(), QStringLiteral("m1"));

    // Cancel res-a from the queue (not the active mapping).
    const QStringList cancelled = sched.cancelMappingsTouchingResource(
        QStringLiteral("res-a"));

    // m3 removed (uses res-a); m2 stays (uses res-b).
    QCOMPARE(cancelled, QStringList{QStringLiteral("m3")});
    QVERIFY(sched.hasQueued()); // m2 still queued

    // m1 completes; only m2 dispatches next (m3 was cancelled).
    sched.onCompleted(QStringLiteral("m1"));
    QCOMPARE(dispatched, (QStringList{QStringLiteral("m1"), QStringLiteral("m2")}));
    QVERIFY(!sched.hasQueued());

    sched.onCompleted(QStringLiteral("m2"));
    QVERIFY(!sched.hasActive());
}

void TstMappingScheduler::cancelTouchingResource_doesNotAffectOthers()
{
    // m1(res-a), m2(res-b), m3(res-c). Cancel res-z (no match).
    MappingScheduler sched;
    QStringList dispatched;

    QHash<QString, QSet<QString>> resourceMap;
    resourceMap[QStringLiteral("m1")] = {QStringLiteral("res-a")};
    resourceMap[QStringLiteral("m2")] = {QStringLiteral("res-b")};
    resourceMap[QStringLiteral("m3")] = {QStringLiteral("res-c")};

    sched.enqueue({QStringLiteral("m1"), QStringLiteral("m2"), QStringLiteral("m3")},
                  resourceMap,
                  [&](const QString &id) { dispatched << id; });

    const QStringList cancelled = sched.cancelMappingsTouchingResource(
        QStringLiteral("res-z"));

    QVERIFY(cancelled.isEmpty());
    QVERIFY(sched.hasQueued()); // m2, m3 still queued

    // Complete all normally.
    sched.onCompleted(QStringLiteral("m1"));
    sched.onCompleted(QStringLiteral("m2"));
    sched.onCompleted(QStringLiteral("m3"));

    QCOMPARE(dispatched,
             (QStringList{QStringLiteral("m1"),
                          QStringLiteral("m2"),
                          QStringLiteral("m3")}));
}

void TstMappingScheduler::emptyQueue_nothingDispatched()
{
    MappingScheduler sched;
    bool dispatched = false;
    sched.enqueue({}, {}, [&](const QString &) { dispatched = true; });
    QVERIFY(!dispatched);
    QVERIFY(!sched.hasActive());
    QVERIFY(!sched.hasQueued());
}

void TstMappingScheduler::onCompletedWithoutActive_isNoop()
{
    // Calling onCompleted when idle must not crash or trigger dispatch.
    MappingScheduler sched;
    bool dispatched = false;
    sched.enqueue({}, {}, [&](const QString &) { dispatched = true; });
    sched.onCompleted(QStringLiteral("nonexistent"));
    QVERIFY(!dispatched);
}

QTEST_MAIN(TstMappingScheduler)
#include "tst_mapping_scheduler.moc"
