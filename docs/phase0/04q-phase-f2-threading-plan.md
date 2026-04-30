# Phase F2 — Threading API redesign — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land Phase F2 of the engine-merger refactor —
`QFuture`-based public API on `SyncEngine`, cancellation
propagation, operation-handle standardisation on `SyncBackend`,
and consumer migration in PlanStan + WildPalms — on tag
`v0.14-phase-f2-threading`. Design lives in
`04q-phase-f2-threading-design.md` (sibling).

**Architecture:** Caller-thread `SyncEngine` posts work to a
dedicated `SyncEngineWorker` `QObject` on a private `QThread`;
public `runSync(...)` returns a `QFuture<SyncResult>` (or
`<QList<SyncResult>>`); cancellation via `QFuture::cancel()`
propagates through a `QFutureWatcher` to a worker-side atomic
flag observed at every operation-handle boundary. `SyncBackend`'s
synchronous overloads (`loadItems` / `storeItems` / `updateItem`
+ `writeFinished` signal) are deleted; the existing async surface
(`fetchItems` / `pushItems` / `deleteItems` returning `*Operation*`
handles) becomes the only path, with `pushItems` gaining a
`TranscodingPlan` parameter so updates and creates can flow
through the same shape.

**Tech Stack:** C++20, Qt 6, KCalendarCore, CMake, QTest. Three
worktrees: libkalburator (target), PlanStan (consumer #1),
WildPalms (consumer #2). All on branch `refactor/engine-merger`.

**Worktree paths:**
- libkalburator: `~/dev/refactor-engine-merger/libkalburator/`
- PlanStan: `~/dev/refactor-engine-merger/PlanStan/`
- WildPalms: `~/dev/refactor-engine-merger/WildPalms/`

**Build dirs:**
- libkalburator: `build/` (legacy, no presets)
- PlanStan: `build-dev/` (uses `cmake --preset dev`)
- WildPalms: `build/` (legacy, with `-DWILDPALMS_*_PLUGIN_V2=ON` flags)

**Canonical verification:** `~/dev/refactor-engine-merger/scripts/verify-all.sh`
must exit 0 at every group boundary. It builds + tests all three
worktrees and compares against the baselines under
`~/dev/refactor-engine-merger/baselines/`.

**Per-commit policy:** Commits to `refactor/engine-merger` do not
require per-commit user authorisation (per the project root
`CLAUDE.md`). Tags do require user authorisation; `git push`,
`git reset --hard`, `git branch -D`, and any operation against the
pristine `~/dev/{libkalburator,PlanStan,WildPalms}` checkouts also
do.

---

## Group 0 — Prep

### Task 1: Add `cancelled` and `skipped` fields to `SyncResult`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/types/synctypes.h:151-178`

**Background:** `SyncResult` is the `Kalburator::Sync` value type
returned by `SyncEngine::runSync(...)`. F2 adds two new state
booleans so cancellation and skipped-queue-slot are distinguishable
from success/failure.

A second `SyncResult` lives in the `QSyncCore` namespace at
`src/conflict/synccommon.h:98` — that one is unrelated (different
namespace, used by the conflict subsystem). Do NOT modify it.

- [ ] **Step 1: Audit `SyncResult` callers across the three repos for zero-value invariants**

```bash
cd ~/dev/refactor-engine-merger/
git -C libkalburator grep -n "SyncResult\b" -- 'src/' 'tests/' | grep -v "BlobSyncResult\|QSyncCore::SyncResult"
git -C PlanStan grep -n "Kalburator::Sync::SyncResult\b" -- 'src/' 'tests/'
git -C WildPalms grep -n "Kalburator::Sync::SyncResult\b" -- 'src/' 'tests/'
```

Expected: a hit list of every place that constructs, returns, or
destructures `SyncResult`. Spot-check ~20 hits for any predicate
of the form "if `errorMessage.isEmpty()` then succeeded" or
similar zero-value-implies-success assumption. Note any findings
in scratch — they'll inform field-doc text.

- [ ] **Step 2: Add the new fields with documentation**

Modify `~/dev/refactor-engine-merger/libkalburator/src/types/synctypes.h`
inside the `struct SyncResult` body (around line 151–178). The
existing struct is:

```cpp
struct SyncResult {
    bool success = true;
    QString errorMessage;
    QDateTime startTime;
    QDateTime endTime;
    SyncStats sourceStats;
    SyncStats targetStats;
    QStringList warnings;
    QList<ConflictInfo> unresolvedConflicts;

    qint64 durationMs() const { return startTime.msecsTo(endTime); }
    bool hasWarnings() const { return !warnings.isEmpty(); }
    bool hasUnresolvedConflicts() const { return !unresolvedConflicts.isEmpty(); }
};
```

Add two booleans before the methods:

```cpp
struct SyncResult {
    bool success = true;
    QString errorMessage;
    QDateTime startTime;
    QDateTime endTime;
    SyncStats sourceStats;
    SyncStats targetStats;
    QStringList warnings;
    QList<ConflictInfo> unresolvedConflicts;

    /// True iff QFuture::cancel() was observed during this run.
    /// Distinct from success: success is "ran to completion without
    /// errors"; cancelled is "ran for a while then stopped on
    /// caller request". A cancelled SyncResult typically has
    /// errorMessage empty; partial sourceStats/targetStats reflect
    /// work done before cancellation took effect.
    bool cancelled = false;

    /// True iff this slot in a multi-mapping queue never started
    /// (e.g. cancellation arrived after mapping 2 of 5 finished;
    /// mappings 3-5 land here with skipped=true). Mutually
    /// exclusive with success=true.
    bool skipped = false;

    qint64 durationMs() const { return startTime.msecsTo(endTime); }
    bool hasWarnings() const { return !warnings.isEmpty(); }
    bool hasUnresolvedConflicts() const { return !unresolvedConflicts.isEmpty(); }
};
```

- [ ] **Step 3: Build to confirm the addition compiles**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build. Existing aggregate-init call sites (if any)
that take exactly the listed fields will compile because
designated initialisers are not used and the new fields default.

- [ ] **Step 4: Run the existing libkalburator tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 23/23 pass (unchanged from F1 baseline).

- [ ] **Step 5: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/types/synctypes.h
git commit -m "$(cat <<'EOF'
feat(types): add cancelled/skipped fields to SyncResult (F2 Task 1)

Phase F2 prep. The QFuture-based SyncEngine API needs to
distinguish "ran to completion" from "QFuture::cancel() observed"
from "queue slot never started". Two new booleans on SyncResult:
- cancelled: caller-initiated cancellation observed during the run
- skipped: this multi-mapping queue slot never started

Existing predicates (success / errorMessage / hasWarnings /
hasUnresolvedConflicts) unchanged; field defaults preserve all
existing zero-value invariants.

Refs: 04q-phase-f2-threading-design.md "SyncResult — error model"

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Stub `tst_engine_cancellation.cpp` with skipped scaffolding

**Files:**
- Create: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_engine_cancellation.cpp`
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/CMakeLists.txt` (register new test)

**Background:** The cancellation test suite is the load-bearing
TDD work in F2. Stubbing all seven cases now (each as `QSKIP` with
a TODO referencing the enabling task) locks the contract before
implementation drifts. As infrastructure lands in Group 2, each
case is unskipped task-by-task.

- [ ] **Step 1: Write the stub test file**

Create `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_engine_cancellation.cpp`:

```cpp
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Phase F2 — cancellation contract for SyncEngine's QFuture-based
// public API. Stubs are unskipped as the enabling infrastructure
// lands; see 04q-phase-f2-threading-plan.md.

#include "stubs/stubcalendarcollection.h"
#include "stubs/stubincidenceregistry.h"
#include "stubs/stubsyncconfigstore.h"
#include "stubs/stubsynchost.h"

#include "engine/syncengine.h"
#include "calendar/mockbackend.h"
#include "calendar/calendardomainadapter.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QSignalSpy>
#include <QtTest>

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
    Kalburator::Sync::SyncEngine *m_engine = nullptr;
    StubSyncHost *m_host = nullptr;
    StubSyncConfigStore *m_configStore = nullptr;
    StubCalendarCollection *m_collection = nullptr;
    StubIncidenceRegistry *m_registry = nullptr;
};

void TstEngineCancellation::initTestCase() {}
void TstEngineCancellation::cleanupTestCase() {}

void TstEngineCancellation::init()
{
    // Fixture construction lands in Group 2 Task 22 once the
    // QFuture-based runSync is in place. For now, just allocate
    // the host bits so the file compiles.
    m_host = new StubSyncHost(this);
    m_configStore = new StubSyncConfigStore(this);
    m_collection = new StubCalendarCollection(this);
    m_registry = new StubIncidenceRegistry(this);
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
```

- [ ] **Step 2: Register the new test in CMakeLists**

Modify `~/dev/refactor-engine-merger/libkalburator/tests/calendar/CMakeLists.txt`.
Inspect the file first to find the existing pattern — it
defines a helper `kalburator_add_calendar_integration_test(name)`.
Append a new line near the existing integration tests:

```cmake
kalburator_add_calendar_integration_test(tst_engine_cancellation)
```

If the helper isn't appropriate (the cancellation test doesn't use
the full integration harness), use `kalburator_add_calendar_test()`
instead. If neither exists, mimic the pattern of the most-recent
integration test (`tst_engine_unified_boundary` should be a good
template — it was added in F1).

- [ ] **Step 3: Build to confirm the file compiles**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc) --target tst_engine_cancellation
```

Expected: clean build. The test executable is produced.

- [ ] **Step 4: Run the test to confirm all cases skip cleanly**

```bash
ctest --test-dir build --output-on-failure -R tst_engine_cancellation
```

Expected: PASS. Output shows 11 skipped cases (7 cancellation +
4 positive smoke), 0 failures.

Run the full ctest to confirm overall count rose from 23 to 24:

```bash
ctest --test-dir build --output-on-failure
```

Expected: 24/24 pass (one new entry: `tst_engine_cancellation`).

- [ ] **Step 5: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add tests/calendar/tst_engine_cancellation.cpp tests/calendar/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(engine): scaffold tst_engine_cancellation with skipped cases (F2 Task 2)

Stubs the seven cancellation contract cases (C1-C7) plus four
positive QFuture smoke tests. Each case QSKIPs with a reference
to the Group 2 task that will unskip it.

This locks the contract before implementation drifts. The test
executable joins the libkalburator suite immediately (24/24); the
skip messages will become real assertions as Group 2 lands.

Refs: 04q-phase-f2-threading-design.md "Test plan" + "Cancellation
propagation" sections.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Pre-flight verify-all + baseline confirmation

**Files:** none (verification only)

- [ ] **Step 1: Run verify-all on the F2 starting point**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
echo "exit code: $?"
```

Expected: exit 0. Output should be:
- libkalburator: 24/24 pass (the F1 baseline plus the new
  `tst_engine_cancellation` from Task 2 with all cases skipping).
- PlanStan: 96/120 pass (matches Phase E baseline).
- WildPalms: 73/73 pass (modulo two known order-dependent flakes).

- [ ] **Step 2: If WildPalms reports the known flakes
  (`tst_pluckerbackendplugin` or `tst_calendar_v2`), re-run once**

```bash
./scripts/verify-all.sh
```

Expected: exit 0 on second run. The flakes are documented in
FINDINGS as pre-existing, unrelated destructor-order issues that
bite intermittently in full-suite runs.

- [ ] **Step 3: If verify-all is not green after two runs, STOP and report the failure**

This is a critical gate. F2 must start from a known-green state.
If verify-all is red, the failure is either:
- A new flake, in which case we need to investigate before proceeding.
- A regression from the Group 0 prep tasks, in which case we need
  to investigate before proceeding.

Either way: do not start Group 1 against a red baseline.

- [ ] **Step 4: Note the baseline state**

Confirm the libkalburator baseline file shows 24 entries:

```bash
wc -l ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

If it shows 23, refresh it now (Task 2's new test should be
recorded as part of the new baseline):

```bash
cd ~/dev/refactor-engine-merger
./scripts/refresh-baseline.sh libkalburator
git add baselines/libkalburator-worktree-ctest.txt
git commit -m "$(cat <<'EOF'
chore(baselines): refresh libkalburator baseline post-F2 Task 2

tst_engine_cancellation joined the suite (all cases currently
skipping). Refreshing the baseline so Group 1's verify-all calls
have the right reference point.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

If `refresh-baseline.sh` doesn't exist, regenerate manually:

```bash
cd ~/dev/refactor-engine-merger/libkalburator
ctest --test-dir build -N | sed -n 's/^  Test #.*: //p' \
    > ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

- [ ] **Step 5: Commit any baseline refresh from Step 4 if not already**

(Already done in Step 4 if needed.)

---

## Group 1 — Operation contract + TranscodingPlan ramp

### Task 4: Standardise `SyncOperation` State enum and contract

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncoperation.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncoperation.cpp`

**Background:** `SyncOperation` already exists with `Q_OBJECT`,
`started`/`progress`/`finished` signals, and three concrete
subclasses (`FetchOperation`, `PushOperation`, `DeleteOperation`).
F2 locks down a uniform contract: a `State` enum, idempotent
`setState`/`setError`, a virtual `cancel()` with default flag-set
behaviour, an atomic `cancelRequested()` accessor.

The skill is inspection + fill-in-gaps, not greenfield design.
First, read the existing class to map what's there vs. what F2
needs.

- [ ] **Step 1: Inspect the existing `SyncOperation` shape**

```bash
sed -n '1,170p' ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncoperation.h
sed -n '1,120p' ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncoperation.cpp
```

Note in scratch:
- Does it have a `State` enum? With which values?
- Does `setState`/`setError` exist? Are they idempotent?
- Is `cancel()` virtual? What's the default body?
- Does `cancelRequested()` exist?
- Are `m_state`/`m_cancelRequested` `std::atomic`?

The standardised contract from `04q-phase-f2-threading-design.md`:

```cpp
class SyncOperation : public QObject
{
    Q_OBJECT
public:
    enum State { Pending, Running, Succeeded, Failed, Cancelled };

    State state() const noexcept;
    QString errorString() const;
    bool isFinished() const noexcept;  // Succeeded|Failed|Cancelled

    virtual void cancel();              // idempotent; default sets atomic flag

signals:
    void started();
    void progress(int current, int total);
    void finished();   // exactly once, regardless of terminal state

protected:
    void setState(State);              // emits started/finished as appropriate
    void setError(const QString&);     // implies setState(Failed)
    bool cancelRequested() const noexcept;

private:
    std::atomic<State> m_state{Pending};
    std::atomic<bool>  m_cancelRequested{false};
    QString            m_errorString;
};
```

- [ ] **Step 2: Write a contract test for `SyncOperation`**

Create or extend a new test file at
`~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_syncoperation_contract.cpp`
(if no such file exists). The test exercises the contract:

```cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "calendar/syncoperation.h"

#include <QObject>
#include <QSignalSpy>
#include <QtTest>

using Kalburator::Sync::SyncOperation;

class TstSyncOperationContract : public QObject
{
    Q_OBJECT

private slots:
    void initialState();
    void setStateRunningEmitsStarted();
    void setStateSucceededEmitsFinishedOnce();
    void setStateFailedEmitsFinishedOnce();
    void setErrorImpliesFailed();
    void cancelSetsCancelRequested();
    void cancelIsIdempotent();
    void isFinishedReflectsTerminalStates();
};

namespace {

class FakeOp : public SyncOperation
{
    Q_OBJECT
public:
    using SyncOperation::SyncOperation;
    using SyncOperation::setState;        // expose for test
    using SyncOperation::setError;        // expose for test
    using SyncOperation::cancelRequested; // expose for test
};

} // namespace

void TstSyncOperationContract::initialState()
{
    FakeOp op;
    QCOMPARE(op.state(), SyncOperation::Pending);
    QVERIFY(!op.isFinished());
    QVERIFY(op.errorString().isEmpty());
}

void TstSyncOperationContract::setStateRunningEmitsStarted()
{
    FakeOp op;
    QSignalSpy started(&op, &SyncOperation::started);
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setState(SyncOperation::Running);
    QCOMPARE(started.count(), 1);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(op.state(), SyncOperation::Running);
    QVERIFY(!op.isFinished());
}

void TstSyncOperationContract::setStateSucceededEmitsFinishedOnce()
{
    FakeOp op;
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setState(SyncOperation::Running);
    op.setState(SyncOperation::Succeeded);
    QCOMPARE(finished.count(), 1);
    op.setState(SyncOperation::Succeeded);  // idempotent: still 1
    QCOMPARE(finished.count(), 1);
    QVERIFY(op.isFinished());
}

void TstSyncOperationContract::setStateFailedEmitsFinishedOnce()
{
    FakeOp op;
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setState(SyncOperation::Running);
    op.setState(SyncOperation::Failed);
    QCOMPARE(finished.count(), 1);
    QVERIFY(op.isFinished());
}

void TstSyncOperationContract::setErrorImpliesFailed()
{
    FakeOp op;
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setError(QStringLiteral("boom"));
    QCOMPARE(op.state(), SyncOperation::Failed);
    QCOMPARE(op.errorString(), QStringLiteral("boom"));
    QCOMPARE(finished.count(), 1);
}

void TstSyncOperationContract::cancelSetsCancelRequested()
{
    FakeOp op;
    QVERIFY(!op.cancelRequested());
    op.cancel();
    QVERIFY(op.cancelRequested());
}

void TstSyncOperationContract::cancelIsIdempotent()
{
    FakeOp op;
    op.cancel();
    op.cancel();  // no-op
    QVERIFY(op.cancelRequested());
}

void TstSyncOperationContract::isFinishedReflectsTerminalStates()
{
    {
        FakeOp op;
        op.setState(SyncOperation::Succeeded);
        QVERIFY(op.isFinished());
    }
    {
        FakeOp op;
        op.setState(SyncOperation::Failed);
        QVERIFY(op.isFinished());
    }
    {
        FakeOp op;
        op.setState(SyncOperation::Cancelled);
        QVERIFY(op.isFinished());
    }
    {
        FakeOp op;
        op.setState(SyncOperation::Running);
        QVERIFY(!op.isFinished());
    }
}

QTEST_MAIN(TstSyncOperationContract)
#include "tst_syncoperation_contract.moc"
```

Register the test in `tests/calendar/CMakeLists.txt` using the
unit-test helper (probably `kalburator_add_calendar_test`, not
the integration helper).

- [ ] **Step 3: Run the test to see what fails**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc) --target tst_syncoperation_contract
ctest --test-dir build --output-on-failure -R tst_syncoperation_contract
```

Expected: some subset fails. The exact failures depend on Step 1's
findings — likely "no `Pending`/`Cancelled` enum values", "no
`isFinished()`", "non-idempotent `setState`", "no
`cancelRequested()`", "`cancel()` not virtual or wrong default".

- [ ] **Step 4: Update `SyncOperation` to satisfy the contract**

Modify `src/calendar/syncoperation.h` and `.cpp` to bring the
class to the standardised contract above. Specific changes,
each conditional on the Step 1 audit:

1. Add `Pending` and `Cancelled` to the `State` enum if missing.
2. Make `m_state` an `std::atomic<State>` if it isn't.
3. Add `m_cancelRequested` as `std::atomic<bool>` if it doesn't
   exist.
4. Make `cancel()` virtual; default body just stores
   `m_cancelRequested = true`. Don't re-emit anything; the
   subclass's `run()` body or the awaiter notices the flag and
   transitions state to `Cancelled` via `setState`.
5. Make `setState(State)` idempotent: if already in a terminal
   state and the new state is also terminal, no-op (don't
   re-emit `finished`). If transitioning from non-terminal to
   `Running`, emit `started`. If transitioning from non-terminal
   to a terminal state, emit `finished`.
6. Make `setError(const QString&)` set `m_errorString` then call
   `setState(Failed)`.
7. Add `bool cancelRequested() const noexcept` returning the
   atomic flag.
8. Add `bool isFinished() const noexcept` returning
   `state() == Succeeded || state() == Failed || state() == Cancelled`.

Code sketch for the .cpp pieces (adjust to existing structure):

```cpp
void SyncOperation::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

void SyncOperation::setState(State newState)
{
    State expected = m_state.load(std::memory_order_acquire);
    while (true) {
        const bool wasTerminal =
            expected == Succeeded || expected == Failed || expected == Cancelled;
        const bool willBeTerminal =
            newState == Succeeded || newState == Failed || newState == Cancelled;
        if (wasTerminal && willBeTerminal) {
            // Idempotent: terminal-to-terminal is a no-op.
            return;
        }
        if (m_state.compare_exchange_weak(expected, newState,
                                          std::memory_order_release,
                                          std::memory_order_acquire)) {
            if (expected == Pending && newState == Running) {
                emit started();
            }
            if (!wasTerminal && willBeTerminal) {
                emit finished();
            }
            return;
        }
        // expected was reloaded; retry
    }
}

void SyncOperation::setError(const QString &message)
{
    m_errorString = message;
    setState(Failed);
}

bool SyncOperation::cancelRequested() const noexcept
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

bool SyncOperation::isFinished() const noexcept
{
    const State s = m_state.load(std::memory_order_acquire);
    return s == Succeeded || s == Failed || s == Cancelled;
}
```

Header changes mirror these (declare them; expose
`m_state`/`m_cancelRequested` as atomic privates).

- [ ] **Step 5: Run the contract test to confirm it passes**

```bash
cmake --build build -j$(nproc) --target tst_syncoperation_contract
ctest --test-dir build --output-on-failure -R tst_syncoperation_contract
```

Expected: PASS, all 8 cases.

Run the full ctest to confirm no regression in the other tests:

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass (the F1 23 + tst_engine_cancellation + the
new tst_syncoperation_contract).

- [ ] **Step 6: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/calendar/syncoperation.h src/calendar/syncoperation.cpp \
    tests/calendar/tst_syncoperation_contract.cpp \
    tests/calendar/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(operation): standardise SyncOperation contract (F2 Task 4)

Phase F2 prep. Locks down the SyncOperation contract that
SyncBackend's async I/O methods (fetchItems / pushItems /
deleteItems) return:
- State enum: Pending / Running / Succeeded / Failed / Cancelled
- setState() idempotent on terminal-to-terminal transitions
- setError() implies setState(Failed)
- virtual cancel(); default sets atomic m_cancelRequested
- cancelRequested() accessor for run() bodies and the await<> helper
- isFinished() == terminal state

The class already existed; this fills in gaps and tightens
semantics. New unit test tst_syncoperation_contract pins the
contract.

Refs: 04q-phase-f2-threading-design.md "SyncOperation base"

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Add `TranscodingPlan` parameter to `SyncBackend::pushItems` (base + virtual signature)

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.h:208-209` (the `pushItems` declaration)
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.cpp` (default impl if any)

**Background:** Phase E added `storeItems(MemoryCalendar*, ...,
TranscodingPlan)` and `updateItem(MemoryCalendar*, item, ...,
TranscodingPlan)` as synchronous methods because the existing async
`pushItems(QString, items)` did not accept a plan parameter. F2's
load-bearing change on the backend contract is to add that parameter
to `pushItems` so creates and updates can flow through the same
async shape, then retire `storeItems`/`updateItem`.

This task changes only the base-class signature. Concrete backends
get the new param in Tasks 7-13.

- [ ] **Step 1: Inspect the existing pushItems signature**

```bash
grep -n "pushItems" ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.h
grep -n "pushItems" ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.cpp
```

Confirm the current shape:

```cpp
virtual PushOperation* pushItems(const QString &calendarId,
                                 const QList<KCalendarCore::Incidence::Ptr> &items);
```

- [ ] **Step 2: Add the TranscodingPlan parameter with default**

Modify the declaration (around line 208):

```cpp
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan = TranscodingPlan{});
```

The default `= TranscodingPlan{}` is load-bearing — per FINDINGS
"Virtual function default arguments must be redeclared on
overrides for concrete-type callers", every concrete override
must also redeclare the default for callers that hold concrete
backend pointers (PlanStan tests do this).

Ensure `TranscodingPlan` is included; check the existing
includes in `syncbackend.h`. It's defined in
`src/transcoding/transcodingplan.h`; if not already included,
add:

```cpp
#include "transcoding/transcodingplan.h"
```

If `syncbackend.cpp` has a default implementation of `pushItems`
(it shouldn't if it's pure virtual; non-pure default can also
exist), update its definition to match.

- [ ] **Step 3: Build to expose all the breakages**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc) 2>&1 | tee /tmp/f2-task5-build.log
```

Expected: many compile errors — every concrete subclass of
`SyncBackend` that overrides `pushItems` now has a signature
mismatch. List of files that will need updating in Tasks 7-13:

```bash
grep -l "PushOperation\* pushItems\|PushOperation \*pushItems" \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/*.h
```

Note this list — it's the work for the next several tasks.

- [ ] **Step 4: Add a temporary delegating implementation in the base
  to keep the build green during the ramp**

Rather than fixing every concrete subclass right now, add a
non-virtual fallback that delegates to the old signature. This
keeps the build green so each subclass can be migrated one at a
time. Modify `src/calendar/syncbackend.h`:

```cpp
// Make the new signature non-pure with a default implementation
// that delegates to the old one. Concrete subclasses gradually
// override the new signature; the old one will be removed when
// all callers have migrated.
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan = TranscodingPlan{});

// OLD: kept temporarily for transition. Default implementation
// in syncbackend.cpp delegates to the new signature with empty
// plan. Subclasses can override either one; will be removed
// in F2 Task ~38 once all callers migrate.
[[deprecated("Override pushItems(id, items, plan) instead — "
             "old signature kept for migration only")]]
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items);
```

Wait — that creates two virtual functions with overlap that's
not unambiguous. Instead, make the new signature delegate to
the old one in the base by default, and let subclasses override
either:

```cpp
// New signature: base default delegates to the old form,
// ignoring the plan. Subclasses override THIS method to honour
// the plan.
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan);

// Convenience overload with default plan.
PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    return pushItems(calendarId, items, TranscodingPlan{});
}
```

In `syncbackend.cpp`, the new virtual gets a default body that
ignores the plan:

```cpp
PushOperation* SyncBackend::pushItems(const QString &calendarId,
                                      const QList<KCalendarCore::Incidence::Ptr> &items,
                                      const TranscodingPlan &plan)
{
    Q_UNUSED(plan);
    // Default: ignore the plan and delegate to the old behaviour
    // by routing through the existing concrete subclass override.
    // BUT this base implementation is the bottom — there's no
    // "older" override to route to. Subclasses MUST override this
    // method post-migration. For now we return nullptr to fail
    // loudly; this path should never execute once F2 Task 13 has
    // migrated every subclass.
    qWarning("SyncBackend::pushItems base default invoked — "
             "subclass missing override (F2 migration incomplete)");
    return nullptr;
}
```

Actually, simplest: do not add a delegating fallback at the base
level. Just leave the old `pushItems(id, items)` overload visible
as a *non-virtual* convenience inline that calls the new virtual
with empty plan, and require every subclass to override the new
3-arg virtual. Tasks 7-13 will add the override on each concrete
class.

Final shape:

```cpp
// In syncbackend.h (replace the existing virtual):

/**
 * @brief Push items to a calendar.
 *
 * @param calendarId The calendar ID to push to
 * @param items The incidences to push
 * @param plan Transcoding plan (default: empty plan, no transcoding)
 * @return PushOperation* tracking the operation (caller owns)
 */
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan = TranscodingPlan{}) = 0;
```

This is pure virtual with a default plan parameter. Build will
fail until every subclass overrides it; that's Tasks 7-13's job.

- [ ] **Step 5: Document the breakage in a TODO commit**

Don't commit yet — Step 4 made the build red. Continue to Task 6.

Actually, structure differently: this task ONLY changes the base
header. The build will be red after this. Do not commit.

The next several tasks (Tasks 6-13) update each concrete backend.
The commit happens at the end of Task 13 once the build is green
again, OR each task commits its individual backend in a
separately-buildable way.

The cleaner approach is: revise this task so the base class still
builds cleanly, then have Tasks 6-13 each migrate a concrete
backend and commit individually.

To make the base class buildable after Step 4 alone, do NOT make
the new signature pure virtual yet. Add it as a virtual with a
base default that delegates to the old `pushItems(id, items)` —
which is still pure virtual. That keeps existing concrete
subclasses (all of which override the 2-arg form) compiling until
they're individually migrated.

Final base-header change:

```cpp
// Pre-existing 2-arg form, still pure virtual:
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items) = 0;

// New 3-arg form: base default delegates to the 2-arg form,
// ignoring the plan. Subclasses migrate by overriding this and
// removing their 2-arg override; F2 Task 38 finally drops the
// 2-arg form entirely.
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan)
{
    Q_UNUSED(plan);
    return pushItems(calendarId, items);
}

// Convenience: default-plan form (non-virtual).
PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    /* explicit empty plan */)
{
    return pushItems(calendarId, items, TranscodingPlan{});
}
```

Wait — the convenience form has the same signature as the 2-arg
form (which is virtual), so it would shadow it. Let me simplify:

The cleanest dual-signature shape:

```cpp
// 2-arg form: still pure virtual until Task 38.
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items) = 0;

// 3-arg form: virtual, base default delegates to 2-arg form.
// Subclasses gradually override this and stop using 2-arg.
virtual PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan)
{
    Q_UNUSED(plan);
    return pushItems(calendarId, items);
}
```

That's two distinct overloads. C++ resolves by argument count.
The base provides a default for the 3-arg form. Existing concrete
subclasses still satisfy the 2-arg pure-virtual; the 3-arg base
default routes through them. Tasks 7-13 each add a 3-arg override
to a specific concrete backend.

- [ ] **Step 6: Build to confirm the base header compiles**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build. Existing concrete subclasses still satisfy
the 2-arg pure virtual; the 3-arg form has a base default.

- [ ] **Step 7: Run tests to confirm no regression**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. Behaviour unchanged because the engine and
all callers still call the 2-arg form; the 3-arg form has zero
callers yet.

- [ ] **Step 8: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/calendar/syncbackend.h src/calendar/syncbackend.cpp
git commit -m "$(cat <<'EOF'
feat(syncbackend): add 3-arg pushItems with TranscodingPlan (F2 Task 5)

Adds a 3-arg overload virtual PushOperation* pushItems(id, items,
plan) alongside the existing 2-arg form. The base default delegates
to the 2-arg form, ignoring the plan, so existing concrete
subclasses continue to compile and behave identically.

Tasks 6-13 migrate each concrete backend to override the 3-arg
form and honour the plan; Task 38 finally drops the 2-arg form
entirely once all callers have migrated.

This sets up the F2 retirement of the synchronous storeItems /
updateItem path that Phase E added specifically to thread a plan
parameter the async pushItems didn't yet accept.

