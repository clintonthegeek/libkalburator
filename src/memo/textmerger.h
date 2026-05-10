#pragma once

#include "recordmerger.h"

namespace Kalburator::Memo {

/// IRecordMerger for (memo, text). Performs 3-way merge by line
/// for body text; falls through to conflict policy when lines diverge.
class TextMerger : public Kalburator::Shape::RecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        const Kalburator::Sync::QSyncCore::ConflictPolicy& policy) const override;
};

} // namespace Kalburator::Memo
