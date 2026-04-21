#include "incidencelock_registry.h"
#include <QDebug>

namespace Kalburator::Sync {

IncidenceLockRegistry::IncidenceLockRegistry(QObject *parent)
    : QObject(parent)
{
}

IncidenceLockRegistry::~IncidenceLockRegistry()
{
    // Clean up all locks
    m_locks.clear();
}

void IncidenceLockRegistry::cleanupInvalidLocks()
{
    // Remove locks whose owners have been destroyed
    auto it = m_locks.begin();
    while (it != m_locks.end()) {
        if (!it->isValid()) {
            QString uid = it.key();
            it = m_locks.erase(it);
            qDebug() << "IncidenceLockRegistry: Auto-removed invalid lock for" << uid;
            emit itemUnlocked(uid);
        } else {
            ++it;
        }
    }
}

bool IncidenceLockRegistry::tryLock(const QString &uid, QObject *owner, const QString &description)
{
    if (!owner) {
        qWarning() << "IncidenceLockRegistry::tryLock: null owner";
        return false;
    }

    cleanupInvalidLocks();

    // Check if already locked
    if (m_locks.contains(uid)) {
        const auto &info = m_locks[uid];

        if (!info.isValid()) {
            // Lock exists but owner destroyed - remove it
            m_locks.remove(uid);
            emit itemUnlocked(uid);
        } else if (info.owner == owner) {
            // Already locked by us - allow (re-entrant)
            qDebug() << "IncidenceLockRegistry: Re-entrant lock by same owner for" << uid;
            return true;
        } else {
            // Locked by someone else - conflict
            qDebug() << "IncidenceLockRegistry: Lock conflict for" << uid
                     << "- currently locked by" << info.ownerDescription;
            emit lockConflict(uid, owner, info.owner);
            return false;
        }
    }

    // Create new lock
    IncidenceLockInfo info;
    info.owner = owner;
    info.ownerDescription = description.isEmpty()
        ? QString("Object %1").arg(reinterpret_cast<quintptr>(owner), 0, 16)
        : description;
    info.acquiredAt = QDateTime::currentDateTime();

    m_locks.insert(uid, info);

    // Auto-cleanup when owner destroyed
    connect(owner, &QObject::destroyed, this, &IncidenceLockRegistry::onOwnerDestroyed,
            Qt::UniqueConnection);

    qDebug() << "IncidenceLockRegistry: Locked" << uid << "by" << info.ownerDescription;
    emit itemLocked(uid, owner);
    return true;
}

bool IncidenceLockRegistry::unlock(const QString &uid, QObject *owner)
{
    if (!owner) {
        qWarning() << "IncidenceLockRegistry::unlock: null owner";
        return false;
    }

    cleanupInvalidLocks();

    if (!m_locks.contains(uid)) {
        qDebug() << "IncidenceLockRegistry: Unlock failed - not locked:" << uid;
        return false;
    }

    const auto &info = m_locks[uid];

    if (info.owner != owner) {
        qWarning() << "IncidenceLockRegistry: Unlock failed - owner mismatch for" << uid
                   << "- locked by" << info.ownerDescription;
        return false;
    }

    m_locks.remove(uid);
    qDebug() << "IncidenceLockRegistry: Unlocked" << uid;
    emit itemUnlocked(uid);
    return true;
}

bool IncidenceLockRegistry::isLocked(const QString &uid) const
{
    if (!m_locks.contains(uid))
        return false;

    const auto &info = m_locks[uid];
    return info.isValid();
}

bool IncidenceLockRegistry::isLockedBy(const QString &uid, QObject *owner) const
{
    if (!owner || !m_locks.contains(uid))
        return false;

    const auto &info = m_locks[uid];
    return info.isValid() && info.owner == owner;
}

IncidenceLockInfo IncidenceLockRegistry::lockInfo(const QString &uid) const
{
    if (m_locks.contains(uid)) {
        return m_locks[uid];
    }

    // Return invalid lock info
    IncidenceLockInfo invalid;
    return invalid;
}

bool IncidenceLockRegistry::tryLockAll(const QList<QString> &uids, QObject *owner)
{
    if (!owner) {
        qWarning() << "IncidenceLockRegistry::tryLockAll: null owner";
        return false;
    }

    cleanupInvalidLocks();

    // First, check if all are available
    QList<QString> conflictUids;
    for (const QString &uid : uids) {
        if (m_locks.contains(uid)) {
            const auto &info = m_locks[uid];
            if (info.isValid() && info.owner != owner) {
                conflictUids.append(uid);
            }
        }
    }

    if (!conflictUids.isEmpty()) {
        qDebug() << "IncidenceLockRegistry: tryLockAll failed - conflicts on" << conflictUids;
        // Emit conflict for first one
        if (!conflictUids.isEmpty()) {
            const auto &info = m_locks[conflictUids.first()];
            emit lockConflict(conflictUids.first(), owner, info.owner);
        }
        return false;
    }

    // All available - acquire all locks
    QString desc = QString("Bulk lock (%1 items)").arg(uids.size());
    for (const QString &uid : uids) {
        tryLock(uid, owner, desc);
    }

    qDebug() << "IncidenceLockRegistry: Successfully locked all" << uids.size() << "items";
    return true;
}

void IncidenceLockRegistry::unlockAll(QObject *owner)
{
    if (!owner) {
        qWarning() << "IncidenceLockRegistry::unlockAll: null owner";
        return;
    }

    QList<QString> uidsToUnlock;

    // Find all locks owned by this object
    for (auto it = m_locks.constBegin(); it != m_locks.constEnd(); ++it) {
        if (it->owner == owner) {
            uidsToUnlock.append(it.key());
        }
    }

    // Unlock them
    for (const QString &uid : uidsToUnlock) {
        m_locks.remove(uid);
        emit itemUnlocked(uid);
    }

    if (!uidsToUnlock.isEmpty()) {
        qDebug() << "IncidenceLockRegistry: Unlocked all" << uidsToUnlock.size()
                 << "items for owner" << owner;
    }
}

QStringList IncidenceLockRegistry::lockedUids() const
{
    QStringList uids;
    for (auto it = m_locks.constBegin(); it != m_locks.constEnd(); ++it) {
        if (it->isValid()) {
            uids.append(it.key());
        }
    }
    return uids;
}

void IncidenceLockRegistry::onOwnerDestroyed(QObject *owner)
{
    qDebug() << "IncidenceLockRegistry: Owner destroyed" << owner << "- releasing locks";
    unlockAll(owner);
}


} // namespace Kalburator::Sync
