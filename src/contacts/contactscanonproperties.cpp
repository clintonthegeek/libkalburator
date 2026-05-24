#include "contactscanonproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Contacts {

Kalburator::Shape::PropertyCatalogue makeContactsCanonCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"uid"}, PropertyKind::String, QStringLiteral("UID"), false });
    cat.addProperty({ PropertyId{"names"}, PropertyKind::Json, QStringLiteral("Names") });
    cat.addProperty({ PropertyId{"nicknames"}, PropertyKind::Json, QStringLiteral("Nicknames") });
    cat.addProperty({ PropertyId{"emails"}, PropertyKind::Json, QStringLiteral("Emails") });
    cat.addProperty({ PropertyId{"phones"}, PropertyKind::Json, QStringLiteral("Phones") });
    cat.addProperty({ PropertyId{"addresses"}, PropertyKind::Json, QStringLiteral("Addresses") });
    cat.addProperty({ PropertyId{"organizations"}, PropertyKind::Json, QStringLiteral("Organizations") });
    cat.addProperty({ PropertyId{"occupations"}, PropertyKind::StringList, QStringLiteral("Occupations") });
    cat.addProperty({ PropertyId{"urls"}, PropertyKind::Json, QStringLiteral("URLs") });
    cat.addProperty({ PropertyId{"imClients"}, PropertyKind::Json, QStringLiteral("IM Clients") });
    cat.addProperty({ PropertyId{"sipAddresses"}, PropertyKind::StringList, QStringLiteral("SIP Addresses") });
    cat.addProperty({ PropertyId{"calendarUrls"}, PropertyKind::Json, QStringLiteral("Calendar URLs") });
    cat.addProperty({ PropertyId{"relations"}, PropertyKind::Json, QStringLiteral("Relations") });
    cat.addProperty({ PropertyId{"birthday"}, PropertyKind::Json, QStringLiteral("Birthday") });
    cat.addProperty({ PropertyId{"anniversary"}, PropertyKind::Json, QStringLiteral("Anniversary") });
    cat.addProperty({ PropertyId{"significantDates"}, PropertyKind::Json, QStringLiteral("Significant Dates") });
    cat.addProperty({ PropertyId{"gender"}, PropertyKind::Json, QStringLiteral("Gender") });
    cat.addProperty({ PropertyId{"notes"}, PropertyKind::String, QStringLiteral("Notes") });
    cat.addProperty({ PropertyId{"photos"}, PropertyKind::Json, QStringLiteral("Photos") });
    cat.addProperty({ PropertyId{"categories"}, PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"languages"}, PropertyKind::StringList, QStringLiteral("Languages") });
    cat.addProperty({ PropertyId{"timeZone"}, PropertyKind::String, QStringLiteral("Time Zone") });
    cat.addProperty({ PropertyId{"externalIds"}, PropertyKind::Json, QStringLiteral("External IDs") });
    cat.addProperty({ PropertyId{"memberships"}, PropertyKind::Json, QStringLiteral("Memberships") });
    cat.addProperty({ PropertyId{"interests"}, PropertyKind::StringList, QStringLiteral("Interests") });
    cat.addProperty({ PropertyId{"skills"}, PropertyKind::StringList, QStringLiteral("Skills") });
    return cat;
}

QList<Kalburator::Shape::PropertyId> contactsCanonPropertyIds()
{
    QList<PropertyId> ids;
    for (const auto& d : makeContactsCanonCatalogue().properties())
        ids.append(d.id);
    return ids;
}

}  // namespace Kalburator::Contacts
