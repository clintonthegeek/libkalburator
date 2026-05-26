#pragma once
#include "recorddiffer.h"
#include <QSet>

namespace Kalburator::Outline {

/// Coarse RecordDiffer for (outline, canon): compares the record-level
/// canon properties (title/created/lastModified/attributes/children) by
/// JSON value-equality. Whole-tree granularity; node-level structural
/// diff is a deferred follow-on.
class OutlineDiffer : public Kalburator::Shape::RecordDiffer {
public:
    QSet<Kalburator::Shape::PropertyId> diff(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& baseline) const override;
    bool equal(const Kalburator::Shape::CanonicalRecord& a,
               const Kalburator::Shape::CanonicalRecord& b) const override;
};

}  // namespace Kalburator::Outline
