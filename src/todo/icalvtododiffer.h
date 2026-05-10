#pragma once

#include "recorddiffer.h"

namespace Kalburator::Todo {

/// IRecordDiffer for (todo, ical-vtodo). Uses KCalendarCore::Todo
/// to compute per-property differences between two VTODO records.
class RecordDifferVTodo : public Kalburator::Shape::RecordDiffer {
public:
    QSet<Kalburator::Shape::PropertyId> diff(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& baseline) const override;

    bool equal(const Kalburator::Shape::CanonicalRecord& a,
               const Kalburator::Shape::CanonicalRecord& b) const override;
};

} // namespace Kalburator::Todo
