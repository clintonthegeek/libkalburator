# Plan 5 — Consolidate `backend/`, `storage/`, `universal/` under a principled split

**Audit refs:** B5 (MAJOR)
**Depends on:** Plan 2 (the cycle break may shift what lives where; do this after).
**Branch:** `feature/redress-5-backend-dirs`
**State:** Architectural plan + first-task detail. Subsequent tasks written when Plan 2
lands.

## Goal

Three backend-adjacent directories with one clear principle each, or two if the third
collapses cleanly. New code has an obvious home; new readers can predict where to look.

## Problem (from AUDIT B5)

- `src/backend/` — **1 file** (`changedetection.h`). Officially a "capabilities" layer
  by intent, but with one inhabitant it reads like a leftover.
- `src/storage/` — `baselinestore.h`, `idmappingstore.h`. Persistence side-tables that
  the sync layer consults.
- `src/universal/` — `genericsqlitebackend`, `rawfilesbackend`, `markdownfilesbackend`,
  `filteredcollectionbackend`. Concrete domain-generic backends.

No clear principle distinguishes the three. The split looks accidental (a refactor
history artifact); a new contributor reaching for a file has no rule to guide them.

## Approach

Pick one of two coherent splits:

### Option A — Three dirs with clear roles (preferred if backend/ grows in Plan 4 byproduct)

- `src/capabilities/` (rename of `backend/`) — capability *interfaces* (mixin abstracts
  that backends implement when they can). `ChangeDetection` is the seed; Plan 4 may add
  more (e.g. a `CalDavCTagStore` interface if extracted as a capability).
- `src/storage/` — persistence side-tables (`BaselineStore`, `IdMappingStore`). Things
  the engine writes/reads about state, not the user's data.
- `src/backends/` (rename of `universal/`) — concrete backend implementations that
  serve multiple domains (`GenericSqliteBackend`, `RawFilesBackend`,
  `MarkdownFilesBackend`, `FilteredCollectionBackend`). Domain-specific concrete
  backends continue to live in their domain dir.

### Option B — Collapse to two

- `src/capabilities/` — `ChangeDetection` and any peer interfaces.
- `src/backends/` — both the universal concrete backends and the persistence
  side-tables (`BaselineStore` etc.), since they're all "backend infrastructure".

Decision deferred to Task 1 inspection — read each file, see whether `storage/`'s
contents actually share a coherent reason with `universal/`'s; pick A or B based on
evidence. Record the decision in STATUS.

## Tasks

### Task 1 — Inspect and decide

1. Read each file in `backend/`, `storage/`, `universal/` (it's not many — under a
   thousand LOC total for the directory boundaries).
2. Confirm consumers of each: who `#include`s `BaselineStore`? `ChangeDetection`?
   `GenericSqliteBackend`? Build a small table.
3. Decision: A or B. Record in STATUS as a Locked decision with one-sentence rationale.

### Task 2 — Execute the move

(Detail written after Task 1. Mechanical: `git mv`, update `#include` paths, update
`CMakeLists.txt`. No code logic changes.)

### Task 3 — Reaffirm or re-place `ChangeDetection`

After Plan 4, `ChangeDetection` may have lost its only direct callers (the backend's
own duplicate-API names are gone; the engine reaches it through the interface). Verify
the interface still earns its keep:

1. Who calls `ChangeDetection*` post-Plan 4? Confirm at least one consumer remains.
2. If yes — `capabilities/changedetection.h` is the new home, and the interface
   pattern is the seed for future capability mixins. Good.
3. If no — `ChangeDetection` is dead post-Plan 4 and goes in Plan 9. Move the FINDING.

### Task 4 — Re-run tests and close

1. Full ctest.
2. PlanStan ctest (reachable surface).
3. Update FINDINGS, open Plan 6.

## Files affected (anticipated, Option A)

- `src/capabilities/` — **new** (rename of `backend/`).
- `src/backends/` — **new** (rename of `universal/`).
- `src/storage/` — unchanged, or collapsed into `backends/` per Option B.
- Many `#include` lines updated repo-wide.

## Acceptance criteria

- Each directory's README (or top-comment in its CMakeLists.txt source list) names its
  role in one sentence, and the rule for what belongs there.
- A grep audit confirms no file `#include`s another module out of role
  (e.g. `capabilities/` doesn't include any concrete backend).
- All tests pass; PlanStan ctest baseline holds.

## Risks

- **Naming collisions with the existing `backendregistry.h` in sync/.** `backends/`
  (plural) and `BackendRegistry` (singular) cohabit cleanly in C++ but a reader could
  confuse them. Verify in Task 1 that the plural-vs-singular convention is clear.
- **PlanStan/WildPalms include lines.** If any downstream consumer `#include`s
  `<libkalburator/backend/changedetection.h>`, the rename breaks them. Coordinate or
  provide a compat header.

## Estimated effort

1–2 sessions. Almost entirely mechanical.
