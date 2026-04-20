#ifndef CALENDARTYPE_H
#define CALENDARTYPE_H

#include <QMetaType>

/**
 * @brief Types of calendars a backend can support.
 *
 * CalDAV servers often restrict calendars to specific component types.
 * Local and org-mode backends typically support hybrid calendars.
 *
 * This enum is in its own header to avoid circular dependencies between
 * syncbackend.h and backendcapabilities.h.
 */
enum class CalendarType : int {
    Event = 0,  ///< VEVENT only (events calendar)
    Todo = 1,   ///< VTODO only (tasks calendar)
    Hybrid = 2  ///< Mixed VEVENT/VTODO in same calendar
};

Q_DECLARE_METATYPE(CalendarType)

#endif // CALENDARTYPE_H