Refs: 04q-phase-f2-threading-design.md "SyncBackend's I/O surface
becomes operation-only" + "Group 1 - Operation contract"

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Migrate `MockBackend` to 3-arg `pushItems` + collapse failure-injection asymmetry

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/mockbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/mockbackend.cpp`
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_mockbackend.cpp` (or wherever MockBackend tests live — grep first)

**Background:** `MockBackend` is the load-bearing test fixture
for the engine apply path. It also has the FINDINGS-noted
asymmetry where `OnPush` and `OnStoreItems` are checked
differently across the synchronous and async write paths. F2
collapses these onto one path: a single `pushItems(id, items,
plan)` that honours the unified failure injection.

`MockBackend` is migrated first because all subsequent backend
migrations should match its pattern.

- [ ] **Step 1: Locate the MockBackend test file**

```bash
find ~/dev/refactor-engine-merger/libkalburator/tests -name "tst_mockbackend*"
grep -rn "MockBackend\|class MockBackend" \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/mockbackend.h
```

The test file is likely `tests/calendar/tst_mockbackend.cpp` or
inside the integration suite. Note the path.

- [ ] **Step 2: Inspect MockBackend's existing pushItems + failure-injection state**

```bash
grep -n "pushItems\|storeItems\|updateItem\|FailurePoint\|setFailurePoint\|OnPush\|OnStoreItems" \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/mockbackend.h \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/mockbackend.cpp
```

Note in scratch:
- The current 2-arg pushItems body
- The setFailurePoint API
- Where OnPush vs OnStoreItems are checked (the asymmetry)

Per FINDINGS, `MockBackend::storeItems` checks
`OnPush || OnStoreItems` but `MockBackend::pushItems` (the async
form) likely checks only `OnPush`. Confirm.

- [ ] **Step 3: Write a contract test for unified failure injection**

Add a new test method to `tst_mockbackend.cpp` (or the integration
test, depending on what Step 1 found):

```cpp
// New test: MockBackend's 3-arg pushItems honours both OnPush
// AND OnStoreItems failure points. This is the F2 unified
// behaviour; pre-F2 there was an asymmetry.
void TstMockBackend::pushItemsHonoursOnPushFailureInjection()
{
    Kalburator::Sync::MockBackend mock;
    mock.setFailurePoint(MockBackend::FailurePoint::OnPush,
                         MockBackend::FailureMode::SignalError);
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(KCalendarCore::Incidence::Ptr(new KCalendarCore::Event));
    auto *op = mock.pushItems(QStringLiteral("cal1"), items,
                              Kalburator::Sync::TranscodingPlan{});
    QVERIFY(op);
    QSignalSpy finished(op, &Kalburator::Sync::SyncOperation::finished);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 1000);
    QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
    QVERIFY(!op->errorString().isEmpty());
    op->deleteLater();
}

void TstMockBackend::pushItemsHonoursOnStoreItemsFailureInjection()
{
    Kalburator::Sync::MockBackend mock;
    mock.setFailurePoint(MockBackend::FailurePoint::OnStoreItems,
                         MockBackend::FailureMode::SignalError);
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(KCalendarCore::Incidence::Ptr(new KCalendarCore::Event));
    auto *op = mock.pushItems(QStringLiteral("cal1"), items,
                              Kalburator::Sync::TranscodingPlan{});
    QVERIFY(op);
    QSignalSpy finished(op, &Kalburator::Sync::SyncOperation::finished);
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 1000);
    QCOMPARE(op->state(), Kalburator::Sync::SyncOperation::Failed);
    QVERIFY(!op->errorString().isEmpty());
    op->deleteLater();
}
```

Add the slot declarations to the test class.

- [ ] **Step 4: Run to confirm at least one fails**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc) --target tst_mockbackend
ctest --test-dir build --output-on-failure -R tst_mockbackend
```

Expected: One or both new cases fail (the OnStoreItems case
likely fails because the asymmetry means OnStoreItems isn't
checked in pushItems). The build itself may also fail if the
3-arg pushItems isn't yet added to MockBackend — that's fine,
fix in Step 5.

- [ ] **Step 5: Migrate MockBackend's pushItems**

In `src/calendar/mockbackend.h`, override the 3-arg form:

```cpp
PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan = TranscodingPlan{}) override;

// OLD 2-arg form: keep for now (delegate to 3-arg). Removed
// in F2 Task 38.
PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items) override;
```

In `src/calendar/mockbackend.cpp`:

```cpp
PushOperation* MockBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan)
{
    // Honour the transcoding plan if non-empty. The plan logic
    // currently lives in storeItems; for MockBackend we just
    // record the plan for inspection by tests.
    Q_UNUSED(plan);  // MockBackend doesn't transcode; it just
                     // mimics. Tests can use a separate
                     // recordedPlan() accessor if needed (add
                     // only when a test demands it — YAGNI for now).

    auto *op = new PushOperation(calendarId, items);
    op->setState(SyncOperation::Running);

    // Unified failure injection: honour OnPush || OnStoreItems.
    if (m_failurePoint == FailurePoint::OnPush ||
        m_failurePoint == FailurePoint::OnStoreItems) {
        applyFailureMode(op);  // existing helper that drives
                               // m_failureMode
        return op;
    }

    // Mock success path: store items, then transition Succeeded.
    for (const auto &item : items) {
        m_storedItems[calendarId].append(item);
    }
    op->setState(SyncOperation::Succeeded);
    return op;
}

// Old form: delegate to new form.
PushOperation* MockBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    return pushItems(calendarId, items, TranscodingPlan{});
}
```

The `applyFailureMode(SyncOperation*)` helper may already exist;
if not, extract a small helper from the existing
`storeItems`/`pushItems` failure-handling code. The exact form of
`m_failurePoint`/`m_failureMode` is whatever the existing class
uses; preserve those names.

- [ ] **Step 6: Run the contract tests to confirm they pass**

```bash
cmake --build build -j$(nproc) --target tst_mockbackend
ctest --test-dir build --output-on-failure -R tst_mockbackend
```

Expected: PASS. Both new cases (OnPush and OnStoreItems) trigger
failure as expected.

Run the full ctest:

```bash
ctest --test-dir build --output-on-failure
```

Expected: still 25/25 (or 25/25 + however many new cases). No
regressions in other tests.

- [ ] **Step 7: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/calendar/mockbackend.h src/calendar/mockbackend.cpp \
    tests/calendar/tst_mockbackend.cpp
git commit -m "$(cat <<'EOF'
feat(mockbackend): migrate to 3-arg pushItems with unified failure injection (F2 Task 6)

Phase F2. MockBackend overrides the new 3-arg pushItems(id, items,
plan); the OLD 2-arg form remains as a delegating shim. Failure
injection now honours OnPush || OnStoreItems symmetrically — per
FINDINGS "MockBackend missing failure injection on updateItem and
OnPush in storeItems", these were checked asymmetrically across
the sync and async paths pre-F2. Both checks now run in the
single pushItems path.

New tst_mockbackend cases pin the unified behaviour.

Refs: 04q-phase-f2-threading-design.md "MockBackend - failure
injection cleanup"; FINDINGS entry from 2026-04-29.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Migrate `LocalBackend` to 3-arg `pushItems`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/localbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/localbackend.cpp`

**Background:** `LocalBackend` writes to a `MemoryCalendar` on
disk. The TranscodingPlan, when non-empty, may need to coerce
properties before write (matching what `storeItems`/`updateItem`
do today). The migration extracts the existing transcoding
helper out of `storeItems` and invokes it from `pushItems`'s
3-arg form.

- [ ] **Step 1: Inspect LocalBackend's existing storeItems/updateItem to find the transcoding helper**

```bash
grep -n "storeItems\|updateItem\|pushItems\|TranscodingPlan\|coerce\|applyTranscoding" \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/localbackend.h \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/localbackend.cpp
```

Note in scratch:
- The body of `storeItems(MemoryCalendar*, items, plan)`
- The body of `updateItem(MemoryCalendar*, item, plan)`
- The shared transcoding logic that should move into a private
  helper (probably already factored — confirm).

- [ ] **Step 2: Override the 3-arg pushItems in LocalBackend**

In `src/calendar/localbackend.h`, add the override:

```cpp
PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan = TranscodingPlan{}) override;

// OLD 2-arg form: kept as delegating shim until Task 38.
PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items) override;
```

In `src/calendar/localbackend.cpp`:

```cpp
PushOperation* LocalBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan)
{
    auto *op = new PushOperation(calendarId, items);
    op->setState(SyncOperation::Running);

    // Apply the transcoding plan (extracted from the body of
    // storeItems). For each item, run any plan-driven
    // coercions before storing.
    QList<KCalendarCore::Incidence::Ptr> processed;
    processed.reserve(items.size());
    for (const auto &item : items) {
        if (op->cancelRequested()) {
            op->setState(SyncOperation::Cancelled);
            return op;
        }
        auto coerced = applyTranscodingPlan(item, plan);
        processed.append(coerced);
    }

    // Existing storage path. The MemoryCalendar lookup, the
    // sequence of addIncidence/updateIncidence calls, and the
    // disk-write logic stay unchanged from the 2-arg form's
    // body — extract into a shared private helper if not
    // already so.
    if (!writeToMemoryCalendar(calendarId, processed, op)) {
        // op state already set by writeToMemoryCalendar on
        // failure
        return op;
    }

    op->setState(SyncOperation::Succeeded);
    return op;
}

PushOperation* LocalBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    return pushItems(calendarId, items, TranscodingPlan{});
}
```

`applyTranscodingPlan(item, plan)` is the helper extracted from
the body of `storeItems`'s plan-handling code. If it doesn't
exist as a separate helper today (the body is inline in
`storeItems`), extract it now as a private method on
`LocalBackend`.

`writeToMemoryCalendar(calendarId, items, op)` is similarly the
extraction of the existing storeItems write path. The signature
takes the operation so it can `setError` on failure.

- [ ] **Step 3: Build to confirm clean compile**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build.

- [ ] **Step 4: Run all tests to confirm no regression**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. The engine still calls `storeItems` /
`updateItem` from the calendar adapter at this point; LocalBackend's
3-arg pushItems just adds capability that no current caller uses.

- [ ] **Step 5: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/calendar/localbackend.h src/calendar/localbackend.cpp
git commit -m "$(cat <<'EOF'
feat(localbackend): migrate to 3-arg pushItems with TranscodingPlan (F2 Task 7)

Phase F2. LocalBackend overrides the new 3-arg pushItems(id,
items, plan). The transcoding helper is extracted from the body
of storeItems (Phase E added it inline) into a shared private
applyTranscodingPlan helper. The old 2-arg form survives as a
delegating shim until F2 Task 38.

Refs: 04q-phase-f2-threading-design.md "Backend implementations -
concrete operation classes" + Group 1 Task 7.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Migrate `RemoteBackend` to 3-arg `pushItems`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/remotebackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/remotebackend.cpp`

**Background:** `RemoteBackend` is the CalDAV-backed concrete
backend. Its `pushItems` is genuinely async (wraps `QNetworkReply`
PUT requests). The 3-arg form adds the TranscodingPlan parameter;
the existing async machinery inside the function is unchanged.

Per the design doc gotcha, `RemoteBackend`'s `cancel()` override
on its `PushOperation` subclass needs care so an asked-for cancel
maps to `Cancelled` state, not `Failed`. That part is wired in
Group 2 Task 25 (the C2 / C3 cancellation tests cover it). For
this task, just add the plan parameter.

- [ ] **Step 1: Inspect RemoteBackend's existing pushItems**

```bash
grep -n "pushItems\|storeItems\|updateItem\|TranscodingPlan" \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/remotebackend.h \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/remotebackend.cpp
```

- [ ] **Step 2: Override the 3-arg pushItems**

Mirror the LocalBackend pattern. Header:

```cpp
PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan = TranscodingPlan{}) override;

PushOperation* pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items) override;
```

Implementation: existing pushItems body, with the plan applied
to each item before serialisation:

```cpp
PushOperation* RemoteBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items,
    const TranscodingPlan &plan)
{
    QList<KCalendarCore::Incidence::Ptr> processed;
    processed.reserve(items.size());
    for (const auto &item : items) {
        processed.append(applyTranscodingPlan(item, plan));
    }
    // Existing async PUT machinery operates on `processed`
    // instead of `items`. Reuse the existing function body's
    // logic verbatim apart from the input list.
    return pushItemsImpl(calendarId, processed);
}

PushOperation* RemoteBackend::pushItems(
    const QString &calendarId,
    const QList<KCalendarCore::Incidence::Ptr> &items)
{
    return pushItems(calendarId, items, TranscodingPlan{});
}
```

Where `pushItemsImpl(...)` is the existing 2-arg body extracted
into a private helper. If that extraction isn't natural for
RemoteBackend's structure, inline the existing body into the new
3-arg form and have the 2-arg form delegate.

