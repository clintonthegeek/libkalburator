# Phase F2 — Threading API redesign — outcome report

**Date written:** 2026-04-30
**Tag:** `v0.14-phase-f2-threading` on libkalburator HEAD `632ab70`
**Scope:** 50 commits since `v0.13-phase-f1-unify`, all on `refactor/engine-merger`
**Verify-all:** green. libkalburator 26/26, PlanStan 96/120 (Phase E baseline), WildPalms 73/73 (modulo two known order-dependent flakes documented in FINDINGS).

This is the as-shipped synthesis report. The design doc (`04q-phase-f2-threading-design.md`) captures intent; the plan (`04q-phase-f2-threading-plan.md`) captures the task-level breakdown; this document captures **what actually shipped, why some things deferred, and what each consumer's perspective looks like**.

---

## What F2 set out to do, and what actually shipped

The roadmap framed F2 as the **threading-API pivot** on top of F1's stable unified engine. Five pieces were on the table:

| Roadmap goal | F2 outcome |
|---|---|
| (a) `QFuture<SyncResult>` public API | ✅ Shipped as `runSyncFuture(...)` (the `Future` suffix is transitional naming; rename to `runSync` deferred for cosmetic-only churn reasons) |
| (b) `QFuture::cancel()` propagation with TDD tests | ✅ Shipped end-to-end with 7 cancellation cases (C1–C7) plus 3 of 4 positive smoke tests |
| (c) `SyncBackend` operation-handle standardisation; sync overloads deleted | ⚠️ **Partial.** Standardisation shipped; **deletion deferred** because consumers still depend on the synchronous methods |
| (d) Concurrent-mapping execution | Deferred (was already deferred at design stage; F2.1 or G) |
| (e) Conflict pause/resume kept verbatim, plus cancellation hook | ✅ Shipped (state-machine yield wired to cancellation channel) |

The single load-bearing principle from the design — **"one engine, one threading contract, all consumers adapt"** — held for PlanStan but **broke for WildPalms**: the F1 transitional facade `runBlobTwoWay`/`runBlobMirror` was supposed to retire in F2 but cannot, because `BlobDomainAdapter` isn't yet registered for unified dispatch. That's a Phase G prerequisite that the F2 plan didn't anticipate. Documented; F1 facade survives until Phase G.

---

## Angle 1 — libkalburator's internals

This is where the bulk of F2's work lives. The changes break into five layers, each with its own contract.

### Layer 1: `SyncOperation` contract (`src/calendar/syncoperation.{h,cpp}`)

The base class for `FetchOperation`, `PushOperation`, `DeleteOperation` was half-finished pre-F2 — it had a `State` enum and signals but inconsistent semantics. F2 locked it down (Task 4, commit `d2831fc`):

- **5-state enum**: `Pending / Running / Succeeded / Failed / Cancelled`
- **`m_state`** promoted to `std::atomic<State>` with acquire/release ordering
- **`m_cancelRequested`** new `std::atomic<bool>`, set by `cancel()`
- **`setState(State)`** rewritten as a CAS loop, idempotent on terminal-to-terminal transitions (the old non-idempotent body would emit `finished` twice on double-cancel)
- **`setError(QString)`** new protected helper: writes error string + transitions to `Failed` atomically
- **`virtual cancel()`** sets `m_cancelRequested` first, then preserves the eager `setState(Cancelled)` flip for backward compat with backends that poll `state() == Cancelled` (eager flip becomes a no-op once those backends migrate to polling `cancelRequested()`)
- **`cancelRequested() const noexcept`** new protected accessor
- **New `started()` signal** (was missing) emitted exactly once on `Pending → Running`
- **`isFinished()`** returns true for any terminal state (was inconsistent before)

A new TDD test `tst_syncoperation_contract.cpp` (8 cases) pins this contract.

The deviation from the original spec: `setState` stayed **public** (not protected) because 5 backends call it externally; making it protected would have broken them. The F2 design's intent is satisfied either way (idempotency + correct signal emission).

### Layer 2: `SyncBackend` I/O surface (`src/calendar/syncbackend.{h,cpp}`)

**Goal:** retire the synchronous `loadItems` / `storeItems` / `updateItem` / `writeFinished` and standardise on the operation-handle pattern.

