#include <kalburator/contacts/contactsplugin.h>
#include <kalburator/contacts/contactsdomaindefinition.h>
#include <kalburator/contacts/contactsstockshapes.h>

namespace Kalburator::Contacts {

QList<std::shared_ptr<Shape::DomainDefinition>> ContactsPlugin::domainDefinitions() const
{
    return { std::make_shared<ContactsDomainDefinition>() };
}

QList<std::shared_ptr<Shape::ShapeContribution>> ContactsPlugin::shapeContributions() const
{
    return { std::make_shared<ContactsStockShapes>() };
}

} // namespace Kalburator::Contacts
