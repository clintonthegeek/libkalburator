#ifndef KALBURATOR_SYNCREQUEST_H
#define KALBURATOR_SYNCREQUEST_H

#include "syncengine.h"
#include "synctypes.h"
#include <QList>
#include <QString>
#include <optional>

namespace Kalburator::Engine {

/**
 * @brief Canonical request object for SyncEngine::runSync.
 *
 * Architectural-redress Plan 1 Task 4 (2026-05-29) — collapses the four
 * `runSyncFuture()` overloads into a single struct-parameterized entry
 * point. The four prior overloads are kept as `[[deprecated]]` shims
 * that construct a SyncRequest and call SyncEngine::runSync; they are
 * scheduled for removal in campaign Plan 8.
 *
 * Three dispatch shapes, distinguished by `mappingIds`:
 *
 * - **All-enabled**: `mappingIds.isEmpty()` — run every enabled mapping.
 *   `executionOverride` is ignored (per-mapping override is a
 *   single-mapping concept; the historical API only ever accepted it
 *   on the single-mapping overload).
 *
 * - **Subset**: `mappingIds.size() > 1` — run only the named mappings
 *   that are also enabled. `executionOverride` is similarly ignored.
 *
 * - **Single**: `mappingIds.size() == 1` — run exactly the named mapping.
 *   `executionOverride`, if set, applies one-way mirror semantics for
 *   this call only (does not mutate the mapping's persisted direction).
 */
struct SyncRequest {
    /// Mappings to dispatch. Empty = run all enabled mappings.
    /// Single element = single-mapping dispatch (where executionOverride
    /// applies). Multiple elements = subset dispatch.
    QList<QString> mappingIds;

    /// Conflict-handling behaviour for this run.
    SyncEngine::SyncBehavior behavior = SyncEngine::SyncBehavior::Unmonitored;

    /// Per-call execution override. Only meaningful when
    /// mappingIds.size() == 1. Ignored for all-enabled and subset
    /// dispatch (the historical API only ever accepted an override
    /// on the single-mapping overload).
    std::optional<Kalburator::Sync::ExecutionOverride> executionOverride;

    /// True iff this request targets exactly one mapping (the only
    /// dispatch shape that consults `executionOverride`).
    bool isSingleMapping() const { return mappingIds.size() == 1; }

    /// True iff this request targets every enabled mapping.
    bool isAllEnabled() const { return mappingIds.isEmpty(); }
};

} // namespace Kalburator::Engine

namespace Kalburator::Sync {
using SyncRequest = Kalburator::Engine::SyncRequest;
} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNCREQUEST_H