**What landed:** the new shape (Tasks 5–13). All 8 concrete backends now override a 3-arg `pushItems(QString, QList<Incidence::Ptr>, TranscodingPlan) → PushOperation*` (commits `1f8418d` through `057ba5f`):

- `MockBackend` (Task 6, `b451e0e`) — also resolves the FINDINGS asymmetry where `OnPush` and `OnStoreItems` were checked differently across the sync and async paths. Now one path, one symmetric check
- `LocalBackend`, `RemoteBackend`, `OrgBackend`, `AkonadiBackend`, `DecSyncBackend`, `HolidaySubscriptionBackend`, `SubscriptionBackend` — same pattern: extract transcoding into the upfront `executeTranscodingPlan` helper, run the existing logic on `finalItems`

The 2-arg `pushItems(id, items)` shim survives during the ramp; it delegates to the 3-arg form. `OrgBackend` and `AkonadiBackend` are gated by their `KALBURATOR_HAVE_*` flags; their migration was applied but not locally compile-tested due to missing system deps on the dev machine. Verify-all caught no breakage.

**What did NOT land (deferred):** Task 43 was supposed to delete the synchronous methods. It hit a scope wall — `~180 call sites` across 10 PlanStan default-built test files (`tst_orgbackend.cpp`, `tst_decsyncbackend.cpp`, `tst_backend_signals.cpp` which spies `&SyncBackend::writeFinished` directly, `tst_remotebackend.cpp`, etc.) plus `CollectionController::convertCalendarToBackend` plus WildPalms's `palmcalendarbackend` still depend on `storeItems`/`updateItem`/`writeFinished`. The synchronous methods remain on `SyncBackend` but are deprecated in spirit. Deletion is a dedicated follow-up task before Phase G can finalize the API surface.

### Layer 3: `SyncEngine` public API (`src/engine/syncengine.{h,cpp}`)

**The headline change.** Pre-F2 the engine exposed:
- Two void `runSync(...)` overloads
- `cancelSync()` slot
- `syncCompleted(mappingId, result)` signal per mapping
- `allSyncsCompleted(aggregate)` signal at end of queue
- Synchronous `runBlobTwoWay(...)` / `runBlobMirror(...)` facade for blob workflows (F1 transitional)

Post-F2:
- **`runSyncFuture(mappingId, behavior) → QFuture<SyncResult>`** for single-mapping
- **`runSyncFuture(behavior) → QFuture<QList<SyncResult>>`** for multi-mapping
- Cancellation via `QFuture::cancel()` only — no public `cancelSync()`
- Streaming signals (`progressChanged`, `phaseChanged`, `conflictDetected`, etc.) preserved — they're orthogonal to completion
- Completion signals (`syncCompleted`/`allSyncsCompleted`) deleted (Task 42, commit `cc8d94e`)
- `runBlobTwoWay`/`runBlobMirror` still exist (Phase G deferral, see below)

The internal architecture changed substantially:

- **Worker stays on its own QThread** (F1 invariant preserved)
- **New `DispatchMode { None, Single, Queue }`** engine-side enum (Task 21, commit `35c1881`). Dispatched modes drive the worker's per-mapping completion handler — the leaky `processNextMapping` that ran from index 0 regardless of how it was invoked is gone. `processSingleMapping` and `advanceQueue` are now distinct paths with no shared mutable index. The bug class is structurally impossible
- **`m_currentSingleIface` / `m_currentMultiIface`** on the engine; `runSyncFuture` allocates the `QFutureInterface` on the heap, sets `setAddResultsIfCanceledEnabled(true)` so the cancellation-marker `SyncResult` survives the cancel, populates it via worker callbacks, deletes it after `reportFinished`
- **`processSingleMapping` cancel-precheck** (Task 23 follow-up, commit `b4cd6af`): if `m_cancelled` is true at entry (caller invoked `QFuture::cancel()` immediately after `runSyncFuture` returned), short-circuit with a `cancelled=true, skipped=true` result before even looking up the mapping
- **`onWorkerSyncCompleted` mode-aware** (Task 21): single-mapping branch finishes the future + clears `m_dispatchMode`; queue branch advances. Walking the call graph: `runSyncFuture(mappingId)` → set DispatchMode::Single → dispatch one Request → worker emits `syncCompleted` → mode-dispatch finishes the future and returns. Never reaches `advanceQueue`. The FINDINGS leak path is unreachable

