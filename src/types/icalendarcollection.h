#ifndef ICALENDARCOLLECTION_H
#define ICALENDARCOLLECTION_H

#include <QColor>
#include <QList>
#include <QString>
#include <KCalendarCore/MemoryCalendar>

namespace Kalburator::Sync {

/**
 * @brief Host contract for the sync engine's view of a calendar collection.
 *
 * The sync engine (currently `libs/sync/`, destined for the libkalburator
 * extraction) does not need the full PlanStan `Collection` API. It needs
 * six narrow accessors/mutators. This interface captures that narrow
 * surface so a non-PlanStan host (e.g. Wild Palms in Full Sync Mode) can
 * implement it without reproducing PlanStan's calendar-collection model.
 *
 * PlanStan's `Collection` implements this interface. A reuse host provides
 * its own implementation over whatever data structure it uses to
 * aggregate MemoryCalendars.
 *
 * See `~/dev/libkalburator/docs/phase0/04-merged-interface-sketch.md` for
 * the library-side target shape (`Kalburator::Sync::ICalendarCollection`,
 * which will accept this surface nearly unchanged).
 */
class ICalendarCollection
{
public:
    virtual ~ICalendarCollection() = default;

    /// Stable identifier for this collection.
    virtual QString id() const = 0;

    /// Return the calendar bound to @a calendarId, or nullptr if none.
    virtual KCalendarCore::MemoryCalendar* calendar(const QString &calendarId) const = 0;

    /// Return every calendar currently in the collection.
    virtual QList<KCalendarCore::MemoryCalendar*> calendars() const = 0;

    /// Add a calendar to the collection. Implementations are expected to
    /// take ownership (Qt parent, shared-ptr, etc.) — callers should not
    /// re-parent or delete after adding.
    virtual void addCalendar(KCalendarCore::MemoryCalendar *calendar) = 0;

    /// Update the display colour for a calendar. No-op if the calendar
    /// is not present.
    virtual void setCalendarColor(const QString &calendarId, const QColor &color) = 0;

    /// Update the per-user visibility flag for a calendar. No-op if the
    /// calendar is not present.
    virtual void setCalendarVisible(const QString &calendarId, bool visible) = 0;
};

} // namespace Kalburator::Sync

#endif // ICALENDARCOLLECTION_H
