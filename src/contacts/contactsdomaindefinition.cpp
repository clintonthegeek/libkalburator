#include "contactsdomaindefinition.h"
#include "vcardproperties.h"
#include "vcarddiffer.h"
#include "vcardmerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Contacts {

Shape::DomainId ContactsDomainDefinition::domain() const
{
    return DomainId{QStringLiteral("contacts")};
}

Shape::Shape ContactsDomainDefinition::canonicalShape() const
{
    return { DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard4")} };
}

Shape::PropertyCatalogue ContactsDomainDefinition::canonicalCatalogue() const
{
    return makeVCardCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> ContactsDomainDefinition::createCanonicalDiffer() const
{
    return std::make_unique<RecordDifferVCard>();
}

std::unique_ptr<Shape::RecordMerger> ContactsDomainDefinition::createCanonicalMerger() const
{
    return std::make_unique<RecordMergerVCard>();
}

int ContactsDomainDefinition::richnessRank(const Shape::Shape &s) const
{
    if (s == canonicalShape())
        return 10;
    if (s.encoding == EncodingId{QStringLiteral("vcard3")})
        return 8;
    return 0;
}

} // namespace Kalburator::Contacts