### Layer 4: cancellation channel (the load-bearing F2 work)

End-to-end, every component participates:

```
caller thread:                                    worker thread:
  QFuture::cancel()                                 (in event loop)
   ↓ (sync, atomic flag in QFutureInterface)
  QFutureWatcher::canceled fires
   ↓ (engine-thread slot, AutoConnection direct)
  SyncEngine::onCancelObserved()
   ↓ QMetaObject::invokeMethod(QueuedConnection)
                                                    SyncEngineWorker::observeCancel()
                                                     ↓ m_cancelled.store(true, release)
                                                     ↓ emit cancellationObserved()
                                                       (DirectConnection on this)
                                                    ┌── any nested QEventLoop wakes:
                                                    ├── await<Op>'s inner loop → calls op->cancel()
                                                    │   → eager state flip + atomic flag
                                                    │   → if op not finished, brief teardown loop
                                                    │   → returns to worker hot path
                                                    ├── conflict-pause yield wake
                                                    │   → onCancelDuringConflictPause clears
                                                    │     m_yieldedForConflict, populates result,
                                                    │     emits syncCompleted (legacy signal),
                                                    │     leaves conflict in SyncConflictStore for
                                                    │     next run
                                                    └── per-record check via CancelOracle in
                                                        CalendarDomainAdapter::applyChangesToBackend
                                                        (Task 19): drops in-flight SyncTransaction
                                                        without commit
```

Components that contributed (commits in chronological order):

- **`m_cancelled` upgrade** to `std::atomic<bool>` (Task 14, commit `6e24150`) — was a plain `bool` under mutex; upgraded in-place because the existing mutex-guarded writers still work via implicit conversion
- **`runSyncFuture` shim** (Task 15, commit `4f1a30e`) — initially a fragile heap-allocate-iface + lambda-captured-disconnect pattern; later rewritten in Task 21 with proper engine-side iface ownership
- **Defensive null-guard on the lambda** (Task 15 follow-up, commit `c08a3cb`) — the FINDINGS leak meant `syncCompleted` could fire twice for the same mappingId, which would have been a use-after-free on the captured iface. Added `mutable` lambda + null-guard to close the window. The proper fix landed with Task 21's worker-driver split, but the defensive guard kept the build correct in the interim
- **`await<Op>` template helper** (Task 16, commit `1a0606d`) — defined in the header (template instantiation), `static_assert(std::is_base_of_v<SyncOperation, Op>)`, runs an inner `QEventLoop` connected to both `op->finished` AND `cancellationObserved`; on cancel, calls `op->cancel()` then re-enters a brief teardown loop. The load-bearing primitive
- **`QFutureWatcher` install** (Task 17, commit `43b971b`) — closes the engine-side half of the channel
- **Apply-path migration** (Tasks 18 → 35, commit `4a92955`) — the original Task 18 was a no-op because the apply path doesn't directly call backend write methods; it goes through `CreateIncidenceItem::commit()` and `UpdateIncidenceItem::commit()` wrappers. Task 35 migrated those wrappers to `pushItems` + operation-handle observation, replacing the fragile temporary-`connect`-to-`writeFinished` pattern that Phase E had introduced. **`DeleteIncidenceItem` was already on the operation-handle pattern** — no migration needed there
- **Per-record `CancelOracle`** (Task 19, commit `6d62c65`) — `std::function<bool()>` installed on `CalendarDomainAdapter` by `SyncEngine` at construction time, captures `this` (engine) and reads `m_worker->isCancelled()` via the new public accessor. The adapter checks it at the top of every per-record iteration in `applyChangesToBackend`. Critical: dropping a partially-built `SyncTransaction` is verified safe (RAII destructor, no signal emissions, no journal writes — confirmed by tracing every relevant destructor)
- **Conflict-pause cancellation slot** (Task 20, commit `c0700bc`) — discovered the conflict pause is **not** a `QEventLoop` (the original task description assumed it was); it's a state-machine yield via `m_yieldedForConflict` + `conflictPauseRequested` emission. The implementer adapted the wiring shape: a self-connected `cancellationObserved → onCancelDuringConflictPause` slot. Both racy interleavings (cancel-then-resume, resume-then-cancel) were walked through and verified safe
- **Worker driver split** (Task 21, commit `35c1881`) — the deepest restructuring. Split the queue iterator engine-side (because the per-mapping body was already extracted into `SyncEngineWorker::processSync(Request)` by F1 Task 8). The structural fix is engine-side: `processSingleMapping` and `advanceQueue` are independent; no shared mutable state. Plus a follow-up `m_isSyncing` cleanup fix (Task 21 follow-up, commit `873c2e7`)

