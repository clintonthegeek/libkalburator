# Phase C.2a — layering migration design (option 2)

**Date:** 2026-04-20.
**Status:** Design spec, approved for implementation. Follows from
`04d-phase-c2-blocker.md`. Implementation plan lives alongside this
spec; the implementation executes in this session.

## Goal

Complete the option-2 layering migration that `04b-phase3-status.md`
deferred: delete PlanStan's duplicate copies of shared types and make
libkalburator the single source of truth for them. This is a
prerequisite for C.2b (the namespace migration) per the C.2 attempt
post-mortem in `04d`.

No namespace changes happen in C.2a. Types stay at global scope.

## Scope — the 16 duplicated types

Phase 3a copied these types from PlanStan into libkalburator's
`src/types/`. PlanStan retained its originals. `diff -rq` between
the two directories today shows no drift outside the three
listed PlanStan libraries.

| PlanStan library | Count | Headers | .cpp |
|---|---|---|---|
| `libs/core/include/` | 10 | backendconfiguration, calendartype, crashjournal, icommanddispatcher, iincidenceregistry, incidencelock, incidenceref, isyncconfigstore, logicalcalendar, synctypes | backendconfiguration.cpp, crashjournal.cpp |
| `libs/models/include/` | 4 | calendarmetadatamanager, icalendarcollection, ilocalesource, incidencelock_registry | calendarmetadatamanager.cpp, ilocalesource.cpp, incidencelock_registry.cpp |
| `libs/scheduling/include/` | 2 | datadomain, iincidencesource | (none) |

Non-duplicated types that stay in PlanStan libs — out of scope:
- `libs/core/include/`: colorutils, gradient, rendermode, renderstyle, selectionref, viewpalette, lifedomaincolors, iplanningmetadatasource.
- `libs/models/include/`: all non-listed headers (collection.h, settings, models, etc.).
- `libs/scheduling/include/`: all non-listed headers (companiondata, schedulingtypes, etc.).

## Architecture

### Before (post-Phase 3b)

```
libkalburator/
  [kalburator] (also aliased Kalburator::Sync, PlanStan::Sync, planstan-sync)
    src/sync/*    (81 files)
    src/types/*   (20 files — suppressed when PROVIDE_TYPES=OFF)
    deps: Qt6::Core/Sql/Network/Xml/Widgets, KF6::CalendarCore/DAV/KIOCore/Holidays
    PUBLIC-links (in-tree): PlanStan::Core/Models/Scheduling/OrgIO

PlanStan/
  libs/core       — owns 10 shared-type duplicates
  libs/models     — owns 4 shared-type duplicates
  libs/scheduling — owns 2 shared-type duplicates
```

Top-level `PlanStan/CMakeLists.txt` forces `KALBURATOR_PROVIDE_TYPES=OFF`
so `src/types/*` is suppressed; PlanStan's own copies feed libkalburator
via PUBLIC linkage from the three libs.

### After (post-C.2a)

```
libkalburator/
  [Kalburator::Types]   ← new target
    src/types/*          (20 files, unchanged content)
    deps: Qt6::Core, Qt6::Gui, KF6::CalendarCore     (minimal)
    PUBLIC includes: src/types/

  [Kalburator::Sync]     ← renamed target (existing aliases preserved)
    src/sync/*
    PUBLIC-depends on: Kalburator::Types, Qt6::Sql/Network/Xml/Widgets, KF6::DAV/KIOCore/Holidays
    PUBLIC-links (in-tree): PlanStan::Core/Models/Scheduling/OrgIO
    KALBURATOR_PROVIDE_TYPES flag: deleted

PlanStan/
  PlanStan::Core       → PUBLIC-links Kalburator::Types. 10 duplicates (+2 .cpp) deleted.
  PlanStan::Models     → PUBLIC-links Kalburator::Types. 4 duplicates (+3 .cpp) deleted.
  PlanStan::Scheduling → PUBLIC-links Kalburator::Types. 2 duplicates deleted.
```

### Dependency graph (acyclic)

```
Kalburator::Types  ←  PlanStan::Core
                  ←  PlanStan::Models         ←   higher PlanStan libs
                  ←  PlanStan::Scheduling     ←   (via existing PlanStan::* links)
                  ←  Kalburator::Sync         →   PlanStan::Core/Models/Scheduling/OrgIO
```

