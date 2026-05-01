#include "vcardproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Contacts {

PropertyCatalogue makeVCardCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"uid"},    PropertyKind::String,     QStringLiteral("UID"),          false });

    // Name fields
    cat.addProperty({ PropertyId{"fn"},     PropertyKind::String,     QStringLiteral("Full Name") });
    cat.addProperty({ PropertyId{"n"},      PropertyKind::Json,       QStringLiteral("Structured Name") });
    cat.addProperty({ PropertyId{"nickname"},PropertyKind::String,    QStringLiteral("Nickname") });

    // Contact info
    cat.addProperty({ PropertyId{"email"},  PropertyKind::Json,       QStringLiteral("Email Addresses") });
    cat.addProperty({ PropertyId{"tel"},    PropertyKind::Json,       QStringLiteral("Phone Numbers") });
    cat.addProperty({ PropertyId{"adr"},    PropertyKind::Json,       QStringLiteral("Addresses") });
    cat.addProperty({ PropertyId{"url"},    PropertyKind::Json,       QStringLiteral("URLs") });

    // Organization
    cat.addProperty({ PropertyId{"org"},    PropertyKind::String,     QStringLiteral("Organization") });
    cat.addProperty({ PropertyId{"title"},  PropertyKind::String,     QStringLiteral("Title") });
    cat.addProperty({ PropertyId{"role"},   PropertyKind::String,     QStringLiteral("Role") });

    // Dates
    cat.addProperty({ PropertyId{"bday"},   PropertyKind::DateTime,   QStringLiteral("Birthday") });
    cat.addProperty({ PropertyId{"rev"},    PropertyKind::DateTime,   QStringLiteral("Revision") });

    // Rich content
    cat.addProperty({ PropertyId{"note"},   PropertyKind::String,     QStringLiteral("Note") });
    cat.addProperty({ PropertyId{"photo"},  PropertyKind::Json,       QStringLiteral("Photo") });
    cat.addProperty({ PropertyId{"categories"}, PropertyKind::StringList, QStringLiteral("Categories") });

    // Extensibility
    cat.addProperty({ PropertyId{"x-custom"}, PropertyKind::Json,    QStringLiteral("Custom Properties") });

    return cat;
}

} // namespace Kalburator::Contacts
