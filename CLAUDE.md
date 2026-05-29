# libkalburator — Claude instructions

This repo is the in-flight extraction of PlanStan's sync library into a
standalone project shared with Wild Palms. The source of truth for the
overall plan lives in PlanStan at
`~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

## Architectural-redress campaign — START HERE if on a branch `feature/redress-N-*`

If your CWD is on any branch matching `feature/redress-N-*` (N = 1..9), you are
working the campaign opened 2026-05-29 from a fresh-eyes audit of the post-canon
codebase. The audit found the canon-upgrade convergence (below) succeeded but the
underlying layering, encapsulation, and naming grew leaks no one stopped to name.
The redress is the next sustained body of work.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/architectural-redress/INVARIANTS.md` — the rules you accept by
   working here. Non-optional.
2. `docs/campaign/architectural-redress/AUDIT.md` — the fresh-eyes findings this
   campaign exists to redress. The audit wins if it disagrees with a plan.
3. `docs/campaign/architectural-redress/STATUS.md` — campaign state, the 9-plan
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
is deleted and the shape graph is the sole transformation mechanism (invariant 1).** What
remains is downstream / out-of-campaign: the PlanStan/WildPalms backend port (FINDINGS O7/O12)
and the eventual merge to `main`. Read `docs/campaign/STATUS.md` "Next action" first.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/INVARIANTS.md` — the rules you accept by working here. Non-optional.
2. `docs/campaign/STATUS.md` — campaign state (now: converged), the 4-plan sequence, locked
   decisions, and the remaining downstream next actions.
3. `docs/campaign/FINDINGS.md` — open watch items (esp. O7, O9, O12) and the discipline log.
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

## Refactor-branch worktree

If your CWD is `~/dev/refactor-engine-merger/libkalburator/`, you are
in a worktree on branch `refactor/engine-merger` participating in
the **engine-merger refactor**. The cross-repo coordination doc set
lives one directory up at `~/dev/refactor-engine-merger/`. Read its
`CLAUDE.md` first — that file is the canonical entry point for
refactor work.

The phase plan is in `docs/phase0/04k-engine-merger-roadmap.md`
(library-side restatement). Cross-cutting findings go in
`~/dev/refactor-engine-merger/FINDINGS.md`, not in this directory.

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

## Cross-repo coordination

Phase 3b (PlanStan FetchContent cutover) is PlanStan-side work. When
doing Phase 3b, also update this repo's `04b-phase3-status.md` and
PlanStan's sync-library-extraction proposal "Status" line in the same
session, even though the code change is in PlanStan.

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

- **`SyncEngine::runSyncFuture(behavior)`** returning
  `QFuture<QList<SyncResult>>`. Wait via
  `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000)` (NOT
  `waitForFinished` — Qt6's `waitForFinished` does NOT spin the
  test event loop). Read results via `future.resultAt(0)` (NOT
  `future.results()` — empty after cancel due to a Qt6 quirk).
  The single-mapping form `runSyncFuture(mappingId, …)` is now
  safe (the FINDINGS leak was structurally fixed by F2 Task 21).
  The void `runSync` overloads, `cancelSync`, and the
  `syncCompleted`/`allSyncsCompleted` signals were deleted in
  F2 Task 42.

- **Cancellation** — call `future.cancel()`. The cancellation
  channel propagates through
  `QFutureWatcher::canceled → SyncEngine::onCancelObserved →
  SyncEngineWorker::observeCancel` and wakes any nested
  `QEventLoop` (via `await<Op>` and the conflict-pause slot).

- **Write path** — `SyncBackend::storeItems()` /
  `updateItem()` / `writeFinished` still exist on the abstract
  base but are deprecated. New code should use the 3-arg
  `pushItems(id, items, TranscodingPlan)` returning a
  `PushOperation*` and read `op->state()` / `op->errorString()`
  for error reporting (per the F2 SyncOperation contract).

- **Conflict tests** — set `mapping.conflictPolicy = AskUser` AND
  seed a baseline via `SyncStore::setBaseline()`. Other policies
  resolve silently without signals; the quick-path (no baseline)
  downgrades AskUser to SourceWins.

- **Transcoding tests** — `cleanup()` must call
  `TranscodingRegistry::instance().clear()`. The registry is a
  process-wide singleton and leaks state across tests otherwise.

- **`StubCalendarCollection`** must hold a `MemoryCalendar` with
  `setId(calendarId)` matching the `SyncMapping`'s calendar id, or
  `applyChangesToBackend` can't find it and writes get dropped.

See `docs/phase0/04l-phase-d0-test-harness-design.md` and
`04l-phase-d0-test-harness-plan.md` for the full pattern, including
test-execution model and gotchas.
