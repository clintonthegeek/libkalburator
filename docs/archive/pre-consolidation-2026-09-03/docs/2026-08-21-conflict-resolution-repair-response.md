# Response to the conflict-resolution handoff — all four defects fixed

**Date:** 2026-08-21
**Answers:** `docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-handoff.md`
**Branch:** `feature/conflict-resolution-repair` (`3902f40` → `79d5d77`), **not yet merged or tagged**
**Suite:** 179 total, 177 passing — exactly the pre-existing baseline
(`tst_remotecalendarbackend` Radicale auth, `tst_calendar_canon_roundtrip`).
`tst_syncengine_unification` grew from 4 test slots to 14 (runner totals 6 → 16, counting fixtures).

## What was fixed

Your two bugs, plus two more found in the same code during the fix.

| # | Defect | Status |
|---|---|---|
| **A** | `ConflictInfo::source/targetIcalData` carried canonical Shape JSON, not native iCal | **Fixed** — demoted through the reverse pipelines at both construction sites |
| **B** | An `Unmonitored` resolution wrote one DB column and never touched data | **Fixed** — resolution injection into the next run |
| **C** | `resumeAfterConflict(resolution, mergedIcal)` never read `mergedIcal`; `CustomMerge` silently ran the auto-merger | **Fixed** |
| **D** | `Duplicate` rewrote the clone's uid with `data.replace("UID:"…)`, which never matches canonical JSON — the clone kept the original uid and collided | **Fixed** |

**D is your separately-filed `docs/bugs/sync-dialog-keepboth-duplicate-not-created.md`** — it has the
same root cause as A and closes with this work. C means every Custom Merge ever
performed, in either behavior mode, discarded the user's merge.

Your diagnosis of A and B was correct in every particular, including that a fix
"cannot just call `resumeAfterConflict()` after the fact".

## How Bug B was fixed

Your option 1, first sub-approach, in its *injection* variant. `SyncEngine`
does **not** get its own write-path access (that would be a second write
mechanism — campaign INVARIANTS §1). Instead:

1. `ConflictManager::conflictResolved` became the single channel for a chosen
   resolution. It now fires unconditionally on a real resolution (it was gated
   on a store being attached); the *store write* stays conditional.
   **Your suspicion about `applyAutoPolicy` was right** — identical
   "record and immediately resolve, write nothing" shape. All three workflow
   modes are fixed by the one channel.
2. `SyncEngine` keeps the resolution keyed by mapping + record, **rehydrated at
   run start** from resolved-but-still-present `SyncConflictStore` rows, so it
   survives a restart.
3. It rides to the worker on `SyncEngineWorker::Request`, and
   `unifiedHandleConflicts()` replays it through the helper extracted from
   `resumeAfterConflict()`'s `switch` — so the write, transcode, baseline and
   host-notify path is the existing one, unchanged.
4. A **follow-up pass** applies it within the same `runSync`, riding
   `pumpQueue()`'s existing L2 re-prime machinery with its own budget
   (`kMaxResolutionPasses = 2`). You do not have to wait for the next tick.

**Your DB full of resolved-but-never-applied rows will be picked up and applied
on the first sync after upgrading.** Most-recent `resolved_at` wins per record.

Two guards you should know about:
- **Stale resolutions are discarded, not applied.** If a record changed between
  the dialog answer and the run that would apply it, the resolution is dropped
  with a `qWarning()` and a fresh conflict is presented — a stale "Keep Local"
  must never clobber a newer edit.
- **Consume-once.** An applied resolution's row is deleted, and only on the
  successful-write branch. A failed apply leaves it pending for retry.

## Consumer-visible changes

Everything is **additive** — no signature changed, no rename. PlanStan needs no
code change to get the fix. But five behaviors differ:

1. **An `Unmonitored` run can now write data where it previously never would.**
   That is the fix, but it is a live behavior change.
2. `conflictDetected` now carries a populated `conflictId` (previously always
   empty — the engine discarded the id `recordConflict()` returned).
3. A **Skipped** conflict now carries full data, so `hasFullData()` returns true
   where it returned false. Your dock/dialog will render a real diff for them.
4. An applied conflict's store row is **deleted** rather than left
   resolved-but-present.
5. `ConflictInfo` gained additive `sourceEncoding`/`targetEncoding` so a UI can
   tell what encoding it was handed. **Transport-only — not persisted**
   (columns would need a schema migration).

## Two things you asked for that you are NOT getting

**`baselineIcalData` stays empty — the 3-way diff path remains unreachable.**
Your handoff suggested wiring it through "with the same `canonToSrc`
conversion", and that is not possible: `op.baselineRecord.data` is *always*
empty. `perRecordDiff` builds it via `baselineShell()`, which sets only `.id`
and `.contentHash`, and `setBaselineHashesV4()` stamps `canonical_bytes` empty
on every steady-state save — baseline **bytes** are not stored anywhere. The
demotion is wired anyway and lights up free if bytes ever return, but today
`ConflictResolutionDialog` will always take the 2-way path. Restoring 3-way is
a baseline-storage change and a decision about what to persist.
Logged as **FINDINGS O48**. **Do not wait on this.**

**A rehydrated `CustomMerge` loses the user's merged payload** (FINDINGS O52).
There is no store column for a merge *result*, so if the app closes between
choosing a Custom Merge and the next sync, the resolution comes back without
the payload and falls back to the auto-merger. In-process it is carried
correctly. Until a `merged_ical` column exists — same schema decision as O48 —
a Custom Merge needs the app to stay open for one more sync cycle.

## One thing you should look at on your side

**FINDINGS O53 — pre-existing, not introduced here, but your concurrency
default makes it live.** `SyncEngine::onWorkerSyncCompleted` calls
`ConflictManager::handleConflicts()` inline, which shows a **modal** dialog and
spins a nested event loop while other mappings of the same run are still
executing on pool workers. Their completion slots fire *inside* the dialog's
event loop. Before parallel sync this was harmless (one mapping at a time);
with `AppSettings::syncMaxConcurrentMappings()` defaulting to **4**, it is a
genuine re-entrancy surface. Related: `m_pendingUnmonitoredConflicts` is a flat
list, not keyed by mapping, so conflicts from mapping A can be presented under
mapping B's completion — the debug line even names the wrong mapping.

The Bug B machinery now makes the right fix *possible*, since a resolution no
longer has to be answered while any particular run is alive: hand the batch to
the host asynchronously instead of calling a modal dialog from a completion
slot. Not attempted here.

## Still explicitly USER-RUN, not attempted by any agent session

Live verification against a real multi-calendar CalDAV account: resolve a
conflict in the dialog and confirm the chosen side actually lands on the
server, that the conflict does not re-present on the next tick, and that
"Keep Both" now produces two items. Everything above is pinned by
stub-backend integration tests with no live server involved.
