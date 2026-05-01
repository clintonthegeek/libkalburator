#pragma once

#include "propertycatalogue.h"

namespace Kalburator::Contacts {

/// Returns a PropertyCatalogue for vCard 3.0 properties.
/// PropertyIds match vCard property names lowercased.
Kalburator::Shape::PropertyCatalogue makeVCardCatalogue();

} // namespace Kalburator::Contacts