### Layer 5: tests

- **New unit test:** `tst_syncoperation_contract.cpp` (Task 4, 8 cases pinning the SyncOperation contract)
- **New unit test:** `tst_mockbackend_failure_injection.cpp` (Task 6, 4 cases pinning the unified failure injection)
- **New integration test:** `tst_engine_cancellation.cpp` (Tasks 2, 23–29, 11 cases total)
  - C1 cancel-before-start
  - C2 cancel-during-fetch (uses MockBackend's blockable fetch fixture)
  - C3 cancel-during-apply (uses target-fetch blocking; see "found gaps" below)
  - C4 cancel-during-conflict-pause
  - C5 cancel-multi-mapping-mid-queue
  - C6 idempotent cancel
  - C7 cancel after finished
  - 3 of 4 positive smoke tests; `progressValueTicks` QSKIPped because the engine emits `progressUpdated` signals but doesn't call `setProgressValue` on the iface (Qt6 distinction not anticipated in the design)
- **Existing tests migrated to QFuture pattern** (Tasks 31–32, 8 commits): all 7 `tst_calendar_*.cpp` tests in `tests/calendar/`. Pattern: replace `runSync(behavior); waitForSignal(allSyncsCompleted)` with `auto future = runSyncFuture(behavior); QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000); future.resultAt(0)` (not `future.results()` — Qt6 quirk)
- **Calendar boundary test** (`tst_engine_unified_boundary.cpp`) had its calendar path migrated; the blob path uses `runBlobMirror` which is Phase G work

### Found gaps (production bugs surfaced by F2 testing)

Three real production gaps were surfaced and closed during F2 implementation, each by a TDD test that wouldn't pass without the fix:

1. **`processSingleMapping` missing cancel-precheck** (Task 21 plan called for it; the actual commit `35c1881` left it out). Surfaced by C1; closed in commit `b4cd6af`. Without this, cancellation that arrived BEFORE the worker dispatched was missed
2. **`QFutureInterface::reportResult` silently dropped after `reportCanceled()`** unless `setAddResultsIfCanceledEnabled(true)` is opted in (per Qt6 docs but not anticipated in the design). Closed in same commit
3. **Worker checkpoints didn't decorate cancelled results with `cancelled=true, skipped=true`** — they emitted `syncCompleted` with `success=false, errorMessage="Cancelled"` but the new boolean flags were not set. Closed in same commit

A fourth hard limitation discovered: **`QFuture::results()` returns empty after `cancel()` regardless** of `setAddResultsIfCanceledEnabled` — that flag only controls whether `reportResult` *stores* the result; reading via `results()` always checks `isCanceled()` first. Workaround: use `future.resultAt(0)` instead. **This is a Qt6 quirk consumers will hit** — documented in FINDINGS, the new CLAUDE.md, and inline in `tst_engine_cancellation.cpp`.

A fifth observation: **`MockBackend::setPushBlocking` is dead code under the current apply path** because the apply path uses synchronous `storeItems()` via `SyncTransaction`, not async `pushItems()`. The fixture is retained for Phase G when the apply path migrates to operation-handle-based async I/O. Documented in FINDINGS.

### Files touched in libkalburator

Engine: `src/engine/syncengine.{h,cpp}` (substantial rewrites in Tasks 14, 15, 16, 17, 19, 20, 21, 35, 42), `SyncResult` location at `src/types/synctypes.h`, `src/calendar/calendardomainadapter.{h,cpp}`, `src/calendar/syncoperation.{h,cpp}` (contract standardisation), `src/calendar/syncbackend.{h,cpp}` (3-arg pushItems base), 8 concrete backends, `src/calendar/createincidenceitem.{h,cpp}`, `src/calendar/updateincidenceitem.{h,cpp}`, `src/calendar/mockbackend.{h,cpp}` (failure injection symmetry + blockable fixtures).

Tests: 9 files in `tests/calendar/`, plus `tst_engine_cancellation.cpp` (new), `tst_syncoperation_contract.cpp` (new), `tst_mockbackend_failure_injection.cpp` (new), `tests/calendar/CMakeLists.txt`.

Docs: `CLAUDE.md` (threading API guidance updated), `docs/phase0/04q-phase-f2-threading-design.md` (Status line + scope adjustments), `docs/phase0/04k-engine-merger-roadmap.md` (F2 row marked landed), `docs/phase0/04q-phase-f2-threading-plan.md` (Status block + Task 43 BLOCKED note).

---

## Angle 2 — PlanStan

PlanStan is the calendar-app consumer. It exercises libkalburator's calendar engine through `SyncEngine` directly. The migration story is mostly clean.

### What changed (Tasks 36–38)

**`SyncProgressManager`** (`src/app/syncprogressmanager.{h,cpp}`, commit `0560b0b0`): the UI's progress-bar driver. Pre-F2 it connected to `SyncEngine::syncCompleted` (per-mapping) and `SyncEngine::allSyncsCompleted` (queue end) to drive UI state. Post-F2 it owns a `QFutureWatcher<QList<SyncResult>>` and connects to `QFutureWatcher::finished`.

**Architectural twist:** `SyncProgressManager` doesn't actually own the `runSyncFuture` call. `CollectionController` does — it's the layer that decides when a sync runs. The migration introduced a `CollectionController::syncRunStarted()` signal carrying the new future, plus a `CollectionController::currentSyncFuture()` accessor. `SyncProgressManager::watchFuture(QFuture<...>)` is the new API; MainWindow wires the two together. This matches PlanStan's existing layering — the controller orchestrates; the progress manager observes — and avoids putting `runSyncFuture` ownership in two places.

**`MainWindow`** (`src/app/mainwindow.cpp`, commit `a1a629bf`): had two direct subscriptions to `SyncEngine::allSyncsCompleted` for application-level reactions to sync completion. Both replaced with a subscription to the new `CollectionController::syncRunFinished(SyncResult)` signal that `CollectionController` emits from inside its own `QFutureWatcher`'s `finished` slot.

**`CollectionController`** (`src/controllers/collectioncontroller.{h,cpp}`, same commit): owns the `QFuture` lifecycle. Calls `runSyncFuture(behavior)`, holds the watcher, emits `syncRunStarted()` (so SyncProgressManager can attach) and `syncRunFinished(SyncResult)` (so MainWindow can react).

**`cancelSync` references**: `cancelSync()` was unused in PlanStan src/ at the time of migration (only existed in test fixtures and comments). Cancellation in PlanStan UI is now `currentSyncFuture().cancel()`.

**EXCLUDE_FROM_ALL sync-workflow tests** (Task 38, 4 commits):
- `tst_sync_conflicts.cpp` (`ca6ac460`) — 123/123 sub-tests pass post-migration
- `tst_sync_caldav_conflicts.cpp` (`1b63723b`) — 9/10 sub-tests pass; the 1 pre-existing failure (`testCalDavETagConflict`) was already in the Phase E baseline
- `tst_sync_error_recovery.cpp` (`103f6f67`) — 22/22 sub-tests pass
- `tst_sync_dialog.cpp` (`47b404ec`) — 8/8 sub-tests pass

These tests are `EXCLUDE_FROM_ALL` per FINDINGS — they need explicit build targets and don't run as part of default ctest. The 24 PlanStan baseline "failures" per F.0 triage continue to be 22 not-built tests + 2 environmental failures; F2 didn't change that count.

**`tst_synctransaction.cpp`** was already on the operation-based async API (no `storeItems`/legacy-API calls remained); no migration needed.

`SyncTestHelper::waitForSyncComplete` — a dead helper that wrapped the deleted `allSyncsCompleted` signal — was removed (commit `98be4159`).

**Aside on commit prefix:** PlanStan commits use `fix(sync):`, `fix(app):`, `test(sync-workflow):`, `test(sync):` prefixes per the project's existing convention.

### What did NOT change (and why)

**~180 backend-test call sites** still use synchronous `storeItems`/`updateItem` and spy on `&SyncBackend::writeFinished` directly. Files affected:

| File | Approximate hits |
|---|---:|
| `tests/backends/tst_orgbackend.cpp` | 65 |
| `tests/backends/tst_decsyncbackend.cpp` | 32 |
| `tests/backends/tst_backend_signals.cpp` | 29 |
| `tests/backends/tst_orgbackend_external.cpp` | 19 |
| `tests/backends/tst_remotebackend.cpp` | 14 |
| `tests/backends/tst_localbackend.cpp` | 9 |
| `tests/backends/syncbackend_test_framework.h` | 6 |
| `tests/sync/tst_sync_directions.cpp` | 4 |
| `tests/integration/tst_calendarcrud.cpp` | 1 |
| `tests/localbackend/tst_localbackend.cpp` | 1 |

These are unit tests that exercise libkalburator's concrete backends directly, bypassing the engine. They test the synchronous write API contract directly. Migrating them means rewriting each call site to use `pushItems` returning a `PushOperation*` and observing `op->state()` / `op->errorString()`, plus rewriting the `&SyncBackend::writeFinished` signal-spy code to `&SyncOperation::finished`.

This is mechanical but voluminous. The F2 plan didn't anticipate this scope (it focused on engine-level consumers). It's now Task 43's prerequisite — once these tests are migrated, the synchronous methods on `SyncBackend` can finally be deleted.

**One production caller** at `src/controllers/collectioncontroller.cpp:1463`: `targetBackend->storeItems(workingCal, incidences)` inside `convertCalendarToBackend`. A single call site, but it's production code — needs operation-handle migration with proper error reporting before the synchronous methods can go.

This is captured as the "Task 43 follow-up" in FINDINGS and CURRENT-STATUS.

---

## Angle 3 — WildPalms

WildPalms is the Palm OS HotSync consumer. It synchronizes Palm device data through libkalburator's blob workflows, NOT the calendar engine. The migration story is essentially "we couldn't, and now we know why."

### What changed

**Nothing in WildPalms itself.** WildPalms commits this phase: zero. Test counts unchanged (73/73, modulo two pre-existing flakes).

The original F2 plan called for migrating WildPalms's `SyncRunner` (`src/runtime/syncrunner_wp.cpp`) and its 4 `tst_*_v2.cpp` test files (plus `tst_palmbackend_roundtrip.cpp`) from the F1 transitional facade `engine.runBlobTwoWay(...)` / `runBlobMirror(...)` to `engine.runSyncFuture(id).waitForFinished()`. Tasks 39–40 were that migration. Task 33's libkalburator-side investigation (the same migration applied to libkalburator's own blob-test files) **found the structural blocker** that also applies to WildPalms.

### Why it didn't migrate (the structural finding)

`runBlobTwoWay` and `runBlobMirror` are documented at `syncengine.h:540-545` as F1-transitional and explicitly **synchronous on the calling thread, never touching the worker**. They take backend pointers and run a one-shot blob sync inline.

`runSyncFuture(mappingId)`, by contrast, dispatches to the worker, which runs the calendar pipeline (`fetchCalendarProperties`, `computePropertyDiff`, `applyPropertyChanges`, then calendar diff/apply). A blob-backed `SyncMapping` submitted via `runSyncFuture` would hit `fetchCalendarProperties` on a non-calendar backend and fail.

**The unification piece is missing.** `BlobDomainAdapter` exists and is used internally inside `runBlobMirror` / `runBlobTwoWay` (lines `1297-1298` and `1358-1360` of `syncengine.cpp` for record fetching). But it's NOT registered with the engine for dispatch — there's no `m_domainAdapters` map keyed by domain type or any mechanism that would route a blob-typed `SyncMapping` through the worker via `runSync`. That registration is **Phase G's scope**: "engine deals only in `BackendRecord`; calendar diff/merge becomes an `IRecordDiffer`/`IRecordMerger` registered for type 'calendar'".

The F2 design's vision of "one engine, one threading contract, both consumers adapt" was correct as direction but premature: it required Phase G's adapter-registration infrastructure that Group 1 didn't add. Phase G adds it; until then, WildPalms keeps its existing usage of `runBlobTwoWay`/`runBlobMirror`.

### What stays as-is in WildPalms

- `WildPalms/src/runtime/syncrunner_wp.cpp` — 3 call sites of `runBlobTwoWay`/`runBlobMirror`, unchanged
- `WildPalms/tests/palmsync/tst_palmbackend_roundtrip.cpp` — unchanged
- `WildPalms/tests/palmsync/tst_palmdevice_roundtrip.cpp` — unchanged
- `WildPalms/tests/plugins/{memo,todos,contacts,calendar}/tst_*_v2.cpp` — unchanged
- `WildPalms/tests/plugins/webcalendar/tst_webcal_v2_e2e.cpp` — unchanged
- `WildPalms/tests/plugins/plucker/tst_plucker_v2_e2e.cpp` — unchanged

**`WildPalms/src/palm/calendar/palmcalendarbackend.{h,cpp}`** is a special case. It's a calendar-backend implementation in WildPalms (Palm device's built-in calendar acts as a calendar source in Palm Desktop scenarios). It overrides `loadItems`/`storeItems`/`updateItem` and emits `writeFinished` directly. The header already comments these as "minimal stubs" but they're not removable while the libkalburator base class declares them. Task 43's deletion blocked here too.

### Implications

WildPalms in its current state is **insulated** from F2's library-side work. Builds clean against new libkalburator. Tests pass at baseline. WildPalms's HotSync conduit framework continues to call the F1 transitional facade; cancellation of those calls is not exposed to F2's `QFuture::cancel()` channel (the facade is synchronous on the caller thread; if WildPalms wants cancellation, it would need to use whatever synchronous-cancel mechanism the existing code provides).

This is the price of F2 shipping without Phase G's adapter-dispatch infrastructure. The F2 plan should have flagged this; it didn't. The implementer flagged it during execution; it's now documented in the F2 design doc, ROADMAP, CURRENT-STATUS, and a marker commit (`f082436`).

---

## FINDINGS — resolved and added by F2

### Resolved (all marked `[RESOLVED in v0.14 — see commit SHA]`)

1. **"`SyncEngine::runSync(mappingId)` is leaky"** (originally 2026-04-28; resolved by Task 21 commit `35c1881`). The single-mapping form double-ran because `processNextMapping` iterated from index 0. Structurally fixed by the engine-side driver split: `processSingleMapping` and `advanceQueue` no longer share mutable state; `onWorkerSyncCompleted` is mode-aware and never re-enters the queue from the single-mapping branch
2. **"Wrapper `commit()` lost error detection when switching from `pushItems` to `storeItems`"** (originally 2026-04-29; resolved by Task 35 commit `4a92955`). The fragile temporary-`connect`-to-`writeFinished` pattern in `CreateIncidenceItem::commit()` and `UpdateIncidenceItem::commit()` is gone — both wrappers now call `pushItems(id, items, plan)` and read `op->state()` / `op->errorString()` directly. The compiler now catches errors in this path
3. **"`MockBackend` missing failure injection on `updateItem` and `OnPush` in `storeItems`"** (originally 2026-04-29; structurally resolved by Task 6 commit `b451e0e`). The asymmetry is gone because there's only one path now — the 3-arg `pushItems` honours `OnPush || OnStoreItems` symmetrically. The fixup applied in commit `438e545` becomes redundant

### Added by F2

1. **"Qt6 `QFuture::results()` returns empty after `cancel()`"**. The `setAddResultsIfCanceledEnabled(true)` flag controls only whether `reportResult` *stores* the result; reading via `results()` always checks `isCanceled()` first and returns empty if true. Workaround: `future.resultAt(0)`. Engine-side mitigation: the iface is opted in so the marker `SyncResult` is at least stored, retrievable via `resultAt(0)`
2. **"Qt6 `QFuture::waitForFinished()` does not spin the test event loop"**. Subtle — the function blocks the calling thread, which means queued slots that need to run on that thread (e.g., `QFutureWatcher::finished` connected via `AutoConnection`) never fire. Workaround: `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), N)` which spins the loop while waiting
3. **"`MockBackend::setPushBlocking` is dead code under the current apply path"**. The apply path uses synchronous `storeItems()` via `SyncTransaction`, not async `pushItems()`. The blockable-push primitive is retained for the eventual Task 43 follow-up / Phase G migration
4. **"F2 Group 3 left ~180 PlanStan backend-test call sites + 1 production caller un-migrated"** — the Task 43 deferral context. Lists every affected file and call-site count

---

## Roadmap state after F2

```
| Phase | Status | Tag |
|---|---|---|
| D.0 — Tests-first | ✅ landed 2026-04-28 | v0.9-phase-d0-tests-first |
| D — Compose | ✅ landed 2026-04-29 | v0.10-phase-d-compose |
| E — Transcoding-into-backends | ✅ landed 2026-04-29 | v0.11-phase-e-transcoding-backends |
| F.0 — Test gap closure | ✅ landed 2026-04-29 | v0.12-phase-f0-test-gaps |
| F1 — Unify (engine + adapter) | ✅ landed 2026-04-30 | v0.13-phase-f1-unify |
| F2 — Threading API redesign | ✅ landed 2026-04-30 | v0.14-phase-f2-threading |
| F2-followup — sync-method deletion | ⬜ deferred | (no tag yet) |
| G — Opaque + plugin diff | ⬜ not started | v0.15-phase-g-opaque-plugin |
```

**Phase G's scope expanded** by the F2 deferrals:

- BlobDomainAdapter registration for unified dispatch (Tasks 33, 39, 40 absorbed)
- `runBlobTwoWay`/`runBlobMirror` deletion (Task 44 absorbed)
- WildPalms migration of `SyncRunner` + 8 test files (was Tasks 39–40)
- Likely WildPalms `palmcalendarbackend` updates as the SyncBackend interface evolves

**A pre-G follow-up** is needed to delete the synchronous SyncBackend methods (Task 43): roughly ~180 PlanStan backend-test call sites + 1 production caller migrated to operation-handle pattern, plus the `palmcalendarbackend` cleanup. This is mechanical but substantial work, comparable in size to F2 Group 3's PlanStan migration. It can ship as `v0.14.1-cleanup` if desired, or just folded into Phase G's preamble.

---

## What's safe and what's not

**Safe to use immediately:**

- `SyncEngine::runSyncFuture(mappingId, behavior)` and `runSyncFuture(behavior)` — the canonical sync entry points
- `QFuture::cancel()` — the canonical cancellation path; works in all 7 contract cases (C1–C7 verified deterministic 5/5)
- The 3-arg `pushItems(id, items, plan)` on every concrete backend
- `await<Op>` template helper inside engine code
- `CancelOracle` on calendar adapters

**Still works but deprecated** (kept for the test-call-site migration follow-up):

- `SyncBackend::storeItems(MemoryCalendar*, ..., TranscodingPlan)`
- `SyncBackend::updateItem(MemoryCalendar*, ..., TranscodingPlan)`
- `SyncBackend::writeFinished(calId, success, err)` signal
- `SyncBackend::loadItems` (already `[[deprecated]]` pre-F2)
- 2-arg `pushItems(id, items)` shim

**Phase G prerequisites (DON'T USE for new code paths):**

- `SyncEngine::runBlobTwoWay(...)` and `runBlobMirror(...)` — the F1 transitional facade. Survives until Phase G's BlobDomainAdapter registration

**Gone permanently:**

- `void runSync(SyncBehavior)` and `void runSync(QString, SyncBehavior)` — replaced by `runSyncFuture` overloads
- `void cancelSync()` — replaced by `QFuture::cancel()`
- `SyncEngine::syncCompleted(mappingId, result)` signal — replaced by `QFuture::resultAt(i)` + `QFutureWatcher::finished`
- `SyncEngine::allSyncsCompleted(aggregate)` signal — same

---

## Cross-references

- Design: `04q-phase-f2-threading-design.md` (sibling)
- Plan: `04q-phase-f2-threading-plan.md` (sibling)
- Roadmap: `04k-engine-merger-roadmap.md`
- F1 design (precedent for the worker-thread shape): `04p-phase-f1-unify-design.md`
- Coordination: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`, `~/dev/refactor-engine-merger/FINDINGS.md`, `~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt`
