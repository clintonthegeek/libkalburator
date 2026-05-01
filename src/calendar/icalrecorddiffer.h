#pragma once

#include "irecorddiffer.h"

namespace Kalburator::Calendar {

/// IRecordDiffer implementation for the (calendar, ical) canonical shape.
/// Wraps IncidenceDiff to compute per-property differences between two
/// iCal-encoded CanonicalRecords.
class IRecordDifferICal : public Kalburator::Shape::IRecordDiffer {
public:
    QSet<Kalburator::Shape::PropertyId> diff(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& baseline) const override;

    bool equal(const Kalburator::Shape::CanonicalRecord& a,
               const Kalburator::Shape::CanonicalRecord& b) const override;
};

} // namespace Kalburator::Calendar
