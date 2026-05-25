#pragma once
#include "shapecontribution.h"

namespace Kalburator::Note {

class NoteStockShapes : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("note")}; }
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override;
    QList<Shape::TransformationEdge> edges() const override;
};

} // namespace Kalburator::Note