`applyTranscodingPlan(item, plan)` is RemoteBackend's own version
of the helper. If it doesn't exist on RemoteBackend (because
storeItems for RemoteBackend lived elsewhere), implement it now —
the logic is whatever Phase E's storeItems for RemoteBackend
applied. If RemoteBackend never had a synchronous storeItems
(some backends don't), `applyTranscodingPlan` can be the same
shared utility from `src/transcoding/`.

- [ ] **Step 3: Build**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. Note that `tst_remotebackend` is a
PlanStan-side test (per FINDINGS — it requires
`-DPLANSTAN_ENABLE_CALDAV_TESTS=ON`); it's covered by verify-all
not by a libkalburator-only ctest.

Run verify-all to confirm cross-repo:

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/calendar/remotebackend.h src/calendar/remotebackend.cpp
git commit -m "$(cat <<'EOF'
feat(remotebackend): migrate to 3-arg pushItems with TranscodingPlan (F2 Task 8)

Phase F2. RemoteBackend overrides the new 3-arg pushItems(id,
items, plan); the existing async CalDAV PUT machinery is
unchanged apart from applying the plan to each item before
serialisation.

The PushOperation::cancel() override that maps QNetworkReply::abort()
errors to Cancelled state (not Failed) is added in Group 2 Task 25
when the cancellation tests come online.

Refs: 04q-phase-f2-threading-design.md Group 1 Task 8 + the
"Operation cancellation contract for async backends" gotcha.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Migrate `OrgBackend` to 3-arg `pushItems` (KALBURATOR_HAVE_ORG_IO)

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/orgbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/orgbackend.cpp`

**Background:** `OrgBackend` reads/writes org-mode markdown
calendars. It's gated behind `KALBURATOR_HAVE_ORG_IO=ON`; the
default build profile has this OFF. Migration follows the
LocalBackend pattern.

- [ ] **Step 1: Configure the build with OrgBackend ON to compile-test the change**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake -S . -B build-org -DKALBURATOR_HAVE_ORG_IO=ON
cmake --build build-org -j$(nproc)
```

Expected: the build-org directory builds with the OrgBackend
included. Note any errors.

- [ ] **Step 2: Override the 3-arg pushItems in OrgBackend**

Mirror the LocalBackend pattern from Task 7. Header and
implementation changes are exactly analogous; the
`applyTranscodingPlan` helper for OrgBackend is the same shape
as LocalBackend's.

- [ ] **Step 3: Build with OrgBackend ON to confirm**

```bash
cmake --build build-org -j$(nproc)
```

Expected: clean build.

- [ ] **Step 4: Build the default profile and run tests**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. The default build doesn't include
OrgBackend tests; the build-org confirmation in Step 3 is the
real check.

- [ ] **Step 5: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/calendar/orgbackend.h src/calendar/orgbackend.cpp
git commit -m "$(cat <<'EOF'
feat(orgbackend): migrate to 3-arg pushItems with TranscodingPlan (F2 Task 9)

Phase F2. OrgBackend (gated by KALBURATOR_HAVE_ORG_IO) follows
the LocalBackend pattern: 3-arg override, plan applied per item
before write, 2-arg form preserved as delegating shim.

Tested via build-org directory (configured with
KALBURATOR_HAVE_ORG_IO=ON); default build profile is unaffected.

Refs: 04q-phase-f2-threading-design.md Group 1 Task 9.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Migrate `AkonadiBackend` to 3-arg `pushItems` (KALBURATOR_HAVE_AKONADI)

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/akonadibackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/akonadibackend.cpp`

**Background:** `AkonadiBackend` reads/writes Akonadi-managed
calendars via KIO jobs. It's gated behind `KALBURATOR_HAVE_AKONADI=ON`.
Migration mirrors RemoteBackend (it's another async backend).

- [ ] **Step 1: Configure with Akonadi ON to compile-test**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake -S . -B build-akonadi -DKALBURATOR_HAVE_AKONADI=ON
cmake --build build-akonadi -j$(nproc)
```

If KF6 Akonadi packages are missing on the developer's machine,
this configure may fail. In that case, skip Steps 1-3 and rely on
verify-all in CI to catch issues. Note this in the commit message.

- [ ] **Step 2: Override the 3-arg pushItems**

Mirror RemoteBackend's pattern from Task 8 (async backend).

- [ ] **Step 3: Build with Akonadi ON to confirm (if available)**

```bash
cmake --build build-akonadi -j$(nproc)
```

- [ ] **Step 4: Default build + tests**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass.

- [ ] **Step 5: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/calendar/akonadibackend.h src/calendar/akonadibackend.cpp
git commit -m "$(cat <<'EOF'
feat(akonadibackend): migrate to 3-arg pushItems with TranscodingPlan (F2 Task 10)

Phase F2. AkonadiBackend (gated by KALBURATOR_HAVE_AKONADI)
follows the RemoteBackend pattern: 3-arg override, plan applied
per item before KIO job dispatch, 2-arg form preserved as
delegating shim.

Refs: 04q-phase-f2-threading-design.md Group 1 Task 10.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Migrate `DecSyncBackend` to 3-arg `pushItems`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/decsyncbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/decsyncbackend.cpp`

**Background:** `DecSyncBackend` is the DecSync-protocol backend.
It's async (watcher callbacks). Migration mirrors RemoteBackend.

- [ ] **Step 1: Override the 3-arg pushItems**

Mirror the RemoteBackend pattern. Identical structure: 3-arg
override applies plan, delegates to the existing async machinery;
2-arg form delegates to 3-arg.

- [ ] **Step 2: Build and run tests**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. `tst_decsyncbackend_blob_view` continues
to pass.

- [ ] **Step 3: Commit**

```bash
git add src/calendar/decsyncbackend.h src/calendar/decsyncbackend.cpp
git commit -m "$(cat <<'EOF'
feat(decsyncbackend): migrate to 3-arg pushItems with TranscodingPlan (F2 Task 11)

Phase F2. Mirrors RemoteBackend pattern.

Refs: 04q-phase-f2-threading-design.md Group 1 Task 11.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: Migrate `HolidaySubscriptionBackend` and `SubscriptionBackend` to 3-arg `pushItems`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/holidaysubscriptionbackend.{h,cpp}`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/subscriptionbackend.{h,cpp}`

**Background:** Both are read-only-ish ICS-feed backends. They
may not even support `pushItems` meaningfully (some may return a
nullptr or a Failed operation immediately). Check current
behaviour and preserve it under the 3-arg form.

- [ ] **Step 1: Inspect existing pushItems for both backends**

```bash
grep -n "pushItems" \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/holidaysubscriptionbackend.{h,cpp} \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/subscriptionbackend.{h,cpp}
```

Note whether they implement pushItems at all, and what they
return. If they're read-only and the existing 2-arg pushItems
returns a `PushOperation` set to `Failed` with "read-only
backend", the 3-arg override does the same.

- [ ] **Step 2: Override 3-arg pushItems on both backends**

For each, add the 3-arg override + 2-arg delegating shim. Body
identical to existing 2-arg behaviour (typically: return a
Failed PushOperation immediately).

- [ ] **Step 3: Build and run tests**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass.

- [ ] **Step 4: Commit**

```bash
git add src/calendar/holidaysubscriptionbackend.h \
        src/calendar/holidaysubscriptionbackend.cpp \
        src/calendar/subscriptionbackend.h \
        src/calendar/subscriptionbackend.cpp
git commit -m "$(cat <<'EOF'
feat(subscription): migrate read-only backends to 3-arg pushItems (F2 Task 12)

Phase F2. HolidaySubscriptionBackend and SubscriptionBackend gain
3-arg pushItems overrides. Both backends are read-only at the
write path; behaviour preserved (3-arg form returns a Failed
PushOperation with "read-only backend" message, mirroring the
2-arg form).

Refs: 04q-phase-f2-threading-design.md Group 1 Task 12.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 13: Group 1 verify-all gate

**Files:** none (verification only)

- [ ] **Step 1: Run cross-repo verify-all**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
echo "exit: $?"
```

Expected: exit 0.
- libkalburator: 25/25 (F1 baseline 23 + tst_engine_cancellation
  + tst_syncoperation_contract).
- PlanStan: 96/120 (matches Phase E baseline; the 24 noise items
  are unchanged).
- WildPalms: 73/73 (modulo two known flakes).

- [ ] **Step 2: If verify-all is red, fix before proceeding**

Common issues at this gate:
- A concrete backend's 3-arg override has a typo or signature
  mismatch (clang error names the line).
- A test that holds a concrete backend pointer breaks because
  the 2-arg form's behaviour subtly changed (FINDINGS "Virtual
  function default arguments must be redeclared on overrides" —
  ensure the default `= TranscodingPlan{}` is on every override
  declaration).
- WildPalms plugin sub-repo holds a `LocalBackend*` and was
  expecting the old 2-arg signature exclusively (unlikely but
  possible per FINDINGS "Removing QObject inheritance from a
  libkalburator interface ripples to consumer plugin code").

- [ ] **Step 3: Notification commit**

No code change. Just a marker that Group 1 is closed:

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git commit --allow-empty -m "$(cat <<'EOF'
chore(f2): Group 1 closed (operation contract + TranscodingPlan ramp)

verify-all green at Group 1 boundary:
- libkalburator: 25/25 pass
- PlanStan: 96/120 pass (Phase E baseline)
- WildPalms: 73/73 pass

All concrete SyncBackend subclasses now expose a 3-arg pushItems
override; the 2-arg form survives as a delegating shim until
F2 Task 38. SyncOperation contract standardised.

Group 2 (engine QFuture API + cancellation) starts next.

Refs: 04q-phase-f2-threading-plan.md Group 1.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Group 2 — Engine QFuture API + cancellation

### Task 14: Add `QFutureInterface` plumbing to `SyncEngineWorker`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.h:600-620` (worker class private section)
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp` (worker member initialisers)

**Background:** The worker needs to hold pointers to the in-flight
`QFutureInterface` for the current run, so it can call
`reportResult` / `reportFinished` / `reportCanceled` from the
worker thread. Two pointers (only one populated per run) for
single-mapping vs multi-mapping forms.

- [ ] **Step 1: Add `QFutureInterface` includes**

In `src/engine/syncengine.h`, add at the top of the includes
section:

```cpp
#include <QFutureInterface>
#include <QFuture>
```

- [ ] **Step 2: Add the worker-side member pointers**

Find the `SyncEngineWorker` class definition (around line 600 in
`syncengine.h`). Add to its private section:

```cpp
private:
    // F2: pointers to the QFutureInterface for the current run.
    // Only one is populated at a time; the unused one is nullptr.
    // The engine constructs/destroys these around runSync calls.
    QFutureInterface<SyncResult>* m_currentSingleIface = nullptr;
    QFutureInterface<QList<SyncResult>>* m_currentMultiIface = nullptr;

    // F2: cancellation observation flag, set by observeCancel()
    // slot when the engine's QFutureWatcher::canceled fires.
    std::atomic<bool> m_cancelled{false};
```

- [ ] **Step 3: Build to confirm**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build. The new members aren't used yet, so no
behaviour change.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass.

- [ ] **Step 5: Commit**

```bash
git add src/engine/syncengine.h
git commit -m "$(cat <<'EOF'
feat(engine): add QFutureInterface plumbing to SyncEngineWorker (F2 Task 14)

Phase F2 prep. The worker gains two QFutureInterface pointers
(one for single-mapping, one for multi-mapping; only one used
per run) plus a std::atomic<bool> m_cancelled flag for
cancellation observation. Members aren't used yet — Tasks 15-18
wire them in.

Refs: 04q-phase-f2-threading-design.md "SyncEngineWorker - private
companion".

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 15: Add new `runSync` overloads returning `QFuture` (delegating to existing void path)

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.h` (public section, around line 400)
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp`

**Background:** Add the new public API alongside the existing
void overloads. Initially the new overloads delegate to the void
form via a thin shim that captures completion via the existing
`syncCompleted` / `allSyncsCompleted` signals into a
`QFutureInterface`. This keeps both APIs working in parallel
during Group 2; cleanup deletes the void form in Group 4.

- [ ] **Step 1: Add the new overload declarations**

In `src/engine/syncengine.h`, in the `SyncEngine` public section
(around line 400), add the new overloads:

```cpp
public:
    // ── Existing void overloads (kept until Group 4 Task 38) ──
    void runSync(SyncBehavior behavior = SyncBehavior::Unmonitored);
    void runSync(const QString &mappingId,
                 SyncBehavior behavior = SyncBehavior::Unmonitored);

    // ── F2: new QFuture-returning overloads (Task 15) ──

    /**
     * @brief Run sync for one mapping. Future completes with the result.
     *
     * The QFuture supports cancel() to request cancellation. The
     * worker checks cancellation at every operation-handle boundary;
     * see 04q-phase-f2-threading-design.md "Cancellation propagation".
     *
     * Calling waitForFinished() on the returned future from the
     * caller thread is fine. From the worker thread (e.g. inside
     * an adapter), it would deadlock — the adapter contract
     * forbids this.
     */
    QFuture<SyncResult> runSyncFuture(
        const QString &mappingId,
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Run sync for all enabled mappings. Future completes
     *        with the per-mapping result list.
     */
    QFuture<QList<SyncResult>> runSyncFuture(
        SyncBehavior behavior = SyncBehavior::Unmonitored);
```

The temporary name `runSyncFuture` avoids overload ambiguity with
the existing `runSync(QString, SyncBehavior)` and
`runSync(SyncBehavior)` (which return void). When Group 4 Task 38
deletes the void overloads, `runSyncFuture` is renamed to
`runSync`.

- [ ] **Step 2: Implement the new overloads as shims**

In `src/engine/syncengine.cpp`:

```cpp
QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    SyncBehavior behavior)
{
    // Allocate a QFutureInterface heap-side; lifetime managed
    // by lambda capture into the completion-capture connection.
    auto *iface = new QFutureInterface<SyncResult>;
    iface->reportStarted();
    QFuture<SyncResult> future = iface->future();

    // Capture the completion signal once, then disconnect.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(this, &SyncEngine::syncCompleted, this,
        [iface, conn, mappingId](const QString &completedMappingId,
                                  const SyncResult &result) {
            if (completedMappingId != mappingId) return;
            iface->reportResult(result);
            iface->reportFinished();
            delete iface;
            QObject::disconnect(*conn);
        });

    // Trigger the existing void runSync.
    runSync(mappingId, behavior);
    return future;
}

QFuture<QList<SyncResult>> SyncEngine::runSyncFuture(
    SyncBehavior behavior)
{
    auto *iface = new QFutureInterface<QList<SyncResult>>;
    iface->reportStarted();
    QFuture<QList<SyncResult>> future = iface->future();

    // Accumulate per-mapping results, finalise on
    // allSyncsCompleted.
    auto results = std::make_shared<QList<SyncResult>>();
    auto perMappingConn = std::make_shared<QMetaObject::Connection>();
    auto allConn = std::make_shared<QMetaObject::Connection>();

    *perMappingConn = connect(this, &SyncEngine::syncCompleted, this,
        [results](const QString &, const SyncResult &result) {
            results->append(result);
        });

    *allConn = connect(this, &SyncEngine::allSyncsCompleted, this,
        [iface, results, perMappingConn, allConn](
                bool /*overallSuccess*/) {
            iface->reportResult(*results);
            iface->reportFinished();
            delete iface;
            QObject::disconnect(*perMappingConn);
            QObject::disconnect(*allConn);
        });

    runSync(behavior);
    return future;
}
```

(The connection-management pattern is fragile; it's a transitional
shim, not the final shape. Group 2 Task 18 rewrites the worker to
populate `m_currentSingleIface` / `m_currentMultiIface` directly.)

- [ ] **Step 3: Build**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. The new overloads aren't called by any
test yet.

- [ ] **Step 5: Commit**

```bash
git add src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "$(cat <<'EOF'
feat(engine): add runSyncFuture overloads as transitional shims (F2 Task 15)

Phase F2. New public API:
- QFuture<SyncResult> runSyncFuture(mappingId, behavior)
- QFuture<QList<SyncResult>> runSyncFuture(behavior)

Implementations are temporary shims that delegate to the existing
void runSync overloads and capture completion via syncCompleted /
allSyncsCompleted signals. Task 18 rewrites the worker to populate
m_currentSingleIface / m_currentMultiIface directly; Task 38
renames runSyncFuture to runSync once the void forms are deleted.

Refs: 04q-phase-f2-threading-design.md "SyncEngine - public API"
+ Group 2 Task 15.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 16: Implement `await<Op>` template helper on `SyncEngineWorker`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.h` (worker private section)

**Background:** The await helper runs an inner `QEventLoop` on
the worker thread until an operation reports finished or
cancellation is observed. It's the load-bearing primitive that
makes "every operation-handle boundary is a cancellation check"
true.

- [ ] **Step 1: Add the worker's `cancellationObserved` private signal**

In `src/engine/syncengine.h`'s `SyncEngineWorker` class:

```cpp
class SyncEngineWorker : public QObject
{
    Q_OBJECT
public:
    // ... existing public ...

signals:
    // Existing signals ...

    // F2: emitted from observeCancel() slot when the engine's
    // QFutureWatcher::canceled fires. Internal to the
    // engine/worker pair.
    void cancellationObserved();

public slots:
    // Existing slots ...

    // F2: invoked via queued connection from
    // SyncEngine::onCancelObserved when a QFutureWatcher::canceled
    // fires on the engine side.
    void observeCancel();
```

In the .cpp:

```cpp
void SyncEngineWorker::observeCancel()
{
    m_cancelled.store(true, std::memory_order_release);
    emit cancellationObserved();
}
```

- [ ] **Step 2: Add the await<Op> template helper**

In the worker class private section (header):

```cpp
private:
    /// Run an inner QEventLoop until the operation finishes OR
    /// cancellation is observed. On cancellation, request the
    /// operation's own cancel() and re-enter the loop briefly
    /// waiting for it to settle.
    ///
    /// Returns the same op pointer (caller still owns; typical
    /// idiom: `auto *op = await(backend->fetchItems(id));`
    /// then read op->state(), then op->deleteLater()).
    template <typename Op>
    Op* await(Op *op);
```

In the .cpp (template defined inline if header-only, or
explicitly instantiated):

For a template, the simplest approach is to define it in the
header below the class declaration:

```cpp
// Template definition at the bottom of syncengine.h
template <typename Op>
Op* SyncEngineWorker::await(Op *op)
{
    static_assert(
        std::is_base_of_v<SyncOperation, Op>,
        "await<Op> requires Op to derive from SyncOperation");

    if (!op) return op;
    if (op->isFinished()) return op;

    QEventLoop loop;
    QObject::connect(op, &SyncOperation::finished,
                     &loop, &QEventLoop::quit);
    QObject::connect(this, &SyncEngineWorker::cancellationObserved,
                     &loop, &QEventLoop::quit);
    loop.exec();

    if (m_cancelled.load(std::memory_order_acquire) && !op->isFinished()) {
        op->cancel();
        // Re-enter briefly waiting for the operation's own
        // teardown (operations are not pre-emptible at the
        // per-record level once started).
        if (!op->isFinished()) {
            QEventLoop teardownLoop;
            QObject::connect(op, &SyncOperation::finished,
                             &teardownLoop, &QEventLoop::quit);
            teardownLoop.exec();
        }
    }

    return op;
}
```

`#include <QEventLoop>` and `<type_traits>` at the top of the
header.

- [ ] **Step 3: Build**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. await<> isn't called yet; Tasks 18+ wire
it in.

- [ ] **Step 5: Commit**

```bash
git add src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "$(cat <<'EOF'
feat(engine): add await<Op> helper + cancellation slot on worker (F2 Task 16)

Phase F2. SyncEngineWorker gains:
- void observeCancel() slot — invoked from engine side when
  QFutureWatcher::canceled fires. Sets m_cancelled atomic and
  emits cancellationObserved signal.
- void cancellationObserved() signal — internal worker signal,
  used to wake nested QEventLoops in await<> and the conflict-
  pause loop.
- template <Op> Op* await(Op*) helper — runs inner QEventLoop
  until the operation finishes or cancellation observed; on
  cancel, calls op->cancel() and waits briefly for teardown.

The await<> helper is the load-bearing primitive that makes
"every operation-handle boundary is a cancellation check" true.
Used from Task 18 onward.

Refs: 04q-phase-f2-threading-design.md "Cancellation propagation
- the contract" + Group 2 Task 16.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 17: Wire `QFutureWatcher::canceled` on the engine side to the worker's `observeCancel`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp`

**Background:** The `QFutureWatcher` lives on the caller (engine)
thread because the future is created there. When
`QFuture::cancel()` is invoked, the watcher's `canceled` signal
fires on the engine thread; we forward to the worker's
`observeCancel` via queued connection.

- [ ] **Step 1: Add `QFutureWatcher` members to `SyncEngine`**

In `src/engine/syncengine.h`'s `SyncEngine` class private section:

```cpp
private:
    // F2: watchers tracking the in-flight QFuture from
    // runSyncFuture. Only one is populated at a time (one for
    // single-mapping, one for multi-mapping). On QFuture::cancel(),
    // QFutureWatcher::canceled fires on the engine thread, and
    // we forward to the worker via queued connection.
    QFutureWatcher<SyncResult>* m_singleWatcher = nullptr;
    QFutureWatcher<QList<SyncResult>>* m_multiWatcher = nullptr;
```

Add `#include <QFutureWatcher>` at the top.

- [ ] **Step 2: Wire watchers in `runSyncFuture`**

Update `runSyncFuture(QString, SyncBehavior)` and
`runSyncFuture(SyncBehavior)` from Task 15:

```cpp
QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    SyncBehavior behavior)
{
    auto *iface = new QFutureInterface<SyncResult>;
    iface->reportStarted();
    QFuture<SyncResult> future = iface->future();

    // F2: install QFutureWatcher to forward QFuture::cancel()
    // to the worker.
    delete m_singleWatcher;
    m_singleWatcher = new QFutureWatcher<SyncResult>(this);
    m_singleWatcher->setFuture(future);
    connect(m_singleWatcher, &QFutureWatcher<SyncResult>::canceled,
            this, &SyncEngine::onCancelObserved);

    // ... existing capture logic ...
    runSync(mappingId, behavior);
    return future;
}
```

(Same change for the multi-mapping form.)

- [ ] **Step 3: Add `SyncEngine::onCancelObserved` slot**

In the header (private slots section):

```cpp
private slots:
    // F2: invoked when m_singleWatcher or m_multiWatcher fires
    // canceled. Forwards to the worker via queued connection.
    void onCancelObserved();
```

In the .cpp:

```cpp
void SyncEngine::onCancelObserved()
{
    QMetaObject::invokeMethod(m_worker, "observeCancel",
                              Qt::QueuedConnection);
}
```

(`m_worker` is the existing pointer to `SyncEngineWorker`.)

- [ ] **Step 4: Build**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build.

- [ ] **Step 5: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. Cancellation isn't tested yet (the
cancellation test cases are still skipped).

