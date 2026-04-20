#ifndef INCIDENCELOCK_H
#define INCIDENCELOCK_H

#include <QObject>
#include <QPointer>
#include <QDateTime>
#include <QString>

/**
 * @brief Information about a locked incidence.
 *
 * Stores who owns the lock, when it was acquired, and a human-readable
 * description for UI display.
 *
 * Uses QPointer for automatic cleanup when the owner is destroyed.
 */
struct IncidenceLockInfo
{
    QPointer<QObject> owner;        ///< Auto-nulls when owner destroyed
    QString ownerDescription;       ///< Human-readable: "Editor: Meeting"
    QDateTime acquiredAt;           ///< When the lock was acquired

    /**
     * Check if the lock is still valid.
     * @return true if the owner still exists, false if it was destroyed
     */
    bool isValid() const { return !owner.isNull(); }

    /**
     * Get a human-friendly string describing this lock.
     * @return Formatted string with owner and timestamp
     */
    QString toString() const {
        if (!isValid()) {
            return QStringLiteral("(Invalid lock - owner destroyed)");
        }
        return QString("%1 (acquired at %2)")
            .arg(ownerDescription)
            .arg(acquiredAt.toString(Qt::ISODate));
    }
};

#endif // INCIDENCELOCK_H
