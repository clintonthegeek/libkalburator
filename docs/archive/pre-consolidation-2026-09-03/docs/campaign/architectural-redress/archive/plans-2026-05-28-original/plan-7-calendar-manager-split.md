# Plan 7 — Split `CalendarManager` and normalize stateless diff helpers

**Audit refs:** B7 (MODERATE), B8 (MODERATE)
**Depends on:** Plan 4 (backend collaborators settled — CalendarManager calls into
them).
**Branch:** `feature/redress-7-calendar-manager`
**State:** Architectural plan + first-task detail.

## Goal

`CalendarManager` answers a single question, the four-meaning `DeleteMode` becomes four
named methods, and `IncidenceDiff`'s 20+ static methods become free functions in an
`incidencediff` namespace. The rename to `CalendarOperations` follows in Plan 8.

## Problem (from AUDIT B7 + B8)

### B7 — CalendarManager

- 927 LOC, ~25 public methods, mixes Calendar CRUD + Binding CRUD + Incidence CRUD +
  bulk operations.
- `DeleteMode { Hide, Disable, Forget, DeleteFromAll }` collapses four operations with
  different semantics behind one method signature. Callers must read the enum
  documentation to know whether their call data-deletes or merely hides.
- Same procedural skeleton repeated across methods: resolve backends → check capabilities
  → call backend in order → regenerate sync mapping → update baseline → persist config.

### B8 — IncidenceDiff

- 1160 LOC, 20+ static methods. A namespace masquerading as a class.
- Same pattern in `vcarddiffer` and `icalvtododiffer` — but those are RecordDiffer
  *subclasses* (legitimate domain-specific algorithms), not static-method bags. The
  static-bag smell is specific to `IncidenceDiff`.

## Approach

Two independent surgical moves; can land in either order.

### B7 fixes

1. Split `DeleteMode` into four explicit methods: `hideCalendar(id)`,
   `disableCalendar(id)`, `forgetCalendar(id)`, `deleteCalendarEverywhere(id)`. Each is
   one paragraph of code, no `switch (mode)`.
2. Extract a `BaselineUpdater` collaborator — the post-mutation baseline refresh logic
   that appears in nearly every CRUD method becomes one method on the collaborator,
   called once.
3. Optionally split into `CalendarLifecycle` (create/update/delete/hide/disable) +
   `IncidenceCRUD` (create/update/delete incidence). Decide in Task 1 by reading the
   consumer list; if every consumer uses both, the split costs more than it saves and
   we keep one class.
4. Leave the rename to `CalendarOperations` for Plan 8 (which sweeps all naming).

### B8 fixes

1. `class IncidenceDiff` becomes `namespace incidencediff { ... }`. Each static method
   becomes a free function.
2. No new behaviour; this is pure refactoring.
3. The protective test is `tests/calendar/tst_incidencediff.cpp` (if absent, write it
   first by exercising the public surface of the class against captured input/output
   pairs).
4. `vcarddiffer` and `icalvtododiffer` are **not** changed by this plan; the audit
   explicitly notes they carry domain-specific algorithms and are legitimately classes.

## Tasks

### Task 1 — Inspect CalendarManager consumers and decide split granularity

1. `git grep -l 'CalendarManager' src/ tests/ ~/dev/PlanStan/src ~/dev/WildPalms/src
   2>/dev/null`.
2. For each consumer, note which CalendarManager methods they call. Group by concern
   (Calendar CRUD, Binding CRUD, Incidence CRUD, bulk).
3. Decision: single class with method renames, or split into LifecycleOperations +
   IncidenceCRUD. Record in STATUS.

### Task 2 — Protective tests

1. `tests/calendar/tst_calendarmanager_split.cpp` — exercise the public surface that
   the split must preserve. One scenario per method group.
2. Verify green against unmodified manager.

### Task 3 — Replace `DeleteMode` with four named methods

1. Add the four new methods, each implementing one branch of the current `switch`.
2. Mark `deleteCalendar(id, DeleteMode mode)` as `[[deprecated]]` with the migration
   pointer.
3. Update internal call sites; downstream consumers continue to compile.
4. The actual deletion of the deprecated overload happens in Plan 8.

### Task 4 — Extract `BaselineUpdater`

(Detail written after Task 3. Sketch: identify the duplicated post-mutation block,
extract to `calendar/baselineupdater.{h,cpp}` or as a private method if the duplication
is genuinely textual not semantic — judge by how much state the block reads.)

### Task 5 — Execute the split-or-not decision from Task 1

(Detail after Task 1. Either rename methods to clarify groups within one class, or move
incidence CRUD methods to a separate class.)

### Task 6 — Convert IncidenceDiff to namespace

1. `tests/calendar/tst_incidencediff.cpp` — protective tests on the existing static
   surface. Verify green.
2. Wholesale convert: `class IncidenceDiff { static ... }` → `namespace incidencediff`,
   methods become free functions, callers update `IncidenceDiff::foo(...)` →
   `incidencediff::foo(...)`.
3. Run the tests. Green.

### Task 7 — Re-run tests and close

1. Full ctest.
2. PlanStan ctest.
3. WildPalms smoke if locally runnable.
4. Update FINDINGS, open Plan 8.

## Files affected (anticipated)

- `src/calendar/calendarmanager.{h,cpp}` — `DeleteMode` removed, four methods added,
  possibly split.
- `src/calendar/calendarincidencecrud.{h,cpp}` — **new** if split chosen.
- `src/calendar/baselineupdater.{h,cpp}` — **new** if extracted.
- `src/calendar/incidencediff.{h,cpp}` — class → namespace.
- `tests/calendar/tst_calendarmanager_split.cpp`, `tst_incidencediff.cpp` — **new**.

## Acceptance criteria

- `DeleteMode` enum no longer exists in CalendarManager's public surface (or is
  `[[deprecated]]` pending Plan 8 sweep).
- Four named delete-flavor methods exist with one-line summaries explaining what they
  do without consulting an enum.
- `IncidenceDiff` is a namespace; no `class IncidenceDiff` remains.
- All tests pass; PlanStan ctest baseline holds.

## Risks

- **PlanStan / WildPalms call `deleteCalendar(id, DeleteMode)` directly.** Likely.
  Keep the deprecated shim until Plan 8 closes; coordinate the downstream migration
  before deletion.
- **Baseline timing.** The post-mutation baseline refresh order matters; if extraction
  re-orders it, sync may go subtly wrong. Tests in Task 2 must include a baseline-state
  assertion after each mutation.

## Estimated effort

2–3 sessions.
