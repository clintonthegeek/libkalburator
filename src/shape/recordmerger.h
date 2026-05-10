#pragma once

#include "canonicalrecord.h"
#include "conflictpolicy.h"

namespace Kalburator::Shape {

/// 3-way merge over canonical records. One implementation per
/// canonical shape, owned by the domain plugin. The engine consults
/// the merger during the merge stage of the worker pipeline.
class RecordMerger {
public:
    virtual ~RecordMerger() = default;

    /// Per-property choice for explanation / instrumentation.
    enum class FieldChoice {
        TakeSource,
        TakeTarget,
        TakeBaseline,
    };

    /// 3-way merge. For each property in the union of source/target/
    /// baseline catalogues, decide TakeSource/TakeTarget/TakeBaseline
    /// per the supplied conflict policy. Result is a fully-realised
    /// CanonicalRecord ready for push-back to the target.
    virtual CanonicalRecord merge(
        const CanonicalRecord& source,
        const CanonicalRecord& target,
        const CanonicalRecord& baseline,
        const Kalburator::Conflict::ConflictPolicy& policy) const = 0;
};

}  // namespace Kalburator::Shape
