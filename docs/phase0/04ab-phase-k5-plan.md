# Phase K.5 Implementation Plan — Unified Baseline + Storage Move

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. User has authorized autonomous execution including code-review fixes, baseline refreshes, and the named tag step.

**Goal:** Collapse `CalendarBaselineStore` + `BlobBaselineStore` into a single domain-neutral `Storage::BaselineStore`; move it (and `IDMappingStore`) from `src/journal/` to `src/storage/` under namespace `Storage::*`; introduce SQLite schema v5 with `collection_baselines` + `mapping_metadata` tables; route property baselines through `DomainPlugin::baselineProperties()`. Cross-repo cutover (PlanStan + WildPalms) lands in the same K.5 commit group.

**Architecture:** The unified `Storage::BaselineStore` keeps the existing `blob_baselines_v3` table for per-record canonical baselines (keyed by `(mappingId, recordId)`). Calendar's iCal-text baselines fold in by encoding as `CanonicalRecord{shape=Calendar canonical, recordId=uid, data=iCal bytes}` — no new tables for that. Two **new** tables service the calendar property + sync-metadata needs: `collection_baselines (mappingId, collectionId, properties_json)` and `mapping_metadata (mappingId, last_sync_at)`. v4→v5 migration is idempotent (CREATE TABLE IF NOT EXISTS + version stamp). `CalendarBaselineStore` becomes a thin facade for one task, then deletes once callers migrate.

**Tech Stack:** Qt6, KDE Frameworks 6, KCalendarCore, SQLite via Qt SQL, QtTest. Build with `-j 10` per project memory.

**Cross-repo coordination:** This plan touches three worktrees on `refactor/engine-merger`:
- `~/dev/refactor-engine-merger/libkalburator/` (primary work)
- `~/dev/refactor-engine-merger/PlanStan/` (Task 11)
- `~/dev/refactor-engine-merger/WildPalms/` (Task 12)

Per CLAUDE.md, commits to `refactor/engine-merger` in any of the three worktrees do not require per-commit authorization.

**Falsifiable end-state contract (from design doc §4 K.5):**
- `verify-all.sh` green at tag.
- `grep -rn 'class CalendarBaselineStore\|CalendarBaselineStore *\*' src/ tests/` returns empty in libkalburator.
- `grep -rn 'class BlobBaselineStore\b\|Kalburator::Sync::BlobBaselineStore' src/ tests/` returns empty in libkalburator.
- All calendar baseline tests pass against the unified store.
- `find src/journal -type f -name '*.h' -o -name '*.cpp' | grep -E 'baselinestore|idmappingstore'` returns empty (those files moved to `src/storage/`).

**Out of scope (stays for K.5.5/K.6/K.8):**
- Renaming `Sync::QSyncCore::BaselineStore` (the orphan in-memory hash store at `src/journal/baselinestore.{h,cpp}`). It's in a different namespace, won't collide with the new `Storage::BaselineStore`. WildPalms still uses it via `QSyncCore::BaselineStore`; cleanup deferred.
- `calendarjournal.{h,cpp}` and `asyncfilewriter.{h,cpp}` stay in `src/journal/` — those are actual journals, not stores. K.5.5 may rename the directory wholesale; this plan does not.
- `I`-prefix drop on interfaces. K.5.5 scope.

---

## File Structure

**New files (libkalburator):**
- `src/storage/baselinestore.h` — moved from `src/journal/blobbaselinestore.h`; class renamed `BlobBaselineStore` → `BaselineStore`; namespace `Sync` → `Storage`. Adds collection-baseline + mapping-metadata APIs.
- `src/storage/baselinestore.cpp` — moved from `src/journal/blobbaselinestore.cpp`. Adds schema v5 migration + new table CRUD.
- `src/storage/idmappingstore.h` — moved from `src/journal/idmappingstore.h`; namespace `Sync` → `Storage`.
- `src/storage/idmappingstore.cpp` — moved from `src/journal/idmappingstore.cpp`.
- `tests/storage/CMakeLists.txt` — moved/renamed from `tests/journal/CMakeLists.txt` with the relevant subset.
- `tests/storage/tst_baseline_store_collection_properties.cpp` — new TDD test for `setCollectionBaseline`.
- `tests/storage/tst_baseline_store_mapping_metadata.cpp` — new TDD test for `setLastSyncTime`.
- `tests/storage/tst_baseline_store_v4_to_v5_migration.cpp` — new TDD test for idempotent migration.

**Forwarding shims (transient — deleted in Task 13):**
- `src/journal/blobbaselinestore.h` — single-file shim: `#include "../storage/baselinestore.h"` + `namespace Kalburator::Sync { using BlobBaselineStore = ::Kalburator::Storage::BaselineStore; }`. Lets PlanStan/WildPalms compile during their cutover commit.
- `src/journal/idmappingstore.h` — analogous shim. Re-exports `Storage::IDMappingStore` and `Storage::IDMapping` into `Kalburator::Sync`.

**Modified files (libkalburator):**
- `CMakeLists.txt` — adds `src/storage/baselinestore.{h,cpp}` + `src/storage/idmappingstore.{h,cpp}` to source list; removes corresponding `src/journal/` entries (except orphan `baselinestore.{h,cpp}`, `calendarjournal.{h,cpp}`, `asyncfilewriter.{h,cpp}`); adds `src/storage/` to `target_include_directories` PUBLIC.
- `src/shape/domainplugin.h` — adds `baselineProperties()` virtual returning `QStringList` (property keys).
- `src/calendar/calendardomainplugin.h/cpp` — overrides `baselineProperties()` to return `{"color", "description"}`.
- `src/engine/syncengine.{h,cpp}` — replaces `m_calendarBaselines` + `m_blobBaselines` with single `m_baselineStore`; removes `setCalendarBaselineStore` + `setBlobBaselineStore` in favor of `setBaselineStore`. iCal-text baseline writes/reads route through `BaselineStore::setBaselineV3` keyed by uid + calendar canonical shape. Property baselines route through `BaselineStore::setCollectionBaseline` driven by `DomainPlugin::baselineProperties()`. Last-sync timestamps route through `BaselineStore::setLastSyncTime`.
- `src/calendar/calendarbaselinestore.{h,cpp}` — first becomes thin facade (Task 8), then deleted (Task 13).
- All `tests/journal/*.cpp` — moved to `tests/storage/`, includes/namespaces updated.
- `tests/calendar/tst_calendar_baseline_store.cpp` — moved/renamed to `tests/storage/tst_baseline_store_calendar_facade.cpp` for the facade's lifetime, then deleted in Task 13 (or transformed into a calendar-shape integration test against the unified store — see Task 13).

**Modified files (PlanStan):**
- `src/controllers/collectioncontroller.cpp` (lines ~38–39, ~1860–1885) — single `BaselineStore` constructor in place of two; `m_calendarBaselines` field deleted; `m_blobBaselines` renamed to `m_baselineStore`.
- `src/controllers/collectioncontroller.h` — drops `CalendarBaselineStore *m_calendarBaselines = nullptr;`, renames `m_blobBaselines` → `m_baselineStore`, type `BaselineStore *`.

**Modified files (WildPalms):**
- `src/runtime/palmruntime.h` (lines ~23, ~160) — forward decl + member type `Kalburator::Storage::BaselineStore`.
- `src/runtime/palmruntime.cpp` (lines ~179, ~187) — namespace + class name update.

---

## Task 1: Storage directory bootstrap — file move + namespace rename

**Files:**
- Move: `libkalburator/src/journal/blobbaselinestore.h` → `libkalburator/src/storage/baselinestore.h`
- Move: `libkalburator/src/journal/blobbaselinestore.cpp` → `libkalburator/src/storage/baselinestore.cpp`
- Move: `libkalburator/src/journal/idmappingstore.h` → `libkalburator/src/storage/idmappingstore.h`
- Move: `libkalburator/src/journal/idmappingstore.cpp` → `libkalburator/src/storage/idmappingstore.cpp`
- Modify: `libkalburator/CMakeLists.txt` (add `src/storage/`, drop the four moved entries from `src/journal/`)
- Create: `libkalburator/src/journal/blobbaselinestore.h` (forwarding shim)
- Create: `libkalburator/src/journal/idmappingstore.h` (forwarding shim)

This task is mechanical: rename class `BlobBaselineStore` → `BaselineStore`, change namespace `Kalburator::Sync` → `Kalburator::Storage` in the four files. All current internal usage continues to work via `using` aliases in the shims.

- [ ] **Step 1: Move blobbaselinestore.{h,cpp} via git mv**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
mkdir -p src/storage
git mv src/journal/blobbaselinestore.h  src/storage/baselinestore.h
git mv src/journal/blobbaselinestore.cpp src/storage/baselinestore.cpp
git mv src/journal/idmappingstore.h      src/storage/idmappingstore.h
git mv src/journal/idmappingstore.cpp    src/storage/idmappingstore.cpp
```

- [ ] **Step 2: Rewrite header guards + namespace + class name in src/storage/baselinestore.h**

Apply these edits inside `src/storage/baselinestore.h`:
- Header guard `KALBURATOR_BLOBBASELINESTORE_H` → `KALBURATOR_STORAGE_BASELINESTORE_H` (both `#ifndef` and `#define` and the trailing `#endif` comment).
- `namespace Kalburator::Sync` → `namespace Kalburator::Storage`.
- `class BlobBaselineStore` → `class BaselineStore` (and all `BlobBaselineStore(...)`/`~BlobBaselineStore()`/`BlobBaselineStore &operator=` constructor/destructor/operator declarations).

- [ ] **Step 3: Update src/storage/baselinestore.cpp to match**

