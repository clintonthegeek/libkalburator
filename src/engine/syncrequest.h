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
 *   `executionOverride.direction` is ignored (per-mapping direction is a
 *   single-mapping concept; the historical API only ever accepted it
 *   on the single-mapping overload). `executionOverride.clobber` DOES
 *   apply — every dispatched mapping runs the clobber semantics
 *   independently.
 *
 * - **Subset**: `mappingIds.size() > 1` — run only the named mappings
 *   that are also enabled. `direction` is similarly ignored; `clobber`
 *   applies to every named mapping.
 *
 * - **Single**: `mappingIds.size() == 1` — run exactly the named mapping.
 *   `executionOverride`, if set, applies one-way mirror semantics for
 *   this call only (does not mutate the mapping's persisted direction).
 */
struct SyncRequest {
    /// Mappings to dispatch. Empty = run all enabled mappings.
    /// Single element = single-mapping dispatch (where executionOverride
    /// applies in full). Multiple elements = subset dispatch.
    QList<QString> mappingIds;

    /// Conflict-handling behaviour for this run.
    SyncEngine::SyncBehavior behavior = SyncEngine::SyncBehavior::Unmonitored;

    /// Per-call execution override. `direction` is only meaningful when
    /// mappingIds.size() == 1 and is ignored for all-enabled and subset
    /// dispatch (the historical API only ever accepted a direction
    /// override on the single-mapping overload). `clobber` is broader:
    /// it applies to EVERY dispatched mapping regardless of shape — each
    /// mapping is wiped-then-repushed independently (see
    /// ExecutionOverride::clobber).
    std::optional<Kalburator::Sync::ExecutionOverride> executionOverride;

    /// True iff this request targets exactly one mapping (the only
    /// dispatch shape that consults `executionOverride` in full;
    /// multi-mapping shapes consult only the `clobber` flag).
    bool isSingleMapping() const { return mappingIds.size() == 1; }

    /// True iff this request targets every enabled mapping.
    bool isAllEnabled() const { return mappingIds.isEmpty(); }
};

} // namespace Kalburator::Engine

namespace Kalburator::Sync {
using SyncRequest = Kalburator::Engine::SyncRequest;
} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNCREQUEST_H
