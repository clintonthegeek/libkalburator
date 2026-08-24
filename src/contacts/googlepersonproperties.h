#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Contacts {

/// Returns a PropertyCatalogue covering the Google People API `Person`
/// field set that the google-person ⇄ canon stages read/write (EEE Phase 3).
/// PropertyIds match the Google JSON field names lowercased.
Kalburator::Shape::PropertyCatalogue makeGooglePersonCatalogue();

} // namespace Kalburator::Contacts
