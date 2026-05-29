/// Architectural-redress Plan 1 Task 3 — MappingQueue unit tests.
///
/// Pin the contract of the queue collaborator extracted from SyncEngine:
/// prime() resets state, next() iterates enabled+in-filter mappings in
/// order, recordResult() accumulates only in Queue mode, drain() clears,
/// lost-resource tracking is a flat set.
///
/// These are the fast inner-loop guard; the engine-level integration
/// protective tests in tests/engine/tst_syncengine_unification.cpp pin
/// the production callsite (INVARIANTS §6). Both layers stay.

#include <QtTest/QtTest>
#include <QSet>

#include "mappingqueue.h"
#include "synctypes.h"

using namespace Kalburator::Engine;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncResult;

namespace {

SyncMapping mk(const QString &id, bool enabled = true)
{
    SyncMapping m;
    m.id = id;
    m.sourceBackend  = QStringLiteral("src");
    m.sourceCalendar = QStringLiteral("cal-s");
    m.targetBackend  = QStringLiteral("tgt");
    m.targetCalendar = QStringLiteral("cal-t");
    m.enabled = enabled;
    return m;
}

} // namespace

class TstMappingQueue : public QObject
{
    Q_OBJECT

private slots:
    // prime()
    void prime_resetsAllPerRunState();
    void prime_setsDispatchModeQueue();

    // primeSingle()
    void primeSingle_setsDispatchModeSingle();
    void primeSingle_nextReturnsNullopt();
    void primeSingle_recordResultIsNoop();

    // next()
    void next_returnsMappingsInOrder();
    void next_skipsDisabled();
    void next_skipsFilteredOut();
    void next_emptyFilterMeansRunNothing();
    void next_exhaustsAtEnd();
    void next_currentIndexTracksProgress();

    // recordResult / drain
    void recordResult_accumulatesInQueueMode();
    void drain_returnsAndClears();
    void drain_isIdempotent();

    // lost-resource tracking
    void markResourceLost_recordsAndQueries();
    void markResourceLost_emptyIdIsNoop();
    void hasLostResources_reportsAnyMarked();

    // reset()
    void reset_returnsToDefaults();

    // DispatchMode getters/setters
    void dispatchMode_roundTrips();
};

// ---------------------------------------------------------------------------
// prime()
// ---------------------------------------------------------------------------

void TstMappingQueue::prime_resetsAllPerRunState()
{
    MappingQueue q;
    q.markResourceLost(QStringLiteral("old-res"));
    SyncResult prior; prior.success = false;
    q.setDispatchMode(MappingQueue::DispatchMode::Queue);
    q.recordResult(prior);

    // Now prime — all prior state should be gone.
    q.prime({mk("m1")}, std::nullopt);

    QVERIFY(!q.hasLostResources());
    QCOMPARE(q.drain().size(), 0);
    QCOMPARE(q.currentIndex(), -1);
    QVERIFY(!q.isExhausted());
}

void TstMappingQueue::prime_setsDispatchModeQueue()
{
    MappingQueue q;
    q.prime({mk("m1")}, std::nullopt);
    QCOMPARE(q.dispatchMode(), MappingQueue::DispatchMode::Queue);
}

// ---------------------------------------------------------------------------
// primeSingle()
// ---------------------------------------------------------------------------

void TstMappingQueue::primeSingle_setsDispatchModeSingle()
{
    MappingQueue q;
    q.primeSingle();
    QCOMPARE(q.dispatchMode(), MappingQueue::DispatchMode::Single);
}

void TstMappingQueue::primeSingle_nextReturnsNullopt()
{
    MappingQueue q;
    q.primeSingle();
    QVERIFY(!q.next().has_value());
}

void TstMappingQueue::primeSingle_recordResultIsNoop()
{
    MappingQueue q;
    q.primeSingle();
    SyncResult r; r.success = true;
    q.recordResult(r);
    QCOMPARE(q.drain().size(), 0);
}

// ---------------------------------------------------------------------------
// next()
// ---------------------------------------------------------------------------

void TstMappingQueue::next_returnsMappingsInOrder()
{
    MappingQueue q;
    q.prime({mk("m1"), mk("m2"), mk("m3")}, std::nullopt);

    auto a = q.next(); QVERIFY(a.has_value()); QCOMPARE(a->id, QString("m1"));
    auto b = q.next(); QVERIFY(b.has_value()); QCOMPARE(b->id, QString("m2"));
    auto c = q.next(); QVERIFY(c.has_value()); QCOMPARE(c->id, QString("m3"));
    auto d = q.next(); QVERIFY(!d.has_value());
}

void TstMappingQueue::next_skipsDisabled()
{
    MappingQueue q;
    q.prime({mk("m1", true), mk("m2", false), mk("m3", true)}, std::nullopt);

    auto a = q.next(); QVERIFY(a.has_value()); QCOMPARE(a->id, QString("m1"));
    auto b = q.next(); QVERIFY(b.has_value()); QCOMPARE(b->id, QString("m3"));
    auto c = q.next(); QVERIFY(!c.has_value());
}

