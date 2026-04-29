# Phase F1 — Unify (engine collapse + IDomainAdapter) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:executing-plans (or subagent-driven-development) to
> implement this plan task-by-task. Steps use checkbox (`- [ ]`)
> syntax for tracking.

**Goal:** Collapse `SyncCoordinator` + `SyncWorker` (calendar
engine, ~2900 LOC) and `BlobSyncEngine` (~420 LOC) into a single
`SyncEngine` parameterized over a runtime `IDomainAdapter*`.
Calendar and blob domain adapters; future vCard slot. Engine ↔
adapter boundary is pure `BackendRecord`. Threading preserved
verbatim from Phase E.

**Architecture:** See `04p-phase-f1-unify-design.md`. Key
decisions: runtime virtual `IDomainAdapter`; pure `BackendRecord`
across the engine/adapter seam; `WildPalms`'s `BlobSyncEngine`
consumer migrates to `SyncEngine::runBlobTwoWay()` /
`runBlobMirror()` one-shot facade methods (mechanical rename).

**Tech Stack:** Qt6, KCalendarCore (KF6), QTest, CMake. C++20.

**Working trees:**
- `~/dev/refactor-engine-merger/libkalburator/` — branch
  `refactor/engine-merger`. Most work lands here.
- `~/dev/refactor-engine-merger/PlanStan/` — same branch.
  `SyncCoordinator → SyncEngine` rename in Group 6.
- `~/dev/refactor-engine-merger/WildPalms/` — same branch.
  `BlobSyncEngine` consumer migration in Group 4.

**Build:** legacy preset-less. Use `-j 12`, never `--parallel`.

**Reference design:** `docs/phase0/04p-phase-f1-unify-design.md`.

**Verify after each task that touches code:**
`cmake --build build -j 12` and
`cd build && ctest --output-on-failure`. After Tasks that touch
consumers: `bash ~/dev/refactor-engine-merger/scripts/verify-all.sh`.

---

## Pre-flight: confirm production interfaces

Before Task 1, briefly confirm shapes haven't drifted from what
the design assumes:

- `src/calendar/synccoordinator.{h,cpp}` — confirm public API
  (signals, `SyncBehavior` enum, `runSync` overloads); 411 + 937
  LOC.
- `src/calendar/syncworker.{h,cpp}` — note 1647 LOC of
  per-mapping logic that gets split across `SyncEngine` (worker
  thread main loop) and `CalendarDomainAdapter` (calendar-specific
  diff/merge/apply).
- `src/blob/blobsyncengine.{h,cpp}` — confirm
  `twoWayWithBaseline`, `mirror`, `twoWayNaive` signatures (these
  become `SyncEngine::runBlob*` methods).
- `src/blob/blobbaselinestore.{h,cpp}` — confirm dual-table
  layout (flat `blob_baselines` + triple `blob_baselines_triple`
  per FINDINGS 2026-04-28). Group 5 consolidates these.
- `src/calendar/calendarbaselinestore.{h,cpp}` — owned by the
  calendar adapter after F1; confirm ctor + accessors.
- `src/transcoding/transcodingrouter.{h,cpp}` — Phase E's
  per-engine router; the calendar adapter holds it. F1 doesn't
  change its interface.
- `src/blob/iblobbackend.h` — confirm 12 pure-virtual methods
  (loadRecords, createRecord, etc.). Adapters call into this.
- `src/calendar/syncbackend.h` — confirm `SyncBackend : public
  QObject, public IBlobBackend` (Phase D inheritance). Calendar
  adapter dispatches via the calendar-shaped methods (`storeItems`
  / `updateItem` / `startSync` / `removeItem`).
- `src/calendar/createincidenceitem.{h,cpp}` and
  `updateincidenceitem.{h,cpp}` — Phase E's wrapper classes with
  the `writeFinished`-capture pattern. Become adapter-internal
  helpers.
- `WildPalms/src/runtime/syncrunner_wp.cpp:266` — the
  `BlobSyncEngine` consumer. Group 4 migration target.
- `tests/calendar/` — 7 integration tests using stub-host and
  MockBackend. All must stay green.
- `baselines/libkalburator-worktree-ctest.txt` — current 21
  tests; F1 grows to 25.

If any production shape has drifted from the design, **stop**
and fix `04p-phase-f1-unify-design.md` first.

---

## Group 0 — Prep

Smallest possible additions; no consumer changes; library still
builds and passes 21/21.

### Task 1: Create `src/engine/` directory + value types

**Files (libkalburator):**
- Create: `src/engine/idomainadapter.h`
- Create: `src/engine/enginediff.h`
- Create: `src/engine/enginediff.cpp` (small; just constructors)
- Modify: `CMakeLists.txt` (add `engine` to `KALBURATOR_SYNC_SUBDIRS`
  with explicit source list per the Phase E pattern)

- [ ] **Step 1: Write `idomainadapter.h`**