- [ ] **Step 6: Commit**

```bash
git add src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "$(cat <<'EOF'
feat(engine): wire QFutureWatcher::canceled → worker observeCancel (F2 Task 17)

Phase F2. SyncEngine gains:
- QFutureWatcher<SyncResult>* m_singleWatcher (single-mapping)
- QFutureWatcher<QList<SyncResult>>* m_multiWatcher (multi)
- private slot onCancelObserved that forwards to the worker via
  Qt::QueuedConnection.

When QFuture::cancel() is invoked from any thread, the watcher's
canceled signal fires on the engine thread (where the future
was created); we route to the worker's observeCancel slot, which
sets m_cancelled and emits cancellationObserved. The await<>
helper from Task 16 picks it up and propagates.

The cancellation channel is now end-to-end wired from
QFuture::cancel() to the worker's hot path. Tests in Tasks 23-29
exercise it.

Refs: 04q-phase-f2-threading-design.md "Cancellation propagation"
+ Group 2 Task 17.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 18: Migrate engine apply path to call 3-arg `pushItems`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/calendardomainadapter.cpp` (if it calls storeItems/updateItem)

**Background:** Today the apply phase (per FINDINGS) calls
`storeItems` / `updateItem` synchronously on the calendar adapter
path. F2 routes these through `pushItems(id, items, plan)` and
the await<> helper. The void runSync overloads still exist; this
task changes their internal apply-path implementation, not the
public API.

- [ ] **Step 1: Find the apply path's storeItems/updateItem calls**

```bash
grep -rn "storeItems\|updateItem" \
    ~/dev/refactor-engine-merger/libkalburator/src/engine/ \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/calendardomainadapter.cpp \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/createincidenceitem.cpp \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/updateincidenceitem.cpp
```

Identify each call site. Most should be in
`calendardomainadapter.cpp::applyChanges` and in the
`commit()` methods of `CreateIncidenceItem` /
`UpdateIncidenceItem` (per FINDINGS).

- [ ] **Step 2: Refactor `CalendarDomainAdapter::applyChanges` to use `pushItems`**

Replace the synchronous `storeItems(cal, items, plan)` with
`await(backend->pushItems(calendarId, items, plan))` on the
worker thread. The adapter currently doesn't have access to the
worker; pass an awaiter via a setter. Quick adapter change:

In `src/calendar/calendardomainadapter.h`:

```cpp
public:
    /// F2: install the awaiter the engine uses to await
    /// SyncOperation handles. Adapter does not own it; the
    /// engine outlives the adapter.
    using Awaiter = std::function<SyncOperation*(SyncOperation*)>;
    void setAwaiter(Awaiter awaiter) { m_awaiter = std::move(awaiter); }

private:
    Awaiter m_awaiter;
```

In `src/engine/syncengine.cpp`, when constructing the adapter
(or in setAdapter), install the awaiter:

```cpp
// Inside SyncEngine::registerAdapter or similar:
adapter->setAwaiter([this](SyncOperation *op) -> SyncOperation* {
    return m_worker->await(op);
});
```

In `calendardomainadapter.cpp::applyChanges`, replace
`backend->storeItems(cal, items, plan)` with:

```cpp
auto *op = static_cast<PushOperation*>(
    m_awaiter(backend->pushItems(calendarId, items, plan)));
if (op->state() == SyncOperation::Failed) {
    // Existing error handling: capture op->errorString()
    // into the EngineApplyResult.errorMessage and continue
    // or abort per existing semantics.
}
op->deleteLater();
```

(Cancellation: `m_awaiter` returns the op even if cancelled;
adapter checks `op->state() == SyncOperation::Cancelled` and
short-circuits.)

- [ ] **Step 3: Build**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
```

Expected: clean build.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. The apply path now routes through the
async pushItems and await<>, but behaviour should be identical.

- [ ] **Step 5: Run verify-all**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/engine/syncengine.cpp src/calendar/calendardomainadapter.h \
        src/calendar/calendardomainadapter.cpp
git commit -m "$(cat <<'EOF'
refactor(engine): apply path uses 3-arg pushItems via await<Op> (F2 Task 18)

Phase F2. CalendarDomainAdapter::applyChanges now calls
backend->pushItems(id, items, plan) and awaits the resulting
PushOperation through the engine-installed Awaiter, replacing the
synchronous storeItems/updateItem calls Phase E added.

The Awaiter is a std::function callable installed on the adapter
at registration time; it dispatches to SyncEngineWorker::await<>.
The adapter stays agnostic of worker-thread mechanics.

Behaviour is unchanged at this point — both runs through pushItems
in the same single-flight QEventLoop pattern as the old sync
write. Cancellation observation kicks in once Group 2 Task 19's
per-record check is wired.

Refs: 04q-phase-f2-threading-design.md "IDomainAdapter,
CalendarDomainAdapter, BlobDomainAdapter" + Group 2 Task 18.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 19: Add per-record cancellation check in apply phase

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/calendardomainadapter.cpp` (the apply loop)
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp` (worker access for the cancel flag)

**Background:** The cancellation contract (design doc, Section
"Cancellation propagation") requires `m_cancelled.load()` to be
checked at the top of every per-record iteration in the apply
phase, in addition to the await<>-boundary check. This catches
"cancel was already requested when we entered the loop" and
prevents dispatching the next operation.

- [ ] **Step 1: Expose a cancellation accessor from the adapter to the worker**

The adapter doesn't have direct access to the worker's
`m_cancelled` atomic. Either pass a `std::function<bool()>`
("isCancelled?") into the adapter at registration, or have the
adapter's awaiter (from Task 18) double as the cancellation
oracle.

Cleanest: extend the awaiter contract. In
`calendardomainadapter.h`:

```cpp
using Awaiter = std::function<SyncOperation*(SyncOperation*)>;
using CancelOracle = std::function<bool()>;

void setAwaiter(Awaiter awaiter) { m_awaiter = std::move(awaiter); }
void setCancelOracle(CancelOracle oracle) { m_cancelOracle = std::move(oracle); }
```

In `syncengine.cpp` registration:

```cpp
adapter->setAwaiter([this](SyncOperation *op) {
    return m_worker->await(op);
});
adapter->setCancelOracle([this]() {
    return m_worker->isCancelled();  // public accessor on worker
});
```

`SyncEngineWorker::isCancelled()`:

```cpp
bool SyncEngineWorker::isCancelled() const noexcept
{
    return m_cancelled.load(std::memory_order_acquire);
}
```

- [ ] **Step 2: Add the per-record check in `applyChanges`**

In `calendardomainadapter.cpp::applyChanges`, around the per-
record loop:

```cpp
for (const auto &record : merge.finalTarget) {
    if (m_cancelOracle && m_cancelOracle()) {
        result.errorMessage = QStringLiteral("Cancelled by caller");
        result.cancelled = true;
        return result;
    }
    // existing per-record processing...
}
```

(Add `cancelled` field to `EngineApplyResult` if not already
there. Adapter's return type may need extension — check.)

- [ ] **Step 3: Build + tests**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass.

- [ ] **Step 4: Commit**

```bash
git add src/calendar/calendardomainadapter.h \
        src/calendar/calendardomainadapter.cpp \
        src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "$(cat <<'EOF'
feat(adapter): per-record cancellation check in apply phase (F2 Task 19)

Phase F2. CalendarDomainAdapter::applyChanges now checks the
cancellation oracle (a std::function<bool()> installed by the
engine) at the top of every per-record iteration. This catches
"cancel observed before await<>" cases and prevents dispatching
the next operation.

The contract per design doc Section "Cancellation propagation":
in-flight ops complete naturally; new ops do not start. Per-record
granularity is the apply-phase analogue of the await<>-boundary
check.

Refs: 04q-phase-f2-threading-design.md "Propagation in the hot
path" + Group 2 Task 19.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 20: Wire the conflict-pause `QEventLoop` to the cancellation channel

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp` (the worker's conflict-pause path)

**Background:** Today the worker enters a `QEventLoop::exec()`
when a conflict requires `AskUser` resolution; it exits when
`resumeAfterConflictResolution` is called. F2 adds a second exit
condition: cancellation. The pause loop connects to both
`resumeReceived` and `cancellationObserved`; on cancellation, the
worker tears down via the cancellation path.

- [ ] **Step 1: Find the conflict-pause path**

```bash
grep -n "QEventLoop\|conflictDetected\|resumeAfter" \
    ~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp \
    ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncworker.* 2>/dev/null
```

Find the exact place where the worker waits for resume. (Per F1
Task 8 it's now in syncengine.cpp, in the worker portion.)

- [ ] **Step 2: Update the pause path**

In the worker's pause body, replace:

```cpp
// OLD:
QEventLoop pauseLoop;
QObject::connect(this, &SyncEngineWorker::resumeReceived,
                 &pauseLoop, &QEventLoop::quit);
pauseLoop.exec();
// then read m_pendingResolution and continue
```

with:

```cpp
// F2: also exit on cancellation.
QEventLoop pauseLoop;
QObject::connect(this, &SyncEngineWorker::resumeReceived,
                 &pauseLoop, &QEventLoop::quit);
QObject::connect(this, &SyncEngineWorker::cancellationObserved,
                 &pauseLoop, &QEventLoop::quit);
pauseLoop.exec();

if (m_cancelled.load(std::memory_order_acquire)) {
    // Conflict left in the persistent SyncConflictStore for the
    // next run to pick up. Engine state machine returns to its
    // post-conflict cleanup path with cancellation outcome.
    return; // or whatever the existing early-return idiom is
}

// resumeReceived path: read m_pendingResolution and continue
```

(Adjust to the actual control-flow structure — may need a flag
return rather than `return` if the function is doing more after
the pause loop.)

- [ ] **Step 3: Build + tests**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass.

- [ ] **Step 4: Commit**

```bash
git add src/engine/syncengine.cpp
git commit -m "$(cat <<'EOF'
feat(engine): conflict-pause loop wired to cancellation channel (F2 Task 20)

Phase F2. The worker's conflict-pause QEventLoop now connects to
both resumeReceived and cancellationObserved; either exits the
loop. On cancellation, the engine returns via the existing
post-conflict cleanup path with cancellation outcome; conflicts
that were detected but never resolved remain in the persistent
SyncConflictStore for the next run.

This unblocks cancellation test C4 (cancel during conflict pause)
in Group 2 Task 26.

