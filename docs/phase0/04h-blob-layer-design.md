# Phase B2 — blob-layer design (Wild Palms adoption, initial)

**Status:** ✅ Implemented 2026-04-21. First Wild-Palms-driven
contribution to libkalburator. All success criteria met pending
Task 9 (cross-repo verification) and Task 10 (tag) landing. 3/3
library-side ctest pass.
**Companion:** the overall phase map lives in `~/dev/WildPalms/docs/plans/2026-04-20-libkalburator-integration.md`
as "Phase B2" of that integration.
**Sibling upstream phase:** this is the first concrete slice of the
"Phase 4 — Wild Palms adoption" slot in `README.md`.

## Scope

Land the **lower layer** described in `04-merged-interface-sketch.md`
as net-new files in `src/blob/` + `src/types/` + a new top-level
`tests/` tree. The upper (calendar-typed) layer is untouched. This
gives Wild Palms' Phase E (`PalmBackend` refactor onto `IBlobBackend`)
a working library-side surface to consume, without disturbing
PlanStan's in-flight sync flow or the existing calendar layer.

**In scope (delivered in this phase):**

- `BackendRecord` (value type, in `src/types/`)
- `CollectionInfo` (value type, in `src/types/`)
- `IBlobBackend` (abstract interface, in `src/blob/`)
- `BlobSyncEngine` (minimum-viable engine: `mirror` + `twoWayNaive`)
- `LocalBlobBackend` (reference disk-backed impl)
- `MockBlobBackend` (in-memory + failure injection)
- First library-side test tree at `tests/blob/`
- CMake wiring: new `KALBURATOR_BUILD_TESTS` option, on by default
  for top-level builds, off by default for `add_subdirectory`
  consumers

**Deferred to later phases (documented in §"Explicitly deferred"
below):**

- Baseline-aware two-way sync (`BlobBaselineStore` + 3-way merge)
- `ConflictStore` integration inside the engine
- `MockBlobBackend` latency injection, operation log, deterministic
  mode
- Wiring `SyncCoordinator` (calendar layer) to compose
  `BlobSyncEngine`
- `AsyncFileWriter` blob/calendar split
- Dissolving PlanStan's `LocalBackend` into `LocalBlobBackend` + a
  calendar adapter

## Architectural position

```
┌──────────────────────────────────────────────────────────────┐
│  Upper layer — calendar-typed (UNCHANGED by this phase)      │
│    SyncBackend (→ ICalendarBackend later), SyncCoordinator,  │
│    CalDAVBackend, LocalBackend, MockBackend, …               │
│  KCalendarCore::Incidence::Ptr in/out                        │
└──────────────────────────────────────────────────────────────┘
               ╳  (no coupling yet — Phase E+ re-introduces)
┌──────────────────────────────────────────────────────────────┐
│  Lower layer — generic blob sync (NEW)                       │
│    IBlobBackend, BlobSyncEngine,                             │
│    LocalBlobBackend, MockBlobBackend                         │
│  BackendRecord in/out (opaque QByteArray + metadata)         │
└──────────────────────────────────────────────────────────────┘
```

The deliberate `╳` is what makes this phase safe and incremental:
nothing upstream of the blob layer calls into it yet, so nothing
upstream can break. Phase E begins to bridge the two layers on the
Wild Palms side via `PalmBackend : IBlobBackend` +
`PalmCalendarBackend : (current SyncBackend aka calendar-backend)`.
Subsequent library phases (to be named when they arrive) bridge them
on the upstream side.

## Types (shared vocabulary)

Both live in `src/types/` alongside the other shared-vocabulary
types and are compiled into `Kalburator::Types`. Consumers that only
need the data types can link `Kalburator::Types` without pulling in
the blob engine.

### `BackendRecord` (value type, header-only)

