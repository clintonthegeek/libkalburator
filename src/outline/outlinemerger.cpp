#include "outlinemerger.h"
#include "outlinediffer.h"

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

CanonicalRecord OutlineMerger::merge(const CanonicalRecord& source,
                                     const CanonicalRecord& target,
                                     const CanonicalRecord& baseline,
                                     Kalburator::Shape::AutoResolveStrategy strategy) const
{
    Q_UNUSED(strategy);
    OutlineDiffer d;
    // If only one side changed since baseline, take that side. If both changed,
    // prefer source (the conflict-policy-aware structural merge is a follow-on).
    const bool sourceChanged = !d.equal(source, baseline);
    const bool targetChanged = !d.equal(target, baseline);
    // Only target changed -> take target. Otherwise (only source changed,
    // both changed [conflict -> source wins], or neither changed) -> source.
    if (targetChanged && !sourceChanged)
        return target;
    return source;
}

}  // namespace Kalburator::Outline
