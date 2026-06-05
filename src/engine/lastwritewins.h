#ifndef KALBURATOR_ENGINE_LASTWRITEWINS_H
#define KALBURATOR_ENGINE_LASTWRITEWINS_H

#include <QDateTime>

namespace Kalburator::Sync {

/**
 * @brief LastWriteWins tie-break: does the source side win?
 *
 * Single source of truth for the LWW comparison so the SyncEngine unified-merge
 * path and ConflictManager::applyAutoPolicy agree (they previously used `>=`
 * and `>` respectively — a true modify-modify tie resolved differently between
 * them; see PlanStan bug doc sync-conflicts-lastwritewins-tie-bias.md).
 *
 * Semantics (relying on Qt6 QDateTime ordering, where an invalid datetime sorts
 * below any valid one):
 *   - modify-delete: the deleted side carries an invalid lastModified, so the
 *     modifier (valid) wins — `valid > invalid` is true, `invalid > valid` is
 *     false.
 *   - true modify-modify tie (both valid and equal): returns false, i.e. the
 *     TARGET keeps its value. The tie outcome is inherently arbitrary; `>`
 *     (target-on-tie) is chosen so the later/destination write is not silently
 *     clobbered by the source, and so this matches ConflictManager.
 */
inline bool lastWriteWinsPrefersSource(const QDateTime &sourceModified,
                                       const QDateTime &targetModified)
{
    return sourceModified > targetModified;
}

} // namespace Kalburator::Sync

#endif // KALBURATOR_ENGINE_LASTWRITEWINS_H
