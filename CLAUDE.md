# libkalburator — Claude instructions

This repo is the in-flight extraction of PlanStan's sync library into a
standalone project shared with Wild Palms. The source of truth for the
overall plan lives in PlanStan at
`~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

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
pin `SyncCoordinator`/`SyncWorker` behavior. They are the contract
the engine-merger refactor (Phases D / E / F / G) preserves.

When writing or modifying tests in this directory:

- Use the four reusable stubs at `tests/calendar/stubs/`:
  `StubSyncHost`, `StubCalendarCollection`, `StubIncidenceRegistry`,
  `StubSyncConfigStore`. Compiled into static lib
  `kalburator_calendar_test_stubs`. Link via the helper function
  `kalburator_add_calendar_integration_test()` in
  `tests/calendar/CMakeLists.txt`.

- **`SyncCoordinator::runSync(behavior)`** (no `mappingId` arg) —
  use this and wait on `allSyncsCompleted`. The single-mapping form
  `runSync(mappingId, …)` has a known leak (re-dispatches the same
  mapping in `processNextMapping`); see
  `~/dev/refactor-engine-merger/FINDINGS.md`.

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