Downstream PlanStan libs (gantt, editor, calendar-views, view-infrastructure,
planning, etc.) need zero CMake changes: their existing PUBLIC links to
PlanStan::Core/Models/Scheduling transitively carry the Kalburator::Types
include path. Their `#include "backendconfiguration.h"` continues to
resolve, just from `libkalburator/src/types/` rather than from the
deleted PlanStan header.

## Commit sequence

Four commits across two repos. Each commit builds green and holds the
existing ctest baseline (87 pass / 27 fail) before the next commit
starts.

### Commit 1 — libkalburator: target split, flag removal

Repo: `~/dev/libkalburator`.

CMakeLists.txt changes:
- Create `kalburator-types` static library from `src/types/*`.
  - PUBLIC deps: Qt6::Core, Qt6::Gui, KF6::CalendarCore.
  - PUBLIC include: `src/types/`.
  - Alias `Kalburator::Types`.
- Keep `kalburator` target as before but:
  - No longer globs `src/types/*` directly.
  - PUBLIC-link `kalburator-types` (so downstream sees its includes + symbols).
  - PUBLIC includes narrowed to `src/sync/` only.
- Remove `KALBURATOR_PROVIDE_TYPES` option and all if-branches referencing it.

Verification:
- `cmake -S ~/dev/libkalburator -B ~/dev/libkalburator/build` configures clean.
- `cmake --build ~/dev/libkalburator/build` builds `libkalburator-types.a` + `libkalburator.a` clean.
- `/project:build` in PlanStan succeeds. ctest with Wayland env holds 87/27.

Notes:
- The top-level PlanStan `set(KALBURATOR_PROVIDE_TYPES OFF CACHE BOOL "" FORCE)` becomes a dead assignment but doesn't break anything — cleanup lands with commit 2.
- Both copies of type headers are still on the compiler's include path for PlanStan consumers during this interval; include-path ordering picks one, but Phase 3a's verbatim copy means either selection is semantically equivalent.

### Commit 2 — PlanStan: libs/core cutover

Repo: `~/dev/PlanStan`.

Changes:
- Top-level `CMakeLists.txt`:
  - **Reorder**: move `add_subdirectory(.../libkalburator ...)` and its
    preceding `KALBURATOR_HAVE_*` option-sets to *before*
    `add_subdirectory(libs/core)`. Required so `Kalburator::Types` is
    defined when libs/core's CMakeLists references it.
  - Keep the `target_link_libraries(kalburator PUBLIC PlanStan::Core ...)`
    top-level wiring + `PlanStan::Sync` / `planstan-sync` aliases where
    they currently sit — after all PlanStan::* libs are declared. (The
    alias creations cannot move earlier because the PlanStan targets
    don't exist yet.)
  - Drop the dead `set(KALBURATOR_PROVIDE_TYPES OFF CACHE BOOL "" FORCE)`
    line and its explanatory comment.
- `libs/core/CMakeLists.txt`:
  - Remove 10 header entries + 2 .cpp entries from `add_library(planstan-core ...)`.
  - Add `Kalburator::Types` to `target_link_libraries(planstan-core PUBLIC ...)`.
- Delete files:
  - `libs/core/include/{backendconfiguration,calendartype,crashjournal,icommanddispatcher,iincidenceregistry,incidencelock,incidenceref,isyncconfigstore,logicalcalendar,synctypes}.h`
  - `libs/core/src/{backendconfiguration,crashjournal}.cpp`

Verification:
- `/project:build` succeeds; ctest holds 87/27.

### Commit 3 — PlanStan: libs/models cutover

Changes:
- `libs/models/CMakeLists.txt`:
  - Remove 4 header entries + 3 .cpp entries from `add_library(planstan-models ...)`.
  - Add `Kalburator::Types` to `target_link_libraries(planstan-models PUBLIC ...)`.
- Delete files:
  - `libs/models/include/{calendarmetadatamanager,icalendarcollection,ilocalesource,incidencelock_registry}.h`
  - `libs/models/src/{calendarmetadatamanager,ilocalesource,incidencelock_registry}.cpp`

Verification:
- `/project:build` succeeds; ctest holds 87/27.

### Commit 4 — PlanStan: libs/scheduling cutover

Changes:
- `libs/scheduling/CMakeLists.txt`:
  - Remove `include/iincidencesource.h` entry (it's the only one listed; `datadomain.h` isn't in the CMakeLists).
  - Add `Kalburator::Types` to `target_link_libraries(planstan-scheduling PUBLIC ...)`.
- Delete files:
  - `libs/scheduling/include/{datadomain,iincidencesource}.h`

Verification:
- `/project:build` succeeds; ctest holds 87/27.

## Consumer impact

Direct consumers of the three cutover libs (PlanStan::Core, ::Models,
::Scheduling) need no changes because the types they previously got
from the lib's own include path now arrive via transitive
`Kalburator::Types` — same file name, same content, same global-scope
symbols.

Kalburator::Sync continues to see its types from its own src/types/
(via Kalburator::Types PUBLIC dep) — no behavioural change; the
previous PlanStan-injected duplicates are simply gone from its
include path.

Tests under `PlanStan/tests/` that link sync-side targets get
`Kalburator::Types` transitively through the Sync target; no
tests/CMakeLists.txt edits expected.

Wild Palms is unaffected — it hasn't adopted libkalburator yet (phase 4
is future). The libkalburator standalone build still works for WP's
eventual adoption: `kalburator-types` builds by default alongside
`kalburator`.

