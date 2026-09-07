# Plan 6 — Decouple `shape/` from `types/BackendRecord` and `conflict/ConflictPolicy`

**Audit refs:** B6 (MAJOR)
**Depends on:** Plan 3 (BackendRecord's home decided; the shape/Record interface lands
where the layering rules let it).
**Branch:** `feature/redress-6-shape-decoupling`
**State:** Architectural plan + first-task detail.

## Goal

`shape/` is what its name promises: the abstract transformation layer. It does not
`#include` a concrete domain record, and it does not `#include` a concrete conflict
policy. Both dependencies become interfaces declared in `shape/` (or thin neutral
headers shape/ owns).

## Problem (from AUDIT B6)

- `shape/recordwriter.h` `#include`s `backendrecord.h` (a concrete record type living
  in `types/`). The abstract writer is coupled to one concrete record shape.
- `shape/recordmerger.h` and `shape/canonjsonmerger.h` `#include`
  `conflict/conflictpolicy.h`. Shape transformations now know about conflict resolution
  policy names, which is an inversion: conflict handling should consume shape, not the
  other way around.

## Approach

Two surgical moves:

### 1. Introduce `shape/Record` interface; make `BackendRecord` an adapter

`shape/record.h` defines an abstract record (id, payload bytes, shape identity,
metadata bag). `BackendRecord` (wherever it lives post-Plan 3 — `types/` if it survives
as a value type, else `models/`) is an *adapter* implementing `shape::Record`.

Writer and other shape/ classes work against the interface. Concrete record types
live where they live; shape/ stops `#include`ing them.

### 2. Move `ConflictPolicy` into a neutral home shape/ owns

Per AUDIT: `ConflictPolicy` is a generic enum/struct (ours, theirs, manual, custom). The
audit also notes the term genuinely belongs in shape/ rather than conflict/ — conflict/
should *consume* the policy, not define it.

Move `conflict/conflictpolicy.{h,cpp}` → `shape/conflictpolicy.{h,cpp}`. Rename
`conflict/` → `conflicthandling/` (or similar) to make its scope clear: it handles
conflict state and the UI surface around it, distinct from the policy enum.

Alternative (judgement call deferred to Task 1): if `ConflictPolicy` carries
domain-specific resolution logic that we have not noticed, leave it in `conflict/` and
*pass* the policy by value as a parameter to merger methods (eliminate the `#include`,
keep the file location). Decide by inspection.

## Tasks

### Task 1 — Inspect and decide ConflictPolicy direction

1. Read `conflict/conflictpolicy.h` and `.cpp`. Confirm whether it's a pure enum/struct
   or carries logic.
2. Decision: move to shape/, or leave and pass by value. Record in STATUS.

### Task 2 — Introduce `shape/record.h` interface

(Detail written after Plan 3 lands and BackendRecord's location is final. Sketch:
define abstract `class shape::Record` with the minimum surface `RecordWriter` needs;
adapt `BackendRecord` via inheritance or a thin wrapper. The wrapper approach avoids
forcing every record-like type to be a polymorphic class — a `concept`-style template
over the existing `BackendRecord` may suffice for C++20.)

### Task 3 — Rewire RecordWriter and friends

(Detail after Task 2.)

### Task 4 — Execute the ConflictPolicy decision from Task 1

(Detail after Task 1.)

### Task 5 — Re-run tests and close

1. Full ctest.
2. `grep -n 'backendrecord' src/shape/*.h` returns empty.
3. `grep -n 'conflictpolicy' src/shape/*.h` returns empty *or* returns only
   `shape/conflictpolicy.h` (depending on Task 1 decision).
4. Update FINDINGS, open Plan 7.

## Files affected (anticipated)

- `src/shape/record.h` — **new** interface.
- `src/shape/conflictpolicy.{h,cpp}` — **new** location, possibly.
- `src/shape/recordwriter.h` — `#include` of `backendrecord.h` removed.
- `src/shape/recordmerger.h`, `canonjsonmerger.h` — `#include` of `conflictpolicy.h`
  removed or repath.
- `src/conflict/` — possibly renamed `conflicthandling/`; depends on Task 1 outcome.

## Acceptance criteria

- `shape/*.h` does not `#include` any file from `types/`, `conflict/`, `calendar/`,
  `contacts/`, or any other domain.
- All tests pass; PlanStan ctest baseline holds.

## Risks

- **Polymorphism cost.** Making `Record` a virtual interface adds a vtable to every
  record value. Measure with the existing perf docs (`docs/perf/`) if any; prefer the
  template/concept approach if measurable.
- **Conflict-handling UI consumers.** Whatever consumes `conflict/` for UI purposes
  (the prior memory's `config-widget` work) must continue to find its types. The rename
  is the riskier piece.

## Estimated effort

2–3 sessions.
