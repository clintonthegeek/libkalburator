# Phase F.0 — Test gap closure (design)

**Date:** 2026-04-29
**Status:** Landed 2026-04-29 on tag `v0.12-phase-f0-test-gaps`.
Approved 2026-04-29 by user via brainstorming session.
Implementation plan in `04o-phase-f0-test-gaps-plan.md` (sibling).
**Gates:** Phase E complete (`v0.11-phase-e-transcoding-backends`).

## Goal

Close the highest-value test gaps before Phase F (Unify) starts to
refactor the engine. Phase F's audit (during the brainstorm) found
that the calendar test suite has strong behavior pinning at the
current `SyncCoordinator`/`SyncWorker` boundary but two gaps that
matter for F:

1. **Backend-write error propagation is only tested in PlanStan.**
   `PlanStan/tests/sync-workflow/tst_sync_error_recovery.cpp`
   is the only test that exercises `MockBackend::setFailurePoint()`
   and asserts `SyncResult.success == false` on backend write
   failures. Phase E nearly broke this contract (see FINDINGS
   "Wrapper commit() lost error detection…"); we got lucky that
   `verify-all.sh` runs PlanStan. libkalburator must own this test.

2. **The 24 PlanStan "failures" reported by `verify-all.sh` are
   uncategorised noise.** Some could in principle hide
   sync-engine bugs; without triage we can't tell.

The brainstorm settled on closing only these two gaps — F.0 (b) in
the brainstorm options. Other audit-identified gaps (cancellation
tests, separable diff/merge tests, unified-engine boundary tests,
test-decoupling from `SyncCoordinator` naming) require Phase F's
APIs to exist before they can be written, and are deferred to be
co-developed with F1/F2 TDD-style.

## Decisions made during the brainstorm

The brainstorm resolved one fork explicitly. Recorded here so the
plan and any future revision pass have a single source of truth.

### 1. Scope: option (b) — error-recovery test + failure triage only

Four scope options were considered in the F-scoping question:

- **(a) Whole F.0 bundle (8–12 new test files).** Audit's
  prescription: error recovery + cancellation API + adapter
  boundary + separable diff/merge + decouple-from-SyncCoordinator.
- **(b) Minimal F.0 — items (i) + (ii) only.** Library-side
  error-recovery test; triage of the 24 PlanStan failures.
- **(c) Triage only (item (ii)).** Document, no new tests.
- **(d) No formal F.0; bundle into F's plan as Group 0.**

**Settled: (b).** Half of the audit's prescription (cancellation
API tests, adapter-boundary tests, unified-engine tests) cannot be
written today because the APIs don't exist yet. They must be
co-developed with the API in Phase F itself, TDD-style. (a)
overstates the prerequisite work; (c) skips the only real
coverage gap (error recovery); (d) loses the independent
verify-all-green gate that the D / D.0 split pioneered. (b) is
the one option that closes the genuine pre-F gaps without
demanding work that depends on F's not-yet-written APIs.

## Triage outcome — the 24 PlanStan failures

Pre-design triage of `baselines/planstan-worktree-ctest.txt`:

| Count | Status | Tests | Category | Phase F relevance |
|---|---|---|---|---|
| 13 | Not Run | `tst_graphscene`, `tst_groups`, `tst_edgepathstrategies`, `tst_terminus`, `tst_graphedgeitem`, `tst_tools`, `tst_circular`, `tst_sugiyama`, `tst_spatialgrid`, `tst_quadtree`, `tst_forcelayout`, `tst_batchrenderer`, `tst_integration` | Graph-layout subsystem (PlanStan-internal). EXCLUDE_FROM_ALL or flag-gated; binaries not built. | None. Unrelated to sync. |
| 9 | Not Run | `integration_*` (recurrence_editing, template_system, incidence_reschedule, collection_switching, calendarcrud, incidencecrud, app_workflow, collection_lifecycle, incidence_crud) | Integration-test executables; same EXCLUDE_FROM_ALL pattern as the four sync-workflow tests documented in FINDINGS. | None. Pre-date refactor. |
| 1 | Failed | `tst_inboxmanager` | PlanStan-internal inbox feature; non-sync. | None. |
| 1 | Failed | `sync_workflow_caldav` | Environmental — needs running Radicale + correct user setup. QWARN trail shows D-Bus registration failures + HTTP 412 from misconfigured CalDAV server. | None. Test logic is fine; setup is wrong. |

**Conclusion: zero of the 24 are real coverage that affects Phase
F.** The triage produces no new test work. The result is recorded
as a FINDINGS entry so future agents don't re-derive it.

## Components

### `tst_calendar_sync_error_recovery` — new integration test

Lives at `tests/calendar/tst_calendar_sync_error_recovery.cpp`.
Follows the existing `tst_calendar_sync_full` /
`tst_calendar_conflict` pattern: stub-`ISyncHost` (already in
`tests/calendar/stubs/`) + two `MockBackend` instances (source +
target) registered in a `BackendRegistry` + a `SyncCoordinator`
wired up the same way the other integration tests wire it.

Test methods (one per failure surface):

