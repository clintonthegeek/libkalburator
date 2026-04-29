#ifndef KALBURATOR_BLOBBASELINESTORE_H
#define KALBURATOR_BLOBBASELINESTORE_H

/**
 * @file blobbaselinestore.h
 * @brief SQLite-backed hash-per-record baseline store for BlobSyncEngine.
 *
 * Records the last-synced content hash for each (mapping, record) pair
 * (legacy API) or (backend, collection, record) triple (Phase D API),
 * enabling the engine to compute a correct 3-way diff and distinguish
 * "deleted since last sync" from "never existed on source."
 *
 * Lives in .kalburator-sync.db alongside sync_id_mappings (IDMappingStore).
 * Uses idempotent CREATE TABLE IF NOT
 * EXISTS; stamps PRAGMA user_version = 3 only on freshly-created DBs.
 *
 * Key shape (Phase D):
 *
 *   Legacy API  (mapping_id, record_id) — stored in the original
 *   blob_baselines table, unchanged.  Existing rows are fully preserved;
 *   legacy callers continue to work without modification.
 *
 *   Triple-key API  (backend_id, collection_id, record_id) — stored in a
 *   separate blob_baselines_triple table, created during schema migration.
 *   The migration is idempotent (CREATE TABLE IF NOT EXISTS).
 *
 * Not thread-safe. Callers must serialize access to a given instance.
 * Not a QObject — pure value-lifetime class with RAII connection
 * ownership.
 */

#include <QMap>
#include <QString>
#include <QStringList>

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

    // -----------------------------------------------------------------------
    // Legacy flat-keyed API — keyed by (mappingId, recordId).
    // Stored in blob_baselines table (unchanged from pre-Phase-D).
    // -----------------------------------------------------------------------

    /// Single-record set. Returns true on success; false on DB error
    /// (check lastError()).
    bool setBaseline(const QString &mappingId,
                     const QString &recordId,
                     const QString &contentHash);

    /// Returns the hash recorded for the mapping+record, or empty
    /// QString if no baseline has been set.
    QString baselineHash(const QString &mappingId,
                         const QString &recordId) const;

    /// Bulk commit wrapped in a transaction. Each entry in recordIdToHash
    /// is inserted-or-replaced for the mapping. Existing entries for
    /// records NOT in the map are left alone (use clearMapping() first
    /// if you want to replace wholesale).
    bool commitBaselines(const QString &mappingId,
                         const QMap<QString, QString> &recordIdToHash);

    /// All record IDs currently recorded for the mapping. Returns an
    /// empty list if no baselines exist for the mapping.
    QStringList baselineRecordIds(const QString &mappingId) const;

    /// Remove all rows for a mapping (e.g. on SyncMapping unbind).
    bool clearMapping(const QString &mappingId);

    // -----------------------------------------------------------------------
    // Triple-keyed API — keyed by (backendId, collectionId, recordId).
    // Stored in blob_baselines_triple table (added in Phase D migration).
    // Intended for calendar-side per-uid version hashes (Phase D Task 8).
    // -----------------------------------------------------------------------

    /// Single-record set keyed by (backendId, collectionId, recordId).
    bool setBaseline(const QString &backendId,
                     const QString &collectionId,
                     const QString &recordId,
                     const QString &contentHash);

    /// Returns the hash for the (backendId, collectionId, recordId) triple,
    /// or empty QString if none has been recorded.
    QString baselineHash(const QString &backendId,
                         const QString &collectionId,
                         const QString &recordId) const;

    /// Bulk commit for a (backendId, collectionId) scope.
    bool commitBaselines(const QString &backendId,
                         const QString &collectionId,
                         const QMap<QString, QString> &recordIdToHash);

    /// All record IDs for a (backendId, collectionId) scope.
    QStringList baselineRecordIds(const QString &backendId,
                                  const QString &collectionId) const;

    /// Remove all rows for a (backendId, collectionId) scope.
    bool clearCollection(const QString &backendId,
                         const QString &collectionId);

private:
    static int s_connectionCounter;

    QString         m_dbPath;
    QString         m_connName;
    bool            m_isOpen = false;
    mutable QString m_lastError;

    bool ensureSchemaAndVersion(bool dbFileExistedBefore);
    void setError(const QString &message) const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_BLOBBASELINESTORE_H
