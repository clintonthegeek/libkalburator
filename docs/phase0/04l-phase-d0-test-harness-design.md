# Phase D.0 — calendar-layer integration test harness (design)

**Date:** 2026-04-28
**Status:** Approved 2026-04-28 by user via brainstorming session.
Implementation plan in `04l-phase-d0-test-harness-plan.md` (sibling).
**Phase tag on completion:** `v0.9-phase-d0-tests-first`.

## Goal

Phase D.0 of `04k-engine-merger-roadmap.md`: add stub-`ISyncHost`
integration tests to libkalburator's calendar layer. The calendar
engine (`SyncWorker`, `SyncCoordinator`, `SyncBackend`,
`CalendarManager`) has zero library-owned tests today. The blob layer
has 3 (`tests/blob/`) and `tests/calendar/` has one focused unit test
(`tst_icsfeedfetcher`) but no integration coverage of the sync engine.

Refactoring the calendar engine through Phases D / E / F / G without
contract-level tests at the libkalburator boundary is too risky.
Phase D.0 closes that gap before any structural change.

## Decisions made during the brainstorm

The brainstorming session settled three judgment calls:

1. **Ambition: extract reusable stubs, keep scenarios inline.** Four
   stub classes (`StubSyncHost`, `StubCalendarCollection`,
   `StubIncidenceRegistry`, `StubSyncConfigStore`) are needed by every
   test and by future Phase D/E/F/G tests, so they're worth promoting
   to a small static library. Per-scenario fixture builders stay
   inline as static helpers in each `tst_*.cpp`; if duplication
   becomes painful (≥ 2 tests want the exact same shape), promote to
   a `calendar_fixtures.{h,cpp}` later. **YAGNI** until then.

2. **Test partition: 4 executables, grouped by shared production
   path.** The roadmap lists 5 scenarios; reading the production code
   shows "conflict detection" and "3-way merge" share
   `SyncDiff::computeThreeWayDiff` — they're not independent. Folding
   them yields:

   | Executable                       | Scenario                          |
   |----------------------------------|-----------------------------------|
   | `tst_calendar_sync_full`         | Full bidirectional sync           |
   | `tst_calendar_sync_oneway`       | One-way upload sync               |
   | `tst_calendar_conflict`          | Conflict + 3-way merge (both modes)|
   | `tst_calendar_transcoding_warning` | Transcoding warning emission    |

3. **Conflict test covers both modes.** `SyncWorker` has two distinct
   conflict-handling code paths — `Mode::Monitored` (pause via
   `conflictPauseRequested` signal, resume via `resumeAfterConflict`)
   and `Mode::Unmonitored` (auto-resolve per policy via
   `conflictDetected` signal). Both will be touched during the
   refactor. `tst_calendar_conflict` covers each as a separate
   QTest method.

## Important production findings

The survey-time concern that conflict pause/resume uses a blocking
`QEventLoop` on the worker thread was wrong. Reading `SyncWorker.h:
164–195` and `SyncWorker.cpp` shows the monitored-mode pause is
**signal-based state machine yielding**, not thread blocking — it
returns from `handleConflicts()` and resumes when
`resumeAfterConflict()` is invoked from the main thread. Tests use
the standard `QSignalSpy` + emit-signal pattern; no special harness.

`MockBackend` (the calendar-layer mock) already exists at
`src/calendar/mockbackend.{h,cpp}` and is **richer** than
`MockBlobBackend` — it has operation logging, latency injection,
deterministic mode, and rich state inspection. The roadmap's
"library-owned MockCalendarBackend" already exists; no new mock is
required.

## Architecture

### File layout

```
tests/calendar/
├── CMakeLists.txt                       (extend existing)
├── fixtures/                            (existing — reuse for new ICS data if needed)
├── stubs/                               (NEW)
│   ├── CMakeLists.txt
│   ├── stubsynchost.{h,cpp}
│   ├── stubcalendarcollection.{h,cpp}
│   ├── stubincidenceregistry.{h,cpp}
│   └── stubsyncconfigstore.{h,cpp}
├── tst_icsfeedfetcher.cpp               (existing — unchanged)
├── tst_calendar_sync_full.cpp           (NEW)
├── tst_calendar_sync_oneway.cpp         (NEW)
├── tst_calendar_conflict.cpp            (NEW)
└── tst_calendar_transcoding_warning.cpp (NEW)
```

