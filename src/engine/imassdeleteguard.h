#ifndef KALBURATOR_SYNC_IMASSDELETEGUARD_H
#define KALBURATOR_SYNC_IMASSDELETEGUARD_H

#include <QString>

namespace Kalburator::Sync {

/// Synchronous gate for bulk-delete operations during sync.
///
/// SyncEngine consults the registered guard before allowing a single
/// mapping's write phase to propagate a large number of deletes to its
/// target backend. The library trips the gate when the proposed delete
/// list exceeds either of two thresholds (per mapping, per sync):
///   - absolute: more than 10 deletes; OR
///   - relative: more than 25% of the mapping's current baseline count.
///
/// If no guard is registered, deletes proceed unconditionally (backward
/// compatible with consumers that don't opt in).
///
/// If the guard returns false, the engine drops the delete list for that
/// mapping this round and proceeds with creates/updates only. Baselines
/// are unchanged; the next sync re-proposes the same deletes.
///
/// Threading: the engine calls `confirmMassDelete` from a worker thread.
/// Concrete implementations that need to interact with a GUI must
/// marshal to the UI thread themselves (Qt::BlockingQueuedConnection
/// or equivalent).
class IMassDeleteGuard
{
public:
    virtual ~IMassDeleteGuard() = default;

    /// Return true to allow the deletes; false to skip them this round.
    /// @param mappingId       SyncMapping::id (e.g. "default-contacts-palm_contact_0")
    /// @param targetBackendId Backend the deletes would apply to
    /// @param proposedDeletes Number of records the engine wants to delete
    /// @param baselineCount   Number of records in the mapping's baseline
    virtual bool confirmMassDelete(const QString &mappingId,
                                   const QString &targetBackendId,
                                   int proposedDeletes,
                                   int baselineCount) = 0;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_IMASSDELETEGUARD_H
