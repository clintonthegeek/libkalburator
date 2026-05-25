#include "noteplugin.h"
#include "notedomaindefinition.h"
#include "notestockshapes.h"

namespace Kalburator::Note {

QList<std::shared_ptr<Shape::DomainDefinition>> NotePlugin::domainDefinitions() const {
    return { std::make_shared<NoteDomainDefinition>() };
}

QList<std::shared_ptr<Shape::ShapeContribution>> NotePlugin::shapeContributions() const {
    return { std::make_shared<NoteStockShapes>() };
}

} // namespace Kalburator::Note
