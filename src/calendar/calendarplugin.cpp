#include "calendarplugin.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"
#include "calendardomainoperations.h"

namespace Kalburator::Calendar {

QList<std::shared_ptr<Kalburator::Shape::DomainDefinition>>
CalendarPlugin::domainDefinitions() const
{
    return { std::make_shared<CalendarDomainDefinition>() };
}

QList<std::shared_ptr<Kalburator::Shape::ShapeContribution>>
CalendarPlugin::shapeContributions() const
{
    return { std::make_shared<CalendarStockShapes>() };
}

QList<std::shared_ptr<Kalburator::Shape::DomainOperations>>
CalendarPlugin::domainOperations() const
{
    return { std::make_shared<CalendarDomainOperations>() };
}

QList<std::shared_ptr<Kalburator::Sync::BackendContribution>>
CalendarPlugin::backendContributions() const
{
    return {};
}

} // namespace Kalburator::Calendar