```cpp
namespace Kalburator::Sync {

struct BackendRecord {
    QString    id;              // backend-assigned unique id
                                // (file path, CalDAV href, PalmID, …)
    QString    type;            // "memo", "contact", "event", "todo",
                                // "binary", … — host-interpreted
    QString    displayName;     // human-readable for UI/logs
    QByteArray data;            // opaque bytes
    QString    contentHash;     // backend-computed; hash algorithm
                                // chosen by backend (SHA-256 for
                                // LocalBlobBackend)
    QDateTime  lastModified;
    bool       isDeleted = false;

    QString description() const
    { return displayName.isEmpty() ? id : displayName; }

    bool operator==(const BackendRecord &other) const = default;
};

} // namespace
```

**Deviations from WP's `Sync::BackendRecord`:**

- `class BackendRecord` with `virtual ~` → plain `struct` with no
  polymorphism. WP's donor has no subclasses and no virtual state; the
  polymorphism was never needed.
- `virtual QString description() const` → plain inline member.
- Added `operator== = default` for test assertions.

### `CollectionInfo` (value type, header-only)

Unchanged from WP's shape. Lifted verbatim into
`Kalburator::Sync::CollectionInfo`:

```cpp
namespace Kalburator::Sync {

struct CollectionInfo {
    QString id;            // unique identifier
    QString name;          // display name
    QString path;          // filesystem path if file-based (optional)
    QString type;          // "memos", "contacts", "calendar", "todos"
    bool    isDefault = false;
};

} // namespace
```

## `IBlobBackend`

Abstract interface in `src/blob/iblobbackend.h`, QObject-based (needs
signals). Methods follow WP's `Sync::SyncBackend` in shape, modernised
to value-type returns and `std::optional` where WP used nullptr
sentinels.

```cpp
namespace Kalburator::Sync {

class IBlobBackend : public QObject {
    Q_OBJECT
public:
    explicit IBlobBackend(QObject *parent = nullptr);
    ~IBlobBackend() override;

    // --- Identity ---
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool    isAvailable() const = 0;

    // --- Collections ---
    virtual QList<CollectionInfo> availableCollections() = 0;
    virtual CollectionInfo collectionInfo(const QString &collectionId) = 0;
    virtual QString createCollection(const CollectionInfo &info) = 0;

    // --- Records (value-returning) ---
    virtual QList<BackendRecord> loadRecords(const QString &collectionId) = 0;
    virtual std::optional<BackendRecord> loadRecord(const QString &recordId) = 0;
    virtual QString createRecord(const QString &collectionId,
                                 const BackendRecord &record) = 0;
    virtual bool    updateRecord(const BackendRecord &record) = 0;
    virtual bool    deleteRecord(const QString &recordId) = 0;

    // --- Change detection ---
    virtual QList<BackendRecord> modifiedSince(const QString &collectionId,
                                               const QDateTime &since) = 0;
    virtual QStringList deletedSince(const QString &collectionId,
                                     const QDateTime &since) = 0;
    virtual bool supportsDeleteTracking() const { return false; }

    // --- Batch / transaction ---
    virtual void beginBatch()       {}
    virtual bool commitBatch()      { return true; }
    virtual void rollbackBatch()    {}
    virtual bool supportsBatch() const { return false; }

Q_SIGNALS:
    void recordCreated(const QString &recordId);
    void recordUpdated(const QString &recordId);
    void recordDeleted(const QString &recordId);
    void errorOccurred(const QString &error);
    void progressUpdated(int current, int total, const QString &message);
};

} // namespace
```

**Deviations from WP's `Sync::SyncBackend`:**

- `QList<BackendRecord*> loadRecords(...)` → `QList<BackendRecord>`.
  Eliminates caller-owns-pointers contract. Qt's implicit sharing
  keeps copies cheap.
- `BackendRecord* loadRecord(...)` → `std::optional<BackendRecord>`.
  Nullptr-return-means-not-found replaced with explicit optional.
- `QList<BackendRecord*> modifiedSince(...)` → `QList<BackendRecord>`.
- Everything else: unchanged.

**Explicitly not added:** `loadRecordStream`, callback-based
streaming, async Operation objects (PlanStan's calendar-layer
pattern). The blob layer is synchronous/simple for now; if Phase E
needs async, that's an additive change to the interface.

