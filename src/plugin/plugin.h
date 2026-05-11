#ifndef KALBURATOR_PLUGIN_PLUGIN_H
#define KALBURATOR_PLUGIN_PLUGIN_H

#include <QList>
#include <QtPlugin>
#include <memory>

namespace Kalburator {

namespace Shape {
class DomainDefinition;
class ShapeContribution;
class DomainOperations;
}
namespace Sync { class BackendContribution; }

class Plugin {
public:
    virtual ~Plugin() = default;

    virtual QList<std::shared_ptr<Shape::DomainDefinition>>
        domainDefinitions() const { return {}; }
    virtual QList<std::shared_ptr<Shape::ShapeContribution>>
        shapeContributions() const { return {}; }
    virtual QList<std::shared_ptr<Shape::DomainOperations>>
        domainOperations() const { return {}; }
    virtual QList<std::shared_ptr<Sync::BackendContribution>>
        backendContributions() const { return {}; }
};

} // namespace Kalburator

Q_DECLARE_INTERFACE(Kalburator::Plugin, "org.kalburator.Plugin/1.0")

#endif
