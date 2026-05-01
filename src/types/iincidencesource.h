#ifndef IINCIDENCESOURCE_H
#define IINCIDENCESOURCE_H

#include <QString>
#include <QDateTime>
#include <QSharedPointer>
#include <QVector>

#include "shape.h"

namespace KCalendarCore {
class Incidence;
}

namespace Kalburator::Sync {

/**
 * @brief Read-only interface for iterating over incidences.
 *
 * Scheduling needs to scan incidences (for determinacy detection and
 * container discovery) but must not depend on GlobalIncidenceModel directly.
 * The app wires up a concrete implementation at startup.
 */
class IIncidenceSource
{
public:
    struct Entry {
        QSharedPointer<KCalendarCore::Incidence> incidence;
        QString calendarId;
        QString backendType;
        Kalburator::Shape::Shape shape;
    };

    virtual ~IIncidenceSource() = default;

    virtual int entryCount() const = 0;
    virtual Entry entryAt(int index) const = 0;
    virtual Entry lookupEntry(const QString &uid,
                              const QString &calendarId) const = 0;

    virtual Entry lookupEntry(const QString &uid,
                              const QString &calendarId,
                              const QDateTime &recurrenceId) const = 0;

    /** Get all entries matching a UID (may be multiple across calendars). */
    virtual QVector<Entry> lookupEntriesForUid(const QString &uid) const = 0;
};

} // namespace Kalburator::Sync

#endif // IINCIDENCESOURCE_H
