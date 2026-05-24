#pragma once

#include <QList>

#include "propertycatalogue.h"

namespace Kalburator::Todo {

/// The todo+canon catalogue: VTODO ∪ Google Tasks ∪ Microsoft To-Do (schema doc §4).
Kalburator::Shape::PropertyCatalogue makeTodoCanonCatalogue();

/// The property ids of the canon catalogue (for the differ/merger). Excludes
/// `_canon` and `providerExtras` (handled specially by the envelope).
QList<Kalburator::Shape::PropertyId> todoCanonPropertyIds();

}  // namespace Kalburator::Todo
