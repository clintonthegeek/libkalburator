#include "googlepersonproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Contacts {

PropertyCatalogue makeGooglePersonCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"resourcename"},   PropertyKind::String, QStringLiteral("Resource Name") });
    cat.addProperty({ PropertyId{"etag"},           PropertyKind::String, QStringLiteral("ETag") });
    cat.addProperty({ PropertyId{"metadata"},       PropertyKind::Json,   QStringLiteral("Metadata") });

    cat.addProperty({ PropertyId{"names"},           PropertyKind::Json,   QStringLiteral("Names") });
    cat.addProperty({ PropertyId{"nicknames"},       PropertyKind::Json,   QStringLiteral("Nicknames") });
    cat.addProperty({ PropertyId{"emailaddresses"},  PropertyKind::Json,   QStringLiteral("Email Addresses") });
    cat.addProperty({ PropertyId{"phonenumbers"},    PropertyKind::Json,   QStringLiteral("Phone Numbers") });
    cat.addProperty({ PropertyId{"addresses"},       PropertyKind::Json,   QStringLiteral("Addresses") });
    cat.addProperty({ PropertyId{"organizations"},   PropertyKind::Json,   QStringLiteral("Organizations") });
    cat.addProperty({ PropertyId{"urls"},            PropertyKind::Json,   QStringLiteral("URLs") });
    cat.addProperty({ PropertyId{"relations"},       PropertyKind::Json,   QStringLiteral("Relations") });
    cat.addProperty({ PropertyId{"birthdays"},       PropertyKind::Json,   QStringLiteral("Birthdays") });
    cat.addProperty({ PropertyId{"genders"},         PropertyKind::Json,   QStringLiteral("Genders") });
    cat.addProperty({ PropertyId{"biographies"},     PropertyKind::Json,   QStringLiteral("Biographies") });
    cat.addProperty({ PropertyId{"photos"},          PropertyKind::Json,   QStringLiteral("Photos") });
    cat.addProperty({ PropertyId{"externalids"},     PropertyKind::Json,   QStringLiteral("External IDs") });
    cat.addProperty({ PropertyId{"memberships"},     PropertyKind::Json,   QStringLiteral("Memberships") });
    cat.addProperty({ PropertyId{"interests"},       PropertyKind::Json,   QStringLiteral("Interests") });
    cat.addProperty({ PropertyId{"skills"},          PropertyKind::Json,   QStringLiteral("Skills") });
    cat.addProperty({ PropertyId{"occupations"},     PropertyKind::Json,   QStringLiteral("Occupations") });
    cat.addProperty({ PropertyId{"locales"},         PropertyKind::Json,   QStringLiteral("Locales") });
    cat.addProperty({ PropertyId{"sipaddresses"},    PropertyKind::Json,   QStringLiteral("SIP Addresses") });
    cat.addProperty({ PropertyId{"calendarurls"},    PropertyKind::Json,   QStringLiteral("Calendar URLs") });
    cat.addProperty({ PropertyId{"imclients"},       PropertyKind::Json,   QStringLiteral("IM Clients") });
    cat.addProperty({ PropertyId{"fileases"},        PropertyKind::Json,   QStringLiteral("File As") });
    cat.addProperty({ PropertyId{"clientdata"},      PropertyKind::Json,   QStringLiteral("Client Data") });

    return cat;
}

} // namespace Kalburator::Contacts
