#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Contacts {

/// Transforms a Google People API `Person` JSON object to canon JSON
/// (EEE Phase 3 promote; lossless by construction).
class GooglePersonToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& googleBytes) const override;
};

/// Transforms canon JSON bytes to a Google People `Person` JSON object
/// (EEE Phase 3 demote; lossy per the declared loss profile).
class CanonToGooglePersonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → google-person demote direction.
Kalburator::Shape::LossProfile canonToGooglePersonLoss();

}  // namespace Kalburator::Contacts
