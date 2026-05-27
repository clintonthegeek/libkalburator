// Phase 5 step 2 — SyncRunCoordinator canary tests.
// Verifies construction and signal emission without requiring a real SyncEngine
// run (which needs a full backend/registry setup).

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QObject>

#include "syncruncoordinator.h"
#include "syncengine.h"
#include "shaperegistries.h"
#include "backendregistry.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Engine;

class TestSyncRunCoordinator : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Construction ──────────────────────────────────────────────────────────

    void constructionWithValidEngine()
    {
        // SyncEngine requires a BackendRegistry + ISyncHost. Use a minimal
        // BackendRegistry and a null ISyncHost (nullptr is accepted for the
        // basic construction path tested here).
        Kalburator::Sync::BackendRegistry registry;
        Kalburator::Shape::ShapeRegistries shape;
        SyncEngine engine(&registry, nullptr, shape);

        // Coordinator borrows all pointers; null stores are allowed (spec § 3).
        SyncRunCoordinator coordinator(&engine, nullptr, nullptr, nullptr);

        QCOMPARE(coordinator.syncEngine(), &engine);
        QVERIFY(coordinator.baselineStore()    == nullptr);
        QVERIFY(coordinator.syncConflictStore()== nullptr);
        QVERIFY(coordinator.conflictManager()  == nullptr);
    }

    void currentSyncFuture_returnsDefaultFutureBeforeAnyRun()
    {
        Kalburator::Sync::BackendRegistry registry;
        Kalburator::Shape::ShapeRegistries shape;
        SyncEngine engine(&registry, nullptr, shape);

        SyncRunCoordinator coordinator(&engine, nullptr, nullptr, nullptr);

        auto f = coordinator.currentSyncFuture();
        // A default-constructed QFuture is finished (no pending work).
        QVERIFY(f.isFinished() || !f.isRunning());
    }

    void accessors_returnPassedPointers()
    {
        Kalburator::Sync::BackendRegistry registry;
        Kalburator::Shape::ShapeRegistries shape;
        SyncEngine engine(&registry, nullptr, shape);

        // Null stores are fine — borrowed ownership allows null.
        SyncRunCoordinator coordinator(&engine, nullptr, nullptr, nullptr);

        QCOMPARE(coordinator.syncEngine(), &engine);
    }

    // runSync() on an engine with no mappings must be a no-op (no crash, no
    // signal emitted — hasSyncWork() returns false).
    void runSync_noMappings_isNoOp()
    {
        Kalburator::Sync::BackendRegistry registry;
        Kalburator::Shape::ShapeRegistries shape;
        SyncEngine engine(&registry, nullptr, shape);

        SyncRunCoordinator coordinator(&engine, nullptr, nullptr, nullptr);

        QSignalSpy startedSpy(&coordinator, &SyncRunCoordinator::syncRunStarted);
        QSignalSpy finishedSpy(&coordinator, &SyncRunCoordinator::syncRunFinished);

        coordinator.runSync(SyncEngine::SyncBehavior::Unmonitored);

        // No sync work → neither signal should have fired.
        QCOMPARE(startedSpy.count(), 0);
        QCOMPARE(finishedSpy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestSyncRunCoordinator)
#include "tst_syncruncoordinator.moc"
