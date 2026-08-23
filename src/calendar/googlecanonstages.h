#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Calendar {

/// Transforms a Google Calendar v3 `event` JSON object to canon JSON (lossless).
class GoogleEventToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& googleBytes) const override;
};

/// Transforms canon JSON bytes to a Google Calendar v3 `event` JSON object (lossy).
class CanonToGoogleEventStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → google-event demote direction.
Kalburator::Shape::LossProfile canonToGoogleEventLoss();

}  // namespace Kalburator::Calendar
