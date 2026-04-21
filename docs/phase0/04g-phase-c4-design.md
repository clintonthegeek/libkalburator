# Phase C.4 — SQLite IDMappingStore design

**Date:** 2026-04-21.
**Status:** Design spec, approved for implementation. Implementation
executes in this session. Predecessor: `04c-phase-c-plan.md §C.4` +
`04a-followups.md §"Audit 2"`. Successor: `docs/superpowers/plans/` plan
file (to be written alongside this spec in the same commit — the plan
is the thing that actually gets executed).

## Goal

Replace the dormant Wild-Palms-lifted JSON `IdMappingStore` (currently
at `src/journal/idmappingstore.{h,cpp}` in the
`Kalburator::Sync::QSyncCore` sub-namespace) with a SQLite-backed
`IDMappingStore` at `Kalburator::Sync` top-level that implements the
merged schema from Audit 2. Deliver the class and its tests only;
PlanStan call-site migration stays in C.5.

## What Audit 2 pinned

From `04a-followups.md §"Audit 2 — IDMappingStore coverage"`:

- **Name:** `IDMappingStore` (capital D; replaces WP's lowercase-d
  `IdMappingStore`).
- **Backend:** SQLite. JSON rejected (can't survive concurrent access,
  can't do partial updates).
- **API shape:** PS's per-backend-qualified methods as primary
  (`targetIdForSourceUid`, `sourceUidForTargetId`, `setIdMapping`,
  `removeIdMapping`, `allIdMappings`). WP's category + archive methods
  as optional additions.
- **Layer:** lower. Backend-agnostic — blob-layer backends need it
  just as much as calendar-layer.
- **Struct fields:** `backendId`, `sourceUid`, `targetId`, `calendarId`,
  `lastSynced`, `sourceCategory`, `targetCategories`, `archived`.

## What this design resolves (not pinned by Audit 2)

1. **`recurrenceId` field (Audit 2 oversight).** PS's existing
   `sync_id_mappings` table has `recurrence_id TEXT DEFAULT ''` in its
   primary key because iCal recurring events split a UID into a master
   plus detached exceptions, each with a distinct `RECURRENCE-ID`.
   Without this field, every exception of a recurring series collides
   on PK. Audit 2 missed this; the new struct and API add
   `recurrenceId` explicitly. Documented as an audit correction rather
   than rewriting Audit 2.
2. **Migration path.** No new DB file, no new table. Share
   `.planstan-sync.db` with `SyncStore` and share the `sync_id_mappings`
   table. Extend the table in place via idempotent `ALTER TABLE ADD
   COLUMN` for the four WP fields.
3. **DB location.** Same as (2) — co-located with `SyncStore`. One
   ACID domain per collection.
4. **Scope boundary.** C.4 delivers the class + tests. Zero PS
   call-site changes. `SyncStore`'s existing identity methods remain
   untouched and continue working against the same table.
5. **Tests.** PS-side (`tests/sync/tst_idmappingstore.cpp`), modeled
   on `tst_syncstore.cpp`. libkalburator-side test suite stays
   deferred to Phase 4 per `05-repo-strategy.md`.
6. **Signals / QObject.** Neither. Plain C++ class with RAII DB
   ownership. Matches `SyncStore`'s passive-store model.

## Architecture

### Before (post-C.3)

```
libkalburator/src/journal/
  idmappingstore.{h,cpp}    ← dormant WP JSON store, QSyncCore:: sub-namespace
                              (no consumers anywhere)

libkalburator/src/calendar/
  syncstore.{h,cpp}         ← SQLite; owns sync_id_mappings table
                              via setIdMapping/targetIdForSourceUid/...

PlanStan DB on disk:
  <collection>/.planstan-sync.db
    sync_id_mappings (backend_id, local_uid, recurrence_id, remote_id,
                      calendar_id, created_at)
    [other tables: sync_versions, sync_baselines, property_baselines,
     sync_metadata, sync_conflicts, collection_ctags]
```

### After (post-C.4)

```
libkalburator/src/journal/
  idmappingstore.{h,cpp}    ← NEW: SQLite; Kalburator::Sync top-level;
                              owns sync_id_mappings operations alongside
                              SyncStore; adds 4 WP columns to existing rows
                              via idempotent ALTER TABLE on open

libkalburator/src/calendar/
  syncstore.{h,cpp}         ← UNCHANGED; continues owning its identity
                              methods as before

PlanStan DB on disk:
  <collection>/.planstan-sync.db
    sync_id_mappings
      (backend_id, local_uid, recurrence_id, remote_id, calendar_id,
       created_at,
       last_synced,           ← NEW (TEXT, nullable)
       source_category,       ← NEW (TEXT, nullable)
       target_categories,     ← NEW (TEXT, nullable, JSON array)
       archived)              ← NEW (INTEGER, default 0)
    [other tables unchanged]
```

Both `IDMappingStore` and `SyncStore` open `.planstan-sync.db` on their
own uniquely-named SQLite connections. SQLite handles multiple
connections to the same file. PS is single-process, so there's only
ever one writer at a time.

## Class shape

File: `libkalburator/src/journal/idmappingstore.h` (replaces the old
file at the same path).

```cpp
#ifndef KALBURATOR_IDMAPPINGSTORE_H
#define KALBURATOR_IDMAPPINGSTORE_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {

struct IDMapping {
    QString     backendId;         // required, non-empty
    QString     sourceUid;         // required, non-empty
    QString     recurrenceId;      // empty for master / non-recurring
    QString     targetId;          // required, non-empty
    QString     calendarId;        // optional
    QDateTime   lastSynced;
    QString     sourceCategory;    // optional; Palm-shaped backends only
    QStringList targetCategories;  // optional
    bool        archived = false;

    bool isValid() const;          // backendId && sourceUid && targetId non-empty
};

class IDMappingStore {
public:
    explicit IDMappingStore(const QString &dbPath);
    ~IDMappingStore();

    IDMappingStore(const IDMappingStore &) = delete;
    IDMappingStore &operator=(const IDMappingStore &) = delete;

    bool    isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    // --- Primary lookup API (PS-derived, extended with recurrenceId) ---

    QString targetIdForSourceUid(const QString &backendId,
                                 const QString &sourceUid,
                                 const QString &recurrenceId = QString()) const;

    QString sourceUidForTargetId(const QString &backendId,
                                 const QString &targetId) const;

    void    setIdMapping(const QString &backendId,
                         const QString &sourceUid,
                         const QString &recurrenceId,
                         const QString &targetId,
                         const QString &calendarId = QString());

    void    removeIdMapping(const QString &backendId,
                            const QString &sourceUid,
                            const QString &recurrenceId = QString());

    void    clearIdMappings(const QString &backendId);

    // --- Bulk accessor (full struct, not bare targetId) ---

    QList<IDMapping> allMappings(const QString &backendId) const;

    // --- WP-contributed optional methods ---

    IDMapping getMapping(const QString &backendId,
                         const QString &sourceUid,
                         const QString &recurrenceId = QString()) const;

    void    updateCategories(const QString &backendId,
                             const QString &sourceUid,
                             const QString &recurrenceId,
                             const QString &sourceCategory,
                             const QStringList &targetCategories);

    void    setArchived(const QString &backendId,
                        const QString &sourceUid,
                        const QString &recurrenceId,
                        bool archived);

private:
    QString          m_dbPath;
    QString          m_connName;
    mutable QString  m_lastError;

    bool ensureSchema();
    bool addColumnIfMissing(const QString &column, const QString &ddl);
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_IDMAPPINGSTORE_H
```

**Non-QObject.** Matches SyncStore's passive-store pattern. Copy/move
deleted to keep the SQLite connection lifetime unambiguous.

**Connection name** is derived from `this` pointer + dbPath hash so
multiple instances in the same process don't collide.

**`mutable` on `m_lastError`** because the const lookup methods need
to record SQL errors without breaking logical constness.

## Schema + migration

### On-disk target schema

```sql
CREATE TABLE sync_id_mappings (
  backend_id        TEXT NOT NULL,
  local_uid         TEXT NOT NULL,
  recurrence_id     TEXT DEFAULT '',
  remote_id         TEXT NOT NULL,
  calendar_id       TEXT,
  created_at        TEXT DEFAULT (datetime('now')),
  last_synced       TEXT,
  source_category   TEXT,
  target_categories TEXT,          -- JSON array: ["a","b"]
  archived          INTEGER DEFAULT 0,
  PRIMARY KEY (backend_id, local_uid, recurrence_id)
);
CREATE INDEX idx_id_mappings_remote
  ON sync_id_mappings(backend_id, remote_id);
```

### Migration sequence on `ensureSchema()`

Idempotent, runs every time the store opens. Must be safe across three
starting states: (a) fresh DB (no table), (b) pre-C.4 DB (table exists
with old 6 columns), (c) post-C.4 DB (table already has all columns).

```text
1. CREATE TABLE IF NOT EXISTS sync_id_mappings (...original 6 columns...);
2. CREATE INDEX  IF NOT EXISTS idx_id_mappings_remote ...;
3. For each new column (last_synced, source_category, target_categories, archived):
     query PRAGMA table_info(sync_id_mappings) → column list.
     if column not present → execute ALTER TABLE ADD COLUMN.
```

Rationale for `PRAGMA table_info` check rather than catching the error
from `ALTER TABLE ADD COLUMN`: cleaner logs, no need to parse SQLite's
error string for "duplicate column name" vs real failures.

No `user_version` pragma is used. The presence/absence of each column
is the schema signal.

### Column encoding

- `last_synced` — ISO-8601 string (same format `SyncStore` uses for
  `synced_at` / `detected_at` elsewhere).
- `source_category` — plain string.
- `target_categories` — JSON array string (`QJsonDocument(QJsonArray)
  .toJson(Compact)`). Empty list serializes to `"[]"`, NULL
  distinguishes "never set" from "explicitly empty".
- `archived` — `0` / `1` integer.

## Interaction with `SyncStore`

Zero direct interaction. Both classes independently open the same DB
file on differently-named connections.

`SyncStore` is unaware the 4 new columns exist. Its `setIdMapping` uses
`INSERT OR REPLACE` over the original 6 columns, which **will reset
`last_synced`, `source_category`, `target_categories`, and `archived`
to their defaults (NULL, NULL, NULL, 0) whenever it touches a row that
`IDMappingStore` has populated.**

### The pre-C.5 double-writer hazard

This is the one real correctness wart between C.4 landing and C.5
landing:

- Interim state: both `SyncStore::setIdMapping` and
  `IDMappingStore::setIdMapping` are live. PS code calls the former;
  only tests call the latter.
- If a future PS code path started using `IDMappingStore` before C.5
  migrated everything, a subsequent `SyncStore::setIdMapping` on the
  same row would blow away the WP fields.

**Why this is acceptable in C.4:**

1. No PS code calls `IDMappingStore` until C.5. The only caller is
   the new test file, which uses its own temp DB.
2. C.5 lands immediately after C.4. The window is small.
3. Even during the window, no PS code populates the WP fields, so
   there's no data to lose.

The hazard is documented in the C.4 commit message and in
`IDMappingStore`'s header comment, so that a mid-window session doesn't
introduce a WP-field writer thinking the interop is safe.

## Deletion of the old JSON store

Delete:

- `libkalburator/src/journal/idmappingstore.h` (old, `QSyncCore::` sub-namespace)
- `libkalburator/src/journal/idmappingstore.cpp` (old)

Replace both with the new files at the same path (different contents,
different namespace).

The old struct `QSyncCore::IdMapping` (in `synccommon.h` or wherever
defined) stays for now — it's used by other qsynccore files
(ConflictRecord, BaselineStore etc.) per the QSyncCore sub-namespace.
The new `Kalburator::Sync::IDMapping` is a separate type, no conflict.
(If build surfaces unused-type warnings on the old struct, delete it
in a follow-up cleanup; not in C.4's scope.)

CMakeLists changes: libkalburator/CMakeLists.txt uses per-directory
globs (per C.3), so the file swap is picked up automatically. No
explicit list edit.

## Tests

New file `tests/sync/tst_idmappingstore.cpp` in PlanStan, wired into
`tests/sync/CMakeLists.txt`.

### Coverage

| Test | What it verifies |
|---|---|
| `test_open_fresh_db` | Construct over nonexistent path → isOpen() true, schema created, `PRAGMA table_info` returns all 10 columns. |
| `test_basic_roundtrip` | `setIdMapping("caldav", "uid1", "", "remote-1", "cal-a")` → both lookups return expected values. |
| `test_roundtrip_with_categories` | `setIdMapping` then `updateCategories` then `getMapping` returns struct with source+target categories set. |
| `test_archived_flag` | `setArchived(..., true)` → `getMapping().archived` is true; survives reopen. |
| `test_recurrence_exceptions` | Master (recurrenceId="") and two exceptions (distinct recurrenceIds) for same sourceUid map to distinct targetIds; all retrievable. |
| `test_remove_by_source` | `removeIdMapping` removes exactly one row (matches backend + sourceUid + recurrenceId). |
| `test_remove_master_keeps_exceptions` | Removing master leaves exceptions intact (tests PK granularity). |
| `test_clear_backend_scoped` | `clearIdMappings("caldav")` leaves `local` backend mappings untouched. |
| `test_all_mappings_returns_full_structs` | `allMappings("caldav")` returns `QList<IDMapping>` with all populated fields. |
| `test_migration_from_pre_c4_schema` | Create DB with hand-written pre-C.4 schema + seeded rows. Open an `IDMappingStore`. Verify: 4 new columns present, seeded rows preserved with NULL/0 in new columns, new writes populate new columns correctly. |
| `test_coexistence_with_syncstore` | Open `SyncStore` + `IDMappingStore` on the same temp DB. Each writes rows. Each can read the other's rows (for the 5 overlapping columns). Confirms the hazard shape: a `SyncStore::setIdMapping` after `IDMappingStore::setIdMapping` resets `last_synced` to NULL. This is a characterization test — it **documents** the hazard so a future regression that "fixes" it surfaces the intent. |
| `test_unused_backend_returns_empty` | `targetIdForSourceUid("nonexistent", ...)` → empty. `allMappings("nonexistent")` → empty list. |

Temp DB per test via `QTemporaryDir`. No Wayland / display needed.

### Baseline impact

Current: 86 pass / 26 fail / 112 total. After C.4: 86 pass + 12 new
passes = 98 pass / 26 fail / 124 total. Failure count unchanged; any
new failure blocks the commit.

## Commit plan

Single commit, message `Phase C.4: SQLite IDMappingStore`.

### Changes

**libkalburator:**
- `src/journal/idmappingstore.h` — full rewrite
- `src/journal/idmappingstore.cpp` — full rewrite
- `CMakeLists.txt` — no change (glob picks up)

**PlanStan:**
- `tests/sync/tst_idmappingstore.cpp` — new
- `tests/sync/CMakeLists.txt` — add new test executable

### Doc updates in the same commit (mandatory per phase0 README)

- `docs/phase0/README.md` — bump "Last updated" to 2026-04-21,
  flip Phase C.4 row from "queued" to "done 2026-04-21", update
  "Current status" section (new Done bullet, new Next target: C.5),
  adjust baseline ctest count (86→98 pass, 112→124 total).
- `docs/phase0/04g-phase-c4-design.md` — append "## Outcome" section.
- `~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`
  — update Status line.

### Verification gates

1. libkalburator standalone: `cmake --build ~/dev/libkalburator/build`
   clean.
2. PlanStan build: `/project:build` clean.
3. PlanStan ctest: all previously-passing tests still pass, new
   `tst_idmappingstore` tests pass, fail count stays 26.

## Not in scope for C.4

- PS call-site migration off `SyncStore::setIdMapping` → C.5.
- Removal or deprecation of `SyncStore` identity methods → C.5.
- Any change to `sync_versions`, `sync_baselines`, `property_baselines`,
  `sync_metadata`, `sync_conflicts`, `collection_ctags` tables.
- libkalburator test suite infrastructure → Phase 4 per
  `05-repo-strategy.md`.
- `ConnectionBehavior` / `ConflictPolicy` changes → already done in
  Phase B.
- WP's actual consumption of `IDMappingStore` → Phase E.
- Cleanup of old `QSyncCore::IdMapping` struct in `synccommon.h` (may
  become dead code; delete in a follow-up if warnings surface).

## Outcome

**Landed:** 2026-04-21. See `git log --oneline --grep="Phase C.4"` in
both repos for the landing commits.

**ctest delta (target-level counts):** 87 → 88 pass (+1 new target,
`tst_idmappingstore`, 12/12 internal tests green). 4 fail unchanged
(pre-existing `tst_blockstore`, `sync_workflow_conflicts`,
`sync_error_recovery`, `tst_treeflatteningproxymodel`). 23 not-run
(env-dependent integration + graffodil targets). No regressions.

**Deviations from spec:** none structural. Two implementation details
surfaced during execution:

1. **NULL-vs-'' recurrence_id bind bug (fixed).** First pass of the
   implementation bound `QString()` directly as the `recurrence_id`
   parameter. Qt's SQL driver binds a null `QString` as SQL `NULL`,
   and `WHERE recurrence_id = NULL` never matches (SQL three-valued
   logic). The existing `sync_id_mappings` schema defaults to `''`,
   and `SyncStore::setBaseline` / `setVersionHash` already work around
   this via an explicit `recurrenceId.isEmpty() ? "" : recurrenceId`
   ternary. `IDMappingStore` now funnels every `recurrence_id` bind
   through a `normRec()` helper that guarantees a non-null empty
   string. Caught by 7 failing tests on first run; all 12 pass after
   the fix.

2. **Schema-version coordination with SyncStore.** `SyncStore::
   initDatabase` deletes the DB file on `PRAGMA user_version != 3`.
   `IDMappingStore::ensureSchemaAndVersion` therefore stamps
   `user_version = 3` on fresh-DB creation only (never on an
   existing DB, where SyncStore's policy takes precedence). The
   constant `3` is hardcoded in both classes; bumps must stay
   coordinated in the same commit — otherwise one class will
   destroy the other's state.

**Follow-ups queued for C.5:**

- Move PS `SyncStore::setIdMapping` / `sourceUidForTargetId` /
  `removeIdMapping` / `allIdMappings` callers onto `IDMappingStore`.
- Decide shim-or-delete for `SyncStore`'s identity methods.
- The pre-C.5 double-writer hazard (characterized in
  `test_coexistence_with_syncstore`) closes when C.5 lands because
  SyncStore stops writing to `sync_id_mappings` at that point.
- Potentially consider a `CalBurator::Sync::SchemaVersion` shared
  constant once a third class joins the DB — two is still fine at a
  local `constexpr`.