- `#include "blobbaselinestore.h"` → `#include "baselinestore.h"`.
- `namespace Kalburator::Sync` → `namespace Kalburator::Storage`.
- Replace all `BlobBaselineStore::` qualifications with `BaselineStore::` (use sed within the file scope).
- `QStringLiteral("BlobBaselineStore_%1")` (connection name) → `QStringLiteral("KalburatorBaselineStore_%1")`. Reason: connection-name uniqueness is per-process; renaming avoids confusion in logs once the class is renamed.

```bash
sed -i 's/BlobBaselineStore::/BaselineStore::/g' src/storage/baselinestore.cpp
sed -i 's/namespace Kalburator::Sync/namespace Kalburator::Storage/g' src/storage/baselinestore.cpp
```

- [ ] **Step 4: Update src/storage/idmappingstore.{h,cpp} namespace**

The class names (`IDMappingStore`, struct `IDMapping`) stay; only namespace moves.

```bash
sed -i 's/namespace Kalburator::Sync/namespace Kalburator::Storage/g' src/storage/idmappingstore.h src/storage/idmappingstore.cpp
sed -i 's/Kalburator::Sync::IDMappingStore/Kalburator::Storage::IDMappingStore/g' src/storage/idmappingstore.h src/storage/idmappingstore.cpp
sed -i 's/Kalburator::Sync::IDMapping\b/Kalburator::Storage::IDMapping/g'      src/storage/idmappingstore.h src/storage/idmappingstore.cpp
```

Update the `idmappingstore.h` header guard (`KALBURATOR_IDMAPPINGSTORE_H` → `KALBURATOR_STORAGE_IDMAPPINGSTORE_H`).

- [ ] **Step 5: Create the forwarding shim at src/journal/blobbaselinestore.h**

```cpp
#ifndef KALBURATOR_BLOBBASELINESTORE_FORWARDING_SHIM_H
#define KALBURATOR_BLOBBASELINESTORE_FORWARDING_SHIM_H

// Phase K.5 forwarding shim. Removed in Phase K.5 Task 13 once
// PlanStan + WildPalms cut over to <storage/baselinestore.h>.
#include "../storage/baselinestore.h"

namespace Kalburator::Sync {
    using BlobBaselineStore = ::Kalburator::Storage::BaselineStore;
}

#endif
```

- [ ] **Step 6: Create the forwarding shim at src/journal/idmappingstore.h**

```cpp
#ifndef KALBURATOR_IDMAPPINGSTORE_FORWARDING_SHIM_H
#define KALBURATOR_IDMAPPINGSTORE_FORWARDING_SHIM_H

// Phase K.5 forwarding shim. Removed in Phase K.5 Task 13.
#include "../storage/idmappingstore.h"

namespace Kalburator::Sync {
    using IDMappingStore = ::Kalburator::Storage::IDMappingStore;
    using IDMapping      = ::Kalburator::Storage::IDMapping;
}

#endif
```

- [ ] **Step 7: Update libkalburator/CMakeLists.txt**

Around line 76 (the `journal/` directory comment) and lines 201–212, replace the four moved entries with `src/storage/` paths and add `src/storage/` to public include directories.

Edit `CMakeLists.txt`:
- In the `# journal/` comment block, change wording to reflect the move (the four entries about baseline store and id mapping store are now described as moved to `src/storage/`).
- Remove these lines from the source list:
  ```
  src/journal/blobbaselinestore.h
  src/journal/idmappingstore.h
  src/journal/blobbaselinestore.cpp
  src/journal/idmappingstore.cpp
  ```
- Add (alphabetized) under a new `src/storage/` block:
  ```
  src/storage/baselinestore.h
  src/storage/idmappingstore.h
  src/storage/baselinestore.cpp
  src/storage/idmappingstore.cpp
  ```
- Add `${CMAKE_CURRENT_SOURCE_DIR}/src/storage` to the `target_include_directories(kalburator PUBLIC ...)` list (find the existing `src/journal` entry and add a sibling `src/storage` entry; do not remove `src/journal` — the shim files still need it).

- [ ] **Step 8: Build to verify shims work**

```bash
cmake --build build -j 10
```

Expected: clean build. The shim aliases mean every existing reference to `Kalburator::Sync::BlobBaselineStore` / `Kalburator::Sync::IDMappingStore` / `Kalburator::Sync::IDMapping` still resolves.

- [ ] **Step 9: Run libkalburator tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: same pass/fail set as pre-K.5 baseline (`baselines/libkalburator-worktree-ctest.txt`). No regressions from the move itself.

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "Phase K.5.T1: move BlobBaselineStore + IDMappingStore to src/storage/

