#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Todo {

/// Transforms a Microsoft Graph v1.0 `todoTask` JSON object to canon JSON
/// (EEE Phase 3 promote; lossless by construction).
class MsTodoTaskToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& msBytes) const override;
};

/// Transforms canon JSON bytes to a Microsoft Graph `todoTask` JSON object
/// (EEE Phase 3 demote; lossy per the declared loss profile). Recurrence
/// cannot-represent rulings + unhandled canon props ride the
/// kalburator.canon open-extension carrier (write-back survival UNVERIFIED
/// until a live drill — O61(e) suspicion class).
class CanonToMsTodoTaskStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → ms-todotask demote direction.
Kalburator::Shape::LossProfile canonToMsTodoTaskLoss();

}  // namespace Kalburator::Todo
