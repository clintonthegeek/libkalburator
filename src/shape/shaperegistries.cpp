#include "shaperegistries.h"

namespace Kalburator::Shape {

ShapeRegistries &defaultShapeRegistries()
{
    static ShapeRegistries s;
    return s;
}

}  // namespace Kalburator::Shape
