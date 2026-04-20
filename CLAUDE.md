# libkalburator — Claude instructions

This repo is the in-flight extraction of PlanStan's sync library into a
standalone project shared with Wild Palms. The source of truth for the
overall plan lives in PlanStan at
`~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

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
