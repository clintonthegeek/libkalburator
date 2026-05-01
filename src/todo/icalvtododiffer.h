#pragma once

#include "irecorddiffer.h"

namespace Kalburator::Todo {

/// IRecordDiffer for (todo, ical-vtodo). Uses KCalendarCore::Todo
/// to compute per-property differences between two VTODO records.
class IRecordDifferVTodo : public Kalburator::Shape::IRecordDiffer {
public:
    QSet<Kalburator::Shape::PropertyId> diff(
        const Kalburator::Shape::CanonicalRecord& source,
        const Kalburator::Shape::CanonicalRecord& baseline) const override;

    bool equal(const Kalburator::Shape::CanonicalRecord& a,
               const Kalburator::Shape::CanonicalRecord& b) const override;
};

} // namespace Kalburator::Todo
