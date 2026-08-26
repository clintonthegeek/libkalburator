#ifndef KALBURATOR_STORAGE_BASELINESTORE_H
#define KALBURATOR_STORAGE_BASELINESTORE_H

/**
 * @file baselinestore.h
 * @brief SQLite-backed baseline store for the blob sync engine.
 *
 * LAYER ROLE (Plan 9): `storage/` (namespace `Kalburator::Storage`) holds the
 * SQLite-persistent engine-side sync stores — `BaselineStore` and
 * `IDMappingStore` — durable state below the domains. "Store" == persists
 * (invariant 5); these are plain RAII value types, not QObjects.
 *
 * ONE table generation is active (post-fanout-collapse, Task 3.1):
 *
 * v3 (G.4, schema v7): `blob_baselines_v3`
 *   Keyed by (mapping_id, record_id) + nullable per-side hash columns
 *   (Phase B4 N2 fix, schema v6) + sync-progress tokens (H3, schema v7).
 *
 * The pre-fanout (keyed-by (backend_id, collection_id, record_id)) API and
 * the v2→v3 data-migration code were deleted in fanout-collapse Task 3.1;
 * the campaign's locked "break + recreate" decision (no compat layer)
 * removed the migration reach. Spec/design: §A (single per-mapping view),
 * §B (cruft removal).
 *
 * Not thread-safe. Callers must serialize access to a given instance.
 * Not a QObject — pure value-lifetime class with RAII connection
 * ownership.
 */

#include <functional>
#include <optional>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "canonicalrecord.h"

namespace Kalburator::Storage {

class BaselineStore
{
public:
    explicit BaselineStore(const QString &dbPath);
    ~BaselineStore();

    BaselineStore(const BaselineStore &) = delete;
    BaselineStore &operator=(const BaselineStore &) = delete;
    BaselineStore(BaselineStore &&) = delete;
    BaselineStore &operator=(BaselineStore &&) = delete;

    bool    isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    // -----------------------------------------------------------------------
    // v3 mapping-keyed API — keyed by (mappingId, recordId).
    // Stored in blob_baselines_v3.
    // -----------------------------------------------------------------------

    bool setBaselineV3(const QString &mappingId,
                       const Kalburator::Shape::CanonicalRecord &rec);

    std::optional<Kalburator::Shape::CanonicalRecord>
    baselineV3(const QString &mappingId, const QString &recordId) const;

    QList<Kalburator::Shape::CanonicalRecord>
    baselinesForMappingV3(const QString &mappingId) const;

    bool removeBaselineV3(const QString &mappingId, const QString &recordId);

    bool clearMappingV3(const QString &mappingId);

    // -----------------------------------------------------------------------
    // Per-side baseline hashes (Phase B4 / N2 fix, schema v6).
    //
    // A single stored content hash compared against both sides' native
    // bytes can never converge: two backends never serialize the same
    // logical record identically (PRODID, property order, folding, server
    // normalization). These methods store what EACH side's native bytes
    // hashed to at the moment of the last successful sync, in two new
    // nullable columns (source_hash, target_hash) on the existing
    // blob_baselines_v3 table — same (mappingId, recordId) key as the v3
    // API above, so removeBaselineV3()/clearMappingV3() also clear these.
    //
    // Legacy rows written before this migration (only canonical_bytes set,
    // domain "blob") have NULL source_hash/target_hash; baselineHashesV4()/
    // baselinesHashesForMappingV4() treat that single legacy hash as BOTH
    // side hashes so the first post-upgrade sync re-diffs exactly as
    // before, then writes proper per-side rows on the next save. No
    // separate data-migration pass is needed.
    // -----------------------------------------------------------------------

    struct BaselineHashes {
        QString recordId;
        QString sourceHash;
        QString targetHash;
    };

    bool setBaselineHashesV4(const QString &mappingId,
                             const QString &recordId,
                             const QString &sourceHash,
                             const QString &targetHash);

    std::optional<BaselineHashes>
    baselineHashesV4(const QString &mappingId, const QString &recordId) const;

    QList<BaselineHashes>
    baselineHashesForMappingV4(const QString &mappingId) const;

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