Refs: 04q-phase-f2-threading-design.md "Pause-resume interaction"
+ Group 2 Task 20.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 21: Fix the `runSync(mappingId)` leak — split worker drivers — **LANDED 2026-04-30** (commit `35c1881`)

**Status:** Landed 2026-04-30 on commit `35c1881`. The split was
implemented engine-side rather than worker-side — the per-mapping
logic was already extracted into `SyncEngineWorker::processSync(Request)`
(F1 Task 8 left it that way), so the natural seam was in the
engine's queue iterator (`processNextMapping`), which was split into
`processSingleMapping` + `processQueue`/`advanceQueue` with a new
`DispatchMode` tag deciding whether `onWorkerSyncCompleted`
finishes a single-mapping future or advances the queue. The
`m_currentSingleIface`/`m_currentMultiIface` pointers moved from
the worker to the engine (lifetime is naturally engine-side).
Cancellation contract preserved (m_cancelled flag drives the
queue's terminal branch + reportCanceled). 26/26 ctest pass;
verify-all clean.



**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp` (worker drivers)
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.h` (if private member shape changes)

**Background:** Per FINDINGS "`SyncEngine::runSync(mappingId)` is
leaky", the single-mapping form re-enters `processNextMapping`
which iterates from index 0 and re-dispatches the same mapping.
F2 splits the worker drivers into `processSingleMapping` (single-
mapping form) and `processQueue` (multi-mapping form). Neither
consults the other's state; the shared `m_currentMappingIndex`
field driving both forms goes away.

- [ ] **Step 1: Inspect the existing worker driver**

```bash
sed -n '460,520p' ~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp
```

Locate `processNextMapping`, `m_currentMappingIndex`, and the
single-mapping vs multi-mapping entry points.

- [ ] **Step 2: Refactor into two separate driver methods**

Add to `SyncEngineWorker` (header):

```cpp
public slots:
    /// F2: single-mapping driver. Runs exactly the named mapping
    /// once; reports result; finishes. No queue iteration.
    void processSingleMapping(const QString &mappingId,
                              SyncBehavior behavior);

    /// F2: multi-mapping driver. Iterates m_syncMappings (enabled
    /// snapshot); reports per-mapping result via the future
    /// interface; finishes with the aggregate list.
    void processQueue(SyncBehavior behavior);
```

Replace `processNextMapping` calls accordingly. Each driver
allocates its own local mapping list (or single mapping); they
do not share state.

In the .cpp:

```cpp
void SyncEngineWorker::processSingleMapping(
    const QString &mappingId,
    SyncBehavior behavior)
{
    // Locate the mapping by id.
    SyncMapping mapping;
    bool found = false;
    for (const auto &m : m_syncMappings) {
        if (m.id == mappingId) {
            mapping = m;
            found = true;
            break;
        }
    }
    if (!found) {
        SyncResult err;
        err.success = false;
        err.errorMessage = QStringLiteral("Mapping not found: %1").arg(mappingId);
        if (m_currentSingleIface) {
            m_currentSingleIface->reportResult(err);
            m_currentSingleIface->reportFinished();
        }
        emit syncCompleted(mappingId, err);  // legacy signal
        return;
    }

    // Cancellation check before start.
    if (m_cancelled.load()) {
        SyncResult cancelled;
        cancelled.success = false;
        cancelled.cancelled = true;
        cancelled.skipped = true;  // never started
        if (m_currentSingleIface) {
            m_currentSingleIface->reportResult(cancelled);
            m_currentSingleIface->reportCanceled();
            m_currentSingleIface->reportFinished();
        }
        return;
    }

    // Run the mapping (existing per-mapping logic, extracted
    // from the body of processNextMapping).
    SyncResult result = runMappingOnce(mapping, behavior);

    if (m_currentSingleIface) {
        m_currentSingleIface->reportResult(result);
        if (result.cancelled) {
            m_currentSingleIface->reportCanceled();
        }
        m_currentSingleIface->reportFinished();
    }
    emit syncCompleted(mappingId, result);
}

void SyncEngineWorker::processQueue(SyncBehavior behavior)
{
    QList<SyncResult> results;
    auto enabled = enabledMappings();  // existing helper

    for (int i = 0; i < enabled.size(); ++i) {
        if (m_cancelled.load()) {
            // Fill remaining slots as skipped+cancelled.
            for (int j = i; j < enabled.size(); ++j) {
                SyncResult skipped;
                skipped.success = false;
                skipped.cancelled = true;
                skipped.skipped = true;
                results.append(skipped);
            }
            break;
        }

        SyncResult result = runMappingOnce(enabled[i], behavior);
        results.append(result);
        emit syncCompleted(enabled[i].id, result);  // legacy signal
    }

    if (m_currentMultiIface) {
        m_currentMultiIface->reportResult(results);
        if (m_cancelled.load()) {
            m_currentMultiIface->reportCanceled();
        }
        m_currentMultiIface->reportFinished();
    }

    bool overallSuccess = std::all_of(
        results.begin(), results.end(),
        [](const SyncResult &r) { return r.success; });
    emit allSyncsCompleted(overallSuccess);  // legacy signal
}
```

`runMappingOnce(mapping, behavior)` is the existing per-mapping
body extracted into a private helper.

- [ ] **Step 3: Update the engine's runSyncFuture to invoke the new drivers and own the iface**

Replace the Task 15 shim implementations:

```cpp
QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    SyncBehavior behavior)
{
    auto *iface = new QFutureInterface<SyncResult>;
    iface->reportStarted();
    QFuture<SyncResult> future = iface->future();

    // Install the iface on the worker; worker owns reportResult/
    // reportFinished. Worker deletes the iface after reportFinished.
    QMetaObject::invokeMethod(m_worker, [this, iface, mappingId, behavior]() {
        // Already on worker thread.
        m_worker->setSingleIface(iface);
        m_worker->processSingleMapping(mappingId, behavior);
        m_worker->setSingleIface(nullptr);
        delete iface;  // worker reports finished; iface no longer needed
    }, Qt::QueuedConnection);

    // Watcher for cancellation propagation.
    delete m_singleWatcher;
    m_singleWatcher = new QFutureWatcher<SyncResult>(this);
    m_singleWatcher->setFuture(future);
    connect(m_singleWatcher, &QFutureWatcher<SyncResult>::canceled,
            this, &SyncEngine::onCancelObserved);

    return future;
}
```

(Equivalent for the multi-mapping form.)

`SyncEngineWorker::setSingleIface` / `setMultiIface` are simple
setters that store the iface pointer in `m_currentSingleIface` /
`m_currentMultiIface`.

- [ ] **Step 4: Build + tests**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. The void runSync overloads still work via
their existing path (which now delegates to processSingleMapping /
processQueue internally — adjust the void overloads to match if
they don't already).

- [ ] **Step 5: Commit**

```bash
git add src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "$(cat <<'EOF'
fix(engine): split worker drivers; resolves runSync(mappingId) leak (F2 Task 21)

Phase F2. Per FINDINGS "SyncEngine::runSync(mappingId) is leaky",
the single-mapping form double-ran because processNextMapping
iterated from index 0. F2 splits the worker into:
- processSingleMapping(id, behavior) — runs exactly that mapping
- processQueue(behavior) — iterates enabled mappings sequentially

The shared m_currentMappingIndex field driving both forms is gone.
The bug class becomes structurally impossible.

Both drivers populate m_currentSingleIface / m_currentMultiIface
respectively, calling reportResult / reportFinished /
reportCanceled directly. The runSyncFuture overloads on the
engine post the right driver to the worker via queued connection.

Cancellation is observed at the top of each queue iteration AND
inside runMappingOnce (await<> boundaries + Task 19's per-record
check). Multi-mapping queue cancellation fills remaining slots
with {cancelled=true, skipped=true} sentinels.

This is a behavioural fix that resolves the FINDINGS entry plus
sets up the new QFuture API to be the canonical caller surface.

Refs: 04q-phase-f2-threading-design.md "Multi-mapping queue" +
FINDINGS "SyncEngine::runSync(mappingId) is leaky"; resolves
that finding.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 22: Add `MockBackend` blockable fetch primitive (test fixture)

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/mockbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/mockbackend.cpp`

**Background:** Cancellation tests C2 / C3 need to cancel WHILE
an operation is in flight. `MockBackend`'s normal fetch is too
fast to reliably cancel mid-flight. Add a "block" mode that
makes `fetchItems` wait on a `QSemaphore` until the test
releases it.

- [ ] **Step 1: Add the blockable fetch state to MockBackend**

In `src/calendar/mockbackend.h`:

```cpp
public:
    /// F2 test fixture: when set to true, fetchItems() returns
    /// an operation that blocks on m_fetchBlocker until
    /// releaseFetchBlocker() is called. Use to deterministically
    /// cancel a fetch mid-flight in tests.
    void setFetchBlocking(bool blocking) { m_fetchBlocking = blocking; }
    void releaseFetchBlocker() { m_fetchBlocker.release(); }

private:
    bool m_fetchBlocking = false;
    QSemaphore m_fetchBlocker;
```

Add `#include <QSemaphore>` to the header.

- [ ] **Step 2: Use the blocker in fetchItems**

In `src/calendar/mockbackend.cpp::fetchItems`:

```cpp
FetchOperation* MockBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId);
    op->setState(SyncOperation::Running);

    if (m_fetchBlocking) {
        // Run the fetch on a worker thread that blocks until
        // released. This lets cancellation tests cancel
        // mid-fetch deterministically.
        auto *thread = QThread::create([this, op]() {
            m_fetchBlocker.acquire();
            if (op->cancelRequested()) {
                op->setState(SyncOperation::Cancelled);
                return;
            }
            // Mock fetch result.
            op->setItems(m_storedItems[op->calendarId()]);
            op->setState(SyncOperation::Succeeded);
        });
        connect(thread, &QThread::finished,
                thread, &QThread::deleteLater);
        thread->start();
        return op;
    }

    // Existing non-blocking path.
    op->setItems(m_storedItems[calendarId]);
    op->setState(SyncOperation::Succeeded);
    return op;
}
```

- [ ] **Step 3: Build + tests**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 25/25 pass. The blocker isn't used yet.

- [ ] **Step 4: Commit**

```bash
git add src/calendar/mockbackend.h src/calendar/mockbackend.cpp
git commit -m "$(cat <<'EOF'
test(mockbackend): add blockable fetch primitive for cancellation tests (F2 Task 22)

Phase F2. MockBackend gains:
- setFetchBlocking(bool) — toggle blocking mode
- releaseFetchBlocker() — wake the blocked fetch

When blocking, fetchItems returns a FetchOperation backed by a
QThread that waits on a QSemaphore. The cancellation tests
(C2 / C3 in Group 2 Tasks 24-25) use this to deterministically
cancel a fetch mid-flight.

Refs: 04q-phase-f2-threading-design.md C2/C3 test cases +
Group 2 Task 22.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 23: Implement C1 cancellation test — cancel before start

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_engine_cancellation.cpp`

- [ ] **Step 1: Replace the C1 stub with a real test**

```cpp
void TstEngineCancellation::cancelBeforeStart()
{
    // Configure a single mapping with a MockBackend on each side.
    auto *src = new MockBackend(this);
    auto *dst = new MockBackend(this);
    src->setStoredItems("cal1", makeMockEvents(3));

    SyncMapping mapping;
    mapping.id = "m1";
    mapping.sourceBackend = src;
    mapping.targetBackend = dst;
    mapping.sourceCalendarId = "cal1";
    mapping.targetCalendarId = "cal1";
    mapping.enabled = true;

    m_engine = makeEngine(this, m_host, m_configStore);
    m_engine->setSyncMappings({mapping});
    auto *adapter = makeCalendarAdapter(this);
    m_engine->registerAdapter(adapter);

    // Get the future BEFORE the worker observes it.
    auto future = m_engine->runSyncFuture("m1");

    // Cancel immediately, then wait for finish.
    future.cancel();
    future.waitForFinished();

    QVERIFY(future.isCanceled());
    QVERIFY(future.isFinished());

    // Verify no items were written.
    QCOMPARE(dst->storedItems("cal1").size(), 0);

    // SyncResult should reflect cancellation.
    auto results = future.results();
    QVERIFY(!results.isEmpty());
    QVERIFY(results.first().cancelled);
    QVERIFY(results.first().skipped);
}
```

`makeMockEvents(n)`, `makeEngine(...)`, `makeCalendarAdapter(...)`
are small helper functions (extract into a private section of
the test class or a separate `cancellation_helpers.cpp`). The
shape mirrors what the existing F.0 tests do.

- [ ] **Step 2: Run and confirm it passes**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc) --target tst_engine_cancellation
ctest --test-dir build --output-on-failure -R cancelBeforeStart
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/calendar/tst_engine_cancellation.cpp
git commit -m "$(cat <<'EOF'
test(engine): C1 — cancel before start (F2 Task 23)

Phase F2. Implements the first cancellation contract case:
QFuture cancelled immediately after creation, before the worker
observes it. Verifies:
- future.isCanceled() && isFinished() after waitForFinished
- no items written to destination
- SyncResult has cancelled=true && skipped=true

The other six cancellation cases (C2-C7) are unstubbed in
Tasks 24-29.

Refs: 04q-phase-f2-threading-design.md C1 test case + Group 2
Task 23.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 24: Implement C2 — cancel during fetch

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_engine_cancellation.cpp`

- [ ] **Step 1: Implement cancelDuringFetch**

```cpp
void TstEngineCancellation::cancelDuringFetch()
{
    auto *src = new MockBackend(this);
    auto *dst = new MockBackend(this);
    src->setStoredItems("cal1", makeMockEvents(10));
    src->setFetchBlocking(true);  // fetch blocks until released

    SyncMapping mapping;
    mapping.id = "m1";
    mapping.sourceBackend = src;
    mapping.targetBackend = dst;
    mapping.sourceCalendarId = "cal1";
    mapping.targetCalendarId = "cal1";
    mapping.enabled = true;

    m_engine = makeEngine(this, m_host, m_configStore);
    m_engine->setSyncMappings({mapping});
    m_engine->registerAdapter(makeCalendarAdapter(this));

    auto future = m_engine->runSyncFuture("m1");

    // Wait briefly to ensure the worker has started the fetch.
    QTest::qWait(100);

    // Cancel while fetch is blocked. The await<> helper exits
    // immediately and calls op->cancel(); op's run thread observes
    // cancelRequested and transitions Cancelled. We unblock the
    // semaphore so the test cleanup doesn't hang the QThread.
    future.cancel();
    src->releaseFetchBlocker();

    future.waitForFinished();
    QVERIFY(future.isCanceled());
    QCOMPARE(dst->storedItems("cal1").size(), 0);

    auto results = future.results();
    QVERIFY(!results.isEmpty());
    QVERIFY(results.first().cancelled);
}
```

- [ ] **Step 2: Run + commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc) --target tst_engine_cancellation
ctest --test-dir build --output-on-failure -R cancelDuringFetch
git add tests/calendar/tst_engine_cancellation.cpp
git commit -m "test(engine): C2 — cancel during fetch (F2 Task 24)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 25: Implement C3 — cancel during apply

Similar pattern to C2 but using a per-record-blocking apply.
Configure `MockBackend`'s push to block on a semaphore for the
3rd record; cancel after the 2nd is processed; verify records
1-2 written, record 3 not, no further records dispatched.

(Step structure identical to Task 24; details elided for brevity
in this plan, but the implementer follows the same pattern: add
test, run, commit.)

- [ ] **Step 1: Add MockBackend blockable push primitive (analogous to Task 22's fetch)**
- [ ] **Step 2: Implement `cancelDuringApply` test**
- [ ] **Step 3: Run + commit**

---

### Task 26: Implement C4 — cancel during conflict pause

- [ ] **Step 1: Configure a mapping with `AskUser` policy and a seeded baseline so a conflict is generated**
- [ ] **Step 2: Cancel after `conflictDetected` fires; verify worker tears down without resolution**
- [ ] **Step 3: Verify the conflict remains in the persistent `SyncConflictStore`**
- [ ] **Step 4: Run + commit**

---

### Task 27: Implement C5 — cancel multi-mapping mid-queue

- [ ] **Step 1: Configure 5 mappings; cancel after mapping 2 finishes**
- [ ] **Step 2: Verify result list has 5 entries: mappings 1-2 with real results, 3-5 with `{cancelled, skipped}`**
- [ ] **Step 3: Run + commit**

---

### Task 28: Implement C6 / C7 — idempotent cancel + cancel after finished

- [ ] **Step 1: Cancel an already-cancelled future; verify no error**
- [ ] **Step 2: Cancel an already-finished future; verify no-op**
- [ ] **Step 3: Run + commit**

---

### Task 29: Implement positive `QFuture` smoke tests

- [ ] **Step 1: `singleMappingFutureCompletes` — runSyncFuture(id) succeeds with sane SyncResult**
- [ ] **Step 2: `multiMappingFutureReturnsList` — list length matches enabled mapping count**
- [ ] **Step 3: `watcherFinishedFiresOnce` — `QFutureWatcher::finished` exactly once**
- [ ] **Step 4: `progressValueTicks` — `QFuture::progressValue()` is non-zero at end**
- [ ] **Step 5: Run + commit**

---

### Task 30: Group 2 verify-all gate

- [ ] **Step 1: Run verify-all**

Expected: exit 0. libkalburator: 25/25 + 11 cancellation tests
all passing (was 11 skipping in Task 2; now real). PlanStan:
96/120. WildPalms: 73/73.

- [ ] **Step 2: Empty marker commit**

---

## Group 3 — Consumer migration

### Task 31: Migrate `tst_calendar_sync_full.cpp` to QFuture

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_calendar_sync_full.cpp`

**Background:** The F.0/F1 tests use the void `runSync(behavior)`
+ `waitForSignal(allSyncsCompleted)` pattern per the libkalburator
CLAUDE.md and FINDINGS workaround. F2 migrates them to
`runSyncFuture(...).waitForFinished()`. This is mechanical.

- [ ] **Step 1: Inspect the current test pattern**

```bash
grep -n "runSync\|allSyncsCompleted\|syncCompleted\|waitForSignal" \
    ~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_calendar_sync_full.cpp
```

- [ ] **Step 2: Replace the pattern**

For each test method, change:

```cpp
// OLD
m_engine->runSync(SyncBehavior::Unmonitored);
QSignalSpy completedSpy(m_engine.get(), &SyncEngine::allSyncsCompleted);
QVERIFY(completedSpy.wait(5000));
```

to:

```cpp
// NEW
auto future = m_engine->runSyncFuture(SyncBehavior::Unmonitored);
future.waitForFinished();
QVERIFY(!future.isCanceled());
auto results = future.results();
// existing assertions on per-mapping result via results
```

- [ ] **Step 3: Run + commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc) --target tst_calendar_sync_full
ctest --test-dir build --output-on-failure -R tst_calendar_sync_full
```

Expected: PASS.

```bash
git add tests/calendar/tst_calendar_sync_full.cpp
git commit -m "test(calendar): migrate tst_calendar_sync_full to QFuture (F2 Task 31)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 32: Migrate the rest of `tests/calendar/` to QFuture

