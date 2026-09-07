#pragma once

#include <kalburator/plugin/plugin.h>

namespace Kalburator::Outline {

class OutlinePlugin : public Plugin {
public:
    QList<std::shared_ptr<Shape::DomainDefinition>> domainDefinitions() const override;
    QList<std::shared_ptr<Shape::ShapeContribution>> shapeContributions() const override;
};

} // namespace Kalburator::Outline
