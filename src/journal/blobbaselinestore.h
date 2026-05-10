#ifndef KALBURATOR_BLOBBASELINESTORE_FORWARDING_SHIM_H
#define KALBURATOR_BLOBBASELINESTORE_FORWARDING_SHIM_H

// Phase K.5 forwarding shim. Removed in Phase K.5 Task 13 once
// PlanStan + WildPalms cut over to <storage/baselinestore.h>.
#include "../storage/baselinestore.h"

namespace Kalburator::Sync {
    using BlobBaselineStore = ::Kalburator::Storage::BaselineStore;
}

#endif
