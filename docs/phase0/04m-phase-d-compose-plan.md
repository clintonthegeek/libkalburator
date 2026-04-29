# Phase D — Compose Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the calendar/blob layer `╳` non-coupling with composition: every concrete `SyncBackend` becomes an `IBlobBackend` (inheritance hoisted to the `SyncBackend` base), `SyncWorker` delegates byte-level fetch/store via the blob view, and the first-sync path is dispatched through `BlobSyncEngine` end-to-end. Bundle in the `SyncStore` carve-up.

**Architecture:** Three groups, sequenced. (1) Storage carve-up using deprecation-with-overlap so libkalburator stays green throughout: introduce `CalendarBaselineStore`, generalize `BlobBaselineStore`'s key shape, move CTags/fingerprints to backend-private state, forward old `SyncStore` methods to new homes, migrate callers, delete `SyncStore`. (2) Backend inheritance hoist: `SyncBackend : public IBlobBackend`; implement the eight backends one-per-commit. (3) Engine wiring: `SyncWorker` fetches via the blob view, applies per-record hash skip, dispatches first-sync via `BlobSyncEngine`. Consumers (PlanStan, WildPalms) migrated, tagged, baselines refreshed.

**Tech Stack:** Qt6, KCalendarCore (KF6), QTest, QtSql, CMake. C++20.

**Working tree:** `~/dev/refactor-engine-merger/libkalburator/` (worktree on `refactor/engine-merger`). PlanStan / WildPalms worktrees in sibling directories under the same coordination folder.

**Build:** legacy preset-less project — build dir is `build/`. Use `-j 12`, never `--parallel`.

**Reference design:** `docs/phase0/04m-phase-d-compose-design.md`.

**Verify after every group:** `bash ~/dev/refactor-engine-merger/scripts/verify-all.sh`.

---

## Pre-flight: confirm production interfaces

Before Task 1, briefly read these to confirm shapes haven't drifted from what the design assumes:

- `src/blob/iblobbackend.h` — confirm method list (record CRUD, change detection, batch, signals).
- `src/blob/blobbaselinestore.{h,cpp}` — current key shape (`RecordId` flat).
- `src/blob/blobsyncengine.{h,cpp}` — `mirror()`, `twoWayNaive()`, `twoWayWithBaseline()` signatures.
- `src/calendar/syncbackend.h` — calendar-typed surface that stays as-is.
- `src/calendar/syncstore.{h,cpp}` — the seven concerns being carved up.
- `src/calendar/syncworker.{h,cpp}` — the call sites (`fetchSourceRecords`, `fetchTargetRecords`, `applyChanges`, `processSync`).
- `src/types/backendrecord.h` — `BackendRecord` shape.
- `tests/calendar/` — the four D.0 test executables that must continue to pass.
- `tests/blob/` — the three blob-layer tests that must continue to pass.

If any production shape has drifted from the design, **stop** and fix the design doc (`04m-…-design.md`) first.

---

## Group 1 — Storage carve-up

Goal: every `SyncStore` concern moves to its proper home; `SyncStore` becomes a deprecated thin facade; libkalburator's ctest stays green throughout.

### Task 1: `CalendarBaselineStore` skeleton

Introduce the new store with no callers yet. Compiles into the library; no behavior change.

**Files:**
- Create: `src/calendar/calendarbaselinestore.h`
- Create: `src/calendar/calendarbaselinestore.cpp`
- Modify: `src/calendar/CMakeLists.txt` (add to source list)
- Reference: `src/calendar/syncstore.{h,cpp}` for the SQLite patterns to mirror

- [ ] **Step 1: Read SyncStore's SQLite patterns**

```bash
sed -n '1,80p' ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncstore.h
sed -n '1,80p' ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncstore.cpp
```

Note the connection-management pattern (`QSqlDatabase` named connection per-instance, `dbPathFor()` helper, transactions in setBaselines).

- [ ] **Step 2: Write `calendarbaselinestore.h`**

```cpp
// src/calendar/calendarbaselinestore.h
#ifndef KALBURATOR_CALENDARBASELINESTORE_H
#define KALBURATOR_CALENDARBASELINESTORE_H

#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

namespace Kalburator::Sync {

/**
 * @brief 3-way merge baseline for calendar-typed sync.
 *
 * Owns:
 *   - per-(mappingId, uid) iCal text baselines for incidence merge
 *   - per-(mappingId, calendarId) JSON property baselines for calendar
 *     property merge
 *
 * Carved out of the dissolving `SyncStore` during Phase D. SQLite-backed,
 * lives in the same `.kalburator-sync.db` file as the blob stores.
 */
class CalendarBaselineStore : public QObject
{
    Q_OBJECT
public:
    explicit CalendarBaselineStore(const QString &dbPath, QObject *parent = nullptr);
    ~CalendarBaselineStore() override;

    bool isValid() const;

    // ---- iCal-text baselines ----
    QString baseline(const QString &mappingId, const QString &uid) const;
    bool    setBaseline(const QString &mappingId, const QString &uid,
                        const QString &icalText);
    bool    setBaselines(const QString &mappingId,
                         const QHash<QString, QString> &uidToIcal);   // bulk
    bool    removeBaseline(const QString &mappingId, const QString &uid);
    bool    removeBaselines(const QString &mappingId);                // per-mapping
    QHash<QString, QString> allBaselines(const QString &mappingId) const;
    bool    clearBaselines();

    bool hasBaselines(const QString &mappingId) const;

    // ---- property-JSON baselines ----
    QString propertyBaseline(const QString &mappingId, const QString &calendarId) const;
    bool    setPropertyBaseline(const QString &mappingId, const QString &calendarId,
                                const QString &propertyJson);
    bool    removePropertyBaseline(const QString &mappingId, const QString &calendarId);
    QHash<QString, QString> allPropertyBaselines(const QString &mappingId) const;

    // ---- last-sync timestamp ----
    QDateTime lastSyncTime(const QString &mappingId) const;
    bool      setLastSyncTime(const QString &mappingId, const QDateTime &when);

private:
    bool ensureSchema();
    QSqlDatabase m_db;
    QString m_connectionName;
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 3: Write `calendarbaselinestore.cpp`**

Mirror `SyncStore`'s patterns:
- Constructor: open `QSqlDatabase::addDatabase("QSQLITE", uniqueName)`, set path, open, `ensureSchema()`.
- `ensureSchema()` runs `CREATE TABLE IF NOT EXISTS calendar_baseline_ical (mapping_id TEXT, uid TEXT, ical_text TEXT, PRIMARY KEY (mapping_id, uid))`. Same for `calendar_baseline_property` keyed `(mapping_id, calendar_id)` and `calendar_baseline_lastsync` keyed `(mapping_id)`.
- Each accessor uses `QSqlQuery` with bind values; bulk write wraps in `db.transaction()/commit()`.

Lift the exact transaction-with-rollback wrapper from `SyncStore::setBaselines()` for consistency.

- [ ] **Step 4: Add to CMake**

In `src/calendar/CMakeLists.txt`, append `calendarbaselinestore.cpp` (and the header next to it) to the calendar source list.

- [ ] **Step 5: Configure + build**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 2>&1 | tail -10
```

