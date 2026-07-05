# libkalburator — Claude instructions

This repo is the in-flight extraction of PlanStan's sync library into a
standalone project shared with Wild Palms. The source of truth for the
overall plan lives in PlanStan at
`~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

## Architectural-redress campaign — START HERE if on a branch `feature/redress-N-*`

If your CWD is on any branch matching `feature/redress-N-*` (N = 1..11), you are
working the campaign opened 2026-05-29 from a fresh-eyes audit of the post-canon
codebase. The audit found the canon-upgrade convergence (below) succeeded but the
underlying layering, encapsulation, and naming grew leaks no one stopped to name.
The redress is the next sustained body of work.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/architectural-redress/INVARIANTS.md` — the rules you accept by
   working here. Non-optional.
2. `docs/campaign/architectural-redress/AUDIT.md` — the fresh-eyes findings this
   campaign exists to redress. The audit wins if it disagrees with a plan.
3. `docs/campaign/architectural-redress/STATUS.md` — campaign state, the 11-plan
   sequence, locked decisions, your next action.
4. `docs/campaign/architectural-redress/FINDINGS.md` — the discipline log; append
   to it (invariant 9) when you walk past a smell.
5. The current plan in `docs/campaign/architectural-redress/plans/`.

New smells go in `FINDINGS.md`; update `STATUS.md` in the same commit that
changes plan state.

## Canon-upgrade / convergence campaign — START HERE if on branch `feature/canon-upgrade-convergence`

If your CWD is on branch `feature/canon-upgrade-convergence`, you are working
the campaign that (a) retires `src/transcoding/` into the shape graph and
(b) upgrades the calendar/contacts/todo canons to rich JSON superset encodings
behind a **versioned canonical spine** with a four-kind loss model.

**Status (2026-05-24): the campaign is COMPLETE — all four plans landed; `src/transcoding/`
is deleted and the shape graph is the sole transformation mechanism (invariant 1).**
Downstream port (FINDINGS O7/O12) DONE; O7 resolved 2026-05-27, O12 effectively closed;
branch merged to `main`. See `docs/campaign/STATUS.md` for the full history.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/INVARIANTS.md` — the rules you accept by working here. Non-optional.
2. `docs/campaign/STATUS.md` — campaign state (now: converged), the 4-plan sequence, locked
   decisions, and the remaining downstream next actions.
3. `docs/campaign/FINDINGS.md` — open watch items (esp. O9) and the discipline log.
4. The plans, all complete: `docs/2026-05-23-plan-1-shape-core-foundations.md`,
   `docs/2026-05-23-plan-2-per-engine-registries.md`, `docs/2026-05-24-plan-3-canon-encodings.md`,
   `docs/2026-05-24-plan-4-calendar-convergence.md`.
5. Design set (as needed): `docs/2026-05-23-canon-upgrade-and-convergence-design.md`,
   `docs/2026-05-23-canon-schema-design.md`, `docs/2026-05-23-vendor-api-shapes-reference.md`.

The one-paragraph why: libkalburator grew **two** parallel conversion mechanisms;
this campaign collapses them into one (the shape graph) and modernizes the canons.
The deepest invariant (INVARIANTS §1): extend the shape graph, never fork a third
mechanism. New issues/smells go in `docs/campaign/FINDINGS.md`; update
`docs/campaign/STATUS.md` in the same commit that changes plan state.

## Sync-convergence campaign — START HERE if working sync/CalDAV correctness

Opened 2026-07-03 from a PlanStan investigation of a real-world Nextcloud
account that never converged (120s soft-freeze, corrupted recurrence,
non-converging diff, silent-empty-read risk). Full roadmap, evidence, exact
file:line references, and RED-first test plans for every phase:
`docs/campaign/2026-07-03-sync-convergence-roadmap.md` — its §5 "Phase status"
checklist is the single source of truth for what's landed; update it in the
same commit that lands or merges a phase (same discipline as the other two
campaigns above).

**Status (2026-07-04): Tracks A, B, and C are COMPLETE** — tagged v0.80,
v0.81, v0.82; PlanStan pinned to v0.82 and live-verified (sync converges on a
real account, fast path by cycle 2). D0 (apply-phase
`ISyncHost::recordChanged` wiring) is merged to `main` @ `928f318`,
**untagged** — it ships with D1 under v0.83. Full landed-work history:
`docs/campaign/FINDINGS.md` and
`docs/campaign/archive/2026-07-03-sync-convergence-tracks-a-b-c.md`.

**Remaining: Phase D1 (N7 — move DAV I/O off the GUI thread, tag v0.83), then
D2 backlog triage.** D1 has a hand-off-ready, task-level execution plan
written from a full cross-repo viability audit:
**`docs/campaign/2026-07-04-d1-threading-execution-plan.md`** — if you are
picking up D1, start there, work its tasks in order, and update its §9
checklist in the same commit as each task. It supersedes the roadmap's D1
sketch on any disagreement.

Work happens on short-lived feature branches per phase group (e.g.
`feature/sync-stack-integrity-b1-b3`), merged `--no-ff` to `main` and tagged
once its full-suite gate is green; branches are not kept around after merge.

## Phase-status docs are living documents

All phase progress is tracked under `docs/phase0/`. When a phase
completes, fails, or pauses, update the corresponding status file in
the same commit that lands the code change. In particular:

- `04b-phase3-status.md` — Phase 3 status. Keep the **Status** line at
  the top accurate ("Phase 3a done", "Phase 3b in progress", etc.) and
  update the "What exists now" and "Next" sections as work lands.
- Any new phase doc should follow the same pattern: Status line at top,
  "What exists" / "What remains" sections, updated every time the phase
  state changes.

Do not leave a status doc saying "paused" after work has resumed, or
"WIP" after it has landed. Future sessions start from these docs — if
they lie, work gets redone or skipped.

## Build

Standalone build:

```
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

