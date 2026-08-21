# Conflict-resolution repair — plan

**Branch:** `feature/conflict-resolution-repair` (from `main` @ `b0bf3d5`, v0.97)
**Source of truth for the defects:**
`docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-handoff.md`
**Opened:** 2026-08-21

Four defects, all in the same conflict code, all with the same origin (the
canon-upgrade campaign promoted `BackendRecord::data` to canonical Shape JSON
inside `dispatchSync()`, and the conflict code was never taught about it).

| # | Defect | Site |
|---|--------|------|
| A | `ConflictInfo::source/targetIcalData` carry canonical JSON, not native iCal | `unifiedHandleConflicts()` both branches, `syncengine.cpp:3273`/`:3299` |
| B | A resolution chosen in `Unmonitored` mode is never applied to any data | `conflictmanager.cpp` store-only paths + `SyncEngine::onWorkerSyncCompleted` |
| C | `resumeAfterConflict(resolution, mergedIcal)` never reads `mergedIcal` — `CustomMerge` silently runs the auto-merger instead | `syncengine.cpp:2105`, `CustomMerge` case |
| D | `Duplicate` rewrites the clone's uid with `data.replace("UID:"+id, ...)`, which never matches canonical JSON (`"uid":"…"`) — the clone keeps the original uid and collides | `syncengine.cpp:2152` |

D is PlanStan's separately-filed
`docs/bugs/sync-dialog-keepboth-duplicate-not-created.md`; it closes with this work.

## Locked decisions (user, 2026-08-21)

1. **Bug B mechanism = resolution injection into the next run.** Persist the
   resolution, hand it to the next `dispatchSync()` for that mapping, apply it
   inline in the `AskUser` branch through a helper extracted from
   `resumeAfterConflict()`'s `switch`. The existing write / transcode /
   baseline / host-notify path is reused verbatim — **no second write
   mechanism** (campaign INVARIANTS §1). Rejected: giving `SyncEngine` its own
   write-path access.
2. **Auto follow-up run.** Once a run reaches its terminal state, `SyncEngine`
   starts one targeted run for mappings that acquired pending resolutions,
   guarded against re-entry. Consumers need no change.
3. **Stale resolutions are discarded, not applied.** Compare the live records'
   `lastModified` against what was recorded at detection; on mismatch drop the
   stored resolution, `qWarning()`, and present a fresh conflict.
4. **Scope includes C and D**, plus explicit test coverage of
   `ConflictManager`'s `Deferred` and `AutoResolve` modes (not just coverage
   inherited from the Bug B mechanism).

## Task sequence

Tasks are **sequential** — 1 and 2 edit adjacent regions of `syncengine.cpp`
and 3 depends on the helper 2 extracts.

- **Task 1 — Bug A. DONE 2026-08-21.** Stashed `m_unifiedSrcToCanon` /
  `m_unifiedCanonToSrc` / `m_unifiedCanonToTgt` next to `m_unifiedCanonical`;
  both `ConflictInfo` sites collapsed onto one
  `SyncEngineWorker::buildConflictInfo(op)` that demotes each side back to
  its backend's native encoding; empty data guarded (modify-delete);
  `sourceEncoding`/`targetEncoding` added additively (transport-only — the
  conflict-store schema was left alone); doc comments corrected.
  **Correction to this plan:** `baselineIcalData` cannot be populated —
  `EngineDiffOp::baselineRecord` is a hash-only shell and the engine loads
  per-side hashes, never bytes, so the field is wired but always empty. See
  FINDINGS **O48**; PlanStan's 3-way diff stays unreachable until baseline
  storage changes. Also fixed in passing: the unmonitored branch never set
  `source/targetModified` (FINDINGS **O49**).
