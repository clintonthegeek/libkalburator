#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Todo {

/// Transforms a Google Tasks API `Task` JSON object to canon JSON
/// (EEE Phase 3 promote; lossless by construction).
class GoogleTaskToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& googleBytes) const override;
};

/// Transforms canon JSON bytes to a Google Tasks `Task` JSON object
/// (EEE Phase 3 demote; lossy per the declared loss profile — the Tasks
/// resource has NO carrier channel, so unrepresented canon props are
/// Dropped, not carried).
class CanonToGoogleTaskStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → google-task demote direction.
Kalburator::Shape::LossProfile canonToGoogleTaskLoss();

}  // namespace Kalburator::Todo