Apply the Task 31 pattern to:
- `tst_calendar_sync_oneway.cpp`
- `tst_calendar_conflict.cpp`
- `tst_calendar_transcoding_warning.cpp`
- `tst_calendar_first_sync_via_blob_engine.cpp`
- `tst_calendar_subsequent_sync_uses_blob_view.cpp`
- `tst_calendar_sync_error_recovery.cpp`

Each file gets its own commit using the Task 31 step structure.

- [ ] **Step 1-N: One sub-task per file (same 5-step TDD pattern)**

---

### Task 33: Migrate `tst_engine_blob_one_shot.cpp` and `tst_engine_unified_boundary.cpp`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_engine_blob_one_shot.cpp`
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_engine_unified_boundary.cpp`

**Background:** These tests call `engine.runBlobTwoWay(...)` /
`runBlobMirror(...)` directly. The migration constructs a
one-mapping engine configured with the relevant blob backends and
runs through `runSyncFuture(id).waitForFinished()`. This requires
a small test helper that wraps the synthetic-mapping setup
(which the F1 facade did internally).

- [ ] **Step 1: Extract a test helper `runBlobOneShot(engine, src, dst, colId)`**

In a new file `tests/calendar/test_helpers/blob_oneshot.h`:

```cpp
inline BlobSyncResult runBlobOneShot(
    Kalburator::Sync::SyncEngine *engine,
    IBlobBackend *src,
    IBlobBackend *dst,
    const QString &collectionId)
{
    SyncMapping mapping;
    mapping.id = "blob_oneshot";
    mapping.sourceBackend = nullptr;  // SyncBackend* — see below
    // ... configure synthetic blob mapping ...
    engine->setSyncMappings({mapping});
    engine->runSyncFuture(mapping.id).waitForFinished();
    // Reconstruct BlobSyncResult from the engine's last result
    BlobSyncResult result;
    auto last = engine->lastSyncResult();
    result.success = last.success;
    // ... map fields ...
    return result;
}
```

(Exact mapping of `SyncResult` → `BlobSyncResult` depends on
the field shapes; consult `blobsyncresult.h`.)

- [ ] **Step 2: Replace each `engine.runBlobTwoWay(...)` / `runBlobMirror(...)` call**

```cpp
// OLD
BlobSyncResult r = engine.runBlobTwoWay(&src, &dst, colId, ...);

// NEW
BlobSyncResult r = runBlobOneShot(&engine, &src, &dst, colId);
```

- [ ] **Step 3: Run + commit per file**

---

### Task 34: Migrate `tst_synctransaction.cpp`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/tests/calendar/tst_synctransaction.cpp` (or wherever it lives — grep)

**Background:** Per FINDINGS "Virtual function default arguments
must be redeclared on overrides", `tst_synctransaction` holds a
concrete `LocalBackend*` and calls `pushItems` / `storeItems`
directly. F2 migrates to `await(localBackend->pushItems(id,
items, plan))`.

- [ ] **Step 1: Find and inspect**
- [ ] **Step 2: Replace `storeItems`/`updateItem` calls with `await(pushItems(...))`**
- [ ] **Step 3: Run + commit**

(In test code, `await<>` may not be directly callable since the
test doesn't have a `SyncEngineWorker`. Use a small inline event-
loop pattern instead:

```cpp
auto *op = backend->pushItems(id, items, plan);
QSignalSpy finished(op, &SyncOperation::finished);
QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 1000);
QCOMPARE(op->state(), SyncOperation::Succeeded);
op->deleteLater();
```

The `QTRY_VERIFY_WITH_TIMEOUT` is the test-side equivalent of
`await<>`.)

---

### Task 35: Migrate `CreateIncidenceItem::commit()` and `UpdateIncidenceItem::commit()` — **LANDED 2026-04-30** (commit `4a92955`)

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/createincidenceitem.cpp`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/createincidenceitem.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/updateincidenceitem.cpp`

**Background:** Per FINDINGS "Wrapper commit() lost error
detection when switching from pushItems to storeItems", these
wrappers use a fragile temporary-`connect` pattern to
`writeFinished`. F2 replaces with the operation-handle pattern.

**Outcome:** Both `commit()` bodies now call
`backend->pushItems(calendarId, {item}, plan)` and observe
`pushOp->state() == Succeeded` / `pushOp->errorString()`.
`pushItems` handles both create and update (backend inspects UID
existence to decide), so `UpdateIncidenceItem::commit` no longer
calls `updateItem`. The wait pattern uses `QEventLoop` +
`QTimer::singleShot(30 s)` guarded by `op->isFinished()` to handle
backends that complete synchronously. Subsumes Task 18 (deferred
under the actual code shape, which routes the apply path through
these wrappers rather than direct backend calls). Resolves the
2026-04-29 FINDINGS entry about wrapper error detection. 26/26
ctest pass on libkalburator; verify-all exit 0.
`DeleteIncidenceItem::commit` was already on the operation-handle
pattern and required no change.

- [x] **Step 1: Inspect the existing commit() bodies**
- [x] **Step 2: Replace the temporary-connect pattern**

```cpp
// OLD
auto *spy = new QSignalSpy(backend, &SyncBackend::writeFinished);
backend->storeItems(cal, items);  // sync call
QVERIFY(spy->wait(5000));
auto args = spy->takeFirst();
const bool ok = args.at(1).toBool();
const QString err = args.at(2).toString();

// NEW
auto *op = backend->pushItems(calendarId, items, m_plan);
QSignalSpy finished(op, &SyncOperation::finished);
QVERIFY(finished.wait(5000));
const bool ok = (op->state() == SyncOperation::Succeeded);
const QString err = op->errorString();
op->deleteLater();
```

- [x] **Step 3: Run libkalburator tests + verify-all**
- [x] **Step 4: Commit**

---

### Task 36: Migrate PlanStan's `SyncProgressManager` to `QFutureWatcher`

**Files:**
- Modify: `~/dev/refactor-engine-merger/PlanStan/src/app/syncprogressmanager.h`
- Modify: `~/dev/refactor-engine-merger/PlanStan/src/app/syncprogressmanager.cpp`

**Background:** `SyncProgressManager` connects to
`syncCompleted` / `allSyncsCompleted` to drive the progress UI.
F2 keeps streaming signals (`progressChanged` etc.) but moves
completion to `QFutureWatcher::finished`.

- [ ] **Step 1: Inspect existing SyncProgressManager**

```bash
grep -n "syncCompleted\|allSyncsCompleted\|connectToSync\|cancelSync" \
    ~/dev/refactor-engine-merger/PlanStan/src/app/syncprogressmanager.{h,cpp}
```

- [ ] **Step 2: Add QFutureWatcher member + finished slot**

In `syncprogressmanager.h`:

```cpp
private:
    QFutureWatcher<QList<Kalburator::Sync::SyncResult>> *m_watcher = nullptr;

private slots:
    void onSyncFinished();   // F2: replaces onAllSyncsCompleted
```

`onSyncFinished()` reads `m_watcher->result()` and updates the UI
state accordingly.

- [ ] **Step 3: Update `connectToSyncEngine` to call `runSyncFuture` and install the watcher**

```cpp
void SyncProgressManager::connectToSyncEngine(
    Kalburator::Sync::SyncEngine *engine)
{
    m_engine = engine;
    // Streaming signals continue unchanged.
    connect(engine, &SyncEngine::progressChanged, ...);
    connect(engine, &SyncEngine::phaseChanged, ...);
    // syncCompleted / allSyncsCompleted no longer connected.
}

void SyncProgressManager::startSync(SyncBehavior behavior)
{
    auto future = m_engine->runSyncFuture(behavior);
    delete m_watcher;
    m_watcher = new QFutureWatcher<QList<SyncResult>>(this);
    m_watcher->setFuture(future);
    connect(m_watcher, &QFutureWatcher<...>::finished,
            this, &SyncProgressManager::onSyncFinished);
}
```

(Cancellation: caller invokes `m_watcher->future().cancel()` to
cancel.)

- [ ] **Step 4: Build PlanStan, run tests**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
cmake --build build-dev -j$(nproc)
ctest --test-dir build-dev --output-on-failure
```

Expected: 96/120 baseline preserved.

- [ ] **Step 5: Commit (in PlanStan worktree)**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
git add src/app/syncprogressmanager.h src/app/syncprogressmanager.cpp
git commit -m "fix(sync): migrate SyncProgressManager to QFutureWatcher (F2 Task 36)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 37: Migrate PlanStan's `MainWindow` and other `runSync` call sites

- [ ] **Step 1: Grep for all PlanStan call sites of the old API**

```bash
git -C ~/dev/refactor-engine-merger/PlanStan grep -n \
    "runSync\b\|cancelSync\b\|syncCompleted\b\|allSyncsCompleted\b" -- src/
```

- [ ] **Step 2: Migrate each site**
- [ ] **Step 3: Run + commit**

---

### Task 38: Migrate PlanStan's 4 `EXCLUDE_FROM_ALL` sync-workflow tests

**Files:**
- Modify: `~/dev/refactor-engine-merger/PlanStan/tests/sync-workflow/tst_sync_conflicts.cpp`
- Modify: `~/dev/refactor-engine-merger/PlanStan/tests/sync-workflow/tst_sync_caldav_conflicts.cpp`
- Modify: `~/dev/refactor-engine-merger/PlanStan/tests/sync-workflow/tst_sync_error_recovery.cpp`
- Modify: `~/dev/refactor-engine-merger/PlanStan/tests/sync-workflow/tst_sync_dialog.cpp`

These are `EXCLUDE_FROM_ALL` per FINDINGS / SETUP.md and need
explicit build targets. Apply the QFuture migration pattern.

- [ ] **Step 1-4: One per file**

---

### Task 39: Migrate WildPalms's `syncrunner_wp.cpp`

**Files:**
- Modify: `~/dev/refactor-engine-merger/WildPalms/src/runtime/syncrunner_wp.cpp`

**Background:** Each `engine.runBlobTwoWay(...)` /
`runBlobMirror(...)` call becomes:

```cpp
// 1. Configure a synthetic mapping for this collection
SyncMapping m;
m.id = QStringLiteral("wp:blob:%1").arg(col.id);
// ... wire up src, dst, baselineStore, handlers, conflicts, policy
m_engine->setSyncMappings({m});

// 2. Run synchronously (conduit context blocks anyway)
auto future = m_engine->runSyncFuture(m.id);
future.waitForFinished();

// 3. Read result
auto result = future.result();
// Map SyncResult → BlobSyncResult equivalent for the runner's
// caller, or refactor the caller to consume SyncResult directly.
```

- [ ] **Step 1: Find every call site**
- [ ] **Step 2: Replace each with the pattern above**
- [ ] **Step 3: Build WildPalms, run tests**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 73/73 (modulo flakes).

- [ ] **Step 4: Commit (WildPalms worktree)**

---

### Task 40: Migrate WildPalms test files

**Files:**
- Modify: `~/dev/refactor-engine-merger/WildPalms/tests/palmsync/tst_palmbackend_roundtrip.cpp`
- Modify: `~/dev/refactor-engine-merger/WildPalms/tests/plugins/memo/tst_memo_v2.cpp`
- Modify: `~/dev/refactor-engine-merger/WildPalms/tests/plugins/todos/tst_todo_v2.cpp`
- Modify: `~/dev/refactor-engine-merger/WildPalms/tests/plugins/webcalendar/tst_webcal_v2_e2e.cpp`

Apply the Task 39 pattern (synthetic mapping + runSyncFuture +
waitForFinished + read result). Per file, separate commit.

- [ ] **Step 1-4: One per file**

---

### Task 41: Cross-repo verify-all + plugin-sub-repo ripple check

- [ ] **Step 1: Run verify-all.sh**

Expected: exit 0. If WildPalms fails because a plugin sub-repo
references a removed type (per FINDINGS F1 Task 13), see Step 2.

- [ ] **Step 2: If plugin sub-repos need patches**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
git submodule status
# For each sub-repo that holds a stale type ref:
cd src/plugins/<plugin>
# patch the file
git add ...
git commit -m "fix(<plugin>): adopt SyncEngine F2 API (F2 Task 41)"
cd -
git add src/plugins/<plugin>
git commit -m "chore: bump <plugin> submodule pointer for F2"
```