Expected: clean build, no new warnings.

- [ ] **Step 6: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add \
    src/calendar/calendarbaselinestore.h \
    src/calendar/calendarbaselinestore.cpp \
    src/calendar/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(calendar): add CalendarBaselineStore skeleton (Phase D)"
```

---

### Task 2: `tst_calendar_baseline_store` test

TDD pass on the new store. Catches schema/queries before any caller relies on it.

**Files:**
- Create: `tests/calendar/tst_calendar_baseline_store.cpp`
- Modify: `tests/calendar/CMakeLists.txt` (add the test line)

- [ ] **Step 1: Write the test executable**

```cpp
// tests/calendar/tst_calendar_baseline_store.cpp
#include <QtTest>
#include <QTemporaryDir>
#include "calendarbaselinestore.h"

using namespace Kalburator::Sync;

class TestCalendarBaselineStore : public QObject {
    Q_OBJECT
private slots:
    void roundTrip_singleBaseline();
    void bulkSet_returnsAll();
    void removePerMapping_clearsOnlyThatMapping();
    void propertyBaseline_isolatedPerCalendar();
    void hasBaselines_falseWhenEmpty();
    void persistsAcrossReopen();

private:
    QTemporaryDir m_dir;
    QString dbPath() const { return m_dir.filePath("test.kalburator-sync.db"); }
};

void TestCalendarBaselineStore::roundTrip_singleBaseline() {
    CalendarBaselineStore store(dbPath());
    QVERIFY(store.isValid());
    QVERIFY(store.setBaseline("m1", "uid-1", "ICAL-TEXT"));
    QCOMPARE(store.baseline("m1", "uid-1"), QString("ICAL-TEXT"));
    QCOMPARE(store.baseline("m1", "uid-missing"), QString());
}

// (Full bodies for the other slots follow the same pattern. setBaselines bulk
// inserts a 3-entry hash; allBaselines returns matching size; removeBaselines
// per-mapping doesn't touch other mappings; persistsAcrossReopen creates a
// store, writes, closes via dtor, opens new store on same path, asserts.)

QTEST_GUILESS_MAIN(TestCalendarBaselineStore)
#include "tst_calendar_baseline_store.moc"
```

Fill in the remaining slot bodies; each is a 5-10 line QCOMPARE/QVERIFY sequence.

- [ ] **Step 2: Wire CMake**

In `tests/calendar/CMakeLists.txt`, add:

```cmake
kalburator_add_calendar_test(tst_calendar_baseline_store)
```

(This uses the existing helper, not the integration-test helper — no stubs needed.)

- [ ] **Step 3: Build + run**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 --target tst_calendar_baseline_store
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_calendar_baseline_store --output-on-failure
```

Expected: 1/1 pass with all slots green. Fix any schema/query bug surfaced.

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add \
    tests/calendar/tst_calendar_baseline_store.cpp tests/calendar/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(calendar): add tst_calendar_baseline_store (Phase D)"
```

---

### Task 3: Generalize `BlobBaselineStore` key shape

Today: keyed flat by `RecordId`. After: keyed by `(backendId, collectionId, recordId)` so calendar-side per-uid version hashes can live here.

**Files:**
- Modify: `src/blob/blobbaselinestore.h`
- Modify: `src/blob/blobbaselinestore.cpp`
- Reference: existing `tests/blob/tst_blob_baseline_store.cpp` — must continue passing.

- [ ] **Step 1: Read current shape**

```bash
sed -n '1,120p' ~/dev/refactor-engine-merger/libkalburator/src/blob/blobbaselinestore.h
```

Identify the public method list. They all take a `RecordId` (typically a `QString`). After Task 3, each method gains an overload taking `(backendId, collectionId, recordId)`. The existing flat overloads remain as convenience wrappers that synthesize backendId/collectionId from a "default sync pair" set on construction.

- [ ] **Step 2: Add the new overloads**

For each existing method, add a triple-keyed sibling. Example:

```cpp
// Before (existing — keep)
QByteArray hash(const QString &recordId) const;
bool       setHash(const QString &recordId, const QByteArray &hash);

// After (added)
QByteArray hash(const QString &backendId, const QString &collectionId,
                const QString &recordId) const;
bool       setHash(const QString &backendId, const QString &collectionId,
                   const QString &recordId, const QByteArray &hash);
```

- [ ] **Step 3: Schema migration**

Today's table is keyed flat by `record_id`. Add columns `backend_id TEXT NOT NULL DEFAULT ''` and `collection_id TEXT NOT NULL DEFAULT ''`. Change PRIMARY KEY to `(backend_id, collection_id, record_id)`. Existing rows pick up the empty-string default and remain accessible via the flat overloads.

In `ensureSchema()`:
- `ALTER TABLE blob_baseline ADD COLUMN backend_id TEXT NOT NULL DEFAULT ''`
- Same for `collection_id`.
- (PRAGMA-protected: detect via `PRAGMA table_info(blob_baseline)`.)
- Wrap migration in a transaction.

- [ ] **Step 4: Implement triple-keyed methods**

Each takes the three keys, binds them in the SQL, returns/writes the hash. Flat-keyed overloads delegate by passing empty-string for backend_id and collection_id (matching the migrated default).

- [ ] **Step 5: Run existing blob tests**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R "tst_blob" --output-on-failure
```

Expected: existing 3 blob tests all pass — no regression.

- [ ] **Step 6: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/blob/blobbaselinestore.h src/blob/blobbaselinestore.cpp
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "feat(blob): generalize BlobBaselineStore key shape to (backend, collection, record) (Phase D)"
```

---

### Task 4: `tst_blob_baseline_store_per_record_keys`

Pin the new key shape with a focused test.

**Files:**
- Create: `tests/blob/tst_blob_baseline_store_per_record_keys.cpp`
- Modify: `tests/blob/CMakeLists.txt`

- [ ] **Step 1: Write the test**

Tests:
- `tripleKey_isolatesByBackend`: same recordId, different backendIds → different hashes don't collide.
- `tripleKey_isolatesByCollection`: same recordId+backendId, different collectionIds → distinct.
- `flatKey_compatible_withTripleKey`: writing via flat overload, reading via triple with empty strings → returns the same value.
- `migration_preservesExistingData`: open a DB (manually preloaded via direct SQL with old schema), reopen via the new code, verify rows still readable.

- [ ] **Step 2: Wire CMake**

```cmake
kalburator_add_blob_test(tst_blob_baseline_store_per_record_keys)
```

(Mirror the helper used by `tst_blob_baseline_store`.)

- [ ] **Step 3: Build + run + commit**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 --target tst_blob_baseline_store_per_record_keys
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_blob_baseline_store_per_record_keys --output-on-failure
git -C ~/dev/refactor-engine-merger/libkalburator add \
    tests/blob/tst_blob_baseline_store_per_record_keys.cpp tests/blob/CMakeLists.txt
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "test(blob): add tst_blob_baseline_store_per_record_keys (Phase D)"
```

