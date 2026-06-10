#ifndef KALBURATOR_SYNC_CALDAVCONTENTTYPES_H
#define KALBURATOR_SYNC_CALDAVCONTENTTYPES_H

#include "backendconfiguration.h"
#include <KDAV/DavCollection>

namespace Kalburator::Sync {

// Synthesize KDAV content-type flags from the discovered per-component bools.
// The discovery walk records supportsVEvent/VTodo as booleans; backends and
// the network loadCalendars path use the KDAV bitmask.
inline KDAV::DavCollection::ContentTypes contentTypesFromCaps(const PerCalendarCapabilities &c)
{
    KDAV::DavCollection::ContentTypes types;
    if (c.supportsVEvent) types |= KDAV::DavCollection::Events;
    if (c.supportsVTodo)  types |= KDAV::DavCollection::Todos;
    return types;
}

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_CALDAVCONTENTTYPES_H
