#include "propertydiff.h"

#include <QSet>

namespace Kalburator::Sync {

PropertyDiff computeMapDiff(const QVariantMap &src,
                            const QVariantMap &tgt,
                            const QVariantMap &base)
{
    PropertyDiff diff;

    // Union of keys across all three maps.
    QSet<QString> keys;
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) keys.insert(it.key());
    for (auto it = tgt.constBegin(); it != tgt.constEnd(); ++it) keys.insert(it.key());
    for (auto it = base.constBegin(); it != base.constEnd(); ++it) keys.insert(it.key());

    for (const QString &key : keys) {
        // Note: value(key, QVariant()) returns invalid QVariant for absent
        // keys, which is what we want to distinguish "not set" from "set to
        // some value." Present-but-invalid values aren't expected for our
        // use cases (color/description), but would compare equal to absent
        // — accepted as a v1 limitation.
        const QVariant srcVal = src.value(key, QVariant());
        const QVariant tgtVal = tgt.value(key, QVariant());
        const QVariant baseVal = base.value(key, QVariant());

        const bool srcChanged = (srcVal != baseVal);
        const bool tgtChanged = (tgtVal != baseVal);

        if (!srcChanged && !tgtChanged) {
            continue;
        }

        if (srcChanged && !tgtChanged) {
            diff.toApplyToTarget.insert(key, srcVal);
            continue;
        }

        if (tgtChanged && !srcChanged) {
            diff.toApplyToSource.insert(key, tgtVal);
            continue;
        }

        // Both changed.
        if (srcVal == tgtVal) {
            // Both ended up at the same value independently — already
            // converged, no apply needed. Baseline update happens
            // elsewhere.
            continue;
        }

        diff.conflicts.append(key);
    }

    return diff;
}

} // namespace Kalburator::Sync
