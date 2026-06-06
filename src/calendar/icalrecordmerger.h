#pragma once

#include "recordmerger.h"

namespace Kalburator::Calendar {

/// IRecordMerger implementation for the (calendar, ical) canonical shape.
/// Wraps IncidenceMerge to compute 3-way merges between iCal-encoded
/// CanonicalRecords.
class RecordMergerICal : public Kalburator::Shape::RecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        Kalburator::Shape::AutoResolveStrategy strategy) const override;
};

} // namespace Kalburator::Calendar
