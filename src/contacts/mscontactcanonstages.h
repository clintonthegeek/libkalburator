#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Contacts {

/// Transforms a Microsoft Graph v1.0 `contact` JSON object to canon JSON
/// (EEE Phase 3 promote; lossless by construction).
class MsContactToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& msBytes) const override;
};

/// Transforms canon JSON bytes to a Microsoft Graph `contact` JSON object
/// (EEE Phase 3 demote; lossy per the declared loss profile).
class CanonToMsContactStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → ms-contact demote direction.
Kalburator::Shape::LossProfile canonToMsContactLoss();

}  // namespace Kalburator::Contacts
