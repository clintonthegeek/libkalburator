# Phase C.2 — blocker discovered, attempt reverted

**Date:** 2026-04-20.
**Status:** C.2 attempted, hit an architectural blocker, reverted in
both repos (libkalburator and PlanStan). C.1 remains landed. C.2 plan
in `04c-phase-c-plan.md` needs revision before retry — see
"Required plan revision" below.

## What was attempted

Following the C.2 plan as written:

1. Wrapped 102 libkalburator files (`src/sync/*` + `src/types/*`, minus
   qsynccore files already in `Kalburator::Sync` from C.1) in
   `namespace Kalburator::Sync { ... }`. Metatype macros hoisted
   outside the namespace with `Kalburator::Sync::` qualification.
   Global forward-decls of Qt types (`QNetworkReply`, `QWidget`,
   `QUndoStack`) hoisted above the namespace open.
2. Re-located qsynccore files from `Kalburator::Sync::` to
   `Kalburator::Sync::QSyncCore::` sub-namespace to avoid collision
   between WP's `ConflictType` / `ConflictResolution` and PlanStan's
   same-named enums in `synctypes.h`. Compatibility alias updated:
   `namespace QSyncCore = Kalburator::Sync::QSyncCore;`.
3. Added `using namespace Kalburator::Sync;` at TU scope to 131
   PlanStan cpp consumers and 50 PlanStan headers (marked with
   `TODO(phase-c-cleanup)`).

libkalburator standalone build was clean after (1)+(2). PlanStan build
failed hard at step (3).

## The blocker

C.2's namespace migration is **incompatible with the current option-1
layering** documented in `04b-phase3-status.md` under "Follow-up —
intention toward option 2".

Option-1 layering means:

- PlanStan keeps its own duplicate copies of shared types at
  `libs/core/include/` (e.g. `backendconfiguration.h`,
  `isyncconfigstore.h`, `logicalcalendar.h`, `synctypes.h`,
  `iincidencesource.h`, `ilocalesource.h`, `icalendarcollection.h`,
  etc.) at **global namespace scope**.
- libkalburator keeps its copies at `src/types/` — after C.2 these
  are in `Kalburator::Sync::`.
- PlanStan consumes libkalburator with `KALBURATOR_PROVIDE_TYPES=OFF`,
  so libkalburator's `src/types/` is NOT on the consumer include path.
- When libkalburator's own sync code (e.g.
  `src/sync/calendarmanager.cpp`) does `#include "isyncconfigstore.h"`,
  the include resolves to **PlanStan's copy** (global scope), not
  libkalburator's (namespaced).

Before C.2, libkalburator's sync code was at global scope too, so
PlanStan's global-scope types resolved naturally at every call site.

After C.2, libkalburator's sync code lives in `Kalburator::Sync::` and
all forward declarations in its headers (`class ISyncConfigStore;`
inside the namespace) now refer to `Kalburator::Sync::ISyncConfigStore`.
The `#include` pulls in PlanStan's `::ISyncConfigStore` — a different
type. Every member access through a forward-declared pointer fails:

```
error: invalid use of incomplete type
    ‘class Kalburator::Sync::ISyncConfigStore’
```

Affected libkalburator files when built under PlanStan's consumer
setup include:

- `calendarmanager.cpp` (ISyncConfigStore, ICalendarCollection,
  CalendarBackendBinding — all incomplete)
- `synccoordinator.cpp` (ISyncConfigStore member access)
- `remotebackend.cpp` (CalendarBackendBinding, BackendCapabilities)
- `localbackend.cpp`, `orgbackend.cpp`, `decsyncbackend.cpp`,
  `akonadibackend.cpp`, `holidaysubscriptionbackend.cpp`
- likely more — the build aborted early

## Why the original plan missed this

The C.2 plan in `04c-phase-c-plan.md` describes the flag-day commit
as touching "80+ files in libkalburator, 200+ in PlanStan" and warns
about single-commit atomicity, but it treats the migration as purely
mechanical (wrap files, qualify references). It does not address the
type-duplication asymmetry:

> [From 04b-phase3-status.md]
> The present setup uses "option 1" from the cutover design:
> libkalburator's `src/types/` is a toggleable duplicate. PlanStan
> and libkalburator can drift... The plan of record is to eventually
> move to "option 2": delete PlanStan's copies of the shared types
> and make libkalburator the single source of truth.

The plan said option 2 is "deferred to a separate effort." What
C.2's attempt showed: **option 2 is a prerequisite for C.2**, not an
independent follow-up. Any namespace migration of libkalburator that
PlanStan must consume forces one of:

1. Both repos put their shared-type copies in the same namespace
   (option 2, for real).
2. libkalburator keeps its `src/sync/` code at global scope so it
   can transparently consume PlanStan's global-scope types.
3. libkalburator's `src/sync/` uses only host-provided types via
   abstract interfaces, and defers its own `src/types/` duplicates
   to standalone-only code paths — essentially a much deeper
   decoupling than the extraction has done so far.

