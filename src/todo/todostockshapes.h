#pragma once
#include "shapecontribution.h"

namespace Kalburator::Todo {

class TodoStockShapes : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("todo")}; }
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override;
    QList<Shape::TransformationEdge> edges() const override;
};

} // namespace Kalburator::Todo
