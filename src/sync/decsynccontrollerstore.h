#ifndef DECSYNCCONTROLLERSTORE_H
#define DECSYNCCONTROLLERSTORE_H

#include <QString>
#include <QMap>
#include <QList>
#include <QStringList>
#include <optional>

namespace Kalburator::Sync {

/**
 * @brief Record representing a baseline iCal snapshot for a UID.
 *
 * The baseline is the last authoritative iCal the controller wrote for a
 * given UID.  It is used to detect when a device's per-app copy diverges
 * from what the controller last published.
 */
struct BaselineRecord {
    QString icalData;
    QString writtenAt;
};

/**
 * @brief Record tracking when an app last wrote to a collection.
 *
 * Used by the garbage collector to decide which apps are still active
 * and whether a new app has appeared that needs onboarding.
 */
struct AppActivityRecord {
    QString appId;
    QString lastActive;
    QString lastCompacted;
};

/**
 * @brief Record noting that a UID was deleted by the controller.
 *
 * Kept so that when a new app joins the collection, the controller can
 * send it the deletion signals it missed.
 */
struct DeletionRecord {
    QString uid;
    QString deletedAt;
};

/**
 * @brief SQLite-backed persistence for the DecSync active controller.
 *
 * Manages three tables:
 *
 *   baselines      — last authoritative iCal per (collection_id, uid)
 *   app_activity   — per-app last-active / last-compacted timestamps
 *   deletion_log   — UIDs deleted by the controller (for new-app onboarding)
 *
 * The database file lives wherever the caller says; the controller places it
 * at `<decsyncDir>/.planstan-controller.db`.
 *
 * A unique QSqlDatabase connection name is generated at construction time
 * (via QUuid) so that multiple instances pointing at different files can
 * coexist safely within the same process.
 */
class DecSyncControllerStore
{
public:
    /**
     * @brief Construct a store for the given database path.
     * @param dbPath  Full path to the SQLite file (created if absent).
     *
     * The DB is NOT opened automatically — call open() before use.
     */
    explicit DecSyncControllerStore(const QString &dbPath);
    ~DecSyncControllerStore();

    // Non-copyable
    DecSyncControllerStore(const DecSyncControllerStore &) = delete;
    DecSyncControllerStore &operator=(const DecSyncControllerStore &) = delete;

    /**
     * @brief Open the database, enable WAL mode, and create tables.
     * @return true on success, false on failure (check lastError()).
     */
    bool open();

    /**
     * @brief Close the database and remove the Qt connection.
     */
    void close();

    /** @brief Whether the database is currently open. */
    bool isOpen() const { return m_isOpen; }

    /** @brief Last error message (empty when no error). */
    QString lastError() const { return m_lastError; }

    // ========================================================================
    // Baselines
    // ========================================================================

    /**
     * @brief Retrieve the baseline for a specific (collection, uid) pair.
     * @return BaselineRecord if found, std::nullopt otherwise.
     */
    std::optional<BaselineRecord> baseline(const QString &collectionId,
                                           const QString &uid) const;

    /**
     * @brief Insert or replace the baseline for a (collection, uid) pair.
     */
    void setBaseline(const QString &collectionId,
                     const QString &uid,
                     const QString &icalData,
                     const QString &writtenAt);

    /**
     * @brief Delete the baseline for a (collection, uid) pair.
     */
    void removeBaseline(const QString &collectionId, const QString &uid);

    /**
     * @brief Return all baselines for a collection, keyed by uid.
     */
    QMap<QString, BaselineRecord> allBaselines(const QString &collectionId) const;

    // ========================================================================
    // App Activity
    // ========================================================================

    /**
     * @brief Record that an app was active (upsert last_active timestamp).
     *
     * If the row does not exist it is created; if it does, only last_active
     * is updated (last_compacted is left unchanged).
     */
    void recordAppActivity(const QString &collectionId,
                           const QString &appId,
                           const QString &lastActive);

    /**
     * @brief Update the last_compacted timestamp for an app.
     *
     * Uses INSERT OR REPLACE so it also creates the row if missing.
     */
    void recordAppCompaction(const QString &collectionId,
                             const QString &appId,
                             const QString &lastCompacted);

    /**
     * @brief Retrieve activity record for a specific (collection, app) pair.
     * @return AppActivityRecord if found, std::nullopt otherwise.
     */
    std::optional<AppActivityRecord> appActivity(const QString &collectionId,
                                                 const QString &appId) const;

    /**
     * @brief Return all activity records for a collection.
     */
    QList<AppActivityRecord> allAppActivity(const QString &collectionId) const;

    /**
     * @brief Return app IDs that have not been active within the given number of days.
     */
    QStringList inactiveApps(const QString &collectionId, int days) const;

    /**
     * @brief Return app IDs from @p currentAppIds that the store has not seen before.
     *
     * "New" means there is no existing app_activity row for that app in the
     * given collection.
     */
    QStringList newApps(const QString &collectionId, const QStringList &currentAppIds) const;

    /**
     * @brief Delete the activity record for a (collection, app) pair.
     */
    void removeAppActivity(const QString &collectionId, const QString &appId);

    // ========================================================================
    // Deletion Log
    // ========================================================================

    /**
     * @brief Record that the controller deleted a UID from a collection.
     */
    void logDeletion(const QString &collectionId,
                     const QString &uid,
                     const QString &deletedAt);

    /**
     * @brief Remove a deletion log entry (e.g. after a new app has been caught up).
     */
    void removeDeletion(const QString &collectionId, const QString &uid);

    /**
     * @brief Return all deletion log entries for a collection.
     */
    QList<DeletionRecord> activeDeletions(const QString &collectionId) const;

private:
    bool createTables();
    void setError(const QString &error);

    QString m_dbPath;
    QString m_connectionName;
    QString m_lastError;
    bool m_isOpen = false;
};

} // namespace Kalburator::Sync

#endif // DECSYNCCONTROLLERSTORE_H
