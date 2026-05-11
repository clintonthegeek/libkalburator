#include "blobplugin.h"
#include "blobdomaindefinition.h"
#include "blobstockshapes.h"

namespace Kalburator::Blob {

QList<std::shared_ptr<Shape::DomainDefinition>> BlobPlugin::domainDefinitions() const {
    return { std::make_shared<BlobDomainDefinition>() };
}

QList<std::shared_ptr<Shape::ShapeContribution>> BlobPlugin::shapeContributions() const {
    return { std::make_shared<BlobStockShapes>() };
}

} // namespace Kalburator::Blob