---

### Task 5: Move CTags to `RemoteBackend` private state

Today: CTags live in `SyncStore::ctag*` methods, keyed `(backendId, calendarId)`. After: a private `CTagStore` member of `RemoteBackend`, persisted to a per-backend table in `.kalburator-sync.db`.

**Files:**
- Modify: `src/calendar/remotebackend.h` (add private member + accessor for tests)
- Modify: `src/calendar/remotebackend.cpp` (use it instead of SyncStore in modifiedSince path)
- Create: `src/calendar/remotebackend_ctagstore.cpp` (or inline in `remotebackend.cpp` — keep the file count down)
- Modify: `src/calendar/syncstore.{h,cpp}` (deprecate the ctag methods; forward to the new home for any straggling callers)

- [ ] **Step 1: Identify callers of `SyncStore::ctag*`**

```bash
grep -rn "SyncStore.*ctag\|->ctag(\|->setCtag(\|->clearCtag" ~/dev/refactor-engine-merger/libkalburator/src/ 2>&1 | head
```

Expected: callers all in `remotebackend.cpp` or close. List them.

- [ ] **Step 2: Add `CTagStore` private to `RemoteBackend`**

In `remotebackend.h`:

```cpp
private:
    class CTagStore;                     // pimpl-ish; defined in .cpp
    std::unique_ptr<CTagStore> m_ctags;
```

In `remotebackend.cpp`, define a small inner class managing a `QHash<QString, QString>` cached in memory plus persistence to a `remote_ctags` table inside `.kalburator-sync.db`. Constructor takes the DB path; load on construction; flush on each `setCtag()` (or batch — the existing CTag access pattern is infrequent so unbatched is fine).

- [ ] **Step 3: Update RemoteBackend's call sites**

Every `m_syncStore->ctag(backendId, calendarId)` becomes `m_ctags->get(calendarId)`. (BackendId is implicit — the store is owned by this backend.)

- [ ] **Step 4: Mark `SyncStore::ctag*` deprecated, forward to backend's store via a hook**

Tricky: `SyncStore` is shared across backends, but the new CTag store is per-backend. The deprecated forwarders should look up the right `RemoteBackend` instance via the existing `BackendRegistry` (passed as a constructor arg or held weakly). Or: simply remove the methods (compile-fail any remaining external caller). The latter is cleaner since the `grep` in step 1 has already shown the callers; if external (consumer) callers exist, this task expands.

- [ ] **Step 5: Verify libkalburator ctest still green**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 2>&1 | tail -5
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build --output-on-failure -j 12 2>&1 | tail -5
```

Expected: all tests still pass. CalDAV-dependent tests still work since the CTag plumbing moved but the public behavior is unchanged.

- [ ] **Step 6: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add src/calendar/remotebackend.h src/calendar/remotebackend.cpp src/calendar/syncstore.h src/calendar/syncstore.cpp
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "refactor(calendar): move CTag storage from SyncStore to RemoteBackend private (Phase D)"
```

---

### Task 6: Move local fingerprints to `LocalBackend` private state

Same pattern as Task 5, applied to `LocalBackend`.

**Files:**
- Modify: `src/calendar/localbackend.h` (add private `FingerprintStore` member)
- Modify: `src/calendar/localbackend.cpp`
- Modify: `src/calendar/syncstore.{h,cpp}` (deprecate `localFingerprint*`)

- [ ] **Step 1: Identify callers** — same `grep` pattern as Task 5 but for `localFingerprint`.

- [ ] **Step 2: Add `FingerprintStore` private** — in-memory `QHash<QString, QByteArray>` plus a `local_fingerprints` SQLite table.

- [ ] **Step 3: Update LocalBackend's call sites and `modifiedSince` short-circuit logic.**

- [ ] **Step 4: Remove or deprecate `SyncStore::localFingerprint*` methods.**

- [ ] **Step 5: Build + ctest + commit:**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(calendar): move local-fingerprint storage from SyncStore to LocalBackend private (Phase D)"
```

---

### Task 7: Forward `SyncStore::baseline*` and `propertyBaseline*` to `CalendarBaselineStore`

Now `SyncStore` becomes a thin facade for the calendar-baseline subset.

**Files:**
- Modify: `src/calendar/syncstore.h`
- Modify: `src/calendar/syncstore.cpp`

- [ ] **Step 1: Add a `CalendarBaselineStore` member to `SyncStore`**

`SyncStore` constructs a `CalendarBaselineStore` on the same DB path. Forwards every `baseline*`, `setBaseline*`, `removeBaseline*`, `propertyBaseline*` method to the inner store.

- [ ] **Step 2: Mark each forwarded method `[[deprecated("use CalendarBaselineStore directly")]]`.**

- [ ] **Step 3: Build + ctest** — should still be green; `[[deprecated]]` only emits warnings, not errors. Confirm warnings appear in build output for callers (next task migrates them).

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(calendar): forward SyncStore baseline methods to CalendarBaselineStore (Phase D)"
```

---

### Task 8: Forward `SyncStore::versionHash*` to `BlobBaselineStore`

