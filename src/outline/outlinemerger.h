#pragma once
#include "recordmerger.h"

namespace Kalburator::Outline {

/// Coarse RecordMerger for (outline, canon): whole-tree pick-a-side per the
/// conflict policy (no structural node merge in the first cut).
class OutlineMerger : public Kalburator::Shape::RecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        const Kalburator::Conflict::ConflictPolicy& policy) const override;
};

}  // namespace Kalburator::Outline
