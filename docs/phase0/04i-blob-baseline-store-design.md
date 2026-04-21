# Phase B3 — BlobBaselineStore

**Date:** 2026-04-21 (drafted)
**Status:** Approved for implementation.
**Phase tag on completion:** `v0.7-phase-b3-baseline`.

## Motivation

`BlobSyncEngine::twoWayNaive` (shipped in Phase B2) is stateless and
compares by `lastModified`. Without a baseline it cannot distinguish
"record deleted on source since last sync" from "record never existed
on source." Deletions silently fail to propagate, which is a real
regression vs WP's existing Client Mode sync.

This phase adds `BlobBaselineStore` — persistent hash-per-record
baselines keyed by sync-mapping ID — so a future engine operation
(`twoWayWithBaseline`, landing in Phase B4 / WP Phase E.2) can compute
a correct 3-way diff.

## Surface

```cpp
namespace Kalburator::Sync {

class BlobBaselineStore
{
public:
    explicit BlobBaselineStore(const QString &dbPath);
    ~BlobBaselineStore();

    BlobBaselineStore(const BlobBaselineStore &) = delete;
    BlobBaselineStore &operator=(const BlobBaselineStore &) = delete;
    BlobBaselineStore(BlobBaselineStore &&) = delete;
    BlobBaselineStore &operator=(BlobBaselineStore &&) = delete;

    bool    isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    // Single-record set. Returns false on DB error; check lastError().
    bool setBaseline(const QString &mappingId,
                     const QString &recordId,
                     const QString &contentHash);

    // Returns empty QString if no baseline recorded yet.
    QString baselineHash(const QString &mappingId,
                         const QString &recordId) const;

    // Bulk commit; wraps in a transaction. INSERT OR REPLACE per row.
    // Intended to be called at the end of a successful sync.
    bool commitBaselines(const QString &mappingId,
                         const QMap<QString, QString> &recordIdToHash);

    // All record IDs currently recorded for a mapping. Used by the
    // engine to compute "deleted since baseline" = baseline - current.
    QStringList baselineRecordIds(const QString &mappingId) const;

    // Remove all rows for a mapping (e.g. when a SyncMapping is unbound).
    bool clearMapping(const QString &mappingId);
};

} // namespace Kalburator::Sync
```

## Schema

New table `blob_baselines` in `.planstan-sync.db`:

```sql
CREATE TABLE IF NOT EXISTS blob_baselines (
    mapping_id    TEXT NOT NULL,
    record_id     TEXT NOT NULL,
    content_hash  TEXT NOT NULL,
    updated_at    TEXT DEFAULT (datetime('now')),
    PRIMARY KEY (mapping_id, record_id)
);
CREATE INDEX IF NOT EXISTS idx_blob_baselines_mapping
    ON blob_baselines(mapping_id);
```

Coexists with `sync_id_mappings` (owned by `IDMappingStore`),
`sync_store_*` tables (owned by `SyncStore`), and future tables. Each
store uses idempotent `CREATE TABLE IF NOT EXISTS`. PRAGMA
`user_version = 3` matches the existing policy.

## Interaction with other stores

Exactly the same pattern as `IDMappingStore`:
- Open the database with a per-instance connection name
  (`BlobBaselineStore_N`, where N increments).
- `CREATE TABLE IF NOT EXISTS` is idempotent and safe on a DB that
  already contains other stores' tables.
- Stamp `PRAGMA user_version = 3` only if the DB file did not exist
  before this instance created it; existing DBs inherit the version
  already stamped.
- Close and remove the connection in the destructor.

## Tests

Library-side tests in `tests/journal/tst_blobbaselinestore.cpp`. Ten
internal slots covering:

- `opensOnValidPath` — constructor + schema path smoke.
- `setBaselineAndReadBack` — single-record round trip.
- `baselineHashMissingReturnsEmpty` — empty QString on miss.
- `setBaselineOverwritesExistingHash` — INSERT OR REPLACE semantics.
- `commitBaselinesBulkInsert` — transaction round trip.
- `commitBaselinesIsAtomic` — mixed new + existing rows commit cleanly.
- `baselineRecordIdsFiltersByMapping` — per-mapping scoping.
- `clearMappingRemovesOnlyThatMapping` — targeted delete.
- `coexistsWithIDMappingStore` — shared `.planstan-sync.db`.
- `dataPersistsAcrossReopen` — SQLite file durability + RAII cleanup.

## Outcome

(Filled in during Task 13 after the implementation lands.)
