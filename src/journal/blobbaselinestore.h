#ifndef KALBURATOR_BLOBBASELINESTORE_H
#define KALBURATOR_BLOBBASELINESTORE_H

/**
 * @file blobbaselinestore.h
 * @brief SQLite-backed hash-per-record baseline store for BlobSyncEngine.
 *
 * Records the last-synced content hash for each (mapping, record)
 * pair, enabling the engine to compute a correct 3-way diff and
 * distinguish "deleted since last sync" from "never existed on source."
 *
 * Lives in .planstan-sync.db alongside sync_id_mappings (IDMappingStore)
 * and sync_store_* (SyncStore). Uses idempotent CREATE TABLE IF NOT
 * EXISTS; stamps PRAGMA user_version = 3 only on freshly-created DBs.
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
