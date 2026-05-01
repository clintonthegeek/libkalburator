#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Calendar {

/// Returns a PropertyCatalogue covering the KCalendarCore::Incidence
/// property set. Each entry's PropertyId matches the iCal property
/// name lowercased (e.g. PropertyId{"summary"} ↔ SUMMARY).
Kalburator::Shape::PropertyCatalogue makeICalCatalogue();

} // namespace Kalburator::Calendar
