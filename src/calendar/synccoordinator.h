#ifndef KALBURATOR_SYNCCOORDINATOR_DEPRECATION_SHIM_H
#define KALBURATOR_SYNCCOORDINATOR_DEPRECATION_SHIM_H

// Phase F1 Task 4 (2026-04-29): SyncCoordinator was renamed to SyncEngine
// and moved to src/engine/syncengine.h. This shim keeps PlanStan and
// WildPalms compiling until the consumer-migration tasks (F1 Tasks 9 +
// 12-13) land. F1 Task 13 deletes this file.
//
// Originally tried as `using SyncCoordinator = SyncEngine;` — that
// collided with consumer-side `class SyncCoordinator;` forward
// declarations (PlanStan src/app/{mainwindow,syncprogressmanager}.h
// etc.; WildPalms src/runtime/backendpluginmanager.h). A trivial
// derived class satisfies both forms while preserving Phase E
// behavior verbatim (inherited constructors; no overrides).
//
// Q_OBJECT is intentionally omitted: SyncEngine carries the meta-object,
// pointer-to-member signal/slot connections via &SyncCoordinator::xxx
// resolve to SyncEngine::xxx at the call site, and consumers don't
// downcast via qobject_cast<SyncCoordinator*>.

#include "syncengine.h"

namespace Kalburator::Sync {

class [[deprecated("renamed to SyncEngine in F1; "
                   "include engine/syncengine.h "
                   "(or syncengine.h via the public include directories)")]]
SyncCoordinator : public SyncEngine
{
public:
    using SyncEngine::SyncEngine;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNCCOORDINATOR_DEPRECATION_SHIM_H
