#pragma once
#include "transformationedge.h"

namespace Kalburator::Calendar {

/// Transforms canon JSON bytes to org-ical bytes:
/// runs CanonToICalStage, then simplifies any complex RRULE to a basic
/// pattern org-mode can represent (stashing the original in X-ORIGINAL-RRULE).
class CanonToOrgICalStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// Transforms org-ical bytes to canon JSON bytes:
/// restores any stashed X-ORIGINAL-RRULE back to a full recurrence, then
/// runs ICalToCanonStage to produce canon JSON.
class OrgICalToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& orgIcalBytes) const override;
};

}  // namespace Kalburator::Calendar
