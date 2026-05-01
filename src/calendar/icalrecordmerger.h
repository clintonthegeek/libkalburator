#pragma once

#include "irecordmerger.h"

namespace Kalburator::Calendar {

/// IRecordMerger implementation for the (calendar, ical) canonical shape.
/// Wraps IncidenceMerge to compute 3-way merges between iCal-encoded
/// CanonicalRecords.
class IRecordMergerICal : public Kalburator::Shape::IRecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        const Kalburator::Sync::QSyncCore::ConflictPolicy& policy) const override;
};

} // namespace Kalburator::Calendar
