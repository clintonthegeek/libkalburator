# Plan 1 — SyncEngine decomposition and Worker fold-resolution

**Audit refs:** B1 (CRITICAL)
**Depends on:** —
**Branch:** `feature/redress-1-syncengine`
**State:** Task-level detail. Tasks 1–3 may proceed concurrently; Tasks 4–6 sequenced.

## Goal

Resolve the incomplete `SyncEngine` + `SyncEngineWorker` unification (INVARIANTS §3) and
split the responsibilities the unified class accumulated (INVARIANTS §4), without
changing observable engine behaviour for PlanStan/WildPalms (INVARIANTS §10).

## Problem (from AUDIT B1)

- `engine/syncengine.cpp` is 2780 LOC; `engine/syncengine.h` is 840 LOC.
- `SyncEngineWorker` is declared **publicly** in `syncengine.h` (~lines 119–335) despite
  being intended as private impl after the F1 "fold". It holds a back-pointer `m_engine`
  and is invoked via `QMetaObject::invokeMethod(m_engine, "onWorkerSyncCompleted", ...)`
  — cross-class slot calls by string, not signals.
- Mixed responsibilities in one class: queue/orchestration, worker thread lifecycle,
  conflict pause/resume, baseline cache, mapping filter, resource scheduling.
- Public surface duplications: `SyncEngineWorker::Mode` vs `SyncEngine::SyncBehavior`
  (same semantics, two enums); four overloads of `runSyncFuture()` with an implicit
  state machine via `m_pendingOverride`.

## Approach

The decomposition is in two movements: **first resolve the fold** (Tasks 1–2 — decide
collapse vs separate, then execute that decision cleanly), **then split out the
collaborators that remain** (Tasks 3–6 — orchestration, worker management, state).

### Decision: full collapse, not separation

Per INVARIANTS §3, the half-fold is the actual smell. The original separation
(SyncCoordinator + SyncWorker + BlobSyncEngine) was retired because the boundary leaked;
re-separating now would re-create the leak. **The fold completes by making Worker
genuinely private.**

If during Task 1 the inspection reveals that Worker has a meaningful independent
lifecycle (it does not, per audit; verify), this decision flips and the plan re-opens.

## Tasks

### Task 1 — Establish the protective test (write first, see it fail)

Per INVARIANTS §6, the protective test runs before any code moves.

1. In `tests/engine/` (creating it if absent — check first) add
   `tst_syncengine_unification.cpp` with three integration scenarios using the existing
   stub-host harness (see `tests/calendar/stubs/`):
   - `multiMappingSequentialCompletesInOrder()` — exercises the queue path.
   - `conflictPauseResumeRoundTrip()` — exercises the worker's nested QEventLoop pause
     and the engine's `resumeAfterConflictResolution` slot.
   - `cancellationPropagates()` — `future.cancel()` reaches the worker, the conflict
     loop wakes, results land via `future.resultAt(0)`.
2. Each scenario uses `runSyncFuture(behavior)` + `QTRY_VERIFY_WITH_TIMEOUT(future.
   isFinished(), 5000)` per the established pattern. No `waitForFinished` (Qt6 quirk).
3. Run the tests against the unmodified engine and confirm they pass. They are the
   *contract* the decomposition must preserve, so green here is the baseline; the
   falsifiability proof in INVARIANTS §6 for this plan is that mutating the worker's
   pause behaviour or the engine's queue order causes the relevant test to fail. Verify
   by deliberately breaking each and observing red.

### Task 2 — Collapse Worker into private impl

1. Move the `SyncEngineWorker` class declaration **out of `syncengine.h`** entirely.
2. Create `engine/syncengine_p.h` (PIMPL-style private header) and place the
   `SyncEngineWorker` declaration there. Mark `SyncEngineWorker` as `final`.
3. Remove `SyncEngineWorker` from any exported header set. Confirm it is not used by
   PlanStan/WildPalms via cross-repo grep:
   ```
   grep -rn "SyncEngineWorker" ~/dev/PlanStan/src ~/dev/WildPalms/src 2>/dev/null
   ```
   If any consumer references it, **stop** and re-open the collapse-vs-separate decision
   in STATUS.
4. Replace every `QMetaObject::invokeMethod(m_engine, "slotName", ...)` call with an
   explicit signal connection set up at construction time:
   - Add `Q_SIGNALS: void syncCompleted(SyncResult)` etc. to `SyncEngineWorker` (or
     reuse existing signals).
   - In `SyncEngine::SyncEngine`, `connect(m_worker, &SyncEngineWorker::syncCompleted,
     this, &SyncEngine::onWorkerSyncCompleted, Qt::QueuedConnection)`.
   - Confirm zero remaining `invokeMethod(m_engine, ...)` calls in `syncengine.cpp`.
5. Remove the worker's back-pointer `SyncEngine *m_engine`. Dependencies the worker
   needs (`BaselineStore`, `SyncConflictStore`, `ConflictManager`) continue to flow via
   the existing `setDependencies()` injector.
6. Collapse the duplicated enums: delete `SyncEngineWorker::Mode`; the worker accepts
   `SyncEngine::SyncBehavior` directly.
