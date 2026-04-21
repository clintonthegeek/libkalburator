#ifndef KALBURATOR_IDMAPPINGSTORE_H
#define KALBURATOR_IDMAPPINGSTORE_H

/**
 * @file idmappingstore.h
 * @brief SQLite-backed store for sync identity mappings.
 *
 * Persistent (source UID, recurrence ID) ↔ target ID mapping keyed
 * per backend, with optional calendar context, category annotations,
 * and an archived-soft-delete flag. Backs the sync_id_mappings table
 * in .planstan-sync.db, co-resident with SyncStore.
 *
 * Schema evolution: this class EXTENDS the sync_id_mappings table
 * created by SyncStore by adding four nullable columns
 * (last_synced, source_category, target_categories, archived) via
 * idempotent ALTER TABLE ADD COLUMN on open. Fresh DBs are stamped
 * with PRAGMA user_version = 3 to match SyncStore's schema-version
 * policy; any mismatch causes SyncStore to delete the DB file on
 * its next open.
 *
 * Pre-C.5 hazard: during the window where both SyncStore and this
 * class are live, a SyncStore::setIdMapping (INSERT OR REPLACE) on
 * a row this class has populated will reset the four WP columns
 * to their defaults (NULL / 0). No PS code currently populates those
 * columns, so the hazard has no data-loss surface until C.5 flips
 * the PS call sites.
 *
 * Not thread-safe. Callers must serialize access to a given instance.
 * Not a QObject — pure value-lifetime class with RAII connection
 * ownership.
 */

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace Kalburator::Sync {

struct IDMapping {
    QString     backendId;         ///< required, non-empty
    QString     sourceUid;         ///< required, non-empty
    QString     recurrenceId;      ///< "" for master / non-recurring
    QString     targetId;          ///< required, non-empty
    QString     calendarId;        ///< optional
    QDateTime   lastSynced;
    QString     sourceCategory;    ///< optional; Palm-shaped backends only
    QStringList targetCategories;  ///< optional
    bool        archived = false;

    bool isValid() const
    {
        return !backendId.isEmpty()
            && !sourceUid.isEmpty()
            && !targetId.isEmpty();
    }
};

class IDMappingStore
{
public:
    explicit IDMappingStore(const QString &dbPath);
    ~IDMappingStore();

    IDMappingStore(const IDMappingStore &) = delete;
    IDMappingStore &operator=(const IDMappingStore &) = delete;
    IDMappingStore(IDMappingStore &&) = delete;
    IDMappingStore &operator=(IDMappingStore &&) = delete;

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

    // --- Bulk accessor (full struct list) ---

    QList<IDMapping> allMappings(const QString &backendId) const;

    // --- WP-contributed category + archive methods ---

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
    static int s_connectionCounter;

    QString         m_dbPath;
    QString         m_connName;
    bool            m_isOpen = false;
    mutable QString m_lastError;

    bool ensureSchemaAndVersion(bool dbFileExistedBefore);
    bool ensureColumn(const QString &column, const QString &ddl);

    void setError(const QString &message) const;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_IDMAPPINGSTORE_H
