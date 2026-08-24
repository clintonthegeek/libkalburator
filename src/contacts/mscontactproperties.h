#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Contacts {

/// Returns a PropertyCatalogue covering the Microsoft Graph v1.0 `contact`
/// field set that the ms-contact ⇄ canon stages read/write (EEE Phase 3).
/// PropertyIds match the Graph JSON field names lowercased.
Kalburator::Shape::PropertyCatalogue makeMsContactCatalogue();

} // namespace Kalburator::Contacts
