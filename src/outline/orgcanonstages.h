#pragma once
#include "transformationedge.h"

namespace Kalburator::Outline {

/// (outline, org) -> (outline, canon). Delegates parsing to OrgGrove and maps
/// each OrgGrove::Headline -> canon OutlineNode.
class OrgToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// (outline, canon) -> (outline, org). Maps canon OutlineNode -> OrgGrove::Headline
/// and delegates serialization to OrgGrove::serialize.
class CanonToOrgStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

}  // namespace Kalburator::Outline
