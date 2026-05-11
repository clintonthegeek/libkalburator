#pragma once
#include "plugin.h"

namespace Kalburator::Calendar {

class CalendarPlugin : public Kalburator::Plugin {
public:
    QList<std::shared_ptr<Kalburator::Shape::DomainDefinition>>    domainDefinitions()  const override;
    QList<std::shared_ptr<Kalburator::Shape::ShapeContribution>>   shapeContributions() const override;
    QList<std::shared_ptr<Kalburator::Shape::DomainOperations>>    domainOperations()   const override;
    QList<std::shared_ptr<Kalburator::Sync::BackendContribution>>  backendContributions() const override;
};

} // namespace Kalburator::Calendar
