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
 *   - Storage::BaselineStore  — record/property baselines + last-sync time
 *   - RemoteCalendarBackend::CTagStore — CalDAV CTags (private to RemoteCalendarBackend)
 *   - LocalBackend::FingerprintStore — directory fingerprints (private)
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
     * @brief One resolved-but-still-present conflict row.
     *
     * Bug B (conflict-resolution-repair Task 3): a row whose `resolution`
     * column is set but which nobody ever applied to any data. Until Task 3
     * every such row was permanent — the whole defect. SyncEngine rehydrates
     * these at run start so a resolution chosen before a restart still lands.
     */
    struct ResolvedConflict {
        ConflictInfo info;
        ConflictResolution resolution = ConflictResolution::AskUser;
        QDateTime resolvedAt;
    };

    /**
     * @brief Get resolved conflicts that are still in the table.
     *
     * Additive (Task 3): the pre-existing readers only ever asked for
     * UNRESOLVED rows, so a resolved row was invisible to every consumer and
     * simply accumulated. Ordered by resolved_at ASCENDING so a caller
     * folding these into a map keyed by (mapping, record) ends up with the
     * MOST RECENT resolution winning.
     *
     * @param mappingId Sync mapping ID (empty for all mappings).
     */
    QList<ResolvedConflict> resolvedConflicts(const QString &mappingId = QString()) const;

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