Same pattern. `SyncStore` gets a `BlobBaselineStore*` member (likely shared with the blob layer's owner — see step 1) and forwards `versionHash*` calls using the new triple-key API: `(backendId, calendarId-as-collectionId, uid-as-recordId)`.

**Files:**
- Modify: `src/calendar/syncstore.h`
- Modify: `src/calendar/syncstore.cpp`

- [ ] **Step 1: Decide on ownership of the shared BlobBaselineStore**

Two reasonable shapes:
- `SyncStore` constructs its own `BlobBaselineStore` on the same DB.
- `SyncStore` holds a non-owning `BlobBaselineStore*` injected by `SyncCoordinator`.

The latter avoids two `QSqlDatabase` connections to the same file. Pick (b) — inject via a setter or constructor arg. Add the parameter; existing call sites in `SyncCoordinator` create one `BlobBaselineStore` and pass it to `SyncStore`.

- [ ] **Step 2: Wire forwarders, mark deprecated**

```cpp
[[deprecated("use BlobBaselineStore::hash(backendId, collectionId, uid)")]]
QByteArray versionHash(const QString &backendId, const QString &calendarId, const QString &uid) const {
    return m_blobStore->hash(backendId, calendarId, uid);
}
```

- [ ] **Step 3: Build + ctest + commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(calendar): forward SyncStore::versionHash* to BlobBaselineStore (Phase D)"
```

---

### Task 9: Migrate callers; rename DB file; delete `SyncStore`

**Files:**
- All callers of `SyncStore::*` in libkalburator (typically `synccoordinator.cpp`, `syncworker.cpp`, plus tests).
- Path constants in libkalburator that reference `.planstan-sync.db` → `.kalburator-sync.db`.
- `src/calendar/syncstore.{h,cpp}` — deleted at the end.
- Both consumers if they touch SyncStore directly (PlanStan / WildPalms).

- [ ] **Step 1: Find all caller sites**

```bash
grep -rn "SyncStore" ~/dev/refactor-engine-merger/libkalburator/src/ ~/dev/refactor-engine-merger/libkalburator/tests/ 2>&1 | grep -v "syncstore\." | head -50
```

Expected: a few dozen call sites — almost all in `synccoordinator.cpp` / `syncworker.cpp`.

- [ ] **Step 2: Migrate each call site to its new home**

Patterns:
- `m_syncStore->baseline(...)` → `m_calendarBaselines->baseline(...)` (after `SyncCoordinator` constructs and exposes a `CalendarBaselineStore`).
- `m_syncStore->versionHash(...)` → `m_blobBaselines->hash(backendId, calendarId, uid)`.
- `m_syncStore->ctag(...)` → already migrated in Task 5 (delete dead callers if any).
- `m_syncStore->localFingerprint(...)` → already migrated in Task 6.
- Conflict storage: was `SyncStore::recordConflict` etc.; route to `ConflictStore` (already at lower layer; see `src/blob/conflictstore.h` — same surface, just reused).
- ID mapping: route to `IDMappingStore` (already exists).
- Last-sync time: `SyncStore::lastSyncTime` → `CalendarBaselineStore::lastSyncTime` (added in Task 1).

Do this in commits-of-three so the diff is reviewable. Build + run tests after each batch.

- [ ] **Step 3: Rename SQLite filename**

In `SyncCoordinator` (or wherever the path is built), change `.planstan-sync.db` to `.kalburator-sync.db`. There's no migration code to write — both consumers are pre-release. Search:

```bash
grep -rn "planstan-sync.db\|planstan_sync_db" ~/dev/refactor-engine-merger/libkalburator/src/ ~/dev/refactor-engine-merger/libkalburator/tests/
```

Update each occurrence.

- [ ] **Step 4: Delete `SyncStore` class**

```bash
rm ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncstore.h \
   ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncstore.cpp
```

Update `src/calendar/CMakeLists.txt` to drop the entry. Build:

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 2>&1 | tail -20
```

If anything still references `SyncStore` it surfaces as an undefined-symbol at link time. Migrate or delete that caller.

- [ ] **Step 5: Run libkalburator ctest end-to-end**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build --output-on-failure -j 12 2>&1 | tail -10
```

Expected: 11 tests pass (9 D.0 + 1 calendar baseline + 1 per-record keys = 11).

- [ ] **Step 6: Commit (the carve-up landing commit)**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add -A
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "refactor(calendar): dissolve SyncStore; rename DB file to .kalburator-sync.db (Phase D Group 1 complete)"
```

- [ ] **Step 7: Verify all three worktrees still build**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh 2>&1 | tail -20
```

If PlanStan or WildPalms references `SyncStore`, the verify script will flag a build failure in that worktree. Migrate that consumer's references — usually a handful of `#include` paths and method-name updates. Land the consumer migration as a commit on its own `refactor/engine-merger` branch.

---

## Group 2 — Backend inheritance hoist

Goal: every concrete `SyncBackend` is now also an `IBlobBackend`. Each backend gets a focused `tst_<backend>_blob_view` test.

### Task 10: Hoist `SyncBackend : public IBlobBackend`

The minimum change to surface the inheritance edge. Subclasses don't compile yet (pure-virtual implementations missing) — that's by design; the next 8 tasks fix one backend at a time, and the build is broken until Task 18.

**Strategy:** Use a temporary default-throwing base impl so the build stays green between tasks.

**Files:**
- Modify: `src/calendar/syncbackend.h`
- Modify: `src/calendar/syncbackend.cpp` (add default-throwing IBlobBackend method bodies)

- [ ] **Step 1: Update header**

```cpp
// src/calendar/syncbackend.h
#include "iblobbackend.h"
// ...
class SyncBackend : public QObject, public IBlobBackend
{
    Q_OBJECT
    // existing surface unchanged
};
```

- [ ] **Step 2: Provide default `IBlobBackend` bodies in `syncbackend.cpp`**

For each `IBlobBackend` pure virtual, add a default impl in `SyncBackend` that:
- For identity/capability methods (`backendId`, `displayName`, `isAvailable`, `availableCollections`, `supportsBatch`, `supportsDeleteTracking`): return a sensible default (`backendId()` -> `backendType()`, `isAvailable()` -> `true`, etc.).
- For data-path methods (`loadRecords`, `loadRecord`, `createRecord`, `updateRecord`, `deleteRecord`, `modifiedSince`, `deletedSince`, `createCollection`, `collectionInfo`): emit a warning via `qWarning() << "SyncBackend default IBlobBackend impl invoked on" << metaObject()->className()` and return an empty/default value (`{}`, `false`, `std::nullopt`).
- Batch methods (`beginBatch`/`commitBatch`/`rollbackBatch`): return `true`/no-op default.

This keeps the build green even if a concrete backend hasn't been migrated yet — it just emits a warning at runtime. As each backend is migrated, it overrides the relevant methods.

- [ ] **Step 3: Build, run all libkalburator tests**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 2>&1 | tail -10
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build --output-on-failure -j 12 2>&1 | tail -10
```

Expected: all green. The default impls aren't invoked yet (no caller is using the blob view).

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(calendar): hoist SyncBackend : public IBlobBackend (Phase D Group 2 begins)"
```

---

### Task 11: Implement `IBlobBackend` on `MockBackend`

`MockBackend` is the simplest — in-memory storage, used by every D.0 test. Doing it first means every subsequent test can rely on `MockBackend`'s blob view.

**Files:**
- Modify: `src/calendar/mockbackend.h`
- Modify: `src/calendar/mockbackend.cpp`
- Create: `tests/calendar/tst_mockbackend_blob_view.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Add `IBlobBackend` overrides to MockBackend**

```cpp
// src/calendar/mockbackend.h, in the public section
public:
    // IBlobBackend
    QString backendId() const override          { return m_id; }
    QString displayName() const override        { return m_id; }
    bool    isAvailable() const override        { return true; }

    QList<CollectionInfo> availableCollections() const override;
    std::optional<CollectionInfo> collectionInfo(const QString &id) const override;
    QString createCollection(const CollectionInfo &info) override;

    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId, const BackendRecord &record) override;
    bool    updateRecord(const BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId) override;

    QList<BackendRecord> modifiedSince(const QString &collectionId, const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId, const QDateTime &since) override;
    bool                 supportsDeleteTracking() const override { return true; }

    bool beginBatch() override     { return true; }
    bool commitBatch() override    { return true; }
    bool rollbackBatch() override  { return true; }
    bool supportsBatch() const override { return false; }
