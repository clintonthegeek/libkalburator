#pragma once
#include "propertycatalogue.h"

namespace Kalburator::Outline {

/// Record-level canon catalogue for the outline domain. Per-node fields
/// live inside the `children` Json payload; tree-awareness is the differ's job.
Kalburator::Shape::PropertyCatalogue makeOutlineCanonCatalogue();

}  // namespace Kalburator::Outline
