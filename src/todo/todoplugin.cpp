#include "todoplugin.h"
#include "tododomaindefinition.h"
#include "todostockshapes.h"

namespace Kalburator::Todo {

QList<std::shared_ptr<Shape::DomainDefinition>> TodoPlugin::domainDefinitions() const
{
    return { std::make_shared<TodoDomainDefinition>() };
}

QList<std::shared_ptr<Shape::ShapeContribution>> TodoPlugin::shapeContributions() const
{
    return { std::make_shared<TodoStockShapes>() };
}

} // namespace Kalburator::Todo
