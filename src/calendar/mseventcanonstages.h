#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Calendar {

/// Transforms a Microsoft Graph v1.0 `event` JSON object to canon JSON
/// (EEE Phase 7.B promote).
class MsEventToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& msBytes) const override;
};

/// Transforms canon JSON bytes to a Microsoft Graph `event` JSON object
/// (EEE Phase 7.B demote; lossy per the declared loss profile).
class CanonToMsEventStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → ms-event demote direction.
Kalburator::Shape::LossProfile canonToMsEventLoss();

}  // namespace Kalburator::Calendar
