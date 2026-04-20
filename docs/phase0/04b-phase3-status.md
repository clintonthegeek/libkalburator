# Phase 3 status — Phase 3a complete

**Date:** 2026-04-20.
**Status:** Phase 3a done — libkalburator builds clean standalone.
Phase 3b (PlanStan FetchContent cutover) not yet started.

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
