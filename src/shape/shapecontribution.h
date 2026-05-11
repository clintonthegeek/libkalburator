#ifndef KALBURATOR_SHAPE_SHAPECONTRIBUTION_H
#define KALBURATOR_SHAPE_SHAPECONTRIBUTION_H

#include <QList>
#include <utility>

#include "propertycatalogue.h"
#include "shape.h"
#include "transformationedge.h"

namespace Kalburator::Shape {

/// Describes the set of shapes and transformation edges that a plugin
/// contributes to the engine's shape graph. A plugin that knows about
/// peer shapes beyond the canonical hub implements this interface so
/// that PluginManager can populate the TransformationRegistry without
/// going through the legacy DomainPlugin::registerEdges() call.
///
/// peerShapes() returns (shape, catalogue) pairs — each peer shape's
/// property catalogue is needed by shape-graph consumers that inspect
/// what fields a given encoding exposes. edges() lists directed
/// TransformationEdge values (including hub→peer and peer→hub pairs).
class ShapeContribution {
public:
    virtual ~ShapeContribution() = default;

    /// The domain whose shape graph this contribution augments.
    virtual DomainId targetDomain() const = 0;

    /// Peer shapes this plugin adds, with their property catalogues.
    virtual QList<std::pair<Shape, PropertyCatalogue>> peerShapes() const = 0;

    /// Transformation edges connecting the peer shapes to the canonical
    /// hub (and back). Include both directions when the transformation
    /// is invertible.
    virtual QList<TransformationEdge> edges() const = 0;
};

} // namespace Kalburator::Shape

#endif // KALBURATOR_SHAPE_SHAPECONTRIBUTION_H
