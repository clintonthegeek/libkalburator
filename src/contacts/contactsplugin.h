#pragma once
#include <kalburator/plugin/plugin.h>

namespace Kalburator::Contacts {

class ContactsPlugin : public Plugin {
public:
    QList<std::shared_ptr<Shape::DomainDefinition>> domainDefinitions() const override;
    QList<std::shared_ptr<Shape::ShapeContribution>> shapeContributions() const override;
};

} // namespace Kalburator::Contacts