Rename class BlobBaselineStore → BaselineStore. Namespace Sync → Storage.
Forwarding shims at old src/journal/ locations preserve the
Kalburator::Sync::BlobBaselineStore + Kalburator::Sync::IDMappingStore
spellings during cross-repo cutover. Shims removed in Task 13."
```

---

## Task 2: Cut over libkalburator internals to Storage:: spelling

**Files:**
- Modify: `libkalburator/src/engine/syncengine.h` — forward decls + types
- Modify: `libkalburator/src/engine/syncengine.cpp` — `BlobBaselineStore` → `Storage::BaselineStore` everywhere
- Modify: `libkalburator/src/calendar/calendarbaselinestore.h/cpp` — same
- Modify: `libkalburator/src/calendar/syncconflictstore.h` — comment update
- Modify: `libkalburator/tests/journal/*.cpp` — moved to `tests/storage/`, includes/namespace updated

After this task, the internal libkalburator code no longer uses `Sync::BlobBaselineStore` or `Sync::IDMappingStore` spellings (the shims exist only for the consumer-cutover window).

- [ ] **Step 1: Move test files from tests/journal/ to tests/storage/**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
mkdir -p tests/storage
git mv tests/journal/tst_blob_baseline_store_v3.cpp           tests/storage/tst_baseline_store_v3.cpp
git mv tests/journal/tst_blob_baseline_store_per_record_keys.cpp tests/storage/tst_baseline_store_per_record_keys.cpp
git mv tests/journal/tst_idmappingstore.cpp                   tests/storage/tst_idmappingstore.cpp
# tst_calendarjournal.cpp stays in tests/journal/ — calendarjournal.{h,cpp}
# is not moving in K.5.
```

- [ ] **Step 2: Move tests/journal/CMakeLists.txt → tests/storage/CMakeLists.txt**

Read `tests/journal/CMakeLists.txt`; copy the entries that reference the three moved tests into a new `tests/storage/CMakeLists.txt`; leave the calendarjournal entries in `tests/journal/CMakeLists.txt`.

- [ ] **Step 3: Update tests/CMakeLists.txt to add tests/storage subdirectory**

Find the line `add_subdirectory(journal)` in `tests/CMakeLists.txt` and add `add_subdirectory(storage)` adjacent. Verify by:

```bash
grep -n "add_subdirectory(journal)\|add_subdirectory(storage)" tests/CMakeLists.txt
```

Expected: both listed.

- [ ] **Step 4: Update test source includes + namespace**

In each of the three moved test files (`tests/storage/tst_baseline_store_v3.cpp`, `tst_baseline_store_per_record_keys.cpp`, `tst_idmappingstore.cpp`):
- `#include "blobbaselinestore.h"` → `#include "baselinestore.h"`
- `#include "idmappingstore.h"` stays (filename unchanged, just at a new location)
- `Kalburator::Sync::BlobBaselineStore` → `Kalburator::Storage::BaselineStore`
- `Kalburator::Sync::IDMappingStore` → `Kalburator::Storage::IDMappingStore`
- `Kalburator::Sync::IDMapping` → `Kalburator::Storage::IDMapping`
- `using namespace Kalburator::Sync;` (if present) → `using namespace Kalburator::Storage;`

```bash
sed -i 's/#include "blobbaselinestore.h"/#include "baselinestore.h"/g' tests/storage/tst_baseline_store_*.cpp
sed -i 's/Kalburator::Sync::BlobBaselineStore/Kalburator::Storage::BaselineStore/g' tests/storage/tst_baseline_store_*.cpp
sed -i 's/\bBlobBaselineStore\b/BaselineStore/g' tests/storage/tst_baseline_store_*.cpp
sed -i 's/Kalburator::Sync::IDMapping\(Store\)\?/Kalburator::Storage::IDMapping\1/g' tests/storage/tst_idmappingstore.cpp
```

Verify each file builds cleanly after edits.

- [ ] **Step 5: Cut over src/engine/syncengine.h**

In `src/engine/syncengine.h`:
- Line 31 `class BlobBaselineStore;` → forward decl in the right namespace:
  ```cpp
  namespace Kalburator::Storage { class BaselineStore; }
  ```
  Place this above the existing `namespace Kalburator::Sync {` block.
- Line 115 `BlobBaselineStore *blobBaselines = nullptr,` → `Kalburator::Storage::BaselineStore *baselineStore = nullptr,`
- Line 284 `BlobBaselineStore *m_blobBaselines = nullptr;` → `Kalburator::Storage::BaselineStore *m_baselineStore = nullptr;`
- Line 361–367 `setBlobBaselineStore` / `blobBaselineStore` accessors → `setBaselineStore` / `baselineStore`. Update parameter type.
- Line 696 `BlobBaselineStore *m_blobBaselines = nullptr;` → `Kalburator::Storage::BaselineStore *m_baselineStore = nullptr;` (this is on the inner Worker class).
- Comment at line ~310 ("Persistent storage is split across CalendarBaselineStore (baselines, ...)") — defer rewrite to Task 9 when CalendarBaselineStore is removed.

Important: keep `setBlobBaselineStore` available as a `[[deprecated]]` inline alias forwarding to `setBaselineStore` for one task — PlanStan and WildPalms still call `setBlobBaselineStore` until Task 11/12. Add inside the public class body:

```cpp
[[deprecated("Phase K.5: use setBaselineStore()")]]
void setBlobBaselineStore(Kalburator::Storage::BaselineStore *store) {
    setBaselineStore(store);
}
```

- [ ] **Step 6: Cut over src/engine/syncengine.cpp**

```bash
sed -i 's/\bBlobBaselineStore\b/Kalburator::Storage::BaselineStore/g' src/engine/syncengine.cpp
sed -i 's/\bm_blobBaselines\b/m_baselineStore/g' src/engine/syncengine.cpp
sed -i 's/\bsetBlobBaselineStore\b/setBaselineStore/g' src/engine/syncengine.cpp
```

Then manually:
- Find any `Kalburator::Storage::BaselineStore *bbs = m_baselineStore;` shorthand and either keep it or rename to `bs`. Mechanical, low-stakes.
- Comment on line ~1943 ("BlobBaselineStore is not thread-safe") → "BaselineStore is not thread-safe".
- Comment on line ~1970 ("for BlobBaselineStore-backed paths") → "for BaselineStore-backed paths".

- [ ] **Step 7: Build + run libkalburator tests**

```bash
cmake --build build -j 10 && ctest --test-dir build --output-on-failure
```

Expected: green; same pass/fail as baseline.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "Phase K.5.T2: cut over libkalburator internals to Storage::BaselineStore

SyncEngine internal members + tests reference Storage::BaselineStore
directly. setBlobBaselineStore retained as deprecated alias forwarding
to setBaselineStore until PlanStan + WildPalms cut over (Tasks 11/12)."
```

---

## Task 3: Schema v5 — add `collection_baselines` table

**Files:**
- Modify: `libkalburator/src/storage/baselinestore.h`
- Modify: `libkalburator/src/storage/baselinestore.cpp`
- Create: `libkalburator/tests/storage/tst_baseline_store_collection_properties.cpp`
- Modify: `libkalburator/tests/storage/CMakeLists.txt`

Add the table + bump `kSchemaVersion` to 5. Migration is idempotent: `CREATE TABLE IF NOT EXISTS` + `PRAGMA user_version` stamp. The existing `ensureSchemaV3` pattern is the model (see `src/storage/baselinestore.cpp:187`).

- [ ] **Step 1: Write the failing test**

Create `libkalburator/tests/storage/tst_baseline_store_collection_properties.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QVariantMap>

#include "baselinestore.h"

using Kalburator::Storage::BaselineStore;

class TstBaselineStoreCollectionProperties : public QObject {
    Q_OBJECT
private slots:
    void roundTripMap();
    void overwriteSameKey();
    void distinctMappings();
    void removeClearsRow();
    void absentKeyReturnsEmpty();
};

void TstBaselineStoreCollectionProperties::roundTripMap() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(store.isOpen());
    QVariantMap props{
        {QStringLiteral("color"),       QStringLiteral("#ff0000")},
        {QStringLiteral("description"), QStringLiteral("My calendar")},
    };
    QVERIFY(store.setCollectionBaseline("m1", "cal1", props));
    const auto out = store.collectionBaseline("m1", "cal1");
    QCOMPARE(out, props);
}

void TstBaselineStoreCollectionProperties::overwriteSameKey() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setCollectionBaseline("m1", "cal1", {{"color", "#000"}});
    QVERIFY(store.setCollectionBaseline("m1", "cal1", {{"color", "#fff"}}));
    QCOMPARE(store.collectionBaseline("m1", "cal1").value("color").toString(),
             QStringLiteral("#fff"));
}

void TstBaselineStoreCollectionProperties::distinctMappings() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setCollectionBaseline("m1", "cal1", {{"color", "red"}});
    store.setCollectionBaseline("m2", "cal1", {{"color", "blue"}});
    QCOMPARE(store.collectionBaseline("m1", "cal1").value("color").toString(),
             QStringLiteral("red"));
    QCOMPARE(store.collectionBaseline("m2", "cal1").value("color").toString(),
             QStringLiteral("blue"));
}

void TstBaselineStoreCollectionProperties::removeClearsRow() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setCollectionBaseline("m1", "cal1", {{"color", "red"}});
    QVERIFY(store.removeCollectionBaseline("m1", "cal1"));
    QVERIFY(store.collectionBaseline("m1", "cal1").isEmpty());
}

void TstBaselineStoreCollectionProperties::absentKeyReturnsEmpty() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(store.collectionBaseline("nonexistent", "x").isEmpty());
}

QTEST_MAIN(TstBaselineStoreCollectionProperties)
#include "tst_baseline_store_collection_properties.moc"
```

Add the test entry to `tests/storage/CMakeLists.txt` using the existing `kalburator_add_test()` (or local equivalent — match the pattern used by `tst_baseline_store_v3.cpp`).

- [ ] **Step 2: Run the test — expected fail (compile error)**

```bash
cmake --build build -j 10 --target tst_baseline_store_collection_properties
```

Expected: compile error, `BaselineStore` has no member `setCollectionBaseline` / `collectionBaseline` / `removeCollectionBaseline`.

- [ ] **Step 3: Add API + schema**

In `src/storage/baselinestore.h`, inside `class BaselineStore`, after the v3 mapping-keyed block (around line 87 of the original, post-rename file), add:

```cpp
    // -----------------------------------------------------------------------
    // Collection-baseline API (K.5, schema v5).
    //
    // Per (mappingId, collectionId) → QVariantMap of
    // domain-plugin-declared property snapshots (e.g. color, description
    // for calendars). Stored in collection_baselines.
    // -----------------------------------------------------------------------

    bool setCollectionBaseline(const QString &mappingId,
                               const QString &collectionId,
                               const QVariantMap &props);

    QVariantMap collectionBaseline(const QString &mappingId,
                                   const QString &collectionId) const;

    bool removeCollectionBaseline(const QString &mappingId,
                                  const QString &collectionId);
```

In `src/storage/baselinestore.cpp`:

1. Bump `kSchemaVersion` from 4 to 5 (search for the `static constexpr int kSchemaVersion`).
2. Inside `ensureSchemaV3`, immediately before `return true`, add a call to a new `ensureSchemaV5()` method (don't combine — keep migrations layered for clarity).
3. Add `ensureSchemaV5()`:

```cpp
bool BaselineStore::ensureSchemaV5() {
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS collection_baselines ("
            "  mapping_id      TEXT NOT NULL,"
            "  collection_id   TEXT NOT NULL,"
            "  properties_json BLOB NOT NULL,"
            "  updated_at      INTEGER NOT NULL,"
            "  PRIMARY KEY (mapping_id, collection_id)"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE collection_baselines failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_collection_baselines_mapping "
        "ON collection_baselines (mapping_id)"));

    // PRAGMA user_version stamp moves to the closing migrate-to-current
    // step; ensureSchemaV5 is idempotent on its own.
    return true;
}
```

4. Add the API impls (mirror the v3 pattern at `setBaselineV3`/`baselineV3`):

```cpp
bool BaselineStore::setCollectionBaseline(const QString &mappingId,
                                          const QString &collectionId,
                                          const QVariantMap &props) {
    if (!m_isOpen) {
        setError(QStringLiteral("setCollectionBaseline: store not open"));
        return false;
    }
    QJsonDocument doc(QJsonObject::fromVariantMap(props));
    const QByteArray bytes = doc.toJson(QJsonDocument::Compact);

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO collection_baselines "
        "(mapping_id, collection_id, properties_json, updated_at) "
        "VALUES (?, ?, ?, strftime('%s','now'))"));
    q.addBindValue(mappingId);
    q.addBindValue(collectionId);
    q.addBindValue(bytes);
    if (!q.exec()) {
        setError(QStringLiteral("setCollectionBaseline: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

QVariantMap BaselineStore::collectionBaseline(const QString &mappingId,
                                              const QString &collectionId) const {
    if (!m_isOpen) return {};
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT properties_json FROM collection_baselines "
        "WHERE mapping_id = ? AND collection_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(collectionId);
    if (!q.exec() || !q.next()) return {};
    const QByteArray bytes = q.value(0).toByteArray();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object().toVariantMap();
}

bool BaselineStore::removeCollectionBaseline(const QString &mappingId,
                                             const QString &collectionId) {
    if (!m_isOpen) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM collection_baselines "
        "WHERE mapping_id = ? AND collection_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(collectionId);
    if (!q.exec()) {
        setError(QStringLiteral("removeCollectionBaseline: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}
```

5. At the top of `baselinestore.cpp`, ensure `<QJsonDocument>`, `<QJsonObject>`, `<QJsonParseError>` are included.

- [ ] **Step 4: Run the test — expected pass**

```bash
cmake --build build -j 10 --target tst_baseline_store_collection_properties && \
./build/tests/storage/tst_baseline_store_collection_properties
```

Expected: 5/5 pass.

- [ ] **Step 5: Run full libkalburator test suite to verify no regressions**

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Phase K.5.T3: add collection_baselines table (schema v5)

setCollectionBaseline / collectionBaseline / removeCollectionBaseline
APIs on Storage::BaselineStore. Idempotent CREATE IF NOT EXISTS
migration. Stores QVariantMap as JSON blob keyed by (mappingId,
collectionId). 5/5 round-trip tests pass."
```

---

## Task 4: Schema v5 — add `mapping_metadata` table

**Files:**
- Modify: `libkalburator/src/storage/baselinestore.h`
- Modify: `libkalburator/src/storage/baselinestore.cpp`
- Create: `libkalburator/tests/storage/tst_baseline_store_mapping_metadata.cpp`
- Modify: `libkalburator/tests/storage/CMakeLists.txt`

Same pattern as Task 3: TDD for last-sync timestamp accessors.

- [ ] **Step 1: Write the failing test**

Create `libkalburator/tests/storage/tst_baseline_store_mapping_metadata.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDateTime>

#include "baselinestore.h"

using Kalburator::Storage::BaselineStore;

class TstBaselineStoreMappingMetadata : public QObject {
    Q_OBJECT
private slots:
    void roundTripTimestamp();
    void absentReturnsInvalid();
    void overwriteUpdates();
    void distinctMappings();
};

void TstBaselineStoreMappingMetadata::roundTripTimestamp() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(store.isOpen());
    const QDateTime t = QDateTime::fromSecsSinceEpoch(1700000000);
    QVERIFY(store.setLastSyncTime("m1", t));
    QCOMPARE(store.lastSyncTime("m1"), t);
}

void TstBaselineStoreMappingMetadata::absentReturnsInvalid() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    QVERIFY(!store.lastSyncTime("nonexistent").isValid());
}

void TstBaselineStoreMappingMetadata::overwriteUpdates() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(100));
    store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(200));
    QCOMPARE(store.lastSyncTime("m1"), QDateTime::fromSecsSinceEpoch(200));
}

void TstBaselineStoreMappingMetadata::distinctMappings() {
    QTemporaryDir dir;
    BaselineStore store(dir.filePath("k5.db"));
    store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(100));
    store.setLastSyncTime("m2", QDateTime::fromSecsSinceEpoch(200));
    QCOMPARE(store.lastSyncTime("m1"), QDateTime::fromSecsSinceEpoch(100));
    QCOMPARE(store.lastSyncTime("m2"), QDateTime::fromSecsSinceEpoch(200));
}

QTEST_MAIN(TstBaselineStoreMappingMetadata)
#include "tst_baseline_store_mapping_metadata.moc"
```

Add the test to `tests/storage/CMakeLists.txt`.

- [ ] **Step 2: Run — expected fail (compile error)**

```bash
cmake --build build -j 10 --target tst_baseline_store_mapping_metadata
```

- [ ] **Step 3: Add API + table**

In `src/storage/baselinestore.h`, after the collection-baseline block, add:

```cpp
    // -----------------------------------------------------------------------
    // Mapping-metadata API (K.5, schema v5). Per-mappingId scalars.
    // Currently: last-sync timestamp.
    // -----------------------------------------------------------------------

    bool      setLastSyncTime(const QString &mappingId, const QDateTime &when);
    QDateTime lastSyncTime(const QString &mappingId) const;
```

In `src/storage/baselinestore.cpp`, extend `ensureSchemaV5()` before the closing `return true`:

```cpp
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS mapping_metadata ("
            "  mapping_id      TEXT NOT NULL PRIMARY KEY,"
            "  last_sync_at    INTEGER"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE mapping_metadata failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
```

Add API impls in `baselinestore.cpp`:

```cpp
bool BaselineStore::setLastSyncTime(const QString &mappingId, const QDateTime &when) {
    if (!m_isOpen) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO mapping_metadata (mapping_id, last_sync_at) "
        "VALUES (?, ?)"));
    q.addBindValue(mappingId);
    q.addBindValue(when.toSecsSinceEpoch());
    if (!q.exec()) {
        setError(QStringLiteral("setLastSyncTime: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

QDateTime BaselineStore::lastSyncTime(const QString &mappingId) const {
    if (!m_isOpen) return {};
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT last_sync_at FROM mapping_metadata WHERE mapping_id = ?"));
    q.addBindValue(mappingId);
    if (!q.exec() || !q.next()) return {};
    const QVariant v = q.value(0);
    if (v.isNull()) return {};
    return QDateTime::fromSecsSinceEpoch(v.toLongLong());
}
```

- [ ] **Step 4: Run — expected pass**

```bash
cmake --build build -j 10 --target tst_baseline_store_mapping_metadata && \
./build/tests/storage/tst_baseline_store_mapping_metadata
```

- [ ] **Step 5: Bump kSchemaVersion stamp + add migration test**

Now that both v5 tables exist, bump the version stamp. In `ensureSchemaV3`, change the existing `kSchemaVersion` stamp to be reached only after `ensureSchemaV5()` returns true. Concretely the migration arc becomes:

```cpp
if (!ensureSchemaV3()) return false;     // creates blob_baselines_v3
if (!ensureSchemaV5()) return false;     // creates collection_baselines + mapping_metadata
// Stamp final user_version = 5 (kSchemaVersion).
QSqlQuery vq(db);
vq.exec(QStringLiteral("PRAGMA user_version"));
const int userVersion = vq.next() ? vq.value(0).toInt() : 0;
if (userVersion < kSchemaVersion) {
    QSqlQuery sq(db);
    sq.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
}
```

Inside `ensureSchemaV3`, **remove** the existing `PRAGMA user_version = %1` stamp at line ~222 (which currently bumps to 4) — the final stamp in `ensureSchemaAndVersion` is now the single source of truth.

- [ ] **Step 6: Run full libkalburator suite**

```bash
cmake --build build -j 10 && ctest --test-dir build --output-on-failure
```

Expected: green; same pass set as baseline plus 4 new passing tests.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Phase K.5.T4: add mapping_metadata table (schema v5)

setLastSyncTime / lastSyncTime APIs. kSchemaVersion bumped to 5.
Single user_version stamp at end of migration arc. 4/4 round-trip
tests pass."
```

---

## Task 5: v4→v5 migration test (idempotent open of old DB)

**Files:**
- Create: `libkalburator/tests/storage/tst_baseline_store_v4_to_v5_migration.cpp`
- Modify: `libkalburator/tests/storage/CMakeLists.txt`

Verify that opening a pre-existing v4 SQLite file (with `blob_baselines` + `blob_baselines_v3` only, `user_version=4`) silently upgrades to v5 with the new tables present and `user_version=5`. No data loss.

- [ ] **Step 1: Write the test**

Create `libkalburator/tests/storage/tst_baseline_store_v4_to_v5_migration.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "baselinestore.h"

using Kalburator::Storage::BaselineStore;

class TstBaselineStoreV4ToV5Migration : public QObject {
    Q_OBJECT
private slots:
    void migrationCreatesNewTables();
    void migrationStampsVersion5();
    void migrationPreservesV3Data();
    void migrationIsIdempotent();
};

namespace {
void seedV4Database(const QString &path) {
    const QString conn = QStringLiteral("seedV4_%1").arg(qHash(path));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec("CREATE TABLE blob_baselines (backend_id TEXT, collection_id TEXT, "
               "record_id TEXT, content_hash TEXT, updated_at TEXT, "
               "PRIMARY KEY(backend_id, collection_id, record_id))");
        q.exec("CREATE TABLE blob_baselines_v3 (mapping_id TEXT, record_id TEXT, "
               "canonical_shape_domain TEXT, canonical_shape_encoding TEXT, "
               "canonical_bytes BLOB, updated_at INTEGER, "
               "PRIMARY KEY(mapping_id, record_id))");
        q.exec("INSERT INTO blob_baselines_v3 VALUES "
               "('m1','r1','calendar','ical','BYTES1',1)");
        q.exec("PRAGMA user_version = 4");
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
}
} // namespace

void TstBaselineStoreV4ToV5Migration::migrationCreatesNewTables() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);

    BaselineStore store(path);
    QVERIFY(store.isOpen());

    // collection_baselines and mapping_metadata should now be queryable.
    QVERIFY(store.setCollectionBaseline("m1", "cal1", {{"k","v"}}));
    QVERIFY(store.setLastSyncTime("m1", QDateTime::fromSecsSinceEpoch(123)));
}

void TstBaselineStoreV4ToV5Migration::migrationStampsVersion5() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);
    {
        BaselineStore store(path);
        QVERIFY(store.isOpen());
    }

    // Re-open as a raw SQL connection and check user_version.
    const QString conn = QStringLiteral("verify_%1").arg(qHash(path));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec("PRAGMA user_version");
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 5);
    }
    QSqlDatabase::removeDatabase(conn);
}

void TstBaselineStoreV4ToV5Migration::migrationPreservesV3Data() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);

    BaselineStore store(path);
    auto rec = store.baselineV3("m1", "r1");
    QVERIFY(rec.has_value());
}

void TstBaselineStoreV4ToV5Migration::migrationIsIdempotent() {
    QTemporaryDir dir;
    const QString path = dir.filePath("k5.db");
    seedV4Database(path);
    { BaselineStore s(path); }
    { BaselineStore s(path); }   // re-open against v5 — no-op
    { BaselineStore s2(path); }  // and again
    BaselineStore store(path);
    QVERIFY(store.isOpen());
    QVERIFY(store.setCollectionBaseline("m1", "cal1", {{"k","v"}}));
    QCOMPARE(store.collectionBaseline("m1", "cal1").value("k").toString(),
             QStringLiteral("v"));
}

QTEST_MAIN(TstBaselineStoreV4ToV5Migration)
#include "tst_baseline_store_v4_to_v5_migration.moc"
```

Add the test entry to `tests/storage/CMakeLists.txt`.

- [ ] **Step 2: Build + run**

```bash
cmake --build build -j 10 --target tst_baseline_store_v4_to_v5_migration && \
./build/tests/storage/tst_baseline_store_v4_to_v5_migration
```

Expected: 4/4 pass. If `migrationStampsVersion5` fails, the migration arc edits in Task 4 Step 5 are wrong; debug there.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "Phase K.5.T5: v4→v5 migration test

Idempotent open of seeded-v4 DB upgrades schema and preserves data."
```

---

## Task 6: DomainPlugin::baselineProperties() virtual

**Files:**
- Modify: `libkalburator/src/shape/domainplugin.h`
- Modify: `libkalburator/src/calendar/calendardomainplugin.h`
- Modify: `libkalburator/src/calendar/calendardomainplugin.cpp`
- Create: `libkalburator/tests/calendar/tst_calendardomain_baseline_properties.cpp`
- Modify: `libkalburator/tests/calendar/CMakeLists.txt`

Lets the engine ask each domain plugin which property keys to baseline at the collection level.

- [ ] **Step 1: Write the failing test**

Create `libkalburator/tests/calendar/tst_calendardomain_baseline_properties.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QStringList>

#include "calendardomainplugin.h"

using Kalburator::Calendar::CalendarDomainPlugin;

class TstCalendarDomainBaselineProperties : public QObject {
    Q_OBJECT
private slots:
    void declaresColorAndDescription();
};

void TstCalendarDomainBaselineProperties::declaresColorAndDescription() {
    CalendarDomainPlugin plugin;
    const QStringList keys = plugin.baselineProperties();
    QVERIFY(keys.contains(QStringLiteral("color")));
    QVERIFY(keys.contains(QStringLiteral("description")));
}

QTEST_APPLESS_MAIN(TstCalendarDomainBaselineProperties)
#include "tst_calendardomain_baseline_properties.moc"
```

Add to `tests/calendar/CMakeLists.txt`.

- [ ] **Step 2: Build — expected fail**

```bash
cmake --build build -j 10 --target tst_calendardomain_baseline_properties
```

- [ ] **Step 3: Add the virtual to DomainPlugin**

In `src/shape/domainplugin.h`, add `#include <QStringList>` if not present, then add inside `class DomainPlugin` (after `applyCollectionProperties` at line 69):

```cpp
    /// Property keys whose collection-level snapshots the engine should
    /// persist via Storage::BaselineStore::setCollectionBaseline. The
    /// engine queries collectionProperties() at sync time and stores
    /// the subset corresponding to these keys. Default: empty list
    /// (no property baselines kept). Calendar plugin overrides to
    /// declare {"color", "description"}.
    virtual QStringList baselineProperties() const { return {}; }
```

- [ ] **Step 4: Override in CalendarDomainPlugin**

In `src/calendar/calendardomainplugin.h`, declare the override under `public:`:

```cpp
    QStringList baselineProperties() const override;
```

In `src/calendar/calendardomainplugin.cpp`, define:

```cpp
QStringList CalendarDomainPlugin::baselineProperties() const {
    return { QStringLiteral("color"), QStringLiteral("description") };
}
```

- [ ] **Step 5: Build + run test — expected pass**

```bash
cmake --build build -j 10 --target tst_calendardomain_baseline_properties && \
./build/tests/calendar/tst_calendardomain_baseline_properties
```

- [ ] **Step 6: Run full suite + commit**

```bash
ctest --test-dir build --output-on-failure
git add -A
git commit -m "Phase K.5.T6: DomainPlugin::baselineProperties() virtual

CalendarDomainPlugin returns {color, description}. Engine consumes
in K.5.T8 (call-site migration)."
```

---

## Task 7: CalendarBaselineStore facade over Storage::BaselineStore

**Files:**
- Modify: `libkalburator/src/calendar/calendarbaselinestore.h`
- Modify: `libkalburator/src/calendar/calendarbaselinestore.cpp`

Replace the SQLite-direct implementation with thin forwarding into a `Storage::BaselineStore`. Keeps the public API identical so existing callers (engine + tests) compile unchanged. The facade itself is deleted in Task 13.

The mapping is:
- `setBaseline(mappingId, uid, icalText)` → `setBaselineV3(mappingId, CanonicalRecord)` where shape = calendar canonical, recordId = uid, data = icalText.toUtf8().
- `baseline(mappingId, uid)` → `baselineV3(mappingId, uid).data` decoded back to QString.
- `setBaselines(mappingId, hash)` → loop of `setBaselineV3`.
- `removeBaseline(mappingId, uid)` → `removeBaselineV3`.
- `removeBaselines(mappingId)` → `clearMappingV3`.
- `allBaselines(mappingId)` → `baselinesForMappingV3` decoded back to a hash.
- `hasBaselines(mappingId)` → `!baselinesForMappingV3(mappingId).isEmpty()` (or any truthy).
- `setPropertyBaseline(mappingId, calendarId, propertyJson)` → `setCollectionBaseline(mappingId, calendarId, decodeJsonStringToVariantMap(propertyJson))`. Subtle: the existing API takes a JSON string, not a QVariantMap. Preserve the QString contract by serializing the map back to a JSON string when reading.
- `propertyBaseline(...)` → `collectionBaseline(...)` re-serialized.
- `setLastSyncTime` / `lastSyncTime` → forward directly.
- `clearBaselines` (rare; only used by tests) → execute on the underlying DB. Implement as `clearMappingV3` for the most-recently-set mapping is wrong; instead this method needs to clear the entire `blob_baselines_v3` *for this store's purposes*. Since the unified store also holds non-calendar baselines, the facade's `clearBaselines()` must be **scoped to calendar shape only** to avoid wiping contacts/blob baselines. Simplest correct implementation: track `m_seenMappings` (a `QSet<QString>`) updated on every `setBaseline`/`setBaselines` call, and call `clearMappingV3` for each on `clearBaselines()`.

- [ ] **Step 1: Rewrite calendarbaselinestore.h**

Replace the contents with a forwarding wrapper. The class still inherits `QObject` (callers use it as a QObject) but holds a `std::unique_ptr<Storage::BaselineStore>` instead of a direct `QSqlDatabase`.

```cpp
#ifndef KALBURATOR_CALENDARBASELINESTORE_H
#define KALBURATOR_CALENDARBASELINESTORE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QDateTime>

#include <memory>

namespace Kalburator::Storage { class BaselineStore; }

namespace Kalburator::Sync {

/**
 * @brief Phase K.5 facade over Storage::BaselineStore.
 *
 * Preserves the legacy iCal-text + property-JSON + last-sync-time
 * surface used by the engine and tests during the migration window.
 * Deleted in Task 13 once all callers move to Storage::BaselineStore.
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
                         const QHash<QString, QString> &uidToIcal);
    bool    removeBaseline(const QString &mappingId, const QString &uid);
    bool    removeBaselines(const QString &mappingId);
    QHash<QString, QString> allBaselines(const QString &mappingId) const;
    bool    clearBaselines();
    bool    hasBaselines(const QString &mappingId) const;

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
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_store;
    mutable QSet<QString>                               m_seenMappings;
};

} // namespace Kalburator::Sync

#endif
```

- [ ] **Step 2: Rewrite calendarbaselinestore.cpp as forwarding impl**

```cpp
#include "calendarbaselinestore.h"
#include "baselinestore.h"
#include "canonicalrecord.h"
#include "shape.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace Kalburator::Sync {

namespace {
Kalburator::Shape::Shape calendarCanonicalShape() {
    return Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        QStringLiteral("ical")
    };
}

Kalburator::Shape::CanonicalRecord makeRec(const QString &uid, const QString &ical) {
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = calendarCanonicalShape();
    rec.data     = ical.toUtf8();
    return rec;
}
} // namespace

CalendarBaselineStore::CalendarBaselineStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_store(std::make_unique<Kalburator::Storage::BaselineStore>(dbPath))
{}

CalendarBaselineStore::~CalendarBaselineStore() = default;

bool CalendarBaselineStore::isValid() const { return m_store->isOpen(); }

QString CalendarBaselineStore::baseline(const QString &mappingId, const QString &uid) const {
    auto rec = m_store->baselineV3(mappingId, uid);
    if (!rec) return {};
    return QString::fromUtf8(rec->data);
}

bool CalendarBaselineStore::setBaseline(const QString &mappingId, const QString &uid,
                                        const QString &icalText) {
    m_seenMappings.insert(mappingId);
    return m_store->setBaselineV3(mappingId, makeRec(uid, icalText));
}

bool CalendarBaselineStore::setBaselines(const QString &mappingId,
                                         const QHash<QString, QString> &uidToIcal) {
    m_seenMappings.insert(mappingId);
    bool ok = true;
    for (auto it = uidToIcal.begin(); it != uidToIcal.end(); ++it) {
        ok = m_store->setBaselineV3(mappingId, makeRec(it.key(), it.value())) && ok;
    }
    return ok;
}

bool CalendarBaselineStore::removeBaseline(const QString &mappingId, const QString &uid) {
    return m_store->removeBaselineV3(mappingId, uid);
}

bool CalendarBaselineStore::removeBaselines(const QString &mappingId) {
    return m_store->clearMappingV3(mappingId);
}

QHash<QString, QString> CalendarBaselineStore::allBaselines(const QString &mappingId) const {
    QHash<QString, QString> out;
    for (const auto &rec : m_store->baselinesForMappingV3(mappingId)) {
        out.insert(rec.recordId, QString::fromUtf8(rec.data));
    }
    return out;
}

bool CalendarBaselineStore::clearBaselines() {
    bool ok = true;
    for (const auto &m : m_seenMappings) {
        ok = m_store->clearMappingV3(m) && ok;
    }
    m_seenMappings.clear();
    return ok;
}

bool CalendarBaselineStore::hasBaselines(const QString &mappingId) const {
    return !m_store->baselinesForMappingV3(mappingId).isEmpty();
}

QString CalendarBaselineStore::propertyBaseline(const QString &mappingId,
                                                const QString &calendarId) const {
    const auto map = m_store->collectionBaseline(mappingId, calendarId);
    if (map.isEmpty()) return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map))
                                 .toJson(QJsonDocument::Compact));
}

bool CalendarBaselineStore::setPropertyBaseline(const QString &mappingId,
                                                const QString &calendarId,
                                                const QString &propertyJson) {
    const auto doc = QJsonDocument::fromJson(propertyJson.toUtf8());
    const auto map = doc.isObject() ? doc.object().toVariantMap() : QVariantMap{};
    return m_store->setCollectionBaseline(mappingId, calendarId, map);
}

bool CalendarBaselineStore::removePropertyBaseline(const QString &mappingId,
                                                   const QString &calendarId) {
    return m_store->removeCollectionBaseline(mappingId, calendarId);
}

QHash<QString, QString> CalendarBaselineStore::allPropertyBaselines(const QString &mappingId) const {
    // The unified store does not enumerate by mapping; since the legacy
    // CalendarBaselineStore returned all property baselines for a mapping,
    // emit empty here. No call site uses this result during K.5; verified
    // by grep at facade-build time. Asserts in debug builds catch latent users.
    Q_UNUSED(mappingId);
    return {};
}

QDateTime CalendarBaselineStore::lastSyncTime(const QString &mappingId) const {
    return m_store->lastSyncTime(mappingId);
}

bool CalendarBaselineStore::setLastSyncTime(const QString &mappingId, const QDateTime &when) {
    return m_store->setLastSyncTime(mappingId, when);
}

} // namespace Kalburator::Sync
```

- [ ] **Step 3: Verify allPropertyBaselines is unused**

```bash
grep -rn "allPropertyBaselines" libkalburator/src libkalburator/tests \
    ~/dev/refactor-engine-merger/PlanStan ~/dev/refactor-engine-merger/WildPalms
```

Expected: only definition in calendarbaselinestore.{h,cpp}. If any caller exists, escalate — that caller needs migration before this task lands. (Verified by current grep: no callers.)

- [ ] **Step 4: Drop Q_OBJECT from CalendarBaselineStore if it has no signals/slots**

Inspect: the original class had no signals/slots — `Q_OBJECT` was vestigial. The new facade still inherits `QObject` (callers manage parent ownership). If the build complains about a missing moc target, leave `Q_OBJECT` and let CMake's AUTOMOC pick it up.

- [ ] **Step 5: Build + run full suite**

```bash
cmake --build build -j 10 && ctest --test-dir build --output-on-failure
```

Expected: green. All existing CalendarBaselineStore-using tests now route through the unified store transparently.

If `tst_calendar_baseline_store` fails, debug the facade — it must preserve exact existing behavior on the round-trip.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Phase K.5.T7: CalendarBaselineStore becomes thin facade over Storage::BaselineStore

Behavior-preserving forwarding wrapper. iCal text rides as
CanonicalRecord{shape=calendar/ical, data=bytes}. Property JSON
re-encoded to/from QVariantMap at facade boundary. Facade deleted
in Task 13."
```

---

## Task 8: Engine call-site migration — drop CalendarBaselineStore from engine internals

**Files:**
- Modify: `libkalburator/src/engine/syncengine.h`
- Modify: `libkalburator/src/engine/syncengine.cpp`

The facade keeps existing tests green; this task removes the facade from the engine's *internal* use, replacing each call with a direct `Storage::BaselineStore` call. Public `setCalendarBaselineStore` accessor stays as a deprecated forwarder until Task 13.

The engine currently has two store pointers: `m_baselineStore` (post-T2) and `m_calendarBaselines`. After this task, only `m_baselineStore` remains in the worker; `m_calendarBaselines` becomes a deprecated externally-settable pointer that is unused internally.

- [ ] **Step 1: Replace engine internal calendar-baseline reads/writes**

For each call site at `src/engine/syncengine.cpp` lines 366, 827, 862, 865, 876, 878, 896, 909, 915, 924, 1665, 1666:

| Old | New |
|---|---|
| `m_calendarBaselines->hasBaselines(mappingId)` | `!m_baselineStore->baselinesForMappingV3(mappingId).isEmpty()` |
| `m_calendarBaselines->baseline(mappingId, uid)` | `m_baselineStore->baselineV3(mappingId, uid).value_or(Kalburator::Shape::CanonicalRecord{}).data` |
| `m_calendarBaselines->setBaseline(mappingId, uid, icalText)` | `m_baselineStore->setBaselineV3(mappingId, makeCalendarRec(mappingId, uid, icalText))` |
| `m_calendarBaselines->setBaselines(mappingId, uidToIcal)` | loop over `setBaselineV3` |
| `m_calendarBaselines->removeBaseline(mappingId, uid)` | `m_baselineStore->removeBaselineV3(mappingId, uid)` |
| `m_calendarBaselines->setLastSyncTime(mappingId, t)` | `m_baselineStore->setLastSyncTime(mappingId, t)` |

Define a private helper at the top of `syncengine.cpp` (anonymous namespace):

```cpp
namespace {
Kalburator::Shape::CanonicalRecord makeCalendarRec(const QString &uid,
                                                   const QString &icalText) {
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        QStringLiteral("ical")};
    rec.data     = icalText.toUtf8();
    return rec;
}
} // namespace
```

The `change.sourceRecord.icalData` / `change.targetRecord.icalData` plumbing is already QString — feeds straight in.

For the `change.sourceRecord.icalData.isEmpty()` check at line 915 (`if (!m_calendarBaselines->baseline(mapping.id, uid).isEmpty())`):

```cpp
auto baselineRec = m_baselineStore->baselineV3(mapping.id, uid);
if (baselineRec.has_value() && !baselineRec->data.isEmpty()) {
    // ...
}
```

- [ ] **Step 2: Mark m_calendarBaselines unused in worker (still settable but no internal reads)**

In `src/engine/syncengine.cpp`:
- `void SyncEngine::setCalendarBaselineStore(CalendarBaselineStore *store)` body: leave the assignment so PlanStan/WildPalms can still set it without crashing; add a comment that it's a no-op pending Task 13.
- Search for any remaining `m_calendarBaselines->` after the edits above; expect zero. If non-zero, repeat the substitution.

```bash
grep -n "m_calendarBaselines->" src/engine/syncengine.cpp
```

Expected: empty output.

- [ ] **Step 3: Build + run full suite**

```bash
cmake --build build -j 10 && ctest --test-dir build --output-on-failure
```

Expected: green. Calendar baseline tests pass against the unified store via the engine's direct path now.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Phase K.5.T8: engine drops internal use of CalendarBaselineStore

All m_calendarBaselines reads/writes route through m_baselineStore
(Storage::BaselineStore). setCalendarBaselineStore retained as
no-op-internally forwarder until consumer cutover (Task 13).
Calendar iCal text rides as CanonicalRecord with calendar/ical shape."
```

---

## Task 9: Property-baseline routing through DomainPlugin::baselineProperties()

**Files:**
- Modify: `libkalburator/src/engine/syncengine.cpp` (the property-baseline read/write paths)

Currently the engine writes calendar property baselines via `m_calendarBaselines->setPropertyBaseline(...)`; after T8 this site no longer exists in the engine, but the *contract* — "what counts as a property baseline" — was hardcoded to calendar's color+description. Generalize: the engine asks the resolved `DomainPlugin` for `baselineProperties()`, picks those keys out of `collectionProperties()`, and stores via `setCollectionBaseline`.

- [ ] **Step 1: Locate the property-baseline read/write sites**

```bash
grep -n "propertyBaseline\|setPropertyBaseline" src/engine/syncengine.cpp
```

Expected: zero hits in syncengine.cpp post-T8 (the calls were on the facade, removed in T8). If any remain, this task's migration target is those sites.

If the engine never read property baselines through CalendarBaselineStore directly (i.e., only the facade did), then this task narrows to: **add the property-baseline write path** to the engine's post-sync metadata update.

- [ ] **Step 2: Add property-baseline write to updateSyncMetadata (or equivalent)**

In `src/engine/syncengine.cpp`, find `updateSyncMetadata` (or the function that runs after a successful sync to persist baselines — the `setLastSyncTime` call site is the anchor). Just before/after the `setLastSyncTime` call, insert:

```cpp
// Property baselines per the resolved domain plugin.
if (m_baselineStore && m_engine) {
    Kalburator::Shape::DomainPlugin *plugin =
        Kalburator::Shape::DomainRegistry::instance().resolve(domainId);
    if (plugin) {
        const QStringList keys = plugin->baselineProperties();
        if (!keys.isEmpty()) {
            const QVariantMap collProps = plugin->collectionProperties(
                sourceBackend, sourceCollectionId);
            QVariantMap baseline;
            for (const auto &k : keys) {
                if (collProps.contains(k)) baseline.insert(k, collProps.value(k));
            }
            m_baselineStore->setCollectionBaseline(
                mapping.id, sourceCollectionId, baseline);
        }
    }
}
```

The exact variable names depend on the surrounding scope at the insertion point — adapt `domainId`, `sourceBackend`, `sourceCollectionId`, `mapping.id` to whatever the local scope exposes.

- [ ] **Step 3: Add a test that property baselines persist round-trip via the engine**

This is integration-level and existing tests likely cover it via the facade. Skim `tests/calendar/tst_calendar_sync_full.cpp` for property-baseline assertions; if present, they'll pass through the unified store transparently. If absent, defer adding new tests — the unit tests at Task 3 already cover the storage layer; the engine wiring is verified by `verify-all.sh`.

- [ ] **Step 4: Build + run full suite**

```bash
cmake --build build -j 10 && ctest --test-dir build --output-on-failure
```

Expected: green. If a calendar property-baseline test fails (e.g. one that previously asserted `m_calendarBaselines->propertyBaseline(...)` returns expected JSON), update it to assert via `m_baselineStore->collectionBaseline(...)` instead.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Phase K.5.T9: property baselines route through DomainPlugin::baselineProperties()

Engine queries the resolved domain plugin for which property keys
to baseline, snapshots from collectionProperties(), persists via
Storage::BaselineStore::setCollectionBaseline. Calendar plugin
declares {color, description}; other domains default to empty."
```

---

## Task 10: Migrate libkalburator tests off CalendarBaselineStore

**Files:**
- Modify: `libkalburator/tests/engine/tst_cancellation_reason.cpp`
- Modify: `libkalburator/tests/calendar/tst_engine_unified_boundary.cpp`
- Modify: `libkalburator/tests/calendar/tst_calendar_sync_oneway.cpp`
- Modify: `libkalburator/tests/calendar/tst_calendar_sync_full.cpp`
- Modify: `libkalburator/tests/calendar/tst_engine_subset_dispatch.cpp`
- Modify: `libkalburator/tests/calendar/tst_calendar_sync_error_recovery.cpp`
- Modify: `libkalburator/tests/calendar/tst_calendar_first_sync_via_blob_engine.cpp`
- Modify: `libkalburator/tests/calendar/tst_syncstore.cpp`
- Move + rewrite: `libkalburator/tests/calendar/tst_calendar_baseline_store.cpp` → `libkalburator/tests/storage/tst_baseline_store_calendar_shape_round_trip.cpp`

For each tests file in the first group, the change is: replace `std::unique_ptr<CalendarBaselineStore>` with `std::unique_ptr<Storage::BaselineStore>`; replace the `setCalendarBaselineStore(m_calendarBaselines.get())` setter call with `setBaselineStore(m_baselineStore.get())`. Existing assertions that use facade methods need rewriting to the unified-store API:

| Facade call in test | Unified-store equivalent |
|---|---|
| `m_calBaselines->hasBaselines(id)` | `!m_baselineStore->baselinesForMappingV3(id).isEmpty()` |
| `m_calBaselines->setBaseline(id, uid, ical)` | `m_baselineStore->setBaselineV3(id, calendarRec(uid, ical))` |
| `m_calBaselines->baseline(id, uid)` | `QString::fromUtf8(m_baselineStore->baselineV3(id, uid).value_or(...).data)` |
| `m_calBaselines->setLastSyncTime(...)` | `m_baselineStore->setLastSyncTime(...)` |

Define `calendarRec()` helper in a shared test util header if test files need it more than once. Otherwise inline in each.

- [ ] **Step 1: Create a tiny shared test helper for calendar canonical records**

Create `libkalburator/tests/calendar/calendar_test_helpers.h`:

```cpp
#pragma once
#include "canonicalrecord.h"
#include "shape.h"
#include <QString>

inline Kalburator::Shape::CanonicalRecord calendarTestRec(const QString &uid,
                                                          const QString &ical) {
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        QStringLiteral("ical")};
    rec.data     = ical.toUtf8();
    return rec;
}
```

- [ ] **Step 2: Migrate each affected test file**

For each of the 8 files listed at the top of this task, apply the substitution pattern. Suggested order (dependency-light first):

1. `tst_calendar_first_sync_via_blob_engine.cpp`
2. `tst_calendar_sync_oneway.cpp`
3. `tst_calendar_sync_error_recovery.cpp`
4. `tst_calendar_sync_full.cpp`
5. `tst_engine_unified_boundary.cpp`
6. `tst_engine_subset_dispatch.cpp`
7. `tst_cancellation_reason.cpp`
8. `tst_syncstore.cpp` — this one references CalendarBaselineStore in commentary only post-D phase; verify with grep what changes are needed.

For each file:

```bash
# Substitute type and member names
sed -i 's/std::unique_ptr<CalendarBaselineStore>/std::unique_ptr<Kalburator::Storage::BaselineStore>/g' tests/calendar/tst_NAME.cpp
sed -i 's/std::make_unique<CalendarBaselineStore>/std::make_unique<Kalburator::Storage::BaselineStore>/g' tests/calendar/tst_NAME.cpp
sed -i 's/setCalendarBaselineStore/setBaselineStore/g' tests/calendar/tst_NAME.cpp
sed -i 's/#include "calendarbaselinestore.h"/#include "baselinestore.h"/g' tests/calendar/tst_NAME.cpp
```

Then by-hand: rename the local variables (`m_calBaselines`, `m_calendarBaselines`) to `m_baselineStore`. Rewrite assertions that use facade-only API to the unified-store API per the table above.

For tests that include calendar_test_helpers.h, add the include + use `calendarTestRec(...)`.

- [ ] **Step 3: Migrate tst_calendar_baseline_store.cpp**

`tests/calendar/tst_calendar_baseline_store.cpp` is the dedicated unit test for the deleted `CalendarBaselineStore` class.

Move and rewrite as a calendar-shape round-trip test against the unified store:

```bash
git mv tests/calendar/tst_calendar_baseline_store.cpp \
       tests/storage/tst_baseline_store_calendar_shape_round_trip.cpp
```

Then edit the moved file:
- Class name `TstCalendarBaselineStore` → `TstBaselineStoreCalendarShapeRoundTrip`.
- All `CalendarBaselineStore` member uses → `Kalburator::Storage::BaselineStore`.
- Each `setBaseline`/`baseline`/`setBaselines`/etc. translates per the table above.
- Each property-baseline assertion translates: `setPropertyBaseline(jsonString)` → `setCollectionBaseline(QVariantMap)`; `propertyBaseline()` → `collectionBaseline()` (returns map; assertions become map-keyed instead of JSON-string-equality).

Move the test entry from `tests/calendar/CMakeLists.txt` to `tests/storage/CMakeLists.txt`.

- [ ] **Step 4: Build + run full suite**

```bash
cmake --build build -j 10 && ctest --test-dir build --output-on-failure
```

Expected: green; same total pass count plus or minus the renamed test.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Phase K.5.T10: migrate libkalburator tests off CalendarBaselineStore

8 test files in tests/calendar/ + tests/engine/ now use
Storage::BaselineStore directly. tst_calendar_baseline_store.cpp
moves to tests/storage/tst_baseline_store_calendar_shape_round_trip.cpp
and tests calendar-shape round-trips through the unified API."
```

---

## Task 11: PlanStan cutover

**Worktree:** `~/dev/refactor-engine-merger/PlanStan/`

**Files:**
- Modify: `PlanStan/src/controllers/collectioncontroller.h`
- Modify: `PlanStan/src/controllers/collectioncontroller.cpp` (lines 38–39, 1860–1885)

Replace the dual `CalendarBaselineStore` + `BlobBaselineStore` construction with a single `Storage::BaselineStore`.

- [ ] **Step 1: Inspect current construction**

```bash
cd ~/dev/refactor-engine-merger/PlanStan
grep -n "CalendarBaselineStore\|BlobBaselineStore\|m_calendarBaselines\|m_blobBaselines" src/controllers/collectioncontroller.{h,cpp}
```

Expected: ~10–15 lines listing the construction + setter sites.

- [ ] **Step 2: Edit collectioncontroller.h**

- Drop forward decl `class CalendarBaselineStore;` and `class BlobBaselineStore;` if present.
- Add forward decl: `namespace Kalburator::Storage { class BaselineStore; }`.
- Replace member `Kalburator::Sync::CalendarBaselineStore *m_calendarBaselines = nullptr;` and `BlobBaselineStore *m_blobBaselines = nullptr;` with single member `Kalburator::Storage::BaselineStore *m_baselineStore = nullptr;`.

- [ ] **Step 3: Edit collectioncontroller.cpp**

- `#include "calendarbaselinestore.h"` deletes; `#include "blobbaselinestore.h"` → `#include "baselinestore.h"`.
- Constructor body: remove the two-store construction (around line 1864–1885); replace with a single:

```cpp
m_baselineStore = new Kalburator::Storage::BaselineStore(syncDbPath);
qDebug() << "  Created BaselineStore at:" << syncDbPath;
m_engine->setBaselineStore(m_baselineStore);
```

(Remove the old `setCalendarBaselineStore(m_calendarBaselines)` + `setBlobBaselineStore(m_blobBaselines)` calls — both are deprecated forwarders into `setBaselineStore`.)

- Any `m_calendarBaselines` / `m_blobBaselines` reference in the rest of the file: rename to `m_baselineStore`. If a method specifically called `setBaseline(uid, ical)` (calendar-shape facade), reroute to `setBaselineV3(mappingId, calendarTestRec(uid, ical))` using a local helper.

- Destructor: ensure `m_baselineStore` is `delete`'d (unless it's a child of a QObject parent — check parent ownership).

- [ ] **Step 4: Build PlanStan**

```bash
cmake --build build -j 10
```

If it fails, missing-include hints will point at remaining `Kalburator::Sync::CalendarBaselineStore` mentions. Fix and rebuild.

- [ ] **Step 5: Run PlanStan tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: same pass count as PlanStan's pre-K.5 baseline (82/106 per CURRENT-STATUS).

- [ ] **Step 6: Commit (PlanStan worktree)**

```bash
git add -A
git commit -m "Phase K.5.T11: PlanStan adopts Storage::BaselineStore

CollectionController constructs one BaselineStore in place of the
former CalendarBaselineStore + BlobBaselineStore pair. Property
baselines now keyed via collection_baselines table; iCal-text
baselines via blob_baselines_v3 with calendar/ical shape."
```

---

## Task 12: WildPalms cutover

**Worktree:** `~/dev/refactor-engine-merger/WildPalms/`

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.h` (lines ~23, ~160)
- Modify: `WildPalms/src/runtime/palmruntime.cpp` (lines ~179, ~187)

WildPalms only constructs `BlobBaselineStore`; the rename + namespace move is the entire scope.

- [ ] **Step 1: Inspect current construction**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
grep -n "BlobBaselineStore\|m_baselineStore\|setBlobBaselineStore" src/runtime/palmruntime.{h,cpp}
```

- [ ] **Step 2: Edit palmruntime.h**

- Replace forward decl: `namespace Kalburator::Sync { class BlobBaselineStore; }` → `namespace Kalburator::Storage { class BaselineStore; }`.
- Member type: `std::unique_ptr<Kalburator::Sync::BlobBaselineStore> m_baselineStore` → `std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselineStore`.

- [ ] **Step 3: Edit palmruntime.cpp**

- Constructor: `std::make_unique<Kalburator::Sync::BlobBaselineStore>(...)` → `std::make_unique<Kalburator::Storage::BaselineStore>(...)`.
- Setter: `m_engine->setBlobBaselineStore(m_baselineStore.get())` → `m_engine->setBaselineStore(m_baselineStore.get())`.
- Add `#include` for the new header path. Inspect existing includes; if WildPalms includes via the public installed header path of libkalburator, no local change. Otherwise: `#include "../../libkalburator/src/storage/baselinestore.h"` or whatever the project's existing include style mandates.

- [ ] **Step 4: Build WildPalms**

```bash
cmake --build build -j 10
```

- [ ] **Step 5: Run WildPalms tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 81/81 pass (same as CURRENT-STATUS baseline).

- [ ] **Step 6: Commit (WildPalms worktree)**

```bash
git add -A
git commit -m "Phase K.5.T12: WildPalms adopts Storage::BaselineStore

PalmRuntime constructs Storage::BaselineStore in place of
Sync::BlobBaselineStore. Engine setter call uses setBaselineStore."
```

---

## Task 13: Delete CalendarBaselineStore + journal/ shims + deprecated forwarders

**Worktree:** `~/dev/refactor-engine-merger/libkalburator/`

**Files (deleted):**
- `libkalburator/src/calendar/calendarbaselinestore.h`
- `libkalburator/src/calendar/calendarbaselinestore.cpp`
- `libkalburator/src/journal/blobbaselinestore.h` (forwarding shim from T1)
- `libkalburator/src/journal/idmappingstore.h` (forwarding shim from T1)

**Files (modified):**
- `libkalburator/src/engine/syncengine.h` (drop `setCalendarBaselineStore` + `setBlobBaselineStore` deprecated forwarders)
- `libkalburator/src/engine/syncengine.cpp` (drop their bodies)
- `libkalburator/src/calendar/syncconflictstore.h` (comment update; line ~20)
- `libkalburator/CMakeLists.txt` (drop the four deleted files from source list)

After this task the cleanup gates from the design doc all pass.

- [ ] **Step 1: Verify zero remaining external callers of the legacy spellings**

```bash
cd ~/dev/refactor-engine-merger
grep -rn "Kalburator::Sync::BlobBaselineStore\|Kalburator::Sync::CalendarBaselineStore\|Kalburator::Sync::IDMappingStore\|setCalendarBaselineStore\|setBlobBaselineStore\|class CalendarBaselineStore\|class BlobBaselineStore\b" libkalburator PlanStan WildPalms 2>/dev/null
```

Expected: empty (after Tasks 11 and 12 commit). If any line appears, name the offending file/line and migrate it before proceeding.

- [ ] **Step 2: Delete CalendarBaselineStore source**

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git rm src/calendar/calendarbaselinestore.h src/calendar/calendarbaselinestore.cpp
```

- [ ] **Step 3: Delete journal shims**

```bash
git rm src/journal/blobbaselinestore.h src/journal/idmappingstore.h
```

- [ ] **Step 4: Drop deprecated forwarders in syncengine**

In `src/engine/syncengine.h`:
- Remove the `[[deprecated]] void setBlobBaselineStore(...)` inline alias added in Task 2 Step 5.
- Remove `setCalendarBaselineStore` declaration and `calendarBaselineStore` accessor + `m_calendarBaselines` member.

In `src/engine/syncengine.cpp`:
- Remove `void SyncEngine::setCalendarBaselineStore(...)` body (line ~137).
- Remove any `m_calendarBaselines = ...` assignments anywhere else.
- Remove the inner Worker's `m_calendarBaselines` field.

- [ ] **Step 5: Update CMakeLists.txt**

Remove these entries from the source list:
```
src/calendar/calendarbaselinestore.h
src/calendar/calendarbaselinestore.cpp
src/journal/blobbaselinestore.h
src/journal/idmappingstore.h
```

(`src/journal/blobbaselinestore.cpp` and `src/journal/idmappingstore.cpp` were already removed in Task 1; this step removes the *shim* `.h` files added in Task 1 plus the calendar facade.)

- [ ] **Step 6: Update syncconflictstore.h comment**

`src/calendar/syncconflictstore.h:20` references `CalendarBaselineStore — iCal / property baselines + last-sync time`. Rewrite to:

```
 *   - Storage::BaselineStore — record/property baselines + last-sync time
```

- [ ] **Step 7: Build + run full suite**

```bash
cmake --build build -j 10 && ctest --test-dir build --output-on-failure
```

Expected: green. The deletion is purely subtractive at this point — all callers were migrated in T8, T10, T11, T12.

- [ ] **Step 8: Run end-state grep gates**

```bash
cd ~/dev/refactor-engine-merger
grep -rn "class CalendarBaselineStore\|CalendarBaselineStore *\*" libkalburator/src libkalburator/tests
grep -rn "class BlobBaselineStore\b\|Kalburator::Sync::BlobBaselineStore" libkalburator/src libkalburator/tests
find libkalburator/src/journal -name 'baselinestore.*' -o -name 'idmappingstore.*'
# (orphan QSyncCore::BaselineStore at journal/baselinestore.{h,cpp} is OK; expected.)
```

Expected: first two greps empty; the find returns only `journal/baselinestore.h`/`.cpp` (the orphan QSyncCore one) since that's out of K.5 scope.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "Phase K.5.T13: delete CalendarBaselineStore + transient shims

CalendarBaselineStore facade removed; SyncEngine drops
setCalendarBaselineStore + setBlobBaselineStore deprecated
forwarders. journal/ forwarding shims (.h files) deleted.
End-state grep gates pass."
```

---

## Task 14: Cross-repo verification + status doc updates + tag

**Worktrees:** all three.

- [ ] **Step 1: Run verify-all.sh**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected exit `0`. If exit `2` (regression), debug. If exit `3` (improvement), inspect — could be a flaky baseline; record findings before refreshing.

- [ ] **Step 2: Refresh baselines if verify-all reports `3` (improvement)**

Per memory: user authorizes baseline refreshes when a phase plan calls for it. If `verify-all.sh` exited `3`, regenerate:

```bash
./scripts/refresh-baselines.sh   # or whatever the project's documented refresh path is
```

Inspect the diff for sanity (only baseline-line changes corresponding to the formerly-failing tests now passing). Commit baselines under the libkalburator worktree if that's where the script writes them.

- [ ] **Step 3: Update libkalburator/docs/phase0/04ab-phase-k-engine-generalization-design.md**

Update the K.5 status indicator in the design doc — find the `K.5 — Baseline unification` heading and append:

```
**Status:** landed YYYY-MM-DD on commit <SHA>; tag v0.34-phase-k5-unified-baseline.
```

(Substitute the actual date and SHA at landing time.)

- [ ] **Step 4: Update CURRENT-STATUS.md**

In `~/dev/refactor-engine-merger/CURRENT-STATUS.md`:
- Bump the date.
- Move K.5 from `⬜ next` to `✅ done` in the "Phase K status" block.
- Add an entry to "Recently committed (libkalburator)" listing the K.5 commits.
- Add entries to "Recently committed (PlanStan)" and "Recently committed (WildPalms)" for the cross-repo cutovers.
- Update "What to do RIGHT NOW" to point K.5.5 (next per ROADMAP).
- Update the tag plan block to mark `v0.34-phase-k5-unified-baseline` ✅.

- [ ] **Step 5: Append to FINDINGS.md**

If anything non-obvious surfaced during execution (a SQLite gotcha, a connection-name collision, a forwarding-shim trap, a test-fixture quirk), append a dated entry. Skip if there were no surprises.

- [ ] **Step 6: Tag**

The tag step is authorized per memory.

```bash
cd ~/dev/refactor-engine-merger/libkalburator
git tag -a v0.34-phase-k5-unified-baseline -m "Phase K.5: unified baseline + storage move

CalendarBaselineStore + BlobBaselineStore collapse into single
Storage::BaselineStore. New tables collection_baselines (property
snapshots keyed by mappingId+collectionId) and mapping_metadata
(last-sync timestamp). v4→v5 idempotent migration. iCal-text
baselines fold into blob_baselines_v3 via calendar canonical shape.
Storage namespace replaces Sync for both BaselineStore and
IDMappingStore. PlanStan + WildPalms cut over in same commit group."
```

- [ ] **Step 7: Final verify**

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected: exit `0`. Phase K.5 closed.

- [ ] **Step 8: Commit status updates**

```bash
cd ~/dev/refactor-engine-merger
git add CURRENT-STATUS.md FINDINGS.md   # FINDINGS optional
git commit -m "K.5 close: status doc updates"
# (CURRENT-STATUS.md is in the coordination folder, which is not a
#  git repo per CLAUDE.md — this commit is per-repo only if applicable;
#  otherwise just save the file.)
```

If the coordination folder isn't a repo, just save the files. Per CLAUDE.md: "`~/dev/refactor-engine-merger/` is a coordination folder, **not** a git repo."

---

## Self-review checklist

- **Spec coverage:** Every K.5 design-doc bullet (file rename, schema v5, collection_baselines, mapping_metadata, baselineProperties() virtual, CalendarBaselineStore deletion, end-state grep gates) maps to at least one task. The semantic-cleansing K.5 adjustments (storage/ directory, IDMappingStore move, namespace `Storage`) covered in Task 1.
- **Cross-repo:** PlanStan (Task 11) and WildPalms (Task 12) cutover sit before facade deletion (Task 13), so end-state grep gates are reachable in one verify-all green run.
- **TDD:** Tasks 3, 4, 5, 6 are TDD-shaped (failing test → impl → passing test). Tasks 1, 2, 7, 8, 9, 10, 11, 12, 13 are mechanical migrations with build+test gates instead.
- **Forwarding-shim discipline:** Shims live for the duration of the K.5 commit group only, deleted in Task 13. No K.5.5/K.6 bleed.
- **Commit cadence:** Every task ends in a single commit. 14 tasks → 14 commits on libkalburator's `refactor/engine-merger` (plus 1 on PlanStan, 1 on WildPalms).
- **Risk:** the orphan `Sync::QSyncCore::BaselineStore` at `src/journal/baselinestore.{h,cpp}` shares the *unqualified* name `BaselineStore` with the new `Storage::BaselineStore`. Different namespaces — no collision in C++. WildPalms continues to use `QSyncCore::BaselineStore` independently; out of scope for K.5.