```

- [ ] **Step 2: Implement bodies**

`MockBackend` already keeps an in-memory `QHash<calendarId, QHash<uid, Incidence::Ptr>>`. The blob view exposes the same data through `BackendRecord`. Translation:
- `recordId` ⇄ `uid`
- `collectionId` ⇄ `calendarId`
- `BackendRecord.data` ← `KCalendarCore::ICalFormat().toString(incidence)`
- `BackendRecord.contentHash` ← QCryptographicHash(Sha256) over the iCal text
- `BackendRecord.lastModified` ← `incidence->lastModified()`
- `BackendRecord.isDeleted` ← false (deletion tracked separately by mock)

Helpers: `static BackendRecord toBackendRecord(const Incidence::Ptr &)` and `static Incidence::Ptr fromBackendRecord(const BackendRecord &)`. Reused by the test.

- [ ] **Step 3: Write the test**

```cpp
// tests/calendar/tst_mockbackend_blob_view.cpp
class TestMockBackendBlobView : public QObject {
    Q_OBJECT
private slots:
    void roundTrip_createLoadDelete();
    void modifiedSince_filtersByTimestamp();
    void loadRecord_missingId_returnsNullopt();
};
```

Each slot: cast to `IBlobBackend*`, exercise the API, verify roundtrips correctly.

- [ ] **Step 4: Build, run, commit**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 --target tst_mockbackend_blob_view
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_mockbackend_blob_view --output-on-failure
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): MockBackend implements IBlobBackend + blob-view test (Phase D)"
```

---

### Task 12: Implement `IBlobBackend` on `LocalBackend`

Real I/O — reads/writes `.ics` files in a calendar directory.

**Files:**
- Modify: `src/calendar/localbackend.h`
- Modify: `src/calendar/localbackend.cpp`
- Create: `tests/calendar/tst_localbackend_blob_view.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Translation rules**

- `recordId` ⇄ uid (filename without `.ics`)
- `collectionId` ⇄ calendarId
- `BackendRecord.data` ← raw bytes of the `.ics` file
- `BackendRecord.contentHash` ← SHA-256 of the file contents
- `BackendRecord.lastModified` ← `QFileInfo::lastModified()`
- `modifiedSince` short-circuits via the directory fingerprint (added in Task 6)

- [ ] **Step 2: Implement methods + use existing fingerprint store for short-circuit**

`LocalBackend::modifiedSince(collId, since)`:
1. Compute current fingerprint (already done in Task 6's helper).
2. If matches stored fingerprint, return empty list.
3. Else: walk the directory; collect files modified since timestamp; update fingerprint.

- [ ] **Step 3: Write the blob-view test**

Use `QTemporaryDir` for the calendar root. Tests:
- `createRecord_writesIcsFile`: assert file exists with expected content.
- `loadRecord_readsIcsFile`: opposite direction.
- `modifiedSince_shortCircuitsOnFingerprint`: write file, sync once, modify nothing, call `modifiedSince` → empty result without scanning.
- `modifiedSince_returnsChanged`: write file after `since`, verify it surfaces.

- [ ] **Step 4: Build + run + commit**

```bash
cmake --build ~/dev/refactor-engine-merger/libkalburator/build -j 12 --target tst_localbackend_blob_view
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R tst_localbackend_blob_view --output-on-failure
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): LocalBackend implements IBlobBackend + blob-view test (Phase D)"
```

---

### Task 13: Implement `IBlobBackend` on `RemoteBackend`

CalDAV-flavored, async. Most complex of the eight.

**Files:**
- Modify: `src/calendar/remotebackend.h`
- Modify: `src/calendar/remotebackend.cpp`
- Create: `tests/calendar/tst_remotebackend_blob_view.cpp` (gated on `KALBURATOR_ENABLE_CALDAV_TESTS=ON` — mirror PlanStan's pattern)
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Translation rules**

- `recordId` ⇄ calendar-relative href (CalDAV serves these natively).
- `collectionId` ⇄ calendarId (which already maps to a CalDAV calendar URL).
- `BackendRecord.data` ← raw iCal text from a CalDAV GET.
- `BackendRecord.contentHash` ← SHA-256 of the bytes (NOT the ETag — we want content-equality).
- `BackendRecord.lastModified` ← parsed CalDAV `getlastmodified` property, falling back to ETag if missing.

`modifiedSince` short-circuits on CTag (added in Task 5).

- [ ] **Step 2: Async wrapping — use the existing `FetchOperation` pattern**

`IBlobBackend` is documented synchronous. `RemoteBackend`'s native API is async-Operation. Reconcile by making the blob view block on operation completion (`QEventLoop::exec` until `finished` signal). This is acceptable because the calendar layer's `SyncWorker` already runs on a worker thread — blocking that thread for I/O is the existing model.

Phase F revisits async; for now, blocking on a per-call event loop is fine.

- [ ] **Step 3: Write the test (CalDAV-gated)**

In `tests/calendar/CMakeLists.txt`:

```cmake
if (KALBURATOR_ENABLE_CALDAV_TESTS)
    kalburator_add_calendar_test(tst_remotebackend_blob_view)
endif()
```

Test exercises against either a live test server (per existing CalDAV test pattern) or a `MockHttpServer` if one exists in the codebase. If neither, gate the test entirely on `KALBURATOR_ENABLE_CALDAV_TESTS`.

- [ ] **Step 4: Build + run + commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): RemoteBackend implements IBlobBackend + blob-view test (Phase D)"
```

---

### Task 14: Implement `IBlobBackend` on `OrgBackend`

`OrgBackend` reads .org files via `OrgModeParser`. Each `:ID:` org property = uid.

**Files:**
- Modify: `src/calendar/orgbackend.{h,cpp}`
- Create: `tests/calendar/tst_orgbackend_blob_view.cpp`

- [ ] **Step 1: Translation: `recordId` ⇄ org `:ID:`, `collectionId` ⇄ calendarId, `BackendRecord.data` ← serialized org element bytes (not iCal — keep org's native format for round-tripping).** 

The blob view operates in raw bytes; calendar-layer code that's already calling `OrgBackend::loadItems()` does the iCal/org translation. Phase E moves that translation into the backend. For Phase D, the blob view's `data` field carries opaque bytes — the org layer hands them off and gets them back. Per-backend `recordId` mapping in the design doc says iCal-UID-derived-from-:ID: — keep it consistent: `recordId == :ID: value`.

- [ ] **Step 2: Implement + write `tst_orgbackend_blob_view`** — gated on `KALBURATOR_HAVE_ORG_IO=ON`.

- [ ] **Step 3: Build + commit.**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): OrgBackend implements IBlobBackend + blob-view test (Phase D)"
```

---

### Task 15: Implement `IBlobBackend` on `AkonadiBackend`

Gated on Akonadi build flag.

**Files:**
- Modify: `src/calendar/akonadibackend.{h,cpp}`
- Create: `tests/calendar/tst_akonadibackend_blob_view.cpp` (gated `KALBURATOR_HAVE_AKONADI=ON`)

Translation: `recordId` ← `Akonadi::Item::id()` rendered as a string. The Akonadi async retrieval API can be wrapped with `QEventLoop::exec` exactly like CalDAV.

If Akonadi isn't built in the default profile, the implementation lands as a stub returning warnings and the test is build-gated. Phase D doesn't need to validate Akonadi works against a live server — only that the type-system change is honored.

- [ ] Build + commit:

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): AkonadiBackend implements IBlobBackend (Phase D)"
```

