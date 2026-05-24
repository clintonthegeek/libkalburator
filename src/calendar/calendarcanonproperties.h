#pragma once

#include <QList>

#include "propertycatalogue.h"

namespace Kalburator::Calendar {

/// The calendar+canon catalogue: iCal VEVENT ∪ Google Calendar ∪ Microsoft
/// Graph event (schema doc §2).
Kalburator::Shape::PropertyCatalogue makeCalendarCanonCatalogue();

/// The property ids of the canon catalogue (for the differ/merger). Excludes
/// `_canon` and `providerExtras` (handled specially by the envelope).
QList<Kalburator::Shape::PropertyId> calendarCanonPropertyIds();

}  // namespace Kalburator::Calendar
