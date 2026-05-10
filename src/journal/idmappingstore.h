#ifndef KALBURATOR_IDMAPPINGSTORE_FORWARDING_SHIM_H
#define KALBURATOR_IDMAPPINGSTORE_FORWARDING_SHIM_H

// Phase K.5 forwarding shim. Removed in Phase K.5 Task 13.
#include "../storage/idmappingstore.h"

namespace Kalburator::Sync {
    using IDMappingStore = ::Kalburator::Storage::IDMappingStore;
    using IDMapping      = ::Kalburator::Storage::IDMapping;
}

#endif