---

### Task 16: Implement `IBlobBackend` on `DecSyncBackend`

`DecSyncBackend` is sync-protocol implementation, not a sync source per OQ4. For Phase D, give it the same `IBlobBackend` shape as the others — its participation in the unified engine is a Phase F decision.

**Files:**
- Modify: `src/calendar/decsyncbackend.{h,cpp}`
- Create: `tests/calendar/tst_decsyncbackend_blob_view.cpp` (gated on whatever build flag controls DecSync — likely always-on but check)

Translation: `recordId` ⇄ uid; the rest mirrors `LocalBackend`.

- [ ] Build + commit:

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): DecSyncBackend implements IBlobBackend (Phase D)"
```

---

### Task 17: Implement `IBlobBackend` on `SubscriptionBackend`

Read-only feed backend (ICS subscription).

**Files:**
- Modify: `src/calendar/subscriptionbackend.{h,cpp}`
- Create: `tests/calendar/tst_subscriptionbackend_blob_view.cpp`

Translation: `recordId` ⇄ uid; reads via `IcsFeedFetcher` → parsed `MemoryCalendar` → `BackendRecord` per incidence. Writes return `false` (read-only). Test asserts:
- `loadRecord_returnsExpected` for a fixture-backed feed.
- `createRecord_returnsEmptyString` (write rejected).
- `updateRecord_returnsFalse`.
- `deleteRecord_returnsFalse`.

- [ ] Build + commit:

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): SubscriptionBackend implements IBlobBackend (read-only) + test (Phase D)"
```

---

### Task 18: Implement `IBlobBackend` on `HolidaySubscriptionBackend`

Inherits from `SubscriptionBackend`. The blob view is inherited transitively; only verify the one-line override (if any) and the test.

**Files:**
- Modify: `src/calendar/holidaysubscriptionbackend.{h,cpp}` (likely no change needed)
- Create: `tests/calendar/tst_holidaysubscriptionbackend_blob_view.cpp` (smoke-test against a fixture holiday feed)

- [ ] Build + commit:

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): HolidaySubscriptionBackend IBlobBackend smoke test (Phase D)"
```

---

### Group 2 wrap

After Task 18:
- All eight concrete backends are static-castable to `IBlobBackend*`.
- libkalburator ctest count: **9 (D.0) + 2 (Group 1) + 8 (Group 2)** = 19.
- `verify-all.sh` should still be green; consumers haven't been touched yet.
- The default-throwing impls in `SyncBackend` are now reachable only by mistake (every concrete subclass overrides them). Optional follow-up: re-mark them pure-virtual and let the linker enforce coverage. **Skip** for this phase — keeping defaults gives Phase E/F room to add backends without immediately implementing the full surface.

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh 2>&1 | tail -10
```

---

## Group 3 — Engine wiring

Goal: `SyncWorker` actually uses the blob view. First-sync goes through `BlobSyncEngine`. The four D.0 tests still pass.

### Task 19: Wire `SyncWorker` fetches to the blob view (subsequent-sync path)

**Files:**
- Modify: `src/calendar/syncworker.h`
- Modify: `src/calendar/syncworker.cpp`

- [ ] **Step 1: Find the existing fetch sites**

```bash
grep -n "fetchSourceRecords\|fetchTargetRecords\|loadItems(" ~/dev/refactor-engine-merger/libkalburator/src/calendar/syncworker.cpp | head -20
```

These are the call sites that pull bytes from each backend.

- [ ] **Step 2: Replace with blob-view calls**

Where today the worker does `backend->loadItems(calendar)` (calendar-typed), now it does:

```cpp
auto *blob = static_cast<IBlobBackend*>(backend);
QList<BackendRecord> records = blob->modifiedSince(collectionId, lastSyncTime);
// translate records back into Incidence::Ptr for the existing merge code
QList<Incidence::Ptr> incidences;
incidences.reserve(records.size());
for (const auto &r : records) {
    incidences.append(KCalendarCore::ICalFormat().fromString(QString::fromUtf8(r.data)));
}
```

(Pull the `static_cast<IBlobBackend*>(backend)` into a small helper if it gets repeated.)

- [ ] **Step 3: Run D.0 tests**

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build -R "tst_calendar_sync_full|tst_calendar_sync_oneway|tst_calendar_conflict|tst_calendar_transcoding_warning" --output-on-failure
```

Expected: 4/4 still pass. If a test fails, the translation between `BackendRecord` and `Incidence::Ptr` is the most likely culprit.

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "refactor(calendar): SyncWorker fetches via IBlobBackend view (Phase D)"
```

---

### Task 20: Per-record hash skip via `BlobBaselineStore`

Before invoking calendar-level diff, skip records whose hash matches the blob baseline.

**Files:**
- Modify: `src/calendar/syncworker.cpp`

- [ ] **Step 1: Inject `BlobBaselineStore*` into `SyncWorker`**

Constructor or setter — `SyncCoordinator` owns it and passes through.

- [ ] **Step 2: Add the skip**

In the loop after `modifiedSince` but before merge:

```cpp
for (const BackendRecord &r : records) {
    if (m_blobBaselines->hash(backend->backendId(), collectionId, r.id) == r.contentHash) {
        continue; // unchanged since last sync
    }
    incidences.append(parseIcal(r));
}
```

- [ ] **Step 3: D.0 tests**

The four tests must still pass — they use `MockBackend`, hash short-circuit reduces work but doesn't change observable behavior.

- [ ] **Step 4: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "perf(calendar): skip per-record hash-equal incidences before merge (Phase D)"
```

---

### Task 21: First-sync mirror via `BlobSyncEngine`

The phase-defining commit. `SyncWorker` detects the no-baseline case and dispatches to `BlobSyncEngine::mirror` (one-way) or `twoWayNaive` (two-way).

**Files:**
- Modify: `src/calendar/syncworker.h`
- Modify: `src/calendar/syncworker.cpp`
- Create: `tests/calendar/tst_calendar_first_sync_via_blob_engine.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Add `dispatchFirstSync` to `SyncWorker`**

