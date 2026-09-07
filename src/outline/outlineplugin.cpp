#include <kalburator/outline/outlineplugin.h>
#include <kalburator/outline/outlinedomaindefinition.h>
#include <kalburator/outline/outlinestockshapes.h>

namespace Kalburator::Outline {

QList<std::shared_ptr<Shape::DomainDefinition>> OutlinePlugin::domainDefinitions() const {
    return { std::make_shared<OutlineDomainDefinition>() };
}

QList<std::shared_ptr<Shape::ShapeContribution>> OutlinePlugin::shapeContributions() const {
    return { std::make_shared<OutlineStockShapes>() };
}

} // namespace Kalburator::Outline