```cpp
class TestCalendarSyncErrorRecovery : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Backend write-failure paths (Phase E's wrapper-commit pattern)
    void targetStoreItemsFailure_propagatesAsSyncResultFailure();
    void targetUpdateItemFailure_propagatesAsSyncResultFailure();
    void targetDeleteFailure_propagatesAsSyncResultFailure();

    // Backend fetch-failure path
    void sourceFetchFailure_propagatesAsSyncResultFailure();
    void targetFetchFailure_propagatesAsSyncResultFailure();
};
```

Each test:

1. Sets up source + target `MockBackend`s with a single calendar
   each, populated with a small fixture (one or two events) so
   that the sync attempts a real write or fetch.
2. Calls `m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems)`
   (or `OnFetch`, `OnDelete` per test method) on the relevant
   backend.
3. Runs the sync via `m_coordinator->runSync(SyncBehavior::All)`.
4. Waits on `SyncCoordinator::allSyncsCompleted`.
5. Asserts `result.success == false`.
6. Asserts `result.errorMessage` is non-empty.
7. (Optional, where applicable) asserts that the source side
   was not mutated when only the target failed — the data-integrity
   property that PlanStan's `tst_sync_error_recovery` verifies.

Test isolation: `cleanup()` clears the registry and tears down
the coordinator. Each test gets a fresh `QTemporaryDir` for the
SQLite stores. This matches the pattern used by the other
integration tests in this directory.

### `tst_sync_error_recovery` parity

The libkalburator-side test does **not** need to mirror PlanStan's
test method-for-method. PlanStan's test has additional scenarios
(JSON workflow files, bidirectional integrity assertions) that
exercise PlanStan-specific orchestration. The library-side test
covers only the SyncBackend → SyncWorker → SyncCoordinator
error-propagation chain, which is the engine-side contract Phase F
must preserve.

When Phase F refactors `SyncCoordinator` into `SyncEngine`, this
test pins the contract: backend write failure ⇒ `success == false`
on the public completion signal. The test keeps PlanStan's
`tst_sync_error_recovery` as a redundant cross-check, not as a
sole defender.

## Test plan

### Existing tests that must stay green

All 20 libkalburator tests; all 73 WildPalms tests; PlanStan
unchanged at 96/120 (the same 24 noise items per the triage above).
F.0 adds tests; it does not modify any existing code.

### New test added in this phase

`tst_calendar_sync_error_recovery` — integration test, five test
methods. Compiled via the existing
`kalburator_add_calendar_integration_test()` CMake helper.

### Acceptance criteria

- libkalburator standalone: 20 → 21 ctest executables. All pass.
- PlanStan baseline: 96/120 (unchanged — F.0 doesn't touch PlanStan).
- WildPalms baseline: 73/73 (unchanged).
- `verify-all.sh` exit 0 after baseline refresh.
- FINDINGS appended with the triage outcome (above) so it doesn't
  need re-deriving.

## Migration order (within Phase F.0)

1. Write `tst_calendar_sync_error_recovery.cpp`. Build it. Run it.
2. Add the test to `tests/calendar/CMakeLists.txt` via
   `kalburator_add_calendar_integration_test`.
3. Run the full libkalburator suite; expect 21/21 green.
4. Run `verify-all.sh`; expect exit 3 (test improvement: count
   went 20 → 21). Refresh `baselines/libkalburator-worktree-ctest.txt`
   and re-run; expect exit 0.
5. Append the triage finding to FINDINGS.
6. Update `04k-engine-merger-roadmap.md` table to add a Phase F.0
   row, and the table's Phase F row to reflect the F1/F2 slice
   decided during brainstorming.
7. Update `CURRENT-STATUS.md` (move F.0 from "Next" to "Where we
   are"; replace "Next" with F1).
8. Tag `v0.12-phase-f0-test-gaps` (user runs the tag command).

## Deferred / future work

The audit identified additional test gaps that do not belong in
F.0 because the corresponding APIs do not yet exist. They will
be co-developed with the relevant phase, TDD-style:

- **Cancellation tests** — Phase F2. Will exercise
  `QFuture::cancel()` propagation on a running sync.
- **`IDomainAdapter` boundary tests** — Phase F1. Tests that
  call `IDomainAdapter::diff()` and `merge()` directly, in
  isolation from a full-sync flow.
- **Separable diff/merge tests** — Phase F1, alongside the
  adapter tests above.
- **Decouple existing tests from `SyncCoordinator` naming** —
  Phase F1, as a side effect of the rename to `SyncEngine`.
- **`tst_engine_unified_boundary`** — Phase F1, pinning
  the public `SyncEngine::runSync()` contract.

## Cross-references

- `~/dev/refactor-engine-merger/CLAUDE.md` — F.0 sits in the
  ROADMAP between E and F1.
- `04k-engine-merger-roadmap.md` — phase tagging convention; F.0
  shifts F to v0.13 and pushes G to v0.15.
- `PlanStan/tests/sync-workflow/tst_sync_error_recovery.cpp` —
  the reference pattern. Library-side test borrows the failure-
  injection idiom; reuses libkalburator's existing
  `tests/calendar/stubs/` rather than re-defining `StubSyncHost`.
- `~/dev/refactor-engine-merger/FINDINGS.md` — "Wrapper commit()
  lost error detection when switching from pushItems to
  storeItems" (2026-04-29) is the precise regression this test
  guards against.
- `~/dev/refactor-engine-merger/baselines/planstan-worktree-ctest.txt`
  — the source for the triage table.