7. Build, run all libkalburator tests. **The tests from Task 1 must pass unchanged.** If
   any test or downstream consumer breaks, the collapse is incomplete; fix before Task 3.

### Task 3 — Extract `MappingQueue` (orchestration collaborator)

The queue state currently sprawled across `m_currentMappingIndex`, `m_queueResults`,
`m_dispatchMode`, `m_mappingIdFilter`, `m_hasMappingFilter`, `m_lostResources`,
`m_currentMappingResult` becomes one collaborator.

1. Create `engine/mappingqueue.{h,cpp}` (or `engine/private/mappingqueue.h` if you
   prefer to keep all private-impl headers under a single directory).
2. `MappingQueue` owns the mapping iteration cursor, the per-run result accumulator, the
   filter set, and the lost-resource book-keeping. Public surface:
   - `void prime(QList<SyncMapping> mappings, SyncBehavior behavior, std::optional<QSet<QString>> filter)`
   - `std::optional<SyncMapping> next()`
   - `void recordResult(SyncResult)`
   - `QList<SyncResult> drain()`
   - `bool isExhausted() const`
3. `SyncEngine` consumes a `MappingQueue` member; the existing dispatch slot delegates
   queue mutation to it. No public API of `SyncEngine` changes.
4. Move the queue-related unit tests (if any standalone tests cover this state) under
   `tests/engine/tst_mappingqueue.cpp`.

### Task 4 — Consolidate `runSyncFuture()` overloads into one entry point

1. Define `struct SyncRequest { QList<QString> mappingIds; SyncBehavior behavior;
   std::optional<SyncBehavior> override; ... };` in `engine/syncrequest.h`.
2. Add `QFuture<QList<SyncResult>> runSync(const SyncRequest &request)` as the canonical
   public method.
3. Keep the four existing `runSyncFuture()` overloads as `[[deprecated]]` thin shims
   that construct a `SyncRequest` and call `runSync()`. Note the deprecation target in
   the `[[deprecated("Use runSync(SyncRequest). Removed in campaign Plan 8.")]]`
   message.
4. PlanStan and WildPalms callers continue to compile (gates per INVARIANTS §10);
   migration to `runSync(SyncRequest)` is a follow-up captured in the deprecation
   message. **Plan 8 deletes the deprecated overloads after migration completes.**

### Task 5 — Inline `m_pendingOverride` into the request

The per-call override stops being a class member and becomes an immutable field of the
`SyncRequest` constructed at `runSync()` entry. Delete `m_pendingOverride` and any
ad-hoc setter that mutates it mid-run. Re-run all engine tests.

### Task 6 — Confirm and close

1. Re-run the full test matrix:
   ```
   cmake --build build && (cd build && ctest --output-on-failure)
   ```
2. Re-run PlanStan tests (if reachable) — same pass set as before this plan.
3. Re-grep for `QMetaObject::invokeMethod(m_engine`, `SyncEngineWorker::Mode`,
   public `SyncEngineWorker` references — all zero.
4. Update STATUS "Locked decisions" with the collapse-vs-separate result and reasoning.
5. Update FINDINGS: cross out the B1-derived seed entries with the closing commit hash.
6. Open Plan 4 (which builds on this newly clean engine surface).

## Files affected

- `src/engine/syncengine.h` — Worker declaration removed; `runSyncFuture` overloads
  deprecated; new `runSync(SyncRequest)`; `m_pendingOverride` removed.
- `src/engine/syncengine.cpp` — `invokeMethod` calls replaced with signal connects;
  queue state moves to `MappingQueue`.
- `src/engine/syncengine_p.h` — **new**, Worker private declaration.
- `src/engine/syncrequest.h` — **new**, the request struct.
- `src/engine/mappingqueue.{h,cpp}` — **new**, queue collaborator.
- `tests/engine/tst_syncengine_unification.cpp` — **new**, protective tests.
- `tests/engine/tst_mappingqueue.cpp` — **new** if queue state had ad-hoc tests.

## Acceptance criteria

- `SyncEngineWorker` no longer appears in any non-`*_p.h` header.
- Zero `QMetaObject::invokeMethod(m_engine, ...)` cross-class slot calls by string.
- Worker has no back-pointer to engine.
- `SyncEngine::SyncBehavior` is the single mode enum.
- `runSync(SyncRequest)` is the canonical entry; overloads are `[[deprecated]]` shims.
- `tests/engine/tst_syncengine_unification.cpp` passes; full ctest passes.
- PlanStan ctest baseline passes against the changed headers.
- `syncengine.cpp` LOC is below 2000 (target — confirm at close).

## Risks

- **Cancellation race.** The pause/resume path uses a nested `QEventLoop`; the signal
  rewiring must preserve `Qt::QueuedConnection` semantics or cancellation tests will
  flake. Task 1's test must catch this; if it doesn't, the test is wrong before the code
  is wrong.
- **Downstream consumer of `SyncEngineWorker`.** Verify via grep before Task 2 step 3.
- **PlanStan calls a deprecated overload.** Acceptable for this plan; the
  `[[deprecated]]` warning is the signal for the downstream migration. The shim does not
  change behaviour.

## Estimated effort

3–5 focused sessions. Task 1 is the longest single piece; the rest is mechanical once
the protective tests exist.
