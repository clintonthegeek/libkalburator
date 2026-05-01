#include "memoproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Memo {

PropertyCatalogue makeMemoCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"id"},           PropertyKind::String,     QStringLiteral("ID"),           false });
    cat.addProperty({ PropertyId{"body"},         PropertyKind::String,     QStringLiteral("Body") });
    cat.addProperty({ PropertyId{"categories"},   PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"lastmodified"}, PropertyKind::DateTime,   QStringLiteral("Last Modified") });

    return cat;
}

} // namespace Kalburator::Memo