Per the design (`04p-...-design.md` §"`IDomainAdapter` — virtual
interface"). Header-only abstract class. Forward-declare the value
types from `enginediff.h`. Include only what's needed
(`<QString>`, `<QList>`, the existing `BackendRecord` and
`SyncMapping` types from `synctypes.h`).

- [ ] **Step 2: Write `enginediff.h` and `.cpp`**

Three value types:

```cpp
struct EngineDiffOp {
    enum class Kind { Create, Update, Delete, Conflict };
    Kind kind;
    BackendRecord record;       // for Create/Update: the new state.
                                // for Delete: the doomed record (id-shaped).
                                // for Conflict: the source-side record.
    BackendRecord targetRecord; // populated only for Conflict.
    BackendRecord baselineRecord; // populated for Update/Delete/Conflict.
};

struct EngineDiff {
    QList<DiffOperation> toSource;  // operations to apply to source side
    QList<DiffOperation> toTarget;  // operations to apply to target side

    bool hasConflicts() const;
    int totalOperations() const;
};

struct EngineMerge {
    QList<BackendRecord> finalSource;   // post-merge state of source
    QList<BackendRecord> finalTarget;   // post-merge state of target
    QList<BackendRecord> updatedBaselines;
    int conflictsResolved = 0;
    int conflictsDeferred = 0;
};

struct EngineApplyResult {
    bool success = true;
    QString errorMessage;
    int created = 0;
    int updated = 0;
    int deleted = 0;
    QList<BackendRecord> appliedBaselines;
};
```

- [ ] **Step 3: Update root `CMakeLists.txt`**

Add `engine` to `KALBURATOR_SYNC_SUBDIRS` and the explicit source
lists (mirror the Phase E pattern):

```cmake
set(KALBURATOR_ENGINE_HEADERS
    src/engine/idomainadapter.h
    src/engine/enginediff.h
)
set(KALBURATOR_ENGINE_SOURCES
    src/engine/enginediff.cpp
)
```

Append to `KALBURATOR_SYNC_HEADERS` / `KALBURATOR_SYNC_SOURCES`
aggregations.

- [ ] **Step 4: Build**

```bash
cmake --build build -j 12
```

Expected: succeeds. New types compile into the library; no
callers yet.

- [ ] **Step 5: Commit**

```bash
git add src/engine/ CMakeLists.txt
git commit -m "feat(engine): IDomainAdapter interface + SyncDiff/Merge value types (F1 Task 1)

Phase F1 foundation. Pure addition; no callers yet."
```

---

## Group 1 — Adapter extraction (engine still uses old paths)

Both adapters compile and pass unit tests. The engine code path is
untouched — adapters exist as separate compilation units that any
future caller can use, but `SyncCoordinator` / `SyncWorker` /
`BlobSyncEngine` still drive the actual sync.

### Task 2: Write `BlobDomainAdapter`

The simpler of the two — blob is identity serialization,
hash-equality diff, LWW merge.

**Files:**
- Create: `src/blob/blobdomainadapter.h`
- Create: `src/blob/blobdomainadapter.cpp`
- Create: `tests/blob/tst_blob_domain_adapter.cpp`
- Modify: `tests/blob/CMakeLists.txt`

- [ ] **Step 1: Implement `BlobDomainAdapter`**

The class implements `IDomainAdapter` for `domainType() ==
"blob"`. The body of `applyChanges` mirrors the per-record write
loop currently in `BlobSyncEngine::twoWayWithBaseline` (around
`blobsyncengine.cpp:170-280`). Lift the logic verbatim where
possible.

`fetchRecords()` → `backend->loadRecords(collectionId)`.

`diff()` walks source / target / baseline by record id, comparing
`BackendRecord::contentHash`:

- present in source, absent in target → Create (toTarget)
- present in target, absent in source → Create (toSource) OR
  Delete (toTarget) depending on baseline (if baseline has it,
  it's a delete; if not, it's a new target-side record to copy
  back)
- present on both, hashes match → unchanged
- present on both, hashes differ:
  - if baseline matches one side → other side is the change
  - if baseline differs from both → conflict

`merge()` resolves conflicts according to `ConflictResolution`
policy (LastWriteWins inspects `lastModified`; SourceWins picks
source; TargetWins picks target; etc.).

`applyChanges()` calls `IBlobBackend::createRecord` /
`updateRecord` / `deleteRecord` per `DiffOperation`.

`loadBaselines` / `saveBaselines` go to `BlobBaselineStore` via
its triple-keyed API (`backend_id` = mapping's source-or-target
backend id; for the one-shot path WildPalms uses, the synthetic
mapping carries the plugin id as backend id).

- [ ] **Step 2: Write `tst_blob_domain_adapter`**

Four test methods (per design's test plan):

- `hashEqualityDetection_returnsUnchanged()` — both sides have
  same record + hash, expect zero ops in diff.
- `createOnlyDiff_returnsToTargetCreate()` — source has record,
  target doesn't, no baseline. Expect one toTarget Create.
- `updateDiff_returnsToTargetUpdate()` — source has v2, target
  has v1, baseline is v1. Expect one toTarget Update.
- `deleteDiff_returnsToTargetDelete()` — source absent, target
  has record, baseline has record. Expect one toTarget Delete.

Use `MockBlobBackend` from `tests/blob/`.

- [ ] **Step 3: Wire into `tests/blob/CMakeLists.txt`**

```cmake
kalburator_add_blob_test(tst_blob_domain_adapter)
```

- [ ] **Step 4: Build, run, commit**

```bash
cmake --build build -j 12 --target tst_blob_domain_adapter
cd build && ctest --output-on-failure -R tst_blob_domain_adapter
```

Expected: 4/4 pass.

```bash
git add src/blob/blobdomainadapter.h src/blob/blobdomainadapter.cpp \
        tests/blob/tst_blob_domain_adapter.cpp \
        tests/blob/CMakeLists.txt CMakeLists.txt
git commit -m "feat(blob): BlobDomainAdapter (F1 Task 2)

Implements IDomainAdapter for blob (identity serde, hash-equality
diff, last-write-wins merge). Body absorbed from BlobSyncEngine;
the engine class itself is still in place — Group 4 deletes it."
```

### Task 3: Write `CalendarDomainAdapter`

The complex one — absorbs most of `SyncWorker`'s 1647 LOC.

**Files:**
- Create: `src/calendar/calendardomainadapter.h`
- Create: `src/calendar/calendardomainadapter.cpp`
- Create: `tests/calendar/tst_calendar_domain_adapter.cpp`
- Modify: `tests/calendar/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add new files to
  `KALBURATOR_CALENDAR_*` lists)

- [ ] **Step 1: Implement `CalendarDomainAdapter`**

Owns:
- `CalendarBaselineStore*` (borrowed; setter)
- `ICalendarCollection*` (borrowed; setter — engine sets per-sync)
- `TranscodingRouter&` (borrowed; ctor)

Inputs come from the engine via the adapter's setters before each
sync runs.

`fetchRecords()`:
- Calls `backend->loadRecords(calendarId)` (the IBlobBackend
  view, post Phase D).
- Returns the records as-is. (The Phase D implementation of
  `fetchSourceRecordsViaBlob` is the model.)

`diff()`:
- Lift the body of `SyncWorker::computeSyncDiff` and
  `computeQuickDiff`. Both produce a `SyncDiff` over
  `BackendRecord`. Calendar-specific bits (parsing iCal,
  IncidenceDiff for property-level conflict detection) live
  inside the adapter, not in the engine.

`merge()`:
- Lift the body of `SyncWorker::resolveConflictAutomatically`
  and the surrounding merge logic. Returns `SyncMerge` with
  populated `finalSource` / `finalTarget` / `updatedBaselines`.

`applyChanges()`:
- The body of `SyncWorker::applyChangesToBackend` goes here.
  Construct the per-direction `TranscodingPlan` via
  `m_router.plan(sourceType, targetType)`. Use the existing
  `CreateIncidenceItem` / `UpdateIncidenceItem` wrappers to
  invoke `backend->storeItems` / `updateItem`. The
  `writeFinished`-capture pattern (Phase E fixup) stays in the
  wrappers; the wrappers themselves move into adapter-private
  scope and lose their public API surface.
- Forward `SyncBackend::transcodingWarning` signals through
  the adapter. The engine connects to the adapter's forwarded
  signal.

`loadBaselines` / `saveBaselines`:
- Calendar baselines are iCal text strings keyed by uid. Wrap
  `CalendarBaselineStore::baseline` / `setBaseline`.
- The adapter's "BackendRecord baseline" view exposes the iCal
  string via `BackendRecord::data`; consumers don't need to
  know about the SQLite layer.

- [ ] **Step 2: Write `tst_calendar_domain_adapter`**

Six test methods (per design):

- `emptyInputs_returnsEmptyDiff()` — empty source/target/
  baseline → empty `SyncDiff`.
- `createOnlyDiff_returnsToTargetCreate()` — source has
  iCal-encoded incidence, target is empty.
- `updateDiff_returnsToTargetUpdate()` — divergent same-uid
  records.
- `deleteDiff_returnsToTargetDelete()` — target has incidence,
  baseline has it, source doesn't.
- `conflictDetection_returnsConflict()` — both sides modified
  vs baseline.
- `mergeAppliesPolicy_LWW()` — given a conflict + LWW policy +
  varying `lastModified`, expect the more-recent record in
  `finalTarget`.

Use real `KCalendarCore::Event::Ptr` to construct iCal text via
`ICalFormat::toICalString`, packaged into `BackendRecord` for the
adapter's input.

- [ ] **Step 3: Wire test into `tests/calendar/CMakeLists.txt`**

```cmake
kalburator_add_calendar_test(tst_calendar_domain_adapter)
```

(Use `kalburator_add_calendar_test`, not the integration variant
— this is a unit test of the adapter, no `ISyncHost` stubs
needed.)

- [ ] **Step 4: Build, run, commit**

```bash
cmake --build build -j 12 --target tst_calendar_domain_adapter
cd build && ctest --output-on-failure -R tst_calendar_domain_adapter
```

Expected: 6/6 pass.

```bash
git add src/calendar/calendardomainadapter.{h,cpp} \
        tests/calendar/tst_calendar_domain_adapter.cpp \
        tests/calendar/CMakeLists.txt CMakeLists.txt
git commit -m "feat(calendar): CalendarDomainAdapter (F1 Task 3)

Implements IDomainAdapter for calendar (iCal serde, IncidenceDiff,
TranscodingRouter integration). Body absorbed from SyncWorker;
SyncWorker is still in place — Group 3 deletes it."
```

---

## Group 2 — Engine collapse: rename + route through adapters

Rename `SyncCoordinator` → `SyncEngine`. Route the existing
calendar code path through `CalendarDomainAdapter`. `SyncWorker`
still exists — its body just delegates more to the adapter.

### Task 4: Rename `SyncCoordinator` → `SyncEngine`; move to `src/engine/`

The mechanical rename. Public API is preserved; only the type name
and include path change.

**Files (libkalburator):**
- Rename: `src/calendar/synccoordinator.h` →
  `src/engine/syncengine.h`
- Rename: `src/calendar/synccoordinator.cpp` →
  `src/engine/syncengine.cpp`
- Modify: `CMakeLists.txt` (move from `KALBURATOR_CALENDAR_*` to
  `KALBURATOR_ENGINE_*` lists)
- Modify (replace): every libkalburator-internal include of
  `synccoordinator.h` → `syncengine.h`
- Modify (replace): every libkalburator-internal use of class
  name `SyncCoordinator` → `SyncEngine`
- Modify (in this commit only): keep
  `src/calendar/synccoordinator.h` as a one-line deprecation shim
  that includes `engine/syncengine.h` and adds
  `using SyncCoordinator = SyncEngine;` — keeps PlanStan and
  WildPalms compiling until Group 6.

- [ ] **Step 1: `git mv` the files**

```bash
git mv src/calendar/synccoordinator.h src/engine/syncengine.h
git mv src/calendar/synccoordinator.cpp src/engine/syncengine.cpp
```

- [ ] **Step 2: Rename the class inside the moved files**

Editor sweep: `SyncCoordinator` → `SyncEngine` in both files.
Update header-guard macro
(`KALBURATOR_SYNCCOORDINATOR_H` → `KALBURATOR_SYNCENGINE_H`).
Update Q_OBJECT-bearing class name. Doxygen `@class` etc.

- [ ] **Step 3: Add the deprecation shim**

```cpp
// src/calendar/synccoordinator.h
#ifndef KALBURATOR_SYNCCOORDINATOR_H
#define KALBURATOR_SYNCCOORDINATOR_H
#include "engine/syncengine.h"
namespace Kalburator::Sync {
    using SyncCoordinator [[deprecated("renamed to SyncEngine in F1")]]
        = SyncEngine;
} // namespace Kalburator::Sync
#endif
```

- [ ] **Step 4: Sweep libkalburator-internal callers**

```bash
git grep -lE "SyncCoordinator|synccoordinator\.h" src/ tests/
```

Replace:
- `#include "synccoordinator.h"` → `#include "engine/syncengine.h"`
- `SyncCoordinator` → `SyncEngine`

The integration tests (`tst_calendar_sync_full`, etc.) update
their `std::unique_ptr<SyncCoordinator>` member declarations and
their `SyncCoordinator::SyncBehavior` references. The shim's
`using` lets them keep compiling if any references slip through,
but for cleanliness the sweep should catch everything.

- [ ] **Step 5: Update `CMakeLists.txt`**

Move `synccoordinator.{h,cpp}` from `KALBURATOR_CALENDAR_*`
lists to `KALBURATOR_ENGINE_*` lists (with the new file name
`syncengine.{h,cpp}`). Keep the shim
`src/calendar/synccoordinator.h` in `KALBURATOR_CALENDAR_HEADERS`
for one phase.

- [ ] **Step 6: Wipe build, reconfigure, build**

The class rename + file move can confuse incremental build /
AUTOMOC — wipe to be safe.

```bash
rm -rf build
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 12
```

- [ ] **Step 7: Run the full ctest**

```bash
cd build && ctest --output-on-failure
```

Expected: all 21 tests still pass (and the new
`tst_calendar_domain_adapter` + `tst_blob_domain_adapter` from
Group 1 — total 23 if Tasks 2 + 3 landed first).

- [ ] **Step 8: Commit**

```bash
git add src/calendar/synccoordinator.h src/engine/syncengine.{h,cpp} \
        src/ tests/ CMakeLists.txt
git commit -m "refactor(engine): SyncCoordinator → SyncEngine; move to src/engine/ (F1 Task 4)

Mechanical rename. Public API unchanged. Deprecation shim at
src/calendar/synccoordinator.h keeps PlanStan/WildPalms
compiling until consumer migration in Group 6."
```

### Task 5: SyncEngine routes calendar path through `CalendarDomainAdapter`

`SyncEngine` (was SyncCoordinator) now constructs a
`CalendarDomainAdapter` and uses it during `SyncWorker`'s
calendar code path. `SyncWorker` still exists; its body just gets
shorter as logic moves to the adapter.

**Files:**
- Modify: `src/engine/syncengine.{h,cpp}`
- Modify: `src/calendar/syncworker.{h,cpp}`

- [x] **Step 1: `SyncEngine` owns a `CalendarDomainAdapter`**

```cpp
// src/engine/syncengine.h
#include "calendar/calendardomainadapter.h"
//...
private:
    CalendarDomainAdapter m_calendarAdapter;
```

Ctor:
```cpp
SyncEngine::SyncEngine(...)
    : ... existing initialisers ...
    , m_calendarAdapter(m_transcodingRouter)  // takes router by ref
{
    // Wire baseline store + collection setters to forward into
    // the adapter when the engine's own setters are called.
}
```

The `setCalendarBaselineStore` / `setCollection` slots that
already exist on `SyncCoordinator` now also call the adapter's
setters.

- [x] **Step 2: `SyncWorker::applyChangesToBackend` delegates**

Replace the body (lines ~1108–1210 of the current file) with a
call to `m_engine->m_calendarAdapter.applyChanges(...)`. The
worker still owns the per-mapping flow; only the
calendar-specific apply step moves.

Similarly, the diff/merge sections of `SyncWorker::onFetchComplete`
delegate to `m_calendarAdapter.diff()` /
`m_calendarAdapter.merge()`.

**Landed:** Two calendar-typed convenience entry points were added
to `CalendarDomainAdapter`: `diffCalendarRecords()` (thin wrap
around `computeQuickDiff`/`computeSyncDiff` — mirrors the
BackendRecord-typed `diff()` but skips the iCal re-parse round-trip)
and `applyChangesToBackend()` (absorbs the SyncTransaction +
incidence-item wrappers + BlockingQueuedConnection commit body).
The IDomainAdapter::applyChanges (BackendRecord-typed) override
remains the F1 Task 3 stub — Task 7's unified-boundary integration
test wires it. Worker now passes the adapter pointer through
`setDependencies()` and delegates diff + apply.

- [x] **Step 3: Build, run all tests**

```bash
cmake --build build -j 12
cd build && ctest --output-on-failure
```

Expected: 21+ tests pass (including the seven `tst_calendar_*`
integration tests). The adapter's `applyChanges` is now
exercised by every calendar sync test.

**Landed:** library 23/23 pass (21 baseline + 2 from Tasks 2-3);
verify-all green on consumers.

- [x] **Step 4: Commit**

```bash
git add src/engine/syncengine.{h,cpp} src/calendar/syncworker.{h,cpp}
git commit -m "refactor(engine): SyncEngine routes calendar path through adapter (F1 Task 5)

SyncEngine constructs CalendarDomainAdapter and delegates the
diff/merge/apply steps to it. SyncWorker still owns the per-mapping
worker-thread flow; its body is now thinner. Group 3 collapses
SyncWorker into SyncEngine entirely."
```

### Task 6: Add `runBlobTwoWay` / `runBlobMirror` one-shot methods

The facade for WildPalms's consumer pattern. Internal: construct a
synthetic mapping, run through `BlobDomainAdapter`.

**Files:**
- Modify: `src/engine/syncengine.{h,cpp}`
- Create: `tests/calendar/tst_engine_blob_one_shot.cpp` (or
  `tests/blob/`; plan-author choice — `tests/blob/` is more
  appropriate since it's a blob-flavored API)
- Modify: `tests/blob/CMakeLists.txt`

- [ ] **Step 1: Implement `runBlobTwoWay`**

```cpp
BlobSyncResult SyncEngine::runBlobTwoWay(IBlobBackend* a, IBlobBackend* b,
    const QString& collectionId, const QString& mappingId,
    BlobBaselineStore* baseline,
    QSyncCore::ConflictHandlerRegistry* handlers,
    QSyncCore::ConflictStore* conflicts,
    const QSyncCore::ConflictPolicy& policy)
{
    // Construct an on-the-fly BlobDomainAdapter (or use a shared
    // m_blobAdapter member). Drive the adapter's diff/merge/apply
    // sequence directly. Don't touch the worker thread — this
    // one-shot path runs synchronously on the caller thread,
    // matching today's BlobSyncEngine behavior verbatim.
    BlobDomainAdapter adapter;
    adapter.setBaselineStore(baseline);
    // ... diff(a's records, b's records, baseline) ...
    // ... merge ...
    // ... applyChanges to b ...
    // ... return BlobSyncResult ...
}
```

`runBlobMirror` is the one-way variant. Bodies are lifted from
`BlobSyncEngine::twoWayWithBaseline` / `mirror`.

- [ ] **Step 2: Write `tst_engine_blob_one_shot`**

Mirrors `tst_blobsyncengine`'s scenarios, calling
`SyncEngine::runBlobTwoWay` / `runBlobMirror` instead. Confirms
behavior parity.

- [ ] **Step 3: Build, run, commit**

```bash
cmake --build build -j 12
cd build && ctest --output-on-failure
```

Expected: all tests pass.

```bash
git add src/engine/syncengine.{h,cpp} tests/blob/
git commit -m "feat(engine): one-shot blob API on SyncEngine (F1 Task 6)

runBlobTwoWay / runBlobMirror facade methods drive the unified
flow with a synthetic mapping + BlobDomainAdapter. Replaces
BlobSyncEngine for ad-hoc callers. Behavior parity with old API."
```

### Task 7: Engine boundary integration test

**Files:**
- Create: `tests/calendar/tst_engine_unified_boundary.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Write the test**

Three methods (per design):
- `runSync_calendarMapping_emitsExpectedSignals()` — the same
  flow `tst_calendar_sync_full` exercises, but asserting
  through the `SyncEngine` boundary (no `SyncCoordinator` shim
  type).
- `runSync_blobMapping_emitsExpectedSignals()` — register a
  blob mapping (synthesize one with `domain = "blob"`); confirm
  the engine routes through `BlobDomainAdapter`.
- `runSyncAll_mixedMappings_drivesBoth()` — calendar + blob
  mapping in one engine. Both run through `runSyncAll()`.

- [ ] **Step 2: Build, run, commit**

```bash
cmake --build build -j 12 --target tst_engine_unified_boundary
cd build && ctest --output-on-failure -R tst_engine_unified_boundary
```

```bash
git add tests/calendar/tst_engine_unified_boundary.cpp tests/calendar/CMakeLists.txt
git commit -m "test(engine): unified-boundary integration test (F1 Task 7)

Pins SyncEngine::runSync()'s contract for both calendar and blob
mappings, and runSyncAll() driving heterogeneous mappings."
```

---

## Group 3 — Collapse `SyncWorker` into `SyncEngine`

After Tasks 5–6, `SyncWorker`'s body is mostly orchestration
(thread management, fetch sequencing, signal forwarding). That
folds into `SyncEngine`'s private members.

### Task 8: Inline `SyncWorker` into `SyncEngine`

**Files:**
- Modify: `src/engine/syncengine.{h,cpp}` (absorb worker body)
- Delete: `src/calendar/syncworker.{h,cpp}` (after no callers
  remain)
- Modify: `CMakeLists.txt` (drop `syncworker.{h,cpp}` from
  `KALBURATOR_CALENDAR_*` lists)

- [ ] **Step 1: Move worker body into `SyncEngine`**

Each `SyncWorker` slot becomes a private `SyncEngine` slot.
Member fields move. The worker `QThread` instance becomes a
private member. The `connect`s and `QMetaObject::invokeMethod`
calls preserve their semantics; only the receiver type
changes.

The signal forwarding layer (today's
`SyncCoordinator::onWorkerSyncCompleted` etc.) collapses into
direct emission since signal source and emitter are now the same
object — but the public API (signal names, signatures) is
unchanged, so callers don't notice.

- [ ] **Step 2: Confirm no `SyncWorker` references**

```bash
git grep -nE "SyncWorker|syncworker" src/ tests/
```

Should return zero hits.

- [ ] **Step 3: Delete the files**

```bash
git rm src/calendar/syncworker.h src/calendar/syncworker.cpp
```

- [ ] **Step 4: Build, test, commit**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 12
cd build && ctest --output-on-failure
```

Expected: all tests pass.

```bash
git add -A
git commit -m "refactor(engine): SyncWorker collapsed into SyncEngine (F1 Task 8)

The 1647 LOC of SyncWorker were already mostly delegating to
CalendarDomainAdapter. The remaining orchestration moves into
SyncEngine private members verbatim. Threading model unchanged
(QThread + invokeMethod dispatch + signal-based completion)."
```

---

## Group 4 — Delete `BlobSyncEngine`; migrate WildPalms

### Task 9: Migrate WildPalms's `syncrunner_wp.cpp`

**Files:**
- Modify: `WildPalms/src/runtime/syncrunner_wp.cpp`

- [ ] **Step 1: Replace `BlobSyncEngine` with `SyncEngine`**

In `syncrunner_wp.cpp:266`:

```diff
- Kalburator::Sync::BlobSyncEngine engine;
+ Kalburator::Sync::SyncEngine engine(/* registry, host - one-shot,
+                                       so pass nullptrs if the API
+                                       supports it; otherwise
+                                       construct minimal stubs */);
```

The constructor signature for `SyncEngine` requires
`BackendRegistry*` and `ISyncHost*` (inherited from
`SyncCoordinator`). For the one-shot path, those are unused — but
the constructor must accept nullptrs gracefully. Confirm in
`SyncEngine` that nullptr-safe construction works for one-shot
use. If not, add a default-constructed overload during this task.

Replace the call site:

```diff
- const auto r = engine.twoWayWithBaseline(
+ const auto r = engine.runBlobTwoWay(
        palmBlob.get(), &localBlob,
        col.id, mappingId,
        &baseline, &handlers, &conflicts, policy);
```

The `runMirror` function (`syncrunner_wp.cpp:334+`) gets the same
treatment for `engine.mirror(...)` → `engine.runBlobMirror(...)`.

- [ ] **Step 2: Update WildPalms's
  `#include "blobsyncengine.h"` lines**

```bash
grep -rln "blobsyncengine.h" ~/dev/refactor-engine-merger/WildPalms/
```

Replace each with `#include "engine/syncengine.h"` (path is
relative to libkalburator's installed include dir). Confirm
WildPalms builds.

- [ ] **Step 3: Build + test WildPalms**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake -S . -B build \
    -DWILDPALMS_DATEBOOK_PLUGIN_V2=ON \
    -DWILDPALMS_ADDRESS_PLUGIN_V2=ON \
    -DWILDPALMS_TODO_PLUGIN_V2=ON \
    -DWILDPALMS_MEMO_PLUGIN_V2=ON \
    -DWILDPALMS_EXPENSE_PLUGIN_V2=ON \
    -DWILDPALMS_MAIL_PLUGIN_V2=ON
cmake --build build -j 12
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: 73/73 pass.

- [ ] **Step 4: Commit (in WildPalms)**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
git add src/runtime/syncrunner_wp.cpp
git commit -m "fix(palm-sync): adopt SyncEngine one-shot blob API (Phase F1)

BlobSyncEngine collapsed into SyncEngine in libkalburator.
Mechanical rename: BlobSyncEngine → SyncEngine; twoWayWithBaseline
→ runBlobTwoWay; mirror → runBlobMirror."
```

### Task 10: Delete `BlobSyncEngine`

**Files (libkalburator):**
- Delete: `src/blob/blobsyncengine.{h,cpp}`
- Delete or migrate: `tests/blob/tst_blobsyncengine.cpp`
- Modify: `CMakeLists.txt` (drop from `KALBURATOR_BLOB_*` lists)

- [ ] **Step 1: Confirm no callers**

```bash
git grep -nE "BlobSyncEngine|blobsyncengine" src/ tests/
```

Expected: only `src/blob/blobsyncengine.{h,cpp}` and possibly
`tests/blob/tst_blobsyncengine.cpp`.

- [ ] **Step 2: Decide test fate**

Either:
- Delete `tst_blobsyncengine.cpp` (its scenarios are covered by
  `tst_engine_blob_one_shot` from Task 6).
- Or migrate its specific assertions into
  `tst_engine_blob_one_shot` and then delete.

Plan-author's call. Recommendation: delete; the parity test in
Task 6 already exists.

- [ ] **Step 3: Delete files; update `CMakeLists.txt`**

```bash
git rm src/blob/blobsyncengine.h src/blob/blobsyncengine.cpp
git rm tests/blob/tst_blobsyncengine.cpp  # if deleting
```

Drop entries from `tests/blob/CMakeLists.txt` and root
`CMakeLists.txt`'s `KALBURATOR_BLOB_*` lists.

- [ ] **Step 4: Build + test**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j 12
cd build && ctest --output-on-failure
```

Expected: all tests pass (count drops by 1 from `tst_blobsyncengine`
deletion; the `tst_engine_blob_one_shot` from Task 6 took its
place).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor(engine): delete BlobSyncEngine; collapsed into SyncEngine (F1 Task 10)

WildPalms migrated in same group (Task 9). Test parity preserved
by tst_engine_blob_one_shot (Task 6)."
```

---

## Group 5 — `BlobBaselineStore` consolidation

### Task 11: Drop the flat-keyed table; rename triple → canonical

**Files:**
- Modify: `src/blob/blobbaselinestore.{h,cpp}`
- Modify: `tests/journal/tst_blobbaselinestore.cpp` (drop
  flat-keyed test methods)

- [ ] **Step 1: Drop flat-keyed methods + table**

In `blobbaselinestore.{h,cpp}`:
- Drop the `setBaseline(mappingId, recordId, ...)` etc. flat-keyed
  methods.
- Drop the `blob_baselines` table creation; keep
  `blob_baselines_triple` and rename it to `blob_baselines` in
  the SQL schema migration.
- Migration SQL: `DROP TABLE IF EXISTS blob_baselines_old; ALTER
  TABLE blob_baselines_triple RENAME TO blob_baselines;` (run
  inside the existing migrator).

- [ ] **Step 2: Drop flat-keyed tests**

`tst_blobbaselinestore.cpp`'s flat-keyed test methods go away.
The triple-keyed methods stay (they're now exercising the
canonically-named table).

- [ ] **Step 3: Build + test**

```bash
cmake --build build -j 12
cd build && ctest --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git add src/blob/blobbaselinestore.{h,cpp} tests/journal/
git commit -m "refactor(blob): consolidate BlobBaselineStore to single table (F1 Task 11)

Flat-keyed (mapping_id, record_id) table dropped along with
BlobSyncEngine. Triple-keyed (backend_id, collection_id, record_id)
table renamed to blob_baselines as the canonical store.

Migration: existing on-disk DBs see their triple-keyed data preserved;
flat-keyed data is dropped. WildPalms users may see one redundant
sync per device on first launch post-upgrade — acceptable per the
design (FINDINGS-noted full-resync-on-baseline-loss tolerance)."
```

---

## Group 6 — PlanStan rename

### Task 12: PlanStan: `SyncCoordinator` → `SyncEngine`

**Files (PlanStan):**
- Modify: every `.cpp` and `.h` referencing `SyncCoordinator`.

- [ ] **Step 1: Sweep**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
git grep -lE "SyncCoordinator|synccoordinator\.h"
```

For each file:
- `#include "synccoordinator.h"` → `#include "engine/syncengine.h"`
- `SyncCoordinator` → `SyncEngine`
- `Kalburator::Sync::SyncCoordinator` → `Kalburator::Sync::SyncEngine`

- [ ] **Step 2: Build + test PlanStan**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
rm -rf build-dev
cmake --preset dev -DPLANSTAN_ENABLE_CALDAV_TESTS=ON
cmake --build build-dev -j 12
cmake --build build-dev -j 12 --target tst_sync_conflicts \
    --target tst_sync_caldav_conflicts \
    --target tst_sync_error_recovery \
    --target tst_sync_dialog
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev --output-on-failure
```

Expected: 96/120 (Phase E baseline; F1 doesn't touch the 24
noise items).

- [ ] **Step 3: Commit (in PlanStan)**

```bash
git add -A
git commit -m "fix(sync): SyncCoordinator → SyncEngine rename (Phase F1)

libkalburator collapsed SyncCoordinator + SyncWorker + BlobSyncEngine
into a unified SyncEngine. PlanStan adopts the new type name across
src and tests; behavior unchanged."
```

### Task 13: Remove the deprecation shim

**Files (libkalburator):**
- Delete: `src/calendar/synccoordinator.h` (the shim)
- Modify: `CMakeLists.txt` (drop the shim from
  `KALBURATOR_CALENDAR_HEADERS`)

- [ ] **Step 1: Confirm no callers reference the shim**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git grep -nE "synccoordinator\.h|SyncCoordinator" src/ tests/
```

Should be empty (or only the shim file itself).

```bash
cd ~/dev/refactor-engine-merger/PlanStan
git grep -nE "synccoordinator\.h|SyncCoordinator" src/ tests/
```

Should be empty.

```bash
cd ~/dev/refactor-engine-merger/WildPalms
git grep -nE "synccoordinator\.h|SyncCoordinator" src/ tests/
```

Should be empty (WildPalms didn't use it).

- [ ] **Step 2: Delete the shim, build, test**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git rm src/calendar/synccoordinator.h
cmake --build build -j 12
cd build && ctest --output-on-failure
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor(engine): remove SyncCoordinator deprecation shim (F1 Task 13)

All consumers (PlanStan, WildPalms, libkalburator tests) migrated
to SyncEngine. Shim served its purpose."
```

---

## Group 7 — Cleanup, verify-all, doc updates, tag

### Task 14: Verify-all green; baseline refresh

- [ ] **Step 1: Run verify-all**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh
```

Expected exit code 3 (improvement: libkalburator went 21 → 25
[four new tests in F1: blob_domain_adapter, calendar_domain_adapter,
engine_unified_boundary, engine_blob_one_shot] minus 1
[tst_blobsyncengine deleted] = 24).

- [ ] **Step 2: Refresh libkalburator baseline**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
ctest --test-dir build --output-on-failure 2>&1 | \
    tee ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

Verify the count and re-run `verify-all.sh`; expect exit 0.

### Task 15: Doc updates + tag

**Files:**
- Modify: `04p-phase-f1-unify-design.md` (Status line)
- Modify: `04p-phase-f1-unify-plan.md` (Status header)
- Modify: `04k-engine-merger-roadmap.md` (status table)
- Modify: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- Modify: `~/dev/refactor-engine-merger/FINDINGS.md` (if
  non-obvious learnings emerged)

- [ ] **Step 1: Update Status lines**

`04p-phase-f1-unify-design.md`:
```
**Status:** Landed YYYY-MM-DD on tag `v0.13-phase-f1-unify`.
```

Plan doc Status header similar.

ROADMAP table: change Phase F1 row from `⬜ not started` to
`✅ landed YYYY-MM-DD`.

- [ ] **Step 2: Update CURRENT-STATUS**

Move F1 from "Next" to "Where we are." Replace "Next" with
"Phase F2 — Threading API redesign". Append F1 commits to
"Recently committed."

- [ ] **Step 3: Append FINDINGS if applicable**

Anything non-obvious learned during the engine collapse —
behaviors that surprised the implementer, edge cases that almost
broke the migration, dependencies between components that
weren't in the design.

- [ ] **Step 4: Commit doc updates**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git add docs/phase0/04p-phase-f1-unify-design.md \
        docs/phase0/04p-phase-f1-unify-plan.md \
        docs/phase0/04k-engine-merger-roadmap.md
git commit -m "docs(phase0): mark Phase F1 landed on tag v0.13-phase-f1-unify"
```

CURRENT-STATUS.md and FINDINGS.md live in the coordination folder
(not a git repo per CLAUDE.md). Save and move on.

- [ ] **Step 5: Tag**

Per CLAUDE.md, the user runs destructive operations including
`git tag` unless explicitly authorized. **Do not tag autonomously.**
Report the libkalburator HEAD sha to the user with:

```
Phase F1 ready to tag. Recommended:
  cd ~/dev/refactor-engine-merger/libkalburator
  git tag v0.13-phase-f1-unify <head-sha>
```

After tag in place, return to Step 1 and replace any
`<short-sha>` placeholders.

---

## Self-review checklist (run by plan executor before declaring done)

- [ ] `git grep "SyncCoordinator" src/` returns zero hits.
- [ ] `git grep "BlobSyncEngine" src/` returns zero hits.
- [ ] `git grep "SyncWorker" src/` returns zero hits.
- [ ] `src/calendar/synccoordinator.h` does not exist.
- [ ] `src/calendar/syncworker.{h,cpp}` does not exist.
- [ ] `src/blob/blobsyncengine.{h,cpp}` does not exist.
- [ ] libkalburator standalone ctest: 24/24 pass (or matches the
  refreshed baseline).
- [ ] All four new tests (`tst_blob_domain_adapter`,
  `tst_calendar_domain_adapter`, `tst_engine_unified_boundary`,
  `tst_engine_blob_one_shot`) pass.
- [ ] PlanStan: 96/120 pass.
- [ ] WildPalms: 73/73 pass.
- [ ] `verify-all.sh` exit 0 on a stable run after baseline refresh.
- [ ] `04k-engine-merger-roadmap.md` table reflects F1 ✅.
- [ ] `04p-phase-f1-unify-design.md` and `-plan.md` Status lines
  reflect the tag.
- [ ] `CURRENT-STATUS.md` updated.
- [ ] `FINDINGS.md` appended if non-obvious learnings emerged.
- [ ] No new TODOs / FIXMEs left in code.
- [ ] `IDomainAdapter` interface is virtual-only; no Q_OBJECT
  inheritance (mirrors Phase D's `IBlobBackend` decision).
