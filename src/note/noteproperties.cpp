#include "noteproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Note {

PropertyCatalogue makeNoteCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"uid"},          PropertyKind::String,     QStringLiteral("UID"),          false });
    cat.addProperty({ PropertyId{"body"},         PropertyKind::String,     QStringLiteral("Body") });
    cat.addProperty({ PropertyId{"categories"},   PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"lastmodified"}, PropertyKind::DateTime,   QStringLiteral("Last Modified") });

    return cat;
}

} // namespace Kalburator::Note