### Stubs as a static library

The four stubs compile into `kalburator_calendar_test_stubs` (a small
CMake `STATIC` target). Each test executable links it. Visibility is
test-only: it lives under `tests/`, not `src/`. The library's
`CMakeLists.txt` is gated by `KALBURATOR_BUILD_TESTS=ON` (the
existing test gate).

### Stub fidelity

Each stub implements its full interface but with the minimum
behavior needed to drive the engine through realistic flows.

- **`StubSyncHost`** — extends PlanStan's existing
  `tests/sync-workflow/tst_sync_error_recovery.cpp:64–96` pattern.
  Holds a `BackendRegistry*` for `backendById/backends()`. Holds
  `StubCalendarCollection*`, `StubIncidenceRegistry*`,
  `StubSyncConfigStore*`. `applyIncidenceAddition/Removal/Update`
  store the change in an internal log accessible for assertions.
  `unloadCalendar` and `generateSyncMappingsFromLogicalCalendars` are
  no-ops.

- **`StubCalendarCollection`** — backed by
  `QHash<QString, MemoryCalendar*>`. `id()` returns a configurable
  default. `addCalendar()` takes ownership; `~StubCalendarCollection`
  cleans up. Setters (`setCalendarColor`, `setCalendarVisible`)
  store into per-id `QHash<QString,QColor>` and
  `QHash<QString,bool>` respectively, exposed as inspectors for
  property-sync assertions.

- **`StubIncidenceRegistry`** — implements `IIncidenceRegistry` with a
  `QHash<QString, Incidence::Ptr>` keyed by UID. Sufficient for
  conflict-path UID lookups.

- **`StubSyncConfigStore`** — implements `ISyncConfigStore` returning
  configurable `QList<SyncMapping>`. Tests build their own mappings
  inline.

### Test execution model

Each test follows this shape:

```cpp
class TestCalendarSyncFull : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()        { /* one-time setup */ }
    void init()                { /* per-test fresh state */ }
    void cleanup()             { /* per-test teardown */ }
    void cleanupTestCase()     { /* one-time teardown */ }

    void fullSync_bothEmpty_doesNothing();
    void fullSync_sourceHasEvents_propagatesToTarget();
    // ... etc
};
QTEST_MAIN(TestCalendarSyncFull)
```

`init()` builds fresh `MockBackend` instances, fresh stubs, and a
fresh `SyncCoordinator`/`SyncWorker`. SQLite-backed `SyncStore` uses
`:memory:` or a `QTemporaryDir`-scoped temp DB. `cleanup()` tears
everything down so tests are isolated.

`TranscodingRegistry` is a singleton — `cleanup()` calls
`registry.clear()` to prevent test interference.

### CMake plumbing

```cmake
# tests/calendar/CMakeLists.txt — extend existing

add_subdirectory(stubs)

# (existing kalburator_add_calendar_test function unchanged)
kalburator_add_calendar_test(tst_icsfeedfetcher)

# New tests link the stubs lib in addition to base deps.
function(kalburator_add_calendar_integration_test TEST_NAME)
    kalburator_add_calendar_test(${TEST_NAME})
    target_link_libraries(${TEST_NAME}
        PRIVATE kalburator_calendar_test_stubs)
endfunction()

kalburator_add_calendar_integration_test(tst_calendar_sync_full)
kalburator_add_calendar_integration_test(tst_calendar_sync_oneway)
kalburator_add_calendar_integration_test(tst_calendar_conflict)
kalburator_add_calendar_integration_test(tst_calendar_transcoding_warning)
```

```cmake
# tests/calendar/stubs/CMakeLists.txt — new file

add_library(kalburator_calendar_test_stubs STATIC
    stubsynchost.cpp
    stubcalendarcollection.cpp
    stubincidenceregistry.cpp
    stubsyncconfigstore.cpp
)
set_target_properties(kalburator_calendar_test_stubs PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(kalburator_calendar_test_stubs
    PUBLIC
        Qt6::Core
        KF6::CalendarCore
        Kalburator::Sync
)
target_include_directories(kalburator_calendar_test_stubs
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
```

## Test scenario specifications

### `tst_calendar_sync_full`

- **`fullSync_bothEmpty_doesNothing`** — empty source, empty target,
  expect zero `applyIncidence*` calls, zero pushed items, baseline
  written.