## `BlobSyncEngine` (minimum viable)

In `src/blob/blobsyncengine.{h,cpp}`. Intentionally narrow — two
methods + a result struct + signals.

```cpp
namespace Kalburator::Sync {

struct BlobSyncStats {
    int created   = 0;
    int updated   = 0;
    int deleted   = 0;
    int unchanged = 0;
    int errors    = 0;
};

struct BlobSyncResult {
    bool          success = true;
    QString       errorMessage;
    BlobSyncStats sourceStats;   // changes on source side
    BlobSyncStats targetStats;   // changes on target side
};

class BlobSyncEngine : public QObject {
    Q_OBJECT
public:
    explicit BlobSyncEngine(QObject *parent = nullptr);
    ~BlobSyncEngine() override;

    // One-way mirror: source → target. Target ends up with a copy of
    // every record in source's collection. Records in target that
    // don't exist in source are deleted.
    BlobSyncResult mirror(IBlobBackend *source,
                          IBlobBackend *target,
                          const QString &collectionId);

    // Two-way naive sync: for each record present on either side,
    // whichever side has the newer `lastModified` wins. No baseline,
    // no 3-way merge, no conflict detection. Deletions not
    // propagated (can't distinguish "deleted" from "never existed"
    // without a baseline).
    BlobSyncResult twoWayNaive(IBlobBackend *a,
                               IBlobBackend *b,
                               const QString &collectionId);

Q_SIGNALS:
    void progressChanged(int current, int total, const QString &message);
    void finished(const BlobSyncResult &result);
};

} // namespace
```

**Design notes:**

- No baseline store. `mirror` is stateless — always reads source's
  full record set, writes to target. `twoWayNaive` is stateless —
  compares current modified times. This is the deliberate simplification
  that keeps this phase scoped.
- Signals are emitted but the methods are also synchronous (blocking
  until done). Async/threaded operation is a later concern.
- Records are compared by `id` (record identity) and `contentHash`
  (change detection). Only records with different hashes are written.

## `LocalBlobBackend`

In `src/blob/localblobbackend.{h,cpp}`. Disk-backed reference impl,
closely modelled on WP's `LocalFileBackend` but adapted to the new
value-type interface.

**Storage layout:**

```
<basePath>/
├── <collectionId>/
│   ├── <record-filename-1>.<ext>
│   └── <record-filename-2>.<ext>
└── <anotherCollection>/
    └── …
```

- Collections = top-level subdirectories.
- Records = files within.
- Filename derivation: `<slugified displayName>-<short-hash-of-id>.<ext>`
  where `<ext>` is derived from `CollectionInfo::type` (e.g.
  `memos` → `.md`, `contacts` → `.vcf`, `calendar` → `.ics`) or
  defaults to `.bin`.
- `contentHash`: SHA-256 of data. Computed on write, stored alongside
  in a sidecar `.hash` file? **Decision:** no sidecar — compute on
  read. A small future optimisation could add a sidecar index, but
  Phase B2 stays simple.
