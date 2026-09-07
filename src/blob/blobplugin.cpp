#include <kalburator/blob/blobplugin.h>
#include <kalburator/blob/blobdomaindefinition.h>
#include <kalburator/blob/blobstockshapes.h>

namespace Kalburator::Blob {

QList<std::shared_ptr<Shape::DomainDefinition>> BlobPlugin::domainDefinitions() const {
    return { std::make_shared<BlobDomainDefinition>() };
}

QList<std::shared_ptr<Shape::ShapeContribution>> BlobPlugin::shapeContributions() const {
    return { std::make_shared<BlobStockShapes>() };
}

} // namespace Kalburator::Blob