- **`fullSync_sourceHasEvents_propagatesToTarget`** — source has 3
  events, target empty. After sync: target has all 3, source
  unchanged, `applyIncidenceAddition` called 0 times (these are
  source→target, not target→host).
- **`fullSync_targetHasEvents_propagatesToSource`** — symmetric.
  After sync: source has target's events,
  `StubSyncHost::applyIncidenceAddition` called 3 times.
- **`fullSync_disjointEvents_bothConverge`** — source has A, target
  has B. After sync: both have A+B.

### `tst_calendar_sync_oneway`

- **`oneWayUpload_sourceToTarget`** — source has events, target
  empty. After sync: target gets source events.
- **`oneWayUpload_ignoresTargetOnlyEvents`** — target has events not
  in source. After sync: target's events are NOT pulled to source;
  source remains empty. (Distinguishes one-way from full.)

### `tst_calendar_conflict`

- **`unmonitored_sameUidDivergent_emitsConflictDetected_appliesPolicy`**
  — both source and target modify event "evt-1" since baseline.
  `Mode::Unmonitored`, `ConflictPolicy::PreferSource`. Expect
  `conflictDetected` signal emitted exactly once. Final state: target
  matches source's version.
- **`monitored_sameUidDivergent_pausesUntilResume`** — same setup,
  `Mode::Monitored`. Expect `conflictPauseRequested` signal. Sync is
  *not* complete until `resumeAfterConflict(KeepSource, "")` is
  called. Final state: target matches source.

### `tst_calendar_transcoding_warning`

- **`transcoding_sourceHasRruleByDay_targetCantRepresent_emitsWarning`**
  — register a stub `IRruleTranscoder` that flags loss for any
  BYDAY-bearing recurrence when target is "lossy-stub" backend.
  Source has an RRULE with BYDAY; target is the lossy stub. After
  sync: `transcodingWarning` signal fires; the lossy version was
  written to target.

## Acceptance criteria

1. `cmake --build build --target tests` succeeds.
2. `ctest --test-dir build` reports **9 tests, all passing** (5
   existing + 4 new).
3. `verify-all.sh` is green for all three repos.
4. Total libkalburator-side new code ~1900 LOC (estimate; stubs ~400,
   tests ~1500).
5. Tag `v0.9-phase-d0-tests-first` on libkalburator's
   `refactor/engine-merger`.

## Risks & gotchas

- **`MemoryCalendar*` ownership.** Tests must transfer ownership to
  `StubCalendarCollection::addCalendar()` and rely on its destructor.
  Using `Q_ASSERT` in the destructor to validate the count would
  catch leaks early.
- **`TranscodingRegistry` singleton state.** Per-test `cleanup()`
  must call `registry.clear()`. Document this prominently in the
  first test's setup.
- **Property sync runs before incidence sync.** Stubs must return
  sensible defaults for `discoveredColor`, `discoveredDisplayName`,
  etc., or property-sync paths crash and never reach incidence
  scenarios. `MockBackend` already returns defaults; verify before
  relying.
- **`SyncStore` SQLite path.** Use `QTemporaryDir`-scoped paths; do
  not collide with PlanStan's `~/.local/share/planstan-dev/`.
- **Threading and signals.** `SyncWorker` runs on a worker thread.
  Tests must use `QSignalSpy::wait(timeout)` to receive signals
  cross-thread. Set generous timeouts (5–10s) to avoid flakes; flag
  any test that needs longer for investigation.
- **PlanStan's stub-host pattern is a starting reference, not a
  drop-in.** PlanStan's `StubSyncHost` returns nullptr for
  `applyIncidence*` / `collection()` / subsystem accessors. Phase
  D.0 must implement those properly to exercise the full sync cycle.

## Cross-references

- `04k-engine-merger-roadmap.md` — Phase D.0 spec section.
- `04h-blob-layer-design.md` — pattern for library-owned tests
  (test executables, fixtures, CMake gating).
- `~/dev/refactor-engine-merger/PlanStan/tests/sync-workflow/tst_sync_error_recovery.cpp:64`
  — lifting reference for `StubSyncHost`.
- `~/dev/refactor-engine-merger/OPERATIONS.md` — deprecation-with-
  overlap pattern these tests will pin during D / E / F / G.
