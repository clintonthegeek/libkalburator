#ifndef INCIDENCEREF_H
#define INCIDENCEREF_H

#include <QString>
#include <QDateTime>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

/**
 * @brief Reference to an incidence within a specific calendar.
 *
 * This lightweight struct pairs an incidence pointer with its
 * containing calendar ID, used throughout the application for
 * selection, drag-drop, and editing operations.
 *
 * For recurring incidences, occurrenceDateTime can optionally specify
 * which particular occurrence is being referenced. This is used to
 * prompt the user whether to edit the master or just this occurrence.
 */
struct IncidenceRef
{
    QString calendarId;
    KCalendarCore::Incidence::Ptr incidence;

    /// For recurring incidences: the start time of the specific occurrence.
    /// If invalid, refers to the master incidence (or a non-recurring incidence).
    QDateTime occurrenceDateTime;

    bool isValid() const {
        return !calendarId.isEmpty() && incidence;
    }

    /// Returns true if this refers to a specific occurrence of a recurring incidence
    bool isOccurrence() const {
        return incidence && incidence->recurs() && occurrenceDateTime.isValid();
    }

    bool operator==(const IncidenceRef &other) const {
        return calendarId == other.calendarId &&
               incidence == other.incidence &&
               occurrenceDateTime == other.occurrenceDateTime;
    }

    bool operator!=(const IncidenceRef &other) const {
        return !(*this == other);
    }
};

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::IncidenceRef)
Q_DECLARE_METATYPE(QList<Kalburator::Sync::IncidenceRef>)

#endif // INCIDENCEREF_H
