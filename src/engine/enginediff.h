#ifndef KALBURATOR_ENGINE_ENGINEDIFF_H
#define KALBURATOR_ENGINE_ENGINEDIFF_H

#include "backendrecord.h"

#include <QList>
#include <QString>

namespace Kalburator::Sync {

/// Single operation produced by the engine's diff phase. The
/// engine inspects DiffOp at the control-flow level (kind counts,
/// conflict presence) but does not interpret payload bytes.
///
/// Distinct from SyncChange (src/transcoding/syncdiff.h), which is
/// calendar-typed (carries KCalendarCore::Incidence::Ptr). Engine
/// types stay BackendRecord-shaped per the F1 design decision.
struct EngineDiffOp
{
    enum class Kind {
        Create,    ///< new record on target side
        Update,    ///< modified record (source vs baseline differs)
        Delete,    ///< doomed record (in baseline, missing from source)
        Conflict   ///< both sides modified vs baseline
    };

    Kind          kind = Kind::Create;
    BackendRecord record;          ///< Create/Update: new state.
                                   ///  Delete: id-shaped doomed record.
                                   ///  Conflict: source-side record.
    BackendRecord targetRecord;    ///< Conflict: target-side record.
                                   ///  toTarget Update (sChanged && !tChanged):
                                   ///  target's current (unchanged) record, used
                                   ///  by MirrorBToA. Zero-initialized otherwise.
    BackendRecord baselineRecord;  ///< populated for Update/Delete/Conflict
};

/// Result of the engine's diff phase. Lists of operations to apply
/// to source side and target side. Engine drives apply via
/// unifiedContinueAfterConflicts which consumes EngineMerge.
struct EngineDiff
{
    QList<EngineDiffOp> toSource;  ///< operations to apply to source
    QList<EngineDiffOp> toTarget;  ///< operations to apply to target

    bool hasConflicts() const noexcept;
    int  totalOperations() const noexcept;
};

/// Result of the engine's merge phase. Conflicts have been resolved
/// per ConflictResolution policy; what remains is what to write.
struct EngineMerge
{
    QList<BackendRecord> finalSource;       ///< post-merge source state
    QList<BackendRecord> finalTarget;       ///< post-merge target state
    QList<BackendRecord> updatedBaselines;  ///< new baselines to persist
    int conflictsResolved = 0;
    int conflictsDeferred = 0;
};

/// Result of the engine's apply phase. Reports outcome and the
/// records actually written (for partial-failure recovery —
/// appliedBaselines contains baseline-shaped records for everything
/// that succeeded before any error).
struct EngineApplyResult
{
    bool    success = true;
    QString errorMessage;
    int     created = 0;
    int     updated = 0;
    int     deleted = 0;
    QList<BackendRecord> appliedBaselines;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_ENGINE_ENGINEDIFF_H
