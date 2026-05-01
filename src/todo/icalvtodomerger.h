#pragma once

#include "irecordmerger.h"

namespace Kalburator::Todo {

/// IRecordMerger for (todo, ical-vtodo). Performs 3-way property-level
/// merge between VTODO canonical records.
class IRecordMergerVTodo : public Kalburator::Shape::IRecordMerger {
public:
    Kalburator::Shape::CanonicalRecord merge(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& target,
        const Kalburator::Shape::CanonicalRecord& baseline,
        const Kalburator::Sync::QSyncCore::ConflictPolicy& policy) const override;
};

} // namespace Kalburator::Todo
