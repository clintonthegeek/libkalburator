#ifndef SYNCSTORE_H
#define SYNCSTORE_H

#include "synctypes.h"

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QSqlDatabase>
#include <QList>
#include <QMap>

namespace Kalburator::Sync {

/**
 * @brief SQLite-backed storage for sync metadata.
 *
 * SyncStore manages persistent storage of:
 * - Identity mappings (local UID <-> remote identifier)
 * - Version hashes (for change detection)
 * - Baseline data (last synced state for 3-way merge)
 * - Unresolved conflicts
 *
 * This is separate from .kalb configuration files to:
 * - Avoid bloating the human-readable config
 * - Support atomic database operations
 * - Enable efficient queries on large datasets
 *
 * The database is stored alongside the .kalb file as:
 *   <collection-root>/.planstan-sync.db
 */
class SyncStore : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a SyncStore with the given database path.
     * @param dbPath Path to SQLite database file (created if doesn't exist)
     * @param parent Parent QObject
     */
    explicit SyncStore(const QString &dbPath, QObject *parent = nullptr);
    ~SyncStore() override;

    /**
     * @brief Check if the database was opened successfully.
     */
    bool isOpen() const;

    /**
     * @brief Get the last error message.
     */
    QString lastError() const;

    /**
     * @brief Get the database path.
     */
    QString databasePath() const { return m_dbPath; }

    // ========================================================================
    // Version Tracking
    // ========================================================================
    // Stores version hashes (ETags, content hashes) for change detection.
    // When a version hash changes, we know the incidence was modified.

    /**
     * @brief Get the version hash for an incidence.
     * @param backendId The backend ID
     * @param calendarId The calendar ID
     * @param uid The incidence UID
     * @return Version hash, or empty string if not tracked
     */
    QString versionHash(const QString &backendId,
                        const QString &calendarId,
                        const QString &uid) const;

    /**
     * @brief Set the version hash for an incidence.
     * @param backendId The backend ID
     * @param calendarId The calendar ID
     * @param uid The incidence UID
     * @param hash The version hash (ETag, content hash, etc.)
     */
    void setVersionHash(const QString &backendId,
                        const QString &calendarId,
                        const QString &uid,
                        const QString &hash);

    /**
     * @brief Remove version tracking for an incidence.
     */
    void removeVersionHash(const QString &backendId,
                           const QString &calendarId,
                           const QString &uid);

    /**
     * @brief Get all version hashes for a calendar.
     * @param backendId The backend ID
     * @param calendarId The calendar ID
     * @return Map of uid -> versionHash
     */
    QMap<QString, QString> allVersionHashes(const QString &backendId,
                                             const QString &calendarId) const;

    /**
     * @brief Clear all version hashes for a backend/calendar.
     */
    void clearVersionHashes(const QString &backendId,
                            const QString &calendarId = QString());

    // ========================================================================
    // Baseline Storage
    // ========================================================================
    // Stores the last synced state of incidences for 3-way merge.
    // The baseline is the "common ancestor" used to detect conflicting changes.

    /**
     * @brief Get the baseline iCal data for an incidence in a sync mapping.
     * @param mappingId The sync mapping ID
     * @param uid The incidence UID
     * @return iCal data, or empty string if no baseline
     */
    QString baseline(const QString &mappingId, const QString &uid) const;

    /**
     * @brief Set the baseline for an incidence.
     * @param mappingId The sync mapping ID
     * @param uid The incidence UID
     * @param icalData The iCal representation at sync time
     */
    void setBaseline(const QString &mappingId,
                     const QString &uid,
                     const QString &icalData);

    /**
     * @brief Set multiple baselines in a single transaction.
     * Much more efficient than calling setBaseline repeatedly.
     * @param mappingId The sync mapping ID
     * @param baselines Map of uid -> icalData
     */
    void setBaselines(const QString &mappingId,
                      const QMap<QString, QString> &baselines);

    /**
     * @brief Remove a baseline.
     */
    void removeBaseline(const QString &mappingId, const QString &uid);

    /**
     * @brief Remove multiple baselines in a single transaction.
     * @param mappingId The sync mapping ID
     * @param uids List of UIDs to remove
     */
    void removeBaselines(const QString &mappingId, const QStringList &uids);

    /**
     * @brief Get all baselines for a sync mapping.
     * @param mappingId The sync mapping ID
     * @return Map of uid -> icalData
     */
    QMap<QString, QString> allBaselines(const QString &mappingId) const;

    /**
     * @brief Clear all baselines for a sync mapping.
     */
    void clearBaselines(const QString &mappingId);

    /**
     * @brief Get the last sync time for a mapping.
     * @param mappingId The sync mapping ID
     * @return Last sync datetime, or invalid datetime if never synced
     */
    QDateTime lastSyncTime(const QString &mappingId) const;

    /**
     * @brief Update the last sync time for a mapping.
     */
    void setLastSyncTime(const QString &mappingId, const QDateTime &time);

    // ========================================================================
    // Property Baselines
    // ========================================================================
    // Stores calendar-level property baselines (color, description) for 3-way merge.
    // Similar to incidence baselines, but for calendar metadata.

    /**
     * @brief Get the baseline property JSON for a calendar in a sync mapping.
     * @param mappingId The sync mapping ID
     * @param calendarId The calendar ID
     * @return JSON properties, or empty string if no baseline
     */
    QString propertyBaseline(const QString &mappingId, const QString &calendarId) const;

    /**
     * @brief Set the property baseline for a calendar.
     * @param mappingId The sync mapping ID
     * @param calendarId The calendar ID
     * @param propertiesJson The JSON representation of properties
     */
    void setPropertyBaseline(const QString &mappingId,
                            const QString &calendarId,
                            const QString &propertiesJson);

    /**
     * @brief Remove a property baseline.
     */
    void removePropertyBaseline(const QString &mappingId, const QString &calendarId);

    /**
     * @brief Get all property baselines for a sync mapping.
     * @param mappingId The sync mapping ID
     * @return Map of calendarId -> propertiesJson
     */
    QMap<QString, QString> allPropertyBaselines(const QString &mappingId) const;

    // ========================================================================
    // CTag Tracking (Collection Tags for CalDAV sync optimization)
    // ========================================================================

    /**
     * @brief Get the stored CTag for a calendar.
     * @param backendId The backend ID (e.g., "caldav-secondary")
     * @param calendarId The calendar ID
     * @return Stored CTag, or empty string if not cached
     */
    QString ctag(const QString &backendId, const QString &calendarId) const;

    /**
     * @brief Store a CTag for a calendar.
     * @param backendId The backend ID
     * @param calendarId The calendar ID
     * @param ctag The CTag value from the server
     */
    void setCtag(const QString &backendId, const QString &calendarId, const QString &ctag);

    /**
     * @brief Remove the stored CTag for a calendar.
     * @param backendId The backend ID
     * @param calendarId The calendar ID
     */
    void clearCtag(const QString &backendId, const QString &calendarId);

    /**
     * @brief Remove all stored CTags for a backend.
     * @param backendId The backend ID
     */
    void clearCtags(const QString &backendId);

    // ========================================================================
    // Conflict Tracking
    // ========================================================================
    // Records detected conflicts for later resolution by the user.

    /**
     * @brief Record a detected conflict.
     * @param conflict The conflict information
     * @return Generated conflict ID
     */
    QString recordConflict(const ConflictInfo &conflict);

    /**
     * @brief Get all unresolved conflicts for a sync mapping.
     * @param mappingId The sync mapping ID (empty for all conflicts)
     * @return List of unresolved conflicts
     */
    QList<ConflictInfo> unresolvedConflicts(const QString &mappingId = QString()) const;

    /**
     * @brief Get a specific conflict by ID.
     * @param conflictId The conflict ID
     * @return Conflict info, or empty struct if not found
     */
    ConflictInfo conflict(const QString &conflictId) const;

    /**
     * @brief Mark a conflict as resolved.
     * @param conflictId The conflict ID
     * @param resolution The resolution that was applied
     */
    void resolveConflict(const QString &conflictId, ConflictResolution resolution);

    /**
     * @brief Remove a conflict record.
     */
    void removeConflict(const QString &conflictId);

    /**
     * @brief Get count of unresolved conflicts.
     */
    int unresolvedConflictCount(const QString &mappingId = QString()) const;

    // ========================================================================
    // Database Maintenance
    // ========================================================================

    /**
     * @brief Vacuum the database to reclaim space.
     */
    void vacuum();

    /**
     * @brief Remove SyncStore-owned sync data for a specific backend.
     *
     * Clears version hashes and conflicts for the backend. Does NOT
     * clear identity mappings; use IDMappingStore::clearIdMappings for
     * that.
     */
    void clearBackendData(const QString &backendId);

    /**
     * @brief Clear all data for a specific sync mapping.
     * Removes baselines, conflicts, and sync timestamps.
     */
    void clearMappingData(const QString &mappingId);

signals:
    /**
     * @brief Emitted when a conflict is recorded.
     */
    void conflictRecorded(const ConflictInfo &conflict);

    /**
     * @brief Emitted when a conflict is resolved.
     */
    void conflictResolved(const QString &conflictId);

private:
    bool initDatabase();
    bool createTables();
    void setError(const QString &error);

    QString m_dbPath;
    QString m_connectionName;
    QString m_lastError;
    bool m_isOpen = false;

    // Use a unique connection name to support multiple SyncStore instances
    static int s_connectionCounter;
};

} // namespace Kalburator::Sync

#endif // SYNCSTORE_H
