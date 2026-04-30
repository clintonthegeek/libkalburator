#ifndef KALBURATOR_BLOBBASELINESTORE_H
#define KALBURATOR_BLOBBASELINESTORE_H

/**
 * @file blobbaselinestore.h
 * @brief SQLite-backed hash-per-record baseline store for the blob sync facade.
 *
 * Records the last-synced content hash for each
 * (backendId, collectionId, recordId) triple, enabling the engine to
 * compute a correct 3-way diff and distinguish "deleted since last sync"
 * from "never existed on source."
 *
 * Lives in .kalburator-sync.db alongside sync_id_mappings (IDMappingStore).
 * Uses idempotent CREATE TABLE IF NOT EXISTS; stamps PRAGMA user_version = 3
 * only on freshly-created DBs.
 *
 * Key shape:
 *
 *   (backend_id, collection_id, record_id) — stored in the canonical
 *   blob_baselines table. (Phase F1 Task 11 dropped the legacy flat
 *   (mapping_id, record_id) API + table; the prior triple-keyed
 *   blob_baselines_triple table was renamed to blob_baselines as the
 *   canonical store. On-disk DBs from before this change have their
 *   triple-keyed data preserved; flat-keyed data is dropped and the
 *   first sync after upgrade falls into "first sync" semantics — see
 *   docs/phase0/04p-phase-f1-unify-design.md.)
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
    // Triple-keyed API — keyed by (backendId, collectionId, recordId).
    // Stored in the canonical blob_baselines table.
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
