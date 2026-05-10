#pragma once

#include "recorddiffer.h"

namespace Kalburator::Memo {

/// IRecordDiffer for (memo, text). Compares body text, categories,
/// and last-modified timestamp stored as JSON in CanonicalRecord.data.
class TextDiffer : public Kalburator::Shape::RecordDiffer {
public:
    QSet<Kalburator::Shape::PropertyId> diff(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& baseline) const override;

    bool equal(const Kalburator::Shape::CanonicalRecord& a,
               const Kalburator::Shape::CanonicalRecord& b) const override;
};

} // namespace Kalburator::Memo
