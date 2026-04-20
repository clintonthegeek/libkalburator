# Phase 3 status — WIP scaffold, build incomplete

**Date:** 2026-04-20.
**Status:** Phase 3a scaffold committed; Phase 3 proper paused.

## What exists

- `CMakeLists.txt` — standalone project definition, Qt6 + KF6 deps,
  optional `HAVE_AKONADI` and `HAVE_ORG_IO` flags, globs source from
  `src/sync/` and `src/types/`, builds a single `kalburator` static
  library target with `Kalburator::Sync` alias.
- `src/sync/` — 81 files copied verbatim from
  `~/dev/PlanStan/libs/sync/`.
- `src/types/` — 16 files copied from `~/dev/PlanStan/libs/core/` +
  `libs/models/` + `libs/scheduling/`: the type headers + their
  implementations that sync transitively consumes
  (`BackendConfiguration`, `LogicalCalendar`, `SyncTypes`,
  `CalendarType`, `DataDomain`, `ICalendarCollection`,
  `ISyncConfigStore`, `IIncidenceRegistry`, `ICommandDispatcher`,
  `IncidenceRef`, `IncidenceLock`, `IncidenceLockRegistry`,
  `CrashJournal`, `ILocaleSource`).

`cmake --preset` (configure) succeeds. Build fails.

## What remains — cascading dependency chase

Building surfaces missing headers / symbols the scaffold hasn't pulled in:

- `iincidencesource.h` (not yet copied)
- `calendarmetadatamanager.h` (not yet copied)
- `ICalendarCollection` incomplete-type errors in `calendarmanager.cpp`
  — the `#include "icalendarcollection.h"` is correct but the
  compiler apparently can't see it on the line in question; needs
  local debugging to find whether the include path is not propagating
  or a forward-decl is stale.

Each missing header typically reveals 2–3 further dependencies.
Estimate 30–60 minutes of careful chase to reach a clean build in a
dedicated session.

## Why paused

Three reasons converge:

1. **Context-pressure**. The session started with Phase 0 and has
   run through 1.0 → 1.4 → 2 already. Phase 3 as planned is the big
   phase — it touches 90+ files plus CMake configuration. Starting
   it with depleted session context increases the risk of a
   half-migrated state.

2. **Phase 3a vs Phase 3b**. The proposal called for Phase 3 to both
   stand up libkalburator standalone *and* cut PlanStan over to
   consuming it via FetchContent. The second half (cutover) is the
   riskier operation — it touches PlanStan's build at the same time
   as libkalburator is still settling. Better to get libkalburator
   clean first, then cut over.

3. **Honest completion signal**. Declaring Phase 3 "done" when the
   library doesn't build is worse than saying "Phase 1–2 done,
   Phase 3 scaffolded and paused." Maintainer can decide whether to
   resume in a fresh session or stage the final cutover differently.

## State of PlanStan (unchanged from end of Phase 2)

`~/dev/PlanStan` still builds fully, all tests pass at baseline
(86 passed / 27 failed). The Phase 1 interface narrowings are live;
the Phase 2 smoke test is live. Nothing about libkalburator's WIP
state affects PlanStan's working tree.

## Suggested resumption path

1. Fresh session dedicated to Phase 3a alone.
2. Add missing headers one-by-one to `src/types/`, re-run build,
   fix include paths as they surface.
3. When `libkalburator` builds clean standalone (`cmake --build`
   with `HAVE_ORG_IO=OFF HAVE_AKONADI=OFF` producing a library with
   no undefined symbols), commit Phase 3a and declare it done.
4. Only then begin Phase 3b (PlanStan cutover via FetchContent),
   keeping PlanStan's `libs/sync/` as a fallback until the cutover
   lands cleanly.

## What's in the libkalburator repo at pause

```
libkalburator/
├── .gitignore
├── CMakeLists.txt          ← scaffold; configures, doesn't yet build
├── README.md
├── docs/
│   └── phase0/
│       ├── 00-open-questions.md   (all 10 resolved)
│       ├── 01-inventory-planstan.md
│       ├── 02-inventory-wildpalms.md
│       ├── 03-conflict-engine-audit.md
│       ├── 04-merged-interface-sketch.md
│       ├── 04a-followups.md
│       ├── 04b-phase3-status.md   (this file)
│       └── 05-repo-strategy.md
└── src/
    ├── sync/     (81 files from PlanStan's libs/sync/)
    └── types/    (16 files from PlanStan's libs/core/ et al.)
```