    // -----------------------------------------------------------------------
    // Mapping-metadata API (K.5, schema v5). Per-mappingId scalars.
    // Currently: last-sync timestamp.
    // -----------------------------------------------------------------------

    bool      setLastSyncTime(const QString &mappingId, const QDateTime &when);
    QDateTime lastSyncTime(const QString &mappingId) const;

    // -----------------------------------------------------------------------
    // Sync-progress tokens (H3, schema v7).
    //
    // Per (mappingId, side) — side is "source" or "target" — the revision
    // token (CTag / fingerprint / whatever Sync::ChangeDetection returns)
    // that this mapping last successfully synced against, captured BEFORE
    // that run's fetch (pre-fetch snapshot). Distinct from a backend's own
    // fetch-time cache-validity token: this one is engine-owned,
    // per-mapping, and written only on a successful mapping run. Written by
    // SyncEngine::onWorkerSyncCompleted; consulted by
    // SyncEngine::prepareSyncFastPath's skip check.
    // -----------------------------------------------------------------------

    QString syncToken(const QString &mappingId, const QString &side) const;
    void    setSyncToken(const QString &mappingId, const QString &side,
                         const QString &token);
    void    clearSyncTokens(const QString &mappingId);

    // -----------------------------------------------------------------------
    // Record-id aliases (O55, schema v8).
    //
    // Per (mappingId, nativeId) → canonicalId: when the engine applies a
    // CREATE and the backend assigns a different id than requested
    // (WriteOperation::idAliases — e.g. GenericSqliteBackend reads back
    // `<collectionId>\x01<origId>`), the pair is persisted here so later
    // passes can join the sides. canonicalId is the id the mapping's
    // baseline rows are keyed under for that record.
    //
    // Aliases are additive engine state; consumers never read them.
    // clearMappingV3() also clears a mapping's aliases (same reasoning as
    // its sync-token wipe). Not migrated: a missing row just means "no
    // alias", same as an absent baseline.
    // -----------------------------------------------------------------------

    bool setIdAlias(const QString &mappingId,
                    const QString &nativeId,
                    const QString &canonicalId);

    QHash<QString, QString> idAliasesForMapping(const QString &mappingId) const;

    void clearIdAliasesForMapping(const QString &mappingId);

    // -----------------------------------------------------------------------
    // Transaction wrapper (VP.b / W2).
    //
    // Single-statement autocommit is the default (WAL, foreign_keys ON,
    // caller-serialized — this store is not thread-safe), but a
    // master+exception pair must persist atomically: the engine's persist
    // loop now wraps its per-record setBaselineHashesV4/setIdAlias writes in
    // one transaction so a mid-loop failure rolls the whole batch back
    // instead of leaving a half-written pair.
    //
    // @p fn runs with the transaction open; its bool return true COMMITs,
    // false ROLLBACKs. Nested transaction() calls JOIN the outer transaction
    // (fn runs directly, no nested BEGIN — the outer frame owns commit/rollback
    // regardless of the inner frame's return). BEGIN IMMEDIATE is used for the
    // outer frame (write-immediate; callers serialize, so there is no
    // concurrent reader to stall) rather than Qt's QSqlDatabase::transaction()
    // deferred BEGIN. On BEGIN/COMMIT/ROLLBACK failure setError() and return
    // false.
    // -----------------------------------------------------------------------

    template<typename Callable>
    bool transaction(Callable &&fn)
    {
        if (m_inTransaction)
            return fn();  // nested: join the outer transaction, no own BEGIN
        if (!beginTransaction())
            return false;
        const bool ok = fn();
        m_inTransaction = false;
        if (ok)
            return commitTransaction();
        rollbackTransaction();  // false result; setError() only on SQL failure
        return false;
    }

private:
    static int s_connectionCounter;

    QString         m_dbPath;
    QString         m_connName;
    bool            m_isOpen = false;
    bool            m_inTransaction = false;
    mutable QString m_lastError;

    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    bool ensureSchemaAndVersion();
    bool ensureSchemaV3();
    bool ensureSchemaV5();
    bool ensureSchemaV6();
    bool ensureSchemaV7();
    bool ensureSchemaV8();
    void setError(const QString &message) const;
};

} // namespace Kalburator::Storage

#endif // KALBURATOR_STORAGE_BASELINESTORE_H