```cpp
// syncworker.h
private:
    void dispatchFirstSync(const SyncMapping &mapping);
    void harvestBaselinesAfterFirstSync(const SyncMapping &mapping);
```

```cpp
// syncworker.cpp
void SyncWorker::dispatchFirstSync(const SyncMapping &mapping) {
    auto *src = static_cast<IBlobBackend*>(backendById(mapping.sourceBackendId));
    auto *tgt = static_cast<IBlobBackend*>(backendById(mapping.targetBackendId));
    BlobSyncEngine engine;  // (or get via injection)
    BlobSyncResult result;
    if (mapping.mode == SyncMode::OneWayUpload) {
        result = engine.mirror(*src, *tgt, mapping.collectionId);
    } else {
        result = engine.twoWayNaive(*src, *tgt, mapping.collectionId);
    }
    if (!result.success) {
        emit syncFailed(mapping.id, result.errorMessage);
        return;
    }
    harvestBaselinesAfterFirstSync(mapping);
    emit syncCompleted(mapping.id);
}

void SyncWorker::harvestBaselinesAfterFirstSync(const SyncMapping &mapping) {
    auto *src = static_cast<IBlobBackend*>(backendById(mapping.sourceBackendId));
    QHash<QString, QString> uidToIcal;
    for (const BackendRecord &r : src->loadRecords(mapping.collectionId)) {
        uidToIcal.insert(r.id, QString::fromUtf8(r.data));
        m_blobBaselines->setHash(src->backendId(), mapping.collectionId, r.id, r.contentHash);
    }
    m_calendarBaselines->setBaselines(mapping.id, uidToIcal);
    m_calendarBaselines->setLastSyncTime(mapping.id, QDateTime::currentDateTime());
}
```

- [ ] **Step 2: Route in `processSync` or wherever per-mapping sync starts**

```cpp
void SyncWorker::syncMapping(const SyncMapping &mapping) {
    if (!m_calendarBaselines->hasBaselines(mapping.id)) {
        dispatchFirstSync(mapping);
        return;
    }
    // (existing path: subsequent sync via blob view + 3-way merge)
    syncMappingSubsequent(mapping);
}
```

- [ ] **Step 3: Write `tst_calendar_first_sync_via_blob_engine`**

```cpp
// Setup: source MockBackend with 3 events, target empty, no calendar baseline.
// Run: SyncCoordinator::runSync.
// Assert:
//   - target now has all 3 records (via blob view).
//   - CalendarBaselineStore::hasBaselines(mappingId) == true.
//   - CalendarBaselineStore::allBaselines(mappingId).size() == 3.
//   - BlobBaselineStore has hashes for all 3.
//   - syncCompleted signal fired exactly once.
```

Use a `QSignalSpy` on `BlobSyncEngine`'s `progressChanged` (if accessible) to confirm the engine ran. Or simpler: spy on the source/target `MockBackend`'s blob-view call log to confirm `loadRecords` (called by `mirror`) ran.

- [ ] **Step 4: D.0 tests still green**

The four D.0 tests pin the *subsequent* sync path. They use seeded baselines, so `dispatchFirstSync` shouldn't fire. If any of them did rely on the no-baseline state (e.g., `fullSync_bothEmpty_doesNothing`), the worker now dispatches via `BlobSyncEngine::mirror`, which on empty source results in `target unchanged`. Verify the assertions still hold.

- [ ] **Step 5: Commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "feat(calendar): first-sync dispatches via BlobSyncEngine; harvests calendar baselines (Phase D)"
```

---

### Task 22: `tst_calendar_subsequent_sync_uses_blob_view`

Pin the subsequent-sync code path: the worker calls `IBlobBackend::modifiedSince` and per-record hash skip works.

**Files:**
- Create: `tests/calendar/tst_calendar_subsequent_sync_uses_blob_view.cpp`
- Modify: `tests/calendar/CMakeLists.txt`

- [ ] **Step 1: Test setup**

Pre-seed `CalendarBaselineStore` for `mapping.id` with two iCal entries (so `hasBaselines` returns true and the subsequent path runs). Pre-seed `BlobBaselineStore` with hashes matching the source's current content.

- [ ] **Step 2: Test assertions**

Configure `MockBackend` to log every `modifiedSince` and `loadRecord` call. Run sync. Assert:
- `MockBackend` saw a `modifiedSince` call (not `loadItems`).
- For records whose hash matches the baseline, NO `loadRecord` call (skipped).
- For modified records, `loadRecord` called and the change propagated.

- [ ] **Step 3: Build + run + commit**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator commit -am "test(calendar): tst_calendar_subsequent_sync_uses_blob_view (Phase D)"
```

---

### Group 3 wrap

After Task 22:
- libkalburator ctest count: **19 (after Group 2) + 2 (this group) = 21**.
- All four D.0 contracts still hold.
- Both sync paths exercised by tests.

---

## Consumer migration

### Task 23: Migrate PlanStan callers

PlanStan's `refactor/engine-merger` worktree may reference removed/renamed APIs (`SyncStore`, `.planstan-sync.db` path, deprecated forwarders). Find and migrate.

**Working tree:** `~/dev/refactor-engine-merger/PlanStan/` on `refactor/engine-merger` branch.

- [ ] **Step 1: Probe for breakage**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
cmake --preset dev 2>&1 | tail -10
cmake --build build-dev -j 12 2>&1 | tail -30
```

Identify compile errors. Common surfaces:
- `#include "syncstore.h"` → switch to `calendarbaselinestore.h` / `blobbaselinestore.h`.
- `SyncStore *` member or argument → split into the two stores.
- `.planstan-sync.db` literals → `.kalburator-sync.db`.

- [ ] **Step 2: Fix one source of breakage at a time, build between fixes**

Each fix lands as its own commit on `refactor/engine-merger` in the PlanStan worktree. Use `git -C ~/dev/refactor-engine-merger/PlanStan commit -am "..."`.

- [ ] **Step 3: Run PlanStan tests; expect baseline (95 pass / 25 fail)**

```bash
ctest --test-dir build-dev --output-on-failure -j 12 2>&1 | tail -10
```

The pre-existing 25 failures are the noise floor (per `CURRENT-STATUS.md`). Anything else means we introduced a regression.

- [ ] **Step 4: Commit (as needed) and continue**

---

### Task 24: Migrate WildPalms callers

WildPalms doesn't use the calendar engine, so likely no direct `SyncStore` references. Verify.

**Working tree:** `~/dev/refactor-engine-merger/WildPalms/` on `refactor/engine-merger`.

