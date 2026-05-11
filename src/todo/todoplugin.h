#pragma once
#include "plugin.h"

namespace Kalburator::Todo {

class TodoPlugin : public Plugin {
public:
    QList<std::shared_ptr<Shape::DomainDefinition>> domainDefinitions() const override;
    QList<std::shared_ptr<Shape::ShapeContribution>> shapeContributions() const override;
};

} // namespace Kalburator::Todo
