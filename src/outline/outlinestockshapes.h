#pragma once
#include "shapecontribution.h"

namespace Kalburator::Outline {

class OutlineStockShapes : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("outline")}; }
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override;
    QList<Shape::TransformationEdge> edges() const override;
};

} // namespace Kalburator::Outline
