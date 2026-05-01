#ifndef IINCIDENCEREGISTRY_H
#define IINCIDENCEREGISTRY_H

#include <QString>
#include <QDateTime>
#include <QVector>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

#include "shape.h"

namespace Kalburator::Sync {

/**
 * @brief Abstract interface for incidence registration into the global model.
 *
 * Decouples producers (ProjectStore, sync backends) from the concrete
 * GlobalIncidenceModel so that planstan-planning can depend on
 * planstan-models without pulling in the full model implementation.
 */
class IIncidenceRegistry
{
public:
    virtual ~IIncidenceRegistry() = default;

    virtual bool addIncidence(const KCalendarCore::Incidence::Ptr &inc,
                              const QString &calendarId,
                              const QString &backendType,
                              KCalendarCore::MemoryCalendar *sourceCal,
                              Kalburator::Shape::Shape shape = Kalburator::Shape::Shape{}) = 0;

    virtual bool removeIncidenceFromCalendar(const QString &uid,
                                             const QString &calendarId) = 0;

    virtual bool removeIncidence(const QString &uid,
                                 const QString &calendarId,
                                 const QDateTime &recurrenceId) = 0;

    virtual bool updateIncidenceForCalendar(const KCalendarCore::Incidence::Ptr &inc,
                                            const QString &calendarId) = 0;

    /** Bulk load incidences for a calendar (replaces existing entries for that calendar) */
    virtual void setIncidencesForCalendar(const QString &calendarId,
                                          const QString &backendType,
                                          KCalendarCore::MemoryCalendar *sourceCalendar,
                                          const QVector<KCalendarCore::Incidence::Ptr> &incidences,
                                          Kalburator::Shape::Shape shape = Kalburator::Shape::Shape{}) = 0;

    /** Clear all incidences */
    virtual void clear() = 0;
};

} // namespace Kalburator::Sync

#endif // IINCIDENCEREGISTRY_H
