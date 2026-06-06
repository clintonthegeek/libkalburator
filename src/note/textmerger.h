#pragma once

#include "recordmerger.h"

namespace Kalburator::Note {

/// IRecordMerger for (note, canon). Performs 3-way merge by line
/// for body text; falls through to conflict policy when lines diverge.
class TextMerger : public Kalburator::Shape::RecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        Kalburator::Shape::AutoResolveStrategy strategy) const override;
};

} // namespace Kalburator::Note
