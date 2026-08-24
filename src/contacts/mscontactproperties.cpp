#include "mscontactproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Contacts {

PropertyCatalogue makeMsContactCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"id"},                    PropertyKind::String, QStringLiteral("Id") });
    cat.addProperty({ PropertyId{"displayname"},           PropertyKind::String, QStringLiteral("Display Name") });
    cat.addProperty({ PropertyId{"givenname"},             PropertyKind::String, QStringLiteral("Given Name") });
    cat.addProperty({ PropertyId{"surname"},               PropertyKind::String, QStringLiteral("Surname") });
    cat.addProperty({ PropertyId{"middlename"},            PropertyKind::String, QStringLiteral("Middle Name") });
    cat.addProperty({ PropertyId{"nickname"},              PropertyKind::String, QStringLiteral("Nick Name") });
    cat.addProperty({ PropertyId{"initials"},              PropertyKind::String, QStringLiteral("Initials") });
    cat.addProperty({ PropertyId{"title"},                 PropertyKind::String, QStringLiteral("Title") });
    cat.addProperty({ PropertyId{"fileas"},                PropertyKind::String, QStringLiteral("File As") });
    cat.addProperty({ PropertyId{"generation"},            PropertyKind::String, QStringLiteral("Generation") });
    cat.addProperty({ PropertyId{"yomigivenname"},         PropertyKind::String, QStringLiteral("Yomi Given Name") });
    cat.addProperty({ PropertyId{"yomisurname"},           PropertyKind::String, QStringLiteral("Yomi Surname") });
    cat.addProperty({ PropertyId{"yomicompanyname"},       PropertyKind::String, QStringLiteral("Yomi Company Name") });

    cat.addProperty({ PropertyId{"emailaddresses"},        PropertyKind::Json,   QStringLiteral("Email Addresses") });
    cat.addProperty({ PropertyId{"primaryemailaddress"},   PropertyKind::Json,   QStringLiteral("Primary Email Address") });
    cat.addProperty({ PropertyId{"secondaryemailaddress"}, PropertyKind::Json,   QStringLiteral("Secondary Email Address") });
    cat.addProperty({ PropertyId{"imaddresses"},           PropertyKind::StringList, QStringLiteral("IM Addresses") });
    cat.addProperty({ PropertyId{"businessphones"},        PropertyKind::StringList, QStringLiteral("Business Phones") });
    cat.addProperty({ PropertyId{"homephones"},            PropertyKind::StringList, QStringLiteral("Home Phones") });
    cat.addProperty({ PropertyId{"mobilephone"},           PropertyKind::String, QStringLiteral("Mobile Phone") });

    cat.addProperty({ PropertyId{"homeaddress"},           PropertyKind::Json,   QStringLiteral("Home Address") });
    cat.addProperty({ PropertyId{"businessaddress"},       PropertyKind::Json,   QStringLiteral("Business Address") });
    cat.addProperty({ PropertyId{"otheraddress"},          PropertyKind::Json,   QStringLiteral("Other Address") });

    cat.addProperty({ PropertyId{"companyname"},           PropertyKind::String, QStringLiteral("Company Name") });
    cat.addProperty({ PropertyId{"department"},            PropertyKind::String, QStringLiteral("Department") });
    cat.addProperty({ PropertyId{"jobtitle"},              PropertyKind::String, QStringLiteral("Job Title") });
    cat.addProperty({ PropertyId{"officelocation"},        PropertyKind::String, QStringLiteral("Office Location") });
    cat.addProperty({ PropertyId{"profession"},            PropertyKind::String, QStringLiteral("Profession") });
    cat.addProperty({ PropertyId{"businesshomepage"},      PropertyKind::String, QStringLiteral("Business Home Page") });

    cat.addProperty({ PropertyId{"assistantname"},         PropertyKind::String, QStringLiteral("Assistant Name") });
    cat.addProperty({ PropertyId{"manager"},               PropertyKind::String, QStringLiteral("Manager") });
    cat.addProperty({ PropertyId{"spousename"},            PropertyKind::String, QStringLiteral("Spouse Name") });
    cat.addProperty({ PropertyId{"children"},              PropertyKind::StringList, QStringLiteral("Children") });

    cat.addProperty({ PropertyId{"personalnotes"},         PropertyKind::String, QStringLiteral("Personal Notes") });
    cat.addProperty({ PropertyId{"birthday"},              PropertyKind::String, QStringLiteral("Birthday") });
    cat.addProperty({ PropertyId{"categories"},            PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"extensions"},            PropertyKind::Json,   QStringLiteral("Open Extensions") });

    return cat;
}

} // namespace Kalburator::Contacts