- `lastModified`: filesystem mtime.
- `isDeleted`: not tracked (file-based backends can't). `loadRecords`
  returns existing files only; `deletedSince` returns `{}`.
- `supportsDeleteTracking()`: false.
- `supportsBatch()`: false.

**Path-safety:** filenames are derived, not user-supplied, to avoid
path-traversal. Collection IDs go through the same slugifier.

**Configuration:** constructor `LocalBlobBackend(const QString &basePath, QObject *parent = nullptr)`.
`basePath` must exist and be writable; constructor throws if not? No
— Qt idiom is deferred failure: `isAvailable()` returns false and
subsequent operations fail.

## `MockBlobBackend`

In `src/blob/mockblobbackend.{h,cpp}`. In-memory for fast testing
plus failure injection.

```cpp
namespace Kalburator::Sync {

class MockBlobBackend : public IBlobBackend {
    Q_OBJECT
public:
    enum class FailurePoint {
        None,
        OnLoadRecords,
        OnLoadRecord,
        OnCreateRecord,
        OnUpdateRecord,
        OnDeleteRecord,
        OnModifiedSince,
        OnCreateCollection,
    };

    explicit MockBlobBackend(QObject *parent = nullptr);

    // IBlobBackend surface …

    // Test configuration
    void setFailNext(FailurePoint point, int count = 1);
    void clearFailures();

    // Direct store access for test assertions
    QHash<QString, BackendRecord> recordsIn(const QString &collectionId) const;
};

} // namespace
```

- Storage: `QHash<QString /*collectionId*/, QHash<QString /*recordId*/, BackendRecord>>`.
- When a method's `FailurePoint` is armed, it decrements the counter
  and returns a failure sentinel: empty list / `std::nullopt` /
  false / empty QString, plus `emit errorOccurred("injected failure")`.
- **Not included** (deferred): latency injection, operation logging,
  deterministic mode, enumerable counters of which method was called
  how many times. Add these when a test actually needs them.

## Tests

New tree at `~/dev/libkalburator/tests/`, gated by
`KALBURATOR_BUILD_TESTS` (ON for top-level standalone builds; OFF for
`add_subdirectory` consumers so PlanStan and WP don't pick them up).

### `tests/blob/tst_mockblobbackend.cpp`

- Empty mock round-trips: `availableCollections()` empty, CRUD on
  non-existent collection returns failures.
- Create collection + record round-trip: `createRecord` then
  `loadRecord` returns the same record.
- Update + delete: `updateRecord` changes `contentHash` / `data`;
  `deleteRecord` removes.
- `modifiedSince`: returns only records with `lastModified >= since`.
- Failure injection: `setFailNext(OnLoadRecords, 1)` causes the next
  `loadRecords` to return `{}` and emit `errorOccurred`; the one
  after succeeds.

### `tests/blob/tst_localblobbackend.cpp`

- Uses `QTemporaryDir` as the base path.
- Create, load, list, update, delete a record on disk; verify files
  appear/disappear.
- `contentHash` matches `SHA-256(data)`.
- `modifiedSince` filters correctly using mtime (touch trick: sleep +
  re-write).
- Survives base path deletion: after `QTemporaryDir` is wiped,
  `isAvailable()` → false and operations fail without crashing.

### `tests/blob/tst_blobsyncengine.cpp`

- `mirror(mockA, mockB, "cal")`: after sync, `mockB` contains the
  same records as `mockA`; unchanged records are not rewritten (track
  via `setFailNext(OnUpdateRecord, ...)` or direct counters).
- `mirror(local, mock, "cal")`: fills local from mock, verifies
  on-disk files.
- `mirror` with `mockA` armed to fail: `BlobSyncResult.errors > 0`,
  stats reflect partial success.
- `twoWayNaive(mockA, mockB, "cal")`: records only-in-A propagate to
  B, records only-in-B propagate to A, records in both with newer
  `lastModified` on one side win.
- Progress signal: `progressChanged` fires at least once during a
  multi-record sync.

Each test is a standalone `QTEST_MAIN` executable registered via
`add_test` under `tests/blob/CMakeLists.txt`. Link: `Qt6::Test`,
`Qt6::Core`, `Kalburator::Sync` (picks up `Kalburator::Types`
transitively).

## CMake wiring

Top-level `CMakeLists.txt` gets a new option block:

```cmake
if(PROJECT_IS_TOP_LEVEL)
    option(KALBURATOR_BUILD_TESTS "Build libkalburator's own tests" ON)
else()
    option(KALBURATOR_BUILD_TESTS "Build libkalburator's own tests" OFF)
endif()

if(KALBURATOR_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
```

Placed after the `add_library(kalburator …)` block so tests can link
`Kalburator::Sync`.

`src/blob/` files pick up automatically via the existing
`KALBURATOR_SYNC_SUBDIRS` glob — no list edit needed. The two new
`src/types/` headers pick up via the existing `src/types/*.h` glob.

## Impact on downstream consumers

- **PlanStan:** no observable effect. PlanStan's `ctest` set remains
  the authoritative regression gate. A second `cmake --build
  build-planstan` + full `ctest` run in `~/dev/PlanStan/` after each
  landing-commit is the sign-off check. No PlanStan source edits.
- **Wild Palms:** no observable effect until Phase E starts. WP's
  current `Phase A` linkage test continues to link `Kalburator::Sync`
  unchanged and pass.
- **ODR / namespace:** all new types are `Kalburator::Sync::*`, same
  as existing. No collisions.

## Explicitly deferred (from the full merged-interface-sketch vision)

The merged-interface-sketch §"`BlobSyncEngine`" and §"Conflict
framework — lifted from Wild Palms wholesale" describe a richer
engine than this phase delivers. The following are **explicitly not
included** in Phase B2, with a note of who's expected to drive each.

| Deferred piece | Driver / when |
|---|---|
| `BlobBaselineStore` (hash baseline per mapping) | **✅ Landed in Phase B3 (`v0.7-phase-b3-baseline`, 2026-04-21)** — see `04i-blob-baseline-store-design.md`. Consumed by Phase B4's `twoWayWithBaseline`. |
| 3-way-merge blob sync | Same phase as `BlobBaselineStore` — they co-arrive. |
| `ConflictStore` integration inside `BlobSyncEngine` | Same phase — the engine is the consumer of both baseline and conflict store. |
| `AutomaticConflictHandler` wired into `BlobSyncEngine` | Same phase — sketch §"Conflict framework" describes. |
| `BlobSyncEngine::registerConflictHandler(backendId, handler)` | Follows from the above. WP's `PalmConflictHandler` plugs in here. |
| `MockBlobBackend`: latency injection, operation log, deterministic mode | On-demand — when a test requires it. |
| Wiring `SyncCoordinator` (calendar) to compose `BlobSyncEngine` | Deliberately out of this phase (scope-(a) decision). Opens a later library-side phase that does the `SyncBackend` → `ICalendarBackend` rename, `SyncCoordinator` → `CalendarSyncCoordinator` rename, and composes the blob engine. That phase is the logical bookend of the blob/calendar split. |
| `AsyncFileWriter` blob/calendar split (`BlobAsyncFileWriter` + `CalendarAsyncFileWriter`, atomic via `QSaveFile`) | Per Audit 4 — its own phase. Not gating this one. `LocalBlobBackend` uses plain `QFile` atomicity via `QSaveFile` directly for now. |
| Dissolving PlanStan's `LocalBackend` into `LocalBlobBackend` + a calendar adapter | Deferred until the calendar-layer refactor phase above. PlanStan's `LocalBackend` continues to work independently. |
| Promotion of `BackendRecord` / `CollectionInfo` to Qt metatype registration, JSON serializers, etc. | On-demand. |

All of these are tracked here, not in scattered TODOs, so future
phases can pick them up cleanly.

## Tagging

On completion, tag libkalburator `v0.6-phase-b2-blob-layer` on `main`.
Update `README.md` phase map: "Phase B2 (blob layer, v0.6) — done."

## Success criteria

1. `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON &&
   cmake --build build` succeeds on `~/dev/libkalburator/` as a
   standalone project.
2. `ctest` in that build produces 100% pass, ≥ 3 test executables
   (mock, local, engine), ≥ 10 total test functions.
3. `cmake --build build` in `~/dev/PlanStan/build-dev/` still
   succeeds. PlanStan's `ctest` pass/fail count at the head of `main`
   before this phase is preserved byte-for-byte after — no new
   failures, no previously-failing tests flip to passing by
   accident. Capture the pre-phase baseline as the first step of
   implementation so it's unambiguous what "preserved" means.
4. `cmake --build build` in `~/dev/WildPalms/build/` still succeeds.
   WP's 16/16 ctest set from Phase D still passes, and
   `test_libkalburator_smoke` still passes.
5. `README.md` phase-map and this doc's Status line are updated in
   the same commit sequence that lands the code.
