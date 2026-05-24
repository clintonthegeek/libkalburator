#pragma once

#include <QList>

#include "propertycatalogue.h"

namespace Kalburator::Contacts {

/// The contacts+canon catalogue: vCard4 ∪ Google People (schema doc §3).
Kalburator::Shape::PropertyCatalogue makeContactsCanonCatalogue();

/// The property ids of the canon catalogue (for the differ/merger). Excludes
/// `_canon` and `providerExtras` (handled specially by the envelope).
QList<Kalburator::Shape::PropertyId> contactsCanonPropertyIds();

}  // namespace Kalburator::Contacts