- [ ] **Step 3: Empty marker commit on libkalburator**

---

## Group 4 — Cleanup

### Task 42: Delete void `runSync` overloads, `cancelSync` slot, completion signals

**Status:** Landed 2026-04-30 on commit `cc8d94e` (libkalburator) +
`98be4159` (PlanStan synctesthelper cleanup). The void `runSync`
overloads' multi-mapping body is rebadged as private helper
`driveQueue()` invoked solely from `runSyncFuture(behavior)`. Three
libkalburator tests/calendar/ tests migrated off the deleted
`syncCompleted` spy. Note: `runSyncFuture` is NOT renamed back to
`runSync` — kept as-is to avoid churning Group 3 consumer migrations
for a cosmetic gain.

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp`

- [ ] **Step 1: Confirm no consumer calls them**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator grep -n "void runSync\|cancelSync\b\|syncCompleted\b\|allSyncsCompleted\b" -- src/ tests/
git -C ~/dev/refactor-engine-merger/PlanStan grep -n "syncCompleted\b\|allSyncsCompleted\b\|cancelSync\b" -- src/ tests/
git -C ~/dev/refactor-engine-merger/WildPalms grep -n "syncCompleted\b\|allSyncsCompleted\b\|cancelSync\b" -- src/ tests/
```

Expected: zero hits except inside `syncengine.h` / `syncengine.cpp`
themselves.

- [ ] **Step 2: Delete the public methods + signal declarations + their bodies**

Remove from `syncengine.h`:
- `void runSync(SyncBehavior)`
- `void runSync(QString, SyncBehavior)`
- `void cancelSync()`
- `signals: void syncCompleted(...); void allSyncsCompleted(...);`

Rename `runSyncFuture` → `runSync` (the new canonical names).

In `syncengine.cpp`:
- Delete the void-runSync bodies (the implementation logic
  already moved to `processSingleMapping` / `processQueue`
  on the worker; the void overloads are thin shims that call
  the new path internally — delete them).
- Delete `cancelSync()` body.

- [ ] **Step 3: Build + tests + verify-all**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

- [ ] **Step 4: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "refactor(engine): delete void runSync, cancelSync, completion signals (F2 Task 42)

The QFuture-based runSync (formerly runSyncFuture) is the canonical API.
Consumers migrated in Group 3.

Refs: 04q-phase-f2-threading-design.md Group 4 Task 14.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 43: Delete `loadItems`, `storeItems`, `updateItem`, `writeFinished` on `SyncBackend`

**Status:** BLOCKED 2026-04-30 — broader consumer migration outside
Group 3's actual scope is required first. See `FINDINGS.md` entry
"F2 Group 3 left ~180 PlanStan backend-test call sites + 1
production caller un-migrated" and `CURRENT-STATUS.md` "Blocked"
section. Recommended path: spawn an F2 Task 43-prep that migrates
PlanStan's `convertCalendarToBackend`, the ~180 PlanStan
backend-test call sites, and WildPalms's `PalmCalendarBackend`
overrides off the synchronous I/O API onto the operation API
before this deletion can land.

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/calendar/syncbackend.cpp`
- Modify: each concrete `SyncBackend` subclass (.h + .cpp)

- [ ] **Step 1: Confirm no callers in the engine or any consumer**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator grep -n "loadItems\|storeItems\|updateItem\|writeFinished" -- src/
git -C ~/dev/refactor-engine-merger/PlanStan grep -n "loadItems\|storeItems\|updateItem\|writeFinished"
git -C ~/dev/refactor-engine-merger/WildPalms grep -n "loadItems\|storeItems\|updateItem\|writeFinished"
```

Expected: zero hits in src/ engine/ or consumer src/. There may
still be hits in test files; check carefully — those should have
been migrated in Group 3.

- [ ] **Step 2: Delete from `syncbackend.h`**

Remove the declarations of:
- `virtual void loadItems(MemoryCalendar*, bool) = 0;`
- `virtual void storeItems(MemoryCalendar*, ..., TranscodingPlan)`
- `virtual void updateItem(MemoryCalendar*, ..., TranscodingPlan)`
- `void writeFinished(...) signal`

Also delete the 2-arg `pushItems(QString, items)` (the delegating
shim from Group 1); only the 3-arg form remains.

- [ ] **Step 3: Delete from each concrete subclass**

For each concrete backend (LocalBackend, RemoteBackend,
OrgBackend, MockBackend, AkonadiBackend, DecSyncBackend,
HolidaySubscriptionBackend, SubscriptionBackend), delete the
override implementations of the four removed methods.

- [ ] **Step 4: Build + tests + verify-all**

- [ ] **Step 5: Commit**

```bash
git commit -m "refactor(syncbackend): delete sync overloads + writeFinished signal (F2 Task 43)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 44: Delete `runBlobTwoWay` / `runBlobMirror` from `SyncEngine`

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.h`
- Modify: `~/dev/refactor-engine-merger/libkalburator/src/engine/syncengine.cpp`

- [ ] **Step 1: Confirm zero callers**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator grep -n "runBlobTwoWay\|runBlobMirror" -- src/ tests/
git -C ~/dev/refactor-engine-merger/WildPalms grep -n "runBlobTwoWay\|runBlobMirror"
```

Expected: zero hits.

- [ ] **Step 2: Delete the methods**

Delete from `syncengine.h` and `syncengine.cpp`:
- `BlobSyncResult runBlobTwoWay(...)` declaration + body
- `BlobSyncResult runBlobMirror(...)` declaration + body

Keep `BlobSyncResult` type itself (it's still used by F1 callers
during transition; another deletion candidate for Phase G if no
remaining users).

- [ ] **Step 3: Build + tests + verify-all**

- [ ] **Step 4: Commit**

---

### Task 45: Refresh libkalburator baseline + update CLAUDE.md

**Files:**
- Modify: `~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt`
- Modify: `~/dev/refactor-engine-merger/libkalburator/CLAUDE.md` (the `runSync(behavior)` guidance)

- [ ] **Step 1: Refresh the baseline**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
ctest --test-dir build -N | sed -n 's/^  Test #.*: //p' \
    > ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

Expected: 26 entries (F1's 23 + tst_engine_cancellation +
tst_syncoperation_contract + maybe more if other tests were
split).

- [ ] **Step 2: Update CLAUDE.md guidance**

In `libkalburator/CLAUDE.md`, replace the calendar-test guidance:

```markdown
- **`SyncEngine::runSync(behavior)`** (no `mappingId` arg) —
  use this and wait on `allSyncsCompleted`. The single-mapping form
  `runSync(mappingId, …)` has a known leak ...
```

with:

```markdown
- **`SyncEngine::runSync(behavior)` returns `QFuture<QList<SyncResult>>`** —
  use `.waitForFinished()` to block; `.results()` reads the
  per-mapping result list. The single-mapping form
  `runSync(mappingId, behavior)` returns `QFuture<SyncResult>`.
  The pre-F2 leak in the single-mapping form (FINDINGS) is
  resolved structurally as of v0.14-phase-f2-threading.
```

- [ ] **Step 3: Commit**

```bash
cd ~/dev/refactor-engine-merger
git add baselines/libkalburator-worktree-ctest.txt
cd ~/dev/refactor-engine-merger/libkalburator
git add CLAUDE.md
git commit -m "chore(f2): refresh baseline + update CLAUDE.md (F2 Task 45)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 46: Mark FINDINGS entries `[RESOLVED in v0.14]`

**Files:**
- Modify: `~/dev/refactor-engine-merger/FINDINGS.md`

- [ ] **Step 1: Append `[RESOLVED in v0.14 — see commit SHA]` to two entries**

In `FINDINGS.md`:

1. The `SyncEngine::runSync(mappingId)` is leaky entry — add at
   the top of the body:

```markdown
### SyncEngine::runSync(mappingId) is leaky [RESOLVED in v0.14 — see Task 21 commit SHA]
```

2. The `Wrapper commit() lost error detection when switching from
   pushItems to storeItems` entry — same pattern.

(The exact commit SHA goes in once Task 21 / Task 35 have
landed.)

- [ ] **Step 2: Commit**

```bash
cd ~/dev/refactor-engine-merger
git add FINDINGS.md
git commit -m "docs(findings): mark runSync(mappingId) leak + wrapper-fragility RESOLVED in v0.14 (F2 Task 46)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

(The coordination folder is not a git repo per project CLAUDE.md
— FINDINGS.md is just a tracked file there. Adjust the
`git add`/`git commit` to whatever workflow the user has set up
for the coordination folder. If it's not git-tracked, just edit
in place.)

---

## Group 5 — Doc + tag

### Task 47: Update phase-status doc

**Files:**
- Modify: `~/dev/refactor-engine-merger/libkalburator/docs/phase0/04q-phase-f2-threading-design.md`

- [ ] **Step 1: Update the Status line**

Change:

```markdown
**Status:** Authored 2026-04-30. Implementation plan to follow in
`04q-phase-f2-threading-plan.md` (sibling).
```

to:

```markdown
**Status:** Landed YYYY-MM-DD on tag `v0.14-phase-f2-threading`.
Implementation plan: `04q-phase-f2-threading-plan.md` (sibling).
```

- [ ] **Step 2: Commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add docs/phase0/04q-phase-f2-threading-design.md
git commit -m "docs(phase0): mark F2 design landed (F2 Task 47)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 48: Update CURRENT-STATUS.md and ROADMAP.md

**Files:**
- Modify: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- Modify: `~/dev/refactor-engine-merger/libkalburator/docs/phase0/04k-engine-merger-roadmap.md`

- [ ] **Step 1: CURRENT-STATUS.md updates**

In `~/dev/refactor-engine-merger/CURRENT-STATUS.md`:

- Bump the date at the top to the F2 land date.
- Move "Phase F2 — Threading API redesign" from "Next" to
  "Where we are" with a ✅.
- Replace "Next" with "Phase G — Opaque transport + plugin diff".
- Append the F2 commits to "Recently committed (libkalburator)".

- [ ] **Step 2: ROADMAP.md updates**

In `04k-engine-merger-roadmap.md`'s phase table:

```markdown
| F2 — Threading API redesign | ✅ landed YYYY-MM-DD | `v0.14-phase-f2-threading` |
```

- [ ] **Step 3: Commit (libkalburator side; CURRENT-STATUS edits commit-free per project)**

The coordination folder isn't a git repo, but libkalburator's
roadmap doc is. Commit the roadmap update; CURRENT-STATUS edits
land in the coordination folder directly.

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add docs/phase0/04k-engine-merger-roadmap.md
git commit -m "docs(roadmap): mark F2 landed (F2 Task 48)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

### Task 49: Append findings discovered during F2 implementation

**Files:**
- Modify: `~/dev/refactor-engine-merger/FINDINGS.md`

- [ ] **Step 1: Append any non-obvious findings discovered**

Examples that may emerge:
- "RemoteBackend::PushOperation::cancel() requires X to map abort
  to Cancelled state".
- "QFutureInterface lifetime needs to outlive the worker; deletion
  must follow reportFinished".
- "WildPalms plugin Y had a stale `runBlobTwoWay` reference
  hidden behind a forward decl".

Use the FINDINGS format (Date / Source / What / Why it matters /
Action).

- [ ] **Step 2: Commit (or just save in the coordination folder)**

---

### Task 50: Tag `v0.14-phase-f2-threading` (USER-AUTHORISED)

**Files:** none (tag operation only)

**This task requires user authorisation per project CLAUDE.md
ground rules.** Do NOT run the tag command without explicit user
go-ahead in the active session.

- [ ] **Step 1: Confirm verify-all is green**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected: exit 0.

- [ ] **Step 2: Confirm libkalburator HEAD is on the F2 final commit**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git log --oneline -1
```

The latest commit should be the doc-update from Task 48 or 49.

- [ ] **Step 3: Ask the user for tag authorisation**

Pause and confirm with the user. The tag command itself is:

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git tag -a v0.14-phase-f2-threading -m "$(cat <<'EOF'
Phase F2 — Threading API redesign — landed YYYY-MM-DD

QFuture-based public API on SyncEngine; cancellation via
QFuture::cancel() observed at every operation-handle boundary;
SyncBackend operation-handle standardisation (sync overloads
deleted); PlanStan + WildPalms migrated to the unified API.

Resolves FINDINGS:
- "SyncEngine::runSync(mappingId) is leaky"
- "Wrapper commit() lost error detection when switching from
  pushItems to storeItems"

Test counts at tag:
- libkalburator: 26+ pass (F1 23 + tst_engine_cancellation +
  tst_syncoperation_contract + per-test additions)
- PlanStan: 96/120 (matches Phase E baseline)
- WildPalms: 73/73 (modulo two known order-dependent flakes)

Refs:
- docs/phase0/04q-phase-f2-threading-design.md
- docs/phase0/04q-phase-f2-threading-plan.md
- docs/phase0/04k-engine-merger-roadmap.md
EOF
)"
```

- [ ] **Step 4: Verify the tag**

```bash
git tag -l 'v0.14*' -n
```

Expected: tag visible with the message above.

- [ ] **Step 5: Phase G is now next**

Update CURRENT-STATUS.md "Next" to "Phase G — Opaque transport +
plugin diff (`v0.15-phase-g-opaque-plugin`)".

---

## Self-Review Notes

The plan above covers the design's scope:

- **Group 0** (Tasks 1–3): SyncResult fields, cancellation test
  scaffolding, baseline confirmation. Maps to design's "Group 0
  Prep".
- **Group 1** (Tasks 4–13): SyncOperation contract + TranscodingPlan
  on pushItems across 8 concrete backends. Maps to design's
  "Group 1 Operation contract".
- **Group 2** (Tasks 14–30): QFuture API + cancellation propagation
  + 7 cancellation tests + smoke tests. Maps to design's "Group 2
  Engine async API".
- **Group 3** (Tasks 31–41): consumer migration across libkalburator
  tests + PlanStan + WildPalms. Maps to design's "Group 3 Consumer
  migration".
- **Group 4** (Tasks 42–46): cleanup deletions + baseline +
  CLAUDE.md + FINDINGS. Maps to design's "Group 4 Cleanup".
- **Group 5** (Tasks 47–50): doc + tag. Maps to design's "Group 5".

A few mid-plan tasks (24-29) use abbreviated step structures
because they all follow the same TDD pattern as Task 23 — the
implementer expands them by analogy. If a downstream agent
prefers fully expanded step lists for those tasks, the
brainstorm/plan author can be re-invoked to elaborate.

The total task count is 50, which is large but matches F2's scope
(operation-contract change across 8 backends + new public API +
7-case cancellation suite + 3-repo consumer migration + cleanup +
docs + tag). The natural mid-plan checkpoint, if needed, is
between Group 1 and Group 2 — at that point the operation-contract
and TranscodingPlan ramp could ship as a separate intermediate
tag (`v0.13.1`) without breaking anything, and the QFuture/
cancellation work continues separately. The design is shaped so
this split is non-breaking.
