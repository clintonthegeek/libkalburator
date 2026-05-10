// src/calendar/syncconflictstore.h
#ifndef KALBURATOR_SYNCCONFLICTSTORE_H
#define KALBURATOR_SYNCCONFLICTSTORE_H

#include "synctypes.h"

#include <QObject>
#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace Kalburator::Sync {

/**
 * @brief SQLite-backed storage for sync conflict records.
 *
 * Carved out of the dissolving `SyncStore` during Phase D Task 9.
 * Owns the `sync_conflicts` table in the `.kalburator-sync.db` file.
 * All other SyncStore concerns have already moved to:
 *   - CalendarBaselineStore  — iCal / property baselines + last-sync time
 *   - Storage::BaselineStore — per-record version hashes
 *   - RemoteCalendarBackend::CTagStore — CalDAV CTags (private to RemoteCalendarBackend)
 *   - LocalBackend::FingerprintStore — directory fingerprints (private)
 *   - IDMappingStore         — journal / ID mappings
 */
class SyncConflictStore : public QObject
{
    Q_OBJECT

public:
    explicit SyncConflictStore(const QString &dbPath, QObject *parent = nullptr);
    ~SyncConflictStore() override;

    bool isOpen() const;
    QString lastError() const;
    QString databasePath() const { return m_dbPath; }

    // ---- Conflict CRUD ----

    /**
     * @brief Record a detected conflict.
     * @param conflict The conflict information.
     * @return Generated conflict ID, or empty string on failure.
     */
    QString recordConflict(const ConflictInfo &conflict);

    /**
     * @brief Get all unresolved conflicts for a sync mapping.
     * @param mappingId The sync mapping ID (empty for all mappings).
     */
    QList<ConflictInfo> unresolvedConflicts(const QString &mappingId = QString()) const;

    /**
     * @brief Get a specific conflict by ID.
     */
    ConflictInfo conflict(const QString &conflictId) const;

    /**
     * @brief Mark a conflict as resolved.
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

    /**
     * @brief Remove sync data for a specific backend (conflicts only).
     */
    void clearBackendData(const QString &backendId);

    /**
     * @brief Clear all conflict data for a specific sync mapping.
     */
    void clearMappingData(const QString &mappingId);

    /**
     * @brief Vacuum the database to reclaim space.
     */
    void vacuum();

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

    static int s_connectionCounter;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNCCONFLICTSTORE_H
