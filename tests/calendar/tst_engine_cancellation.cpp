// SPDX-License-Identifier: GPL-2.0-or-later
//
// Phase F2 — cancellation contract for SyncEngine's QFuture-based
// public API. Stubs are unskipped as the enabling infrastructure
// lands; see 04q-phase-f2-threading-plan.md.

#include "stubs/stubcalendarcollection.h"
#include "stubs/stubincidenceregistry.h"
#include "stubs/stubsyncconfigstore.h"
#include "stubs/stubsynchost.h"

#include "syncengine.h"
#include "mockbackend.h"
#include "calendardomainadapter.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QtTest>

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

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
    // Fixtures populated in init(); torn down in cleanup().
    // Pointers only — actual construction lands in Group 2 Task 22
    // once the QFuture-based runSync is in place. The stub
    // constructors take real arguments (BackendRegistry*, calendar
    // id, etc.) that only become meaningful alongside the engine
    // wiring; until then the test methods QSKIP before touching
    // any of these.
    SyncEngine *m_engine = nullptr;
    StubSyncHost *m_host = nullptr;
    StubSyncConfigStore *m_configStore = nullptr;
    StubCalendarCollection *m_collection = nullptr;
    StubIncidenceRegistry *m_registry = nullptr;
};

void TstEngineCancellation::initTestCase() {}
void TstEngineCancellation::cleanupTestCase() {}

void TstEngineCancellation::init()
{
    // No-op for now. Group 2 Task 22 fills this in with the
    // BackendRegistry + StubSyncHost + SyncEngine wiring once
    // the QFuture-based runSync is available.
}

void TstEngineCancellation::cleanup()
{
    delete m_engine; m_engine = nullptr;
    delete m_registry; m_registry = nullptr;
    delete m_collection; m_collection = nullptr;
    delete m_configStore; m_configStore = nullptr;
    delete m_host; m_host = nullptr;
}

void TstEngineCancellation::cancelBeforeStart()
{
    QSKIP("Stub. Implemented in Group 2 Task 23 once QFuture-based "
          "runSync + QFutureWatcher cancellation observation are in place.");
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
