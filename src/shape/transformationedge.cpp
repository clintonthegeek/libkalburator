#include "transformationedge.h"

namespace Kalburator::Shape {

QString TransformationEdge::toString() const {
    return from.toString() + QStringLiteral(" → ") + to.toString()
           + QStringLiteral(" [") + loss.summary() + QStringLiteral("]");
}

}  // namespace Kalburator::Shape
