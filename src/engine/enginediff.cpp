#include "enginediff.h"

namespace Kalburator::Sync {

bool EngineDiff::hasConflicts() const noexcept
{
    for (const auto& op : toSource) {
        if (op.kind == EngineDiffOp::Kind::Conflict) {
            return true;
        }
    }
    for (const auto& op : toTarget) {
        if (op.kind == EngineDiffOp::Kind::Conflict) {
            return true;
        }
    }
    return false;
}

int EngineDiff::totalOperations() const noexcept
{
    return toSource.size() + toTarget.size();
}

} // namespace Kalburator::Sync
