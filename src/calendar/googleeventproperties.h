#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Calendar {

/// Returns a PropertyCatalogue covering the Google Calendar v3 `event`
/// field set that the google-event ⇄ canon stages read/write.
/// Each entry's PropertyId matches the Google JSON field name lowercased
/// (e.g. PropertyId{"icaluid"} ↔ iCalUID).
Kalburator::Shape::PropertyCatalogue makeGoogleEventCatalogue();

} // namespace Kalburator::Calendar