void TstMappingQueue::next_skipsFilteredOut()
{
    MappingQueue q;
    QSet<QString> filter{QStringLiteral("m2"), QStringLiteral("m3")};
    q.prime({mk("m1"), mk("m2"), mk("m3")}, filter);

    auto a = q.next(); QVERIFY(a.has_value()); QCOMPARE(a->id, QString("m2"));
    auto b = q.next(); QVERIFY(b.has_value()); QCOMPARE(b->id, QString("m3"));
    auto c = q.next(); QVERIFY(!c.has_value());
}

void TstMappingQueue::next_emptyFilterMeansRunNothing()
{
    MappingQueue q;
    // G.6 Task 43 semantics: filter engaged with empty set = run nothing.
    q.prime({mk("m1"), mk("m2")}, QSet<QString>{});
    QVERIFY(!q.next().has_value());
    QVERIFY(q.isExhausted());
}

void TstMappingQueue::next_exhaustsAtEnd()
{
    MappingQueue q;
    q.prime({mk("m1")}, std::nullopt);
    QVERIFY(!q.isExhausted());
    (void)q.next();
    QVERIFY(!q.isExhausted());   // m1 returned; not exhausted yet
    (void)q.next();
    QVERIFY(q.isExhausted());    // now past the end
}

void TstMappingQueue::next_currentIndexTracksProgress()
{
    MappingQueue q;
    q.prime({mk("m1"), mk("m2", false), mk("m3")}, std::nullopt);

    QCOMPARE(q.currentIndex(), -1);
    (void)q.next(); QCOMPARE(q.currentIndex(), 0);  // m1
    (void)q.next(); QCOMPARE(q.currentIndex(), 2);  // skipped m2, returned m3
}

// ---------------------------------------------------------------------------
// recordResult / drain
// ---------------------------------------------------------------------------

void TstMappingQueue::recordResult_accumulatesInQueueMode()
{
    MappingQueue q;
    q.prime({mk("m1"), mk("m2")}, std::nullopt);

    SyncResult r1; r1.success = true;
    SyncResult r2; r2.success = false;
    q.recordResult(r1);
    q.recordResult(r2);

    QList<SyncResult> results = q.drain();
    QCOMPARE(results.size(), 2);
    QCOMPARE(results[0].success, true);
    QCOMPARE(results[1].success, false);
}

void TstMappingQueue::drain_returnsAndClears()
{
    MappingQueue q;
    q.prime({mk("m1")}, std::nullopt);
    SyncResult r; r.success = true;
    q.recordResult(r);

    QCOMPARE(q.drain().size(), 1);
    QCOMPARE(q.drain().size(), 0);   // already drained
}

void TstMappingQueue::drain_isIdempotent()
{
    MappingQueue q;
    q.prime({mk("m1")}, std::nullopt);
    QCOMPARE(q.drain().size(), 0);
    QCOMPARE(q.drain().size(), 0);
}

// ---------------------------------------------------------------------------
// lost-resource tracking
// ---------------------------------------------------------------------------

void TstMappingQueue::markResourceLost_recordsAndQueries()
{
    MappingQueue q;
    q.prime({mk("m1")}, std::nullopt);
    QVERIFY(!q.isResourceLost(QStringLiteral("res-a")));
    q.markResourceLost(QStringLiteral("res-a"));
    QVERIFY(q.isResourceLost(QStringLiteral("res-a")));
    QVERIFY(!q.isResourceLost(QStringLiteral("res-b")));
}

void TstMappingQueue::markResourceLost_emptyIdIsNoop()
{
    MappingQueue q;
    q.prime({mk("m1")}, std::nullopt);
    q.markResourceLost(QString{});
    QVERIFY(!q.hasLostResources());
}

void TstMappingQueue::hasLostResources_reportsAnyMarked()
{
    MappingQueue q;
    q.prime({mk("m1")}, std::nullopt);
    QVERIFY(!q.hasLostResources());
    q.markResourceLost(QStringLiteral("res-a"));
    QVERIFY(q.hasLostResources());
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------

void TstMappingQueue::reset_returnsToDefaults()
{
    MappingQueue q;
    q.prime({mk("m1"), mk("m2")}, QSet<QString>{QStringLiteral("m1")});
    q.markResourceLost(QStringLiteral("res-a"));
    SyncResult r; r.success = true;
    q.recordResult(r);
    (void)q.next();

    q.reset();

    QCOMPARE(q.dispatchMode(), MappingQueue::DispatchMode::None);
    QCOMPARE(q.currentIndex(), -1);
    QCOMPARE(q.totalSize(), 0);
    QVERIFY(!q.isExhausted());
    QVERIFY(!q.hasLostResources());
    QCOMPARE(q.drain().size(), 0);
}

// ---------------------------------------------------------------------------
// DispatchMode
// ---------------------------------------------------------------------------

void TstMappingQueue::dispatchMode_roundTrips()
{
    MappingQueue q;
    QCOMPARE(q.dispatchMode(), MappingQueue::DispatchMode::None);
    q.setDispatchMode(MappingQueue::DispatchMode::Single);
    QCOMPARE(q.dispatchMode(), MappingQueue::DispatchMode::Single);
    q.setDispatchMode(MappingQueue::DispatchMode::Queue);
    QCOMPARE(q.dispatchMode(), MappingQueue::DispatchMode::Queue);
    q.setDispatchMode(MappingQueue::DispatchMode::None);
    QCOMPARE(q.dispatchMode(), MappingQueue::DispatchMode::None);
}

QTEST_MAIN(TstMappingQueue)
#include "tst_mappingqueue.moc"
