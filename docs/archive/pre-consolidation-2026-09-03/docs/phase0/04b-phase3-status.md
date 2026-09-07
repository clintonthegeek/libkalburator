# Phase 3 status — Phase 3 complete, Phase B partial

**Date:** 2026-04-20.
**Status:** Phase 3 done — libkalburator builds clean standalone,
and PlanStan now consumes it in-tree via `add_subdirectory` with
`KALBURATOR_PROVIDE_TYPES=OFF`. Old `libs/sync/` removed from PlanStan.

**Phase B (partial, 2026-04-20):** WP's `qsynccore/` lifted upstream —
6 pairs of files (`baselinestore`, `conflictpolicy`, `conflictrecord`,
`conflictstore`, `idmappingstore`, `synccommon`) copied from
`~/dev/WildPalms/src/sync/qsynccore/` into `src/sync/` (flat; layered
split is Phase C). `ConnectionBehavior` enum + fields + string
helpers stripped from `ConflictPolicy` (Palm-specific). Standalone
build clean; PlanStan builds clean against it; ctest baseline held
at 87 pass / 27 fail.

**Phase B deferred:**

- Rewrite `IdMappingStore` into SQLite-backed merged-schema
  `IDMappingStore` (Audit 2 decision). Currently present as WP's
  original JSON-backed implementation — dormant, no consumer.
- Add `ConflictHandlerRegistry` + `coordinator->registry()` public
  accessor (Audit 3). The `ConflictHandler` abstract + concrete
  `AutomaticConflictHandler` are already in `conflictpolicy.{h,cpp}`;
  just the registry wiring remains.
- PlanStan call-site migration off `SyncStore::setIdMapping` (Phase B
  item 7). Naturally folds into Phase C where `SyncStore` dissolves.
- `AsyncFileWriter` QSaveFile change was **dropped** — existing
  non-atomic writes are a deliberate performance tradeoff. See
  `04a-followups.md` Audit-4 revision.

Phase B remainder can land in a follow-up session or be merged into
Phase C (the layering + namespacing pass) — the two sub-phases touch
the same code regions and can be bundled without loss.

## Phase 3b resolution (2026-04-20 continuation)

> **Update (2026-06-10):** Phase 3b is fully closed. PlanStan subsequently migrated from
> the `add_subdirectory` cutover (described below) to a **FetchContent + pinned tag**
> consumption pattern. The `KALBURATOR_PROVIDE_TYPES` flag is no longer in active use.

The cutover used `add_subdirectory` rather than FetchContent, mirroring
the existing Graffodil consumption pattern at
`add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Graffodil graffodil
EXCLUDE_FROM_ALL)`.

Key mechanical steps:

1. Added `KALBURATOR_PROVIDE_TYPES` CMake flag to libkalburator.
   Default `ON` for standalone (Wild Palms, bare checkout). When `OFF`,
   `src/types/*` is not globbed into the build and `src/types` is not
   added to the target's include path — the host must supply those
   headers and symbols via PUBLIC linkage.
2. In PlanStan's top-level `CMakeLists.txt`: set
   `KALBURATOR_PROVIDE_TYPES=OFF`, then `add_subdirectory` libkalburator,
   then `target_link_libraries(kalburator PUBLIC PlanStan::Core
   PlanStan::Models PlanStan::Scheduling PlanStan::OrgIO)`.
3. Added transitional aliases `PlanStan::Sync` and `planstan-sync` for
   `kalburator` so existing consumer link lines needed no change.
4. Fixed six stale `${CMAKE_SOURCE_DIR}/libs/sync/include` references in
   `tests/{sync,backends,integration}/CMakeLists.txt` to point at
   `${CMAKE_SOURCE_DIR}/../libkalburator/src/sync`.
5. Deleted `libs/sync/` from PlanStan (~80 files).

Result: PlanStan builds clean, **87 passed / 27 failed** in ctest —
same 27-failure baseline as before the extraction started (one more
passing, likely a pre-existing flake settling).

## Follow-up — intention toward "option 2" layering

The present setup uses "option 1" from the cutover design: libkalburator's
`src/types/` is a toggleable duplicate. PlanStan and libkalburator can
drift, and if they do, silent ODR hazards appear. The plan of record is
to eventually move to "option 2": delete PlanStan's copies of the shared
types and make libkalburator the single source of truth. That requires
PlanStan to reorganize — libraries currently at or below `libs/models`
that use `BackendConfiguration`/`LogicalCalendar`/etc. would all link
libkalburator just to see those types. Deferred to a separate effort.

## Phase 3a resolution (2026-04-20 continuation)

The cascading dependency chase resolved in one short pass:

1. Copied `calendarmetadatamanager.{h,cpp}` from PlanStan's
   `libs/models/` into `src/types/`.
2. Copied `iincidencesource.h` from PlanStan's `libs/scheduling/include/`
   into `src/types/`.
3. Replaced the stub comment in `src/sync/calendarmanager.cpp`
   (`// collection.h removed — using icalendarcollection.h only`)
   with an actual `#include "icalendarcollection.h"`. The earlier
   "incomplete type" errors were simply this missing include — the
   compiler had never been given the full declaration.

With those three adjustments:

```
$ cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
$ cmake --build build
[100%] Built target kalburator
```

The `org-io=OFF akonadi=OFF` default profile produces
`build/libkalburator.a` with no undefined symbols.

## What exists now

- `CMakeLists.txt` — standalone project definition, Qt6 + KF6 deps,
  optional `KALBURATOR_HAVE_AKONADI` and `KALBURATOR_HAVE_ORG_IO` flags,
  globs source from `src/sync/` and `src/types/`, builds a single
  `kalburator` static library target with `Kalburator::Sync` alias.
- `src/sync/` — 81 files from `~/dev/PlanStan/libs/sync/`.
- `src/types/` — 19 files (the original 16 plus
  `calendarmetadatamanager.{h,cpp}` and `iincidencesource.h`).

## State of PlanStan

Unchanged — Phase 3a was entirely libkalburator-side. PlanStan's
`libs/sync/` is still live; cutover is Phase 3b.

## Phase 3b — next

1. Decide FetchContent vs git submodule vs published-release form.
2. Add libkalburator as a consumable dependency in PlanStan's top-level
   CMake, behind a feature flag so the in-tree `libs/sync/` can remain
   as a fallback during cutover.
3. Switch PlanStan's sync consumers to link the Kalburator target.
4. Retire `libs/sync/` from PlanStan once the cutover is stable.

Phase 3b is a PlanStan-side change and should be done in a session
rooted in the PlanStan tree, not here.

## What's in the libkalburator repo at Phase 3a close

```
libkalburator/
├── .gitignore
├── CMakeLists.txt          ← configures + builds clean
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
    └── types/    (19 files from PlanStan's libs/{core,models,scheduling}/)
```
