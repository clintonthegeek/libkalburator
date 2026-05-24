#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Calendar {

/// Transforms a VEVENT iCalendar byte-string to canon JSON (lossless).
class ICalToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& icalBytes) const override;
};

/// Transforms canon JSON bytes to a VEVENT iCalendar string (lossy).
class CanonToICalStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → ical demote direction.
Kalburator::Shape::LossProfile canonToIcalLoss();

}  // namespace Kalburator::Calendar
