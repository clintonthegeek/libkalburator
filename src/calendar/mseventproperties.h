#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Calendar {

/// Returns a PropertyCatalogue covering the Microsoft Graph v1.0 `event`
/// field set that the ms-event ⇄ canon stages read/write (EEE Phase 7.B).
/// Each entry's PropertyId matches the Graph JSON field name lowercased
/// (e.g. PropertyId{"icaluid"} ↔ iCalUId).
Kalburator::Shape::PropertyCatalogue makeMsEventCatalogue();

} // namespace Kalburator::Calendar
