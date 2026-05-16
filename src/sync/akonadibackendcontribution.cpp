#ifdef HAVE_AKONADI

#include "akonadibackendcontribution.h"
#include "shape.h"
#include "../contacts/akonadicontactsbackend.h"

namespace Kalburator::Sync {

QList<Shape::Shape> AkonadiBackendContribution::nativeShapes() const
{
    // Start with contacts shapes from AkonadiContactsBackend.
    AkonadiContactsBackend contacts;
    auto shapes = contacts.nativeShapes();
    // Calendar shape — mirrors what AkonadiBackend::nativeShapes() returns.
    shapes.append(Shape::Shape{
        Shape::DomainId{QStringLiteral("calendar")},
        Shape::EncodingId{QStringLiteral("ical")}
    });
    return shapes;
}

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
