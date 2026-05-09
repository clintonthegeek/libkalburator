#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Kalburator::Sync {

/// Result of a 3-way QVariantMap diff over (source, target, baseline).
/// Used by SyncEngineWorker's generic property phase (Phase Ia.5 Task 7).
///
/// Convention: "key absent from a map" is treated as "not set" — represented
/// internally as an invalid QVariant. The caller is responsible for using
/// the omit-if-unset convention when constructing the input maps (see
/// KalburatorDomainCalendar::collectionProperties for the canonical usage).
///
/// Limitation (v1, Task 7): removal of a previously-set property is NOT
/// modelled. If a key was in `base` but absent from `src`, computeMapDiff
/// will treat it as a change-to-invalid; whether the consumer interprets
/// that as "remove" is a downstream concern. Task 13's integration work
/// will revisit this when the calendar-typed path is deleted.
///
/// Note: named MapPropertyDiff (not PropertyDiff) to avoid ODR conflict with
/// the unrelated Kalburator::Sync::PropertyDiff in transcoding/incidencediff.h.
struct MapPropertyDiff {
    QVariantMap toApplyToTarget;  ///< Changes that should propagate to target.
    QVariantMap toApplyToSource;  ///< Changes that should propagate to source (TwoWay only).
    QStringList conflicts;        ///< Keys where both src and tgt diverged from baseline differently.

    bool hasChanges() const {
        return !toApplyToTarget.isEmpty() || !toApplyToSource.isEmpty();
    }
};

/// 3-way diff over the union of keys in (src, tgt, base).
///
/// For each key:
/// - srcChanged = (srcVal != baseVal), tgtChanged = (tgtVal != baseVal)
/// - Only src changed     -> toApplyToTarget[key] = srcVal
/// - Only tgt changed     -> toApplyToSource[key] = tgtVal
/// - Both changed, same   -> agree; no diff entry (already converged)
/// - Both changed, differ -> append key to conflicts; caller resolves
/// - Neither changed      -> skip
MapPropertyDiff computeMapDiff(
    const QVariantMap &src,
    const QVariantMap &tgt,
    const QVariantMap &base);

} // namespace Kalburator::Sync