## Testing strategy

Each commit: `/project:build` + ctest with Wayland env
(`WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland`). Baseline 87 pass
/ 27 fail must hold. A new failure is a stop-the-line signal.

Commit 1 additionally gets a standalone libkalburator build verification
(`cmake -S ~/dev/libkalburator -B ~/dev/libkalburator/build && cmake --build ~/dev/libkalburator/build`).

## Risks and rollback

- **Undiscovered duplicate type.** Mitigation: the scope map was built
  from `diff -rq` of libs/core/include + direct file listings of
  libs/{models,scheduling}/include against libkalburator/src/types.
  The user confirmed the branch is static and single-developer, so
  missed duplicates are unlikely to have grown in the 2 days since
  Phase 3a. If one surfaces during build, add it to the relevant
  cutover commit.
- **Flag removal breaks a hidden standalone consumer.** Mitigation:
  libkalburator has exactly two consumers (PlanStan, WildPalms).
  WildPalms hasn't adopted it yet. PlanStan is the only active
  consumer and is being migrated in the same commit sequence.
- **ctest regression from some subtle include-order difference.**
  Mitigation: commit-by-commit bisection is easy; `git revert` restores
  any commit cleanly.

Every commit is `git revert`-safe because it only deletes files and
adjusts CMakeLists.txt — no code edits cross library boundaries.

## What unblocks after C.2a lands

- **C.2b (namespace migration)** can proceed on a clean layering:
  libkalburator's `src/types/*` and `src/sync/*` both move into
  `Kalburator::Sync::*` with PlanStan consumers using `using namespace`
  TU-scope declarations. The tooling from the C.2 attempt
  (`wrap_namespace.py`, `add_using_cpp.py`, `add_using_header.py`)
  applies directly.
- C.3 (directory layering into blob/calendar/conflict/transcoding/
  journal/types), C.4 (SQLite IDMappingStore), C.5 (SyncStore dissolution)
  proceed per `04c-phase-c-plan.md` unchanged.

## Decisions log

Q1 — target structure: **split** (Kalburator::Types + Kalburator::Sync).
Rationale: avoids dragging KF6::DAV/KIO/Holidays into PlanStan::Core.

Q2 — atomicity: **per-PlanStan-lib, 4 commits** (1 libkalburator prep +
3 PlanStan cutovers). Rationale: smaller blast radius; git-bisect
locates regressions cleanly; duplicate sets don't overlap across libs
so intermediate states compile.

Q3 — safety gate: **no byte-identity diff gate**. Rationale: solo
developer, static branch, Phase 3a is 2 days old and was a verbatim
copy.

Q4 — `KALBURATOR_PROVIDE_TYPES` flag: **delete** in commit 1. Rationale:
no OFF use case post-migration; removes a dead path.

## References

- `04c-phase-c-plan.md` — original C.2 plan (supersedes note needed).
- `04d-phase-c2-blocker.md` — post-mortem of the first C.2 attempt; source
  of the C.2a/C.2b split.
- `04b-phase3-status.md` — option-1 layering disposition this design reverses.
- PlanStan `docs/proposals/2026-04-20-sync-library-extraction.md` —
  parent proposal; status line updated when C.2a lands.
