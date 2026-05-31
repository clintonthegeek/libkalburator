#ifndef INCIDENCELOCK_REGISTRY_H
#define INCIDENCELOCK_REGISTRY_H

#include "incidencelock.h"
#include <QObject>
#include <QHash>
#include <QList>

namespace Kalburator::Sync {

/**
 * @brief Central registry for managing incidence edit locks.
 *
 * Prevents concurrent editing of the same incidence by multiple views/editors.
 * Locks are automatically released when the owner is destroyed.
 *
 * ## Usage
 *
 * @code
 * auto *registry = controller->lockRegistry();
 *
 * // Try to acquire lock
 * if (registry->tryLock(uid, this, "Proactive Editor: Meeting")) {
 *     // Lock acquired - proceed with editing
 * } else {
 *     // Lock failed - show conflict message
 *     auto info = registry->lockInfo(uid);
 *     QMessageBox::warning(this, "Locked",
 *         QString("Item locked by: %1").arg(info.ownerDescription));
 * }
 *
 * // Locks are auto-released on owner destruction
 * // Or manually:
 * registry->unlock(uid, this);
 * @endcode
 */
class IncidenceLockRegistry : public QObject
{
    Q_OBJECT

public:
    explicit IncidenceLockRegistry(QObject *parent = nullptr);
    ~IncidenceLockRegistry() override;

    /**
     * Try to acquire a lock on an incidence.
     *
     * @param uid Incidence UID to lock
     * @param owner Object that owns the lock (typically a view or editor)
     * @param description Human-readable description for UI (e.g., "Editor: Meeting")
     * @return true if lock acquired, false if already locked by someone else
     *
     * Notes:
     * - If already locked by the same owner, returns true (re-entrant)
     * - Lock is automatically released when owner is destroyed
     * - Emits itemLocked() on success, lockConflict() on failure
     */
    bool tryLock(const QString &uid, QObject *owner, const QString &description = QString());

    /**
     * Release a lock on an incidence.
     *
     * @param uid Incidence UID to unlock
     * @param owner Object that owns the lock (must match lock owner)
     * @return true if unlocked, false if not locked or owner mismatch
     *
     * Only the lock owner can release the lock. Attempting to unlock
     * someone else's lock will fail and return false.
     */
    bool unlock(const QString &uid, QObject *owner);

    /**
     * Check if an incidence is locked.
     *
     * @param uid Incidence UID to check
     * @return true if locked (and lock is still valid)
     */
    bool isLocked(const QString &uid) const;

    /**
     * Check if an incidence is locked by a specific owner.
     *
     * @param uid Incidence UID to check
     * @param owner Object to check ownership against
     * @return true if locked by this owner
     */
    bool isLockedBy(const QString &uid, QObject *owner) const;

    /**
     * Get lock information for an incidence.
     *
     * @param uid Incidence UID
     * @return Lock info (check isValid() to see if lock exists)
     */
    IncidenceLockInfo lockInfo(const QString &uid) const;

    /**
     * Try to acquire locks on multiple incidences atomically.
     *
     * @param uids List of incidence UIDs to lock
     * @param owner Object that will own all locks
     * @return true if ALL locks acquired, false if ANY failed
     *
     * This is all-or-nothing: if any lock fails, all are released.
     * Useful for multi-select editing.
     */
    bool tryLockAll(const QList<QString> &uids, QObject *owner);

    /**
     * Release all locks held by a specific owner.
     *
     * @param owner Object whose locks to release
     *
     * Useful when closing an editor or view that may have multiple locks.
     */
    void unlockAll(QObject *owner);

    /**
     * Get list of all locked UIDs.
     *
     * @return List of incidence UIDs currently locked
     *
     * Useful for debugging or UI display.
     */
    QStringList lockedUids() const;

    /**
     * Get count of active locks.
     *
     * @return Number of incidences currently locked
     */
    int lockCount() const { return m_locks.size(); }

signals:
    /**
     * Emitted when an incidence is successfully locked.
     *
     * @param uid Incidence UID that was locked
     * @param owner Object that acquired the lock
     */
    void itemLocked(const QString &uid, QObject *owner);

    /**
     * Emitted when an incidence is unlocked.
     *
     * @param uid Incidence UID that was unlocked
     */
    void itemUnlocked(const QString &uid);

    /**
     * Emitted when a lock attempt fails due to existing lock.
     *
     * @param uid Incidence UID that failed to lock
     * @param requestor Object that tried to acquire the lock
     * @param currentOwner Object that currently holds the lock
     */
    void lockConflict(const QString &uid, QObject *requestor, QObject *currentOwner);

private slots:
    /**
     * Called when a lock owner is destroyed.
     * Automatically releases all locks held by that owner.
     */
    void onOwnerDestroyed(QObject *owner);

private:
    QHash<QString, IncidenceLockInfo> m_locks;  ///< UID -> Lock info mapping

    /**
     * Clean up any invalid locks (owner destroyed but not yet removed).
     * Called before operations to ensure consistency.
     */
    void cleanupInvalidLocks();
};

} // namespace Kalburator::Sync

#endif // INCIDENCELOCK_REGISTRY_H