Option-1 layering is inherently single-namespace: you can't have the
library code in one namespace and the host-provided types in another
unless the host also places its types in the library's namespace
(which is option 2).

## What's landed vs reverted

- **Landed:** C.1 (qsynccore into `Kalburator::Sync::QSyncCore`) —
  commit `515ade2` on libkalburator's `main`.
- **Reverted:** all C.2 work in both repos (`git reset --hard HEAD`
  on libkalburator and PlanStan).
- **Note:** the C.1 compatibility alias `namespace QSyncCore =
  Kalburator::Sync;` in `synccommon.h` still points at the top
  namespace, not `Kalburator::Sync::QSyncCore`. That's fine as long
  as qsynccore files stay in the top namespace — which they do now
  that C.2 is reverted. If/when C.2 is retried, the sub-namespace
  relocation of qsynccore may or may not be needed depending on how
  the `ConflictType`/`ConflictResolution` collision is resolved.

## Required plan revision

Before retrying C.2, the plan must answer:

1. **Which option?** Commit to option 2 as a prerequisite, OR
   explicitly keep libkalburator's `src/sync/` at global scope and
   only namespace types in `src/types/`, OR design a third path.
2. **PlanStan migration scope.** If option 2: what is the scope of
   the PlanStan-side reorganization? Every library at or below
   `libs/models` that references `BackendConfiguration`,
   `LogicalCalendar`, `SyncTypes`, `IncidenceRef`, `CalendarType`,
   etc. must either link libkalburator for the types or move into
   a compatible namespace. This is a much larger refactor than C.2
   as planned.
3. **Conflict-type reconciliation.** The collision between
   qsynccore's `ConflictType`/`ConflictResolution` (dormant) and
   PlanStan's `synctypes.h` set (active) remains. The attempted
   sub-namespace workaround (Kalburator::Sync::QSyncCore) would
   still be needed under option 2. Make the reconciliation or
   sub-namespace choice explicit in the revised plan.
4. **Atomicity.** C.2 was already scoped as a single flag-day commit
   per repo. With option 2 included, the flag-day grows — PlanStan
   loses `libs/core/include/`'s shared types AND gains them back
   from libkalburator in the same commit, across every consumer.
   Consider whether this is still feasible as two commits (one per
   repo) or whether the scope now requires a multi-session plan
   with incremental sub-steps that each build green.

## Recommendation

The mechanical migration tooling works (Python scripts wrapping
libkalburator headers/cpps, adding `using` directives to consumers).
The surgical fixes for global forward-decls of Qt types and
metatype macros are straightforward. The architectural work — which
types live in which namespace, and how PlanStan reorganizes to
consume libkalburator's types directly — is the part the plan
needs to own before any more code is written.

Phase C.2 should be rewritten as two sub-steps:

- **C.2a — layering migration.** Option 2 (delete PlanStan's
  duplicate type headers; PlanStan consumes libkalburator's
  `src/types/` with `KALBURATOR_PROVIDE_TYPES=ON`; update every
  PlanStan consumer of these types). Plan a green-at-every-step
  sequence — likely N commits, not one. Design doc needed.
- **C.2b — namespace migration.** The original C.2 scope: wrap
  libkalburator's `src/sync/` and remaining non-namespaced types
  in `Kalburator::Sync::`. Update PlanStan consumers (now sourcing
  types from libkalburator) to qualify or `using`-declare. The
  tooling already built (`/tmp/wrap_namespace.py`,
  `/tmp/add_using_cpp.py`, `/tmp/add_using_header.py`) applies.
  This commit really is a flag-day — but it's tractable **after**
  C.2a lands.

C.3–C.6 proceed unchanged once C.2b completes. `04c-phase-c-plan.md`
should be superseded by a revised plan that sequences C.2a → C.2b.

## Artifacts from the attempt

Not committed anywhere, but documented for the retry:

- `/tmp/wrap_namespace.py` — wraps libkalburator files in
  `namespace Kalburator::Sync`, hoists Qt forward-decls and
  metatype macros correctly.
- `/tmp/add_using_cpp.py` / `/tmp/add_using_header.py` — adds
  `using namespace Kalburator::Sync;` to PlanStan consumers.
- `/tmp/kalb_real_syms.txt` — 105 symbols to migrate (with
  forward-decl-only false positives filtered).
- `/tmp/planstan_real_consumers.txt` — 181 PlanStan files that
  actually reference these symbols (50 headers + 131 cpp, minus
  the ~52 that revert cleanly because they consume PlanStan's
  global-scope duplicates, not libkalburator).

## Trigger summary

What the user needs to decide before this retry:

- Do we take on option-2 migration now (large PlanStan-side refactor),
  or pick one of the alternative paths (leave sync at global scope;
  decouple libkalburator further)?
- If option 2: plan and sequence the PlanStan reorganization as its
  own design pass (C.2a).
