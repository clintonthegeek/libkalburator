#pragma once
#include "shapecontribution.h"

namespace Kalburator::Contacts {

class ContactsStockShapes : public Shape::ShapeContribution {
public:
    Shape::DomainId targetDomain() const override { return Shape::DomainId{QStringLiteral("contacts")}; }
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peerShapes() const override;
    QList<Shape::TransformationEdge> edges() const override;
};

} // namespace Kalburator::Contacts