- **Task 2 — Bugs C + D + helper extraction. DONE 2026-08-21.**
  `resumeAfterConflict()`'s `switch` extracted verbatim (bar the two fixes)
  into

  ```cpp
  void SyncEngineWorker::applyConflictResolution(const EngineDiffOp &op,
                                                 ConflictResolution resolution,
                                                 const QString &mergedNative);
  ```

  — `op` is a **parameter**, not `m_unifiedDiff.toTarget[m_unifiedConflictIdx]`,
  so Task 3 can call it mid-walk with an op that is not the yielded one; it
  still reads the per-run state both callers share (`m_unifiedCanonical`,
  `m_unifiedMerger`, `m_unifiedSrcToCanon`, `m_currentRequest`) and writes
  `m_unifiedMerge` / `m_currentResult`. `resumeAfterConflict()` keeps the
  not-yielded guard, the index bump, the flag reset and the
  `unifiedHandleConflicts()` re-entry.
  **Bug C:** `CustomMerge` now promotes a non-empty `mergedNative` through
  `m_unifiedSrcToCanon` (the payload is the SOURCE backend's native encoding —
  it is built from `ConflictInfo::sourceIcalData`) and writes that; empty
  payload, a null pipeline, or a promotion that empties the record
  (`transcodeEmptiedRecord`) all fall back to the pre-existing auto-merger
  path, so the `!m_unifiedMerger` deferral branch stays reachable.
  **Bug D:** the `Duplicate` clone's uid is rewritten through
  `CanonEnvelope::parse`/`uidKey`/`serialize` instead of
  `data.replace("UID:"+id, …)` — the record is canonical Shape JSON there and
  the iCal spelling never matched, so the clone kept the original uid and the
  backends' uid-keyed stores collapsed the pair back into one record. Data
  that does not parse as canonical JSON degrades to a TargetWins-shaped
  resolution with a `qWarning()` rather than emitting a colliding copy.
  Closes PlanStan's `docs/bugs/sync-dialog-keepboth-duplicate-not-created.md`.
  Tests: `customMergeUsesCallerSuppliedMerge` and
  `duplicateResolutionWritesASecondRecord` in
  `tests/engine/tst_syncengine_unification.cpp`, both driven through the
  production `IConflictResolver` callsite (new `StubConflictResolver`,
  `WorkflowMode::Immediate`) and both **shown RED** against the pre-fix code
  (INVARIANTS §5). New finding **O50** — the two hand-built `ConflictInfo`s
  that remain inside `applyConflictResolution`'s deferral branches carry none
  of the payload fields the detection-walk builder sets; logged, not fixed
  (out of Task 2's "behaviour identical apart from C and D" contract).
- **Task 3 — Bug B. DONE 2026-08-21.** Resolution injection into the next
  run, end to end:

  1. **One channel.** `ConflictManager::conflictResolved(conflictId,
     resolution)` is now emitted for every real (non-Skip) resolution, not
     only when a `SyncConflictStore` happens to be attached — the store write
     stays conditional, the signal does not. That one change covers all three
     store-only paths (`showImmediateDialog`, `queueForDeferred`→dock→
     `applyResolution`, `applyAutoPolicy`); the handoff's suspicion about
     `applyAutoPolicy` is **confirmed** — it had the identical shape and
     already emitted, so it needed no change of its own. `CustomMerge`'s
     payload is captured per conflict (`mergedDataFor(conflictId)`, additive)
     because `lastMergedIcalData()` is last-call-scoped and useless across the
     batch loop. `conflictResolved`'s signature is unchanged.
  2. **Identity.** `SyncEngine::onWorkerConflictDetected` keeps the id
     `recordConflict()` returns (it was discarded) and synthesizes a `QUuid`
     for store-less hosts, indexing `conflictId → (mappingId, recordId,
     source/targetModified)` in `m_conflictIdentity`; ids from a previous
     process fall back to `SyncConflictStore::conflict()`. `conflictDetected`
     now carries a populated `conflictId`.
  3. **Store + transport.** `m_pendingResolutions` (mapping → record →
     `PendingConflictResolution`), rehydrated at every run entry from the new
     additive `SyncConflictStore::resolvedConflicts()` (ordered by
     `resolved_at` ASC so the most recent wins), carried on
     `SyncEngineWorker::Request::pendingResolutions` from all three dispatch
     sites. **Confirmed: the store's read side already populated
     `sourceModified`/`targetModified`** from `local_modified`/
     `remote_modified` — the three row readers were collapsed onto one
     `readConflictRow()` while verifying it, rather than adding a third copy.
  4. **Consumption + staleness.** `unifiedHandleConflicts()`'s `AskUser`
     branch replays a pending resolution through Task 2's
     `applyConflictResolution()` before either the Monitored yield or the
     Unmonitored defer. `sameModifiedInstant()` compares each side's live
     `lastModified` against detection; on mismatch the resolution is
     discarded with a `qWarning()` and the conflict re-presented.
  5. **Consume-once.** `SyncResult::appliedConflictIds` / `staleConflictIds`
     (additive). Applied ids are populated **only on the successful-write
     branch** of `unifiedContinueAfterConflicts` — same rule as baseline
     saves, so a failed apply retries next run. `consumeAppliedResolutions()`
     drops them from the pending map and deletes their store rows.
  6. **Auto follow-up.** Queue runs ride `pumpQueue()`'s re-prime machinery
     with their own budget (`m_resolutionPasses`/`kMaxResolutionPasses = 2`,
     deliberately separate from `m_currentPass`/`kMaxSyncPasses`); Single runs
     re-dispatch via `redispatchForResolutions()` before Complete/finish,
     reusing the open run's iface so the caller's future still resolves once.
     `m_resolvingMonitoredConflict` suppresses queueing when the Monitored
     yield is about to apply the answer inline (and deletes the now-consumed
     row).
  7. **FINDINGS O50 folded in** (prerequisite, not follow-up — the staleness
     guard needs those timestamps): both hand-built `ConflictInfo`s in
     `applyConflictResolution` now call `buildConflictInfo(op)`.

  **New findings: O51** (staleness guard is second-granular and blind on a
  backend with no `lastModified`), **O52** (a rehydrated `CustomMerge` loses
  the user's payload — no store column), **O53** (pre-existing, confirmed: the
  batch dialog is modal and runs inside `onWorkerSyncCompleted` while other
  mappings may still be in flight).

  Tests: seven new slots in `tests/engine/tst_syncengine_unification.cpp`
  (headline round trip, restart durability, consume-once, staleness, Deferred,
  AutoResolve, store-less host) on a shared `seedConflict()` fixture, each
  shown RED against the specific fix it protects (see the commit message for
  the per-probe matrix). `tests/calendar/tst_calendar_conflict.cpp`'s
  `unmonitored_sameUidDivergent_emitsConflictDetected` **flipped contract** —
  it asserted the target was NOT written, which was pinning the defect; it now
  asserts the AutoResolve answer lands, and covers the Queue-mode follow-up
  gate.

  **Consumer-visible for Task 4:** an Unmonitored run can now write data it
  previously never would; `conflictDetected` carries a populated
  `conflictId`; a Skipped conflict now has full data (`hasFullData()` true);
  and an applied conflict's `SyncConflictStore` row is deleted rather than
  left resolved-but-present.
- **Task 4 — Verification and docs.** Full suite, FINDINGS O-numbers,
  `docs/2026-07-19-consumer-coordination-status.md`, `CLAUDE.md`, tag.

## Baseline suite state (from `main`, unchanged by this work)

179 total, 177 passing. Two pre-existing failures:
`tst_remotecalendarbackend` (broken local Radicale test-server auth) and
`tst_calendar_canon_roundtrip` (pre-existing on `main`, uncatalogued).
**Any third failure is ours.**
