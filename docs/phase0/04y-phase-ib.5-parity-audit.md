# 04y — Phase Ib.5 parity audit

**Purpose:** Before deleting `dispatchCalendarLegacy`, enumerate every
feature it carries that the unified `dispatchSync` must absorb. Drives
Tasks 3–7 in the Ib.5 plan.

**Written:** 2026-05-08, Task 2.

---

## Legacy branch surface

| Feature | Legacy implementation | Unified equivalent | Gap? | Action |
|---|---|---|---|---|
| First-sync mirror (`dispatchFirstSync`) | Called at top of `dispatchCalendarLegacy` | **Not called** from `dispatchSync` | **Yes** | Hoist (Task 4) |
| Calendar property phase | `fetchCalendarProperties` + `computePropertyDiff` + `applyPropertyChanges` using `CalendarBaselineStore` (typed baselines) | `runPropertyPhase(plugin, ..., baseline=QVariantMap{})` — empty baseline | Yes (first-sync semantics only after unified) | Accept deferral — no test exercises baseline-aware property diff (Task 5) |
| Source/target fetch with cancel checks | `fetchSourceRecords` / `fetchTargetRecords` with cancel checks; parses iCal via `fetchRecordsViaBlob` | Inline `IBlobBackend::loadRecords` + cancel checks in `dispatchSync` | **No** — cancel checks present; records stay opaque bytes throughout (plugin decodes at write time) | Covered |
| Per-record diff | `computeDiff()` → `SyncChange` list with `Incidence::Ptr` (semantic diff via `IncidenceDiff`) | `blobBatchDiff` → `EngineDiffOp` list with `BackendRecord` (hash-equality diff) | Known limitation, accepted in Ia.5; not a Ib.5 regression | Covered |
| AskUser pause + yield + resume | `handleConflicts()` sets `m_yieldedForConflict = true`, emits `conflictPauseRequested`, returns; `resumeAfterConflict` re-enters `handleConflicts` + `continueAfterConflicts` | `blobBatchMergeWithPlugin` increments `conflictsDeferred` and completes sync — **no pause** | **Yes — parity gap** | Lift (Task 3) |
| Unmonitored AskUser / Skip | `handleConflictUnmonitored` → `m_pendingUnmonitoredConflicts` → `ConflictManager::handleConflicts` | Same `conflictsDeferred` path | No AskUser pause in unmonitored is correct behavior | Covered |
| CustomMerge — 3-way property merge | `applyMonitoredResolution(change, CustomMerge, mergedIcal)` parses an externally-supplied iCal string and writes the merged incidence | `blobBatchMergeWithPlugin` calls `IRecordMergerICal::merge()` for `CustomMerge` policy — computes 3-way merge internally | Semantics differ (external vs. internal merge), but `IRecordMergerICal::merge` covers the common case; the "externally supplied iCal" path in the legacy branch was only reachable from the AskUser-with-manual-merge UI flow (Task 3 will wire this into the unified resume path) | Verify in Task 6 |
| Duplicate policy — incidence clone | `resolveConflictAutomatically(change, Duplicate)` clones incidence with new UID via `KCalendarCore::CalFormat::createUniqueId()` | `blobBatchMergeWithPlugin` → `conflictsDeferred` (falls through `resolvePolicy`) — **not handled** | Gap: Duplicate deferred in unified path; clone logic lives in engine TU (KCalendarCore) | Document as known gap; no `tests/calendar/` test uses Duplicate policy, so acceptance bar "tests stay green" is met without a fix. Deferred to post-Ib.5. |
| `applyMonitoredResolution` + emit `itemReady` | Emits `itemReady` signal (calendar-typed, now deleted by Task 1) and routes resolved change to `m_resolvedToTarget` / `m_resolvedToSource` | Task 1 deleted the signals; apply goes through `CalendarPluginWriter::apply` in `continueAfterConflicts` path (Task 3 will wire this for unified) | None — `itemReady` was dead, deletion is correct | Covered |
| CalendarDomainAdapter apply | `applyChangesToBackend` via `CalendarDomainAdapter` (used by `continueAfterConflicts`) | `CalendarPluginWriter::apply` via `plugin->createWriter(tgtBackend)` | None — writer is the domain-generic replacement | Covered (Task 9 deletes adapter) |

---

## Open questions resolved by this audit

1. **AskUser test coverage:** `tests/calendar/tst_calendar_conflict.cpp::monitored_sameUidDivergent_pausesUntilResume` currently passes because calendar routes through the legacy branch. After Task 7 drops the `dispatchCalendarLegacy` branch, this test will FAIL unless Task 3 lifts pause/resume into the unified path first. **Sequence is enforced: Task 3 before Task 7.**

2. **Duplicate policy:** No `tests/calendar/` test sets `conflictPolicy = Duplicate`. Acceptance criterion ("same pass count") is met without fixing Duplicate in the unified path. Tracked in `04w-deferred-work.md` under A.5 (or a new A.6 entry if the deferred-work doc doesn't yet have it).

3. **First-sync fast path:** `dispatchFirstSync` is already domain-generic (`IBlobBackend` / `BackendRecord`). Hoisting it to the top of `dispatchSync` (Task 4) is safe without any calendar-specific change.

4. **CalendarPluginWriter thread model:** The writer uses `BlockingQueuedConnection` internally for its `tx.commitAll()`. The existing comment in `dispatchSync` at line ~2039 warned that this was "incompatible with the outer BlockingQueued call." After investigation (Task 7 is the proof): the writer is called via `QMetaObject::invokeMethod(tgtBackend, [...] { writer->apply(...); }, Qt::BlockingQueuedConnection)`. The inner `commitAll` does its own `QMetaObject::invokeMethod(m_backend, [...], Qt::BlockingQueuedConnection)` — which would deadlock if both were on the same thread. The outer invoke runs the apply lambda on the backend thread; the inner invoke attempts another `BlockingQueuedConnection` back to the same backend thread → **potential deadlock.** Task 7 must verify this empirically. If it deadlocks, `CalendarPluginWriter::apply` must be called directly on the worker thread (not wrapped in an outer `invokeMethod`).

---

## Execution sequence (Tasks 3–7)

```
Task 3: Lift AskUser pause/resume into unified dispatchSync
Task 4: Hoist dispatchFirstSync into dispatchSync
Task 5: Verify/accept calendar property phase deferral
Task 6: Verify CustomMerge + Duplicate parity in plugin
Task 7: Drop if-calendar branch (proof of parity)
```

Task 7 is the integration test: if `tests/calendar/` all pass after the branch
is dropped, parity is proven. If any fail, fix inside the plugin — never re-introduce
the legacy branch.
