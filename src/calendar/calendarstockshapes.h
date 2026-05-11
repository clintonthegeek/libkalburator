#pragma once
#include "shapecontribution.h"

namespace Kalburator::Calendar {

class CalendarStockShapes : public Kalburator::Shape::ShapeContribution {
public:
    Kalburator::Shape::DomainId targetDomain() const override;
    QList<std::pair<Kalburator::Shape::Shape, Kalburator::Shape::PropertyCatalogue>> peerShapes() const override;
    QList<Kalburator::Shape::TransformationEdge> edges() const override;
};

} // namespace Kalburator::Calendar
