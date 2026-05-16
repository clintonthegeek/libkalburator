#ifdef HAVE_AKONADI

#include "akonadibackendcontribution.h"
#include "shape.h"

namespace Kalburator::Sync {

QList<Shape::Shape> AkonadiBackendContribution::nativeShapes() const
{
    // Mirrors AkonadiBackend::nativeShapes(): Akonadi stores calendar data
    // in iCal format, so the native shape is calendar+ical.
    return { Shape::Shape{
        Shape::DomainId{QStringLiteral("calendar")},
        Shape::EncodingId{QStringLiteral("ical")} } };
}

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