- [ ] **Step 1: Build**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake -S . -B build [...required flags from SETUP.md...] 2>&1 | tail -5
cmake --build build -j 12 2>&1 | tail -10
```

- [ ] **Step 2: Probe for any references**

```bash
grep -rn "SyncStore\|planstan-sync.db" ~/dev/refactor-engine-merger/WildPalms/src/ ~/dev/refactor-engine-merger/WildPalms/tests/
```

If results are empty, no migration needed; just confirm the build is green.

- [ ] **Step 3: Run tests**

```bash
ctest --test-dir build --output-on-failure -j 12 2>&1 | tail -10
```

Expected: **73/73 pass**. Anything else is investigated.

- [ ] **Step 4: Commit (only if changes made)**

---

## Final wrap

### Task 25: `verify-all.sh` clean + baseline refresh

- [ ] **Step 1: Run verify-all**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh 2>&1 | tail -30
```

Expected: exit 0, "all green, no flips". If exit 3 (improvement), that means a previously-flaky test stabilized — investigate before refreshing.

- [ ] **Step 2: Refresh baselines if needed**

If new tests legitimately added to libkalburator, refresh `~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt`:

```bash
ctest --test-dir ~/dev/refactor-engine-merger/libkalburator/build --output-on-failure -j 12 \
  > ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt 2>&1
grep "tests passed" ~/dev/refactor-engine-merger/baselines/libkalburator-worktree-ctest.txt
```

Expected: `100% tests passed, 0 tests failed out of 21`.

PlanStan and WildPalms baselines should be unchanged.

---

### Task 26: Tag, status updates, FINDINGS

- [ ] **Step 1: Confirm clean state**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator status
git -C ~/dev/refactor-engine-merger/libkalburator log --oneline v0.9-phase-d0-tests-first..HEAD
```

Expected: working tree clean; substantial commit log spanning Tasks 1–26.

- [ ] **Step 2: Tag**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator tag v0.10-phase-d-compose
git -C ~/dev/refactor-engine-merger/libkalburator describe --tags v0.10-phase-d-compose
```

- [ ] **Step 3: Update `CURRENT-STATUS.md`**

Move "Phase D — Compose" from "Next" to "Where we are" with the landed-date and tag SHA. Replace it under "Next" with "Phase E — Transcoding into backends." Bump the date.

- [ ] **Step 4: Update `04m-…-design.md` and `04m-…-plan.md` Status lines**

Per libkalburator's CLAUDE.md, phase-status docs are living documents. Open both and flip the Status line to "Landed YYYY-MM-DD on tag v0.10-phase-d-compose."

- [ ] **Step 5: Append non-obvious learnings to `FINDINGS.md`**

If anything was discovered during execution that future agents would benefit from knowing — schema migration gotchas, async-wrapping pitfalls in `RemoteBackend`, surprising test-ordering effects from per-backend storage — write it in `~/dev/refactor-engine-merger/FINDINGS.md` using the file's format. **This is a load-bearing step**; tribal knowledge is what FINDINGS exists to prevent.

- [ ] **Step 6: Final verify-all**

```bash
bash ~/dev/refactor-engine-merger/scripts/verify-all.sh 2>&1 | tail -10
```

Confirm green one more time.

- [ ] **Step 7: Commit any doc updates and announce**

```bash
git -C ~/dev/refactor-engine-merger/libkalburator add docs/phase0/04m-phase-d-compose-design.md docs/phase0/04m-phase-d-compose-plan.md
git -C ~/dev/refactor-engine-merger/libkalburator commit -m "docs(phase0): mark Phase D landed (tag v0.10-phase-d-compose)"
```

CURRENT-STATUS / FINDINGS / baselines aren't in any git repo — they live in the coordination folder. Just save them in place.

---

## Self-review

**1. Spec coverage:**
- Decision 1 (delegation depth) → Tasks 19, 20 (subsequent sync), Task 21 (first-sync via BlobSyncEngine). ✓
- Decision 2 (inheritance hoist) → Tasks 10–18. ✓
- Decision 3 (DB rename) → Task 9, Step 3. ✓
- Decision 4 (carve-up) → Tasks 1–9. ✓
- Decision 5 (modifiedSince stays) → no Task — explicit non-change. Documented in design's "What deliberately doesn't change". ✓
- D.0 contracts preserved → re-run after Tasks 19, 20, 21 (steps explicit). ✓
- New tests enumerated → tasks 2, 4, 11, 12, 13, 14, 15, 16, 17, 18, 21, 22 — ten explicit, eight per-backend. ✓

**2. Placeholder scan:**
- Bracketed sketches in Tasks 14, 15, 16, 17 are intentional (pattern is shown in detail in Tasks 11–13; remaining backends apply the same pattern with translation rules called out per backend).
- "if neither, gate the test entirely" in Task 13 is an honest disclaimer — we don't know the test infra status until we read it. Documented next-step.
- All commands, file paths, and signatures are concrete.

**3. Type consistency:**
- `CalendarBaselineStore` method names (`baseline`, `setBaseline`, `setBaselines`, `removeBaseline`, `removeBaselines`, `allBaselines`, `hasBaselines`, `propertyBaseline`, `setPropertyBaseline`, `lastSyncTime`, `setLastSyncTime`) consistent across Tasks 1, 2, 7, 21.
- `BlobBaselineStore::hash(backendId, collectionId, recordId)` triple-key signature consistent across Tasks 3, 4, 8, 20, 21.
- `BackendRecord` field names (`id`, `data`, `contentHash`, `lastModified`, `isDeleted`) consistent throughout.
- `IBlobBackend` method list matches the design's "Component changes" section.

**4. Cross-task naming:**
- `kalburator_calendar_test_stubs` (existing static lib from D.0) referenced when adding new integration-style tests; new per-backend tests use the simpler `kalburator_add_calendar_test` since they don't drive the full sync engine.

## Plan-level risks

- **Task 5/6 destructive removal of `SyncStore::ctag*` / `localFingerprint*`.** If a consumer (especially PlanStan) calls these directly outside the migration scope of Task 23, the build breaks late. Mitigation: at Task 5/6 completion, grep both consumer worktrees too.
- **Task 9's "delete `SyncStore`" is destructive.** Consumers may have included the header. Verify in Task 23/24 with grep before declaring victory. If found, add migration commits in the consumer worktree before tagging.
- **Task 13's CalDAV blocking-on-event-loop is a temporary measure.** It works because the worker thread already exists; if Phase F changes the threading model, this needs to be revisited. Documented in the design's "What deliberately doesn't change" + "Open questions deferred."
- **Task 21's `harvestBaselinesAfterFirstSync` is the load-bearing step.** If it fails partway, `CalendarBaselineStore` may end up with a partial baseline; next sync would see `hasBaselines==true` and skip first-sync but the merge baseline would be incomplete. Mitigation: wrap the harvest in a transaction; if it fails, clear the partial baseline and report a sync error. Implementation note: `CalendarBaselineStore::setBaselines(mappingId, ...)` already wraps in a SQL transaction (per Task 1 design); the post-harvest commit is the atomic boundary.
- **Plan length: 26 tasks, ~30+ commits, multiple worktrees.** Use `subagent-driven-development` so each task is dispatched to a fresh subagent and the main session reviews between tasks; this keeps context-window usage manageable across the long execution.
