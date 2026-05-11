#pragma once

#include "shapecontribution.h"

namespace Kalburator::Blob {

class BlobStockShapes : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("blob")}; }
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override { return {}; }
    QList<Shape::TransformationEdge> edges() const override;
};

} // namespace Kalburator::Blob
