#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Todo {

/// Returns a PropertyCatalogue covering the Google Tasks API `Task`
/// field set that the google-task ⇄ canon stages read/write (EEE Phase 3).
/// PropertyIds match the Tasks JSON field names lowercased.
Kalburator::Shape::PropertyCatalogue makeGoogleTaskCatalogue();

} // namespace Kalburator::Todo
