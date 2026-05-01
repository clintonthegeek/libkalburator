#pragma once

#include <QSet>

#include "canonicalrecord.h"
#include "propertycatalogue.h"

namespace Kalburator::Shape {

/// Compute per-property differences between canonical records.
/// One implementation per canonical shape, owned by the domain
/// plugin. The engine consults the differ during the diff stage of
/// the worker pipeline.
class IRecordDiffer {
public:
    virtual ~IRecordDiffer() = default;

    /// Set of properties whose values differ between source and
    /// baseline. Empty result means "no change since baseline".
    virtual QSet<PropertyId> diff(const CanonicalRecord& source,
                                   const CanonicalRecord& baseline) const = 0;

    /// True iff `a` and `b` are byte-identical OR semantically
    /// equivalent under the differ's notion of equality.
    virtual bool equal(const CanonicalRecord& a,
                        const CanonicalRecord& b) const = 0;
};

}  // namespace Kalburator::Shape
