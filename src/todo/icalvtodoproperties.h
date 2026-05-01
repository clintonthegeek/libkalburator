#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Todo {

/// Returns a PropertyCatalogue covering the VTODO property set.
/// PropertyIds match vCal property names lowercased.
Kalburator::Shape::PropertyCatalogue makeVTodoCatalogue();

} // namespace Kalburator::Todo
