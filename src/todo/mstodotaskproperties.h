#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Todo {

/// Returns a PropertyCatalogue covering the Microsoft Graph v1.0 `todoTask`
/// field set that the ms-todotask ⇄ canon stages read/write (EEE Phase 3).
/// PropertyIds match the Graph JSON field names lowercased.
Kalburator::Shape::PropertyCatalogue makeMsTodoTaskCatalogue();

} // namespace Kalburator::Todo
