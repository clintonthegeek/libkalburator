#ifndef KALBURATOR_SYNCREQUEST_H
#define KALBURATOR_SYNCREQUEST_H

#include <kalburator/engine/syncengine.h>
#include <kalburator/types/synctypes.h>
#include <QList>
#include <QString>
#include <optional>

namespace Kalburator::Engine {

/**
 * @brief Canonical request object for SyncEngine::runSync.
 *
 * Architectural-redress Plan 1 Task 4 (2026-05-29) — collapses the four
 * former `runSyncFuture()` overloads into a single struct-parameterized
 * entry point. Plan 8 step 3 (2026-06-10) deleted those `[[deprecated]]`
 * overloads; runSync(SyncRequest) is now the sole sync entry.
 *
 * Three dispatch shapes, distinguished by `mappingIds`:
 *
 * - **All-enabled**: `mappingIds.isEmpty()` — run every enabled mapping.
 *   `executionOverride` applies to every dispatched mapping. Direction uses
 *   each mapping's declared source/target orientation.
 *
 * - **Subset**: `mappingIds.size() > 1` — run only the named mappings
 *   that are also enabled. The override applies to every named mapping.
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

    /// Per-call execution override. It applies to every dispatched mapping;
    /// direction follows each mapping's source/target orientation.
    std::optional<Kalburator::Sync::ExecutionOverride> executionOverride;

    /// True iff this request targets exactly one mapping.
    bool isSingleMapping() const { return mappingIds.size() == 1; }

    /// True iff this request targets every enabled mapping.
    bool isAllEnabled() const { return mappingIds.isEmpty(); }
};

} // namespace Kalburator::Engine

namespace Kalburator::Sync {
using SyncRequest = Kalburator::Engine::SyncRequest;
} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNCREQUEST_H