Default profile: `KALBURATOR_HAVE_ORG_IO=OFF`, `KALBURATOR_HAVE_AKONADI=OFF`.

## Calendar-layer integration tests (since Phase D.0, 2026-04-28)

`tests/calendar/` contains stub-`ISyncHost` integration tests that
pin `SyncEngine` behavior. They are the contract the engine-merger
refactor (Phases D / E / F / G) preserves. Phase F1 (2026-04-30,
tag `v0.13-phase-f1-unify`) collapsed `SyncCoordinator` +
`SyncWorker` + `BlobSyncEngine` into the unified `SyncEngine` at
`src/engine/syncengine.{h,cpp}` — historical references to those
old class names appear in commit messages and FINDINGS but should
not be used in new code or comments.

When writing or modifying tests in this directory:

- Use the four reusable stubs at `tests/calendar/stubs/`:
  `StubSyncHost`, `StubCalendarCollection`, `StubIncidenceRegistry`,
  `StubSyncConfigStore`. Compiled into static lib
  `kalburator_calendar_test_stubs`. Link via the helper function
  `kalburator_add_calendar_integration_test()` in
  `tests/calendar/CMakeLists.txt`.

- **Canonical engine entry: `SyncEngine::runSync(SyncRequest)`**
  returning `QFuture<QList<SyncResult>>` (redress Plan 1). This is the
  **sole** sync entry — the four `runSyncFuture(...)` overloads were
  DELETED in redress Plan 8 step 3 (2026-06-10), along with
  `dispatchSingleNative` and the dual `m_currentSingleIface`/
  `m_currentMultiIface` interface; the engine now holds one
  `m_currentIface` + one `m_currentWatcher` wired by `beginRun()`.
  Build a `SyncRequest` (`mappingIds` empty ⇒ all enabled; size 1 ⇒
  single mapping; size >1 ⇒ subset). Wait via
  `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000)` (NOT
  `waitForFinished` — Qt6's `waitForFinished` does NOT spin the test
  event loop). Read results via `future.resultAt(0)` — a
  `QList<SyncResult>` (NOT `future.results()`, empty after cancel due
  to a Qt6 quirk). The void `runSync` overloads, `cancelSync`, and the
  `syncCompleted`/`allSyncsCompleted` signals were deleted in
  F2 Task 42.

- **Single-mapping cancel is now native** (Plan 8 step 3): a canceled
  single-mapping `runSync(SyncRequest)` future preserves the F2 Task 23
  contract — `resultCount()==1`, `resultAt(0).first().cancelled==true` —
  with **no `.then()` wrap** and **no `resultCount()>0` guard** needed.
  (Pre-collapse the canonical single path lost this; only the deleted
  shims preserved it. Pinned by `tst_engine_single_mapping_cancel`.)

- **Cancellation** — call `future.cancel()`. The cancellation
  channel propagates through
  `QFutureWatcher::canceled → SyncEngine::onCancelObserved →
  SyncEngineWorker::observeCancel` and wakes any nested
  `QEventLoop` (via `await<Op>` and the conflict-pause slot).

- **Write path** — `SyncBackend::storeItems()` / `updateItem()` /
  `writeFinished` were DELETED (canon-upgrade campaign; only stale
  comments mention them). The write API is the 2-arg
  `pushItems(calendarId, items)` returning a `PushOperation*`;
  read `op->state()` / `op->errorString()` for error reporting
  (per the F2 SyncOperation contract). `TranscodingPlan` no longer
  exists — transformation flows through the shape graph.

- **Conflict tests** — set `mapping.conflictPolicy = AskUser` AND
  seed a baseline via `BaselineStore::setBaselineV3()` (the
  mapping-keyed v3 API in `storage/baselinestore.h`; there is no
  `SyncStore` class). Other policies resolve silently without
  signals; the quick-path (no baseline) downgrades AskUser to
  SourceWins.

- **`StubCalendarCollection`** must hold a `MemoryCalendar` with
  `setId(calendarId)` matching the `SyncMapping`'s calendar id, or
  `applyChangesToBackend` can't find it and writes get dropped.

See `docs/phase0/04l-phase-d0-test-harness-design.md` and
`04l-phase-d0-test-harness-plan.md` for the full pattern, including
test-execution model and gotchas.
