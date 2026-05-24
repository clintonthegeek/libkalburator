#include "contactsdomaindefinition.h"
#include "contactscanonproperties.h"
#include "canonjsondiffer.h"
#include "canonjsonmerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Contacts {

Shape::DomainId ContactsDomainDefinition::domain() const
{
    return DomainId{QStringLiteral("contacts")};
}

Shape::Shape ContactsDomainDefinition::canonicalShape() const
{
    return { DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("canon")} };
}

Shape::PropertyCatalogue ContactsDomainDefinition::canonicalCatalogue() const
{
    return makeContactsCanonCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> ContactsDomainDefinition::createCanonicalDiffer() const
{
    return std::make_unique<Kalburator::Shape::CanonJsonDiffer>(contactsCanonPropertyIds());
}

std::unique_ptr<Shape::RecordMerger> ContactsDomainDefinition::createCanonicalMerger() const
{
    return std::make_unique<Kalburator::Shape::CanonJsonMerger>(QStringLiteral("contacts"), contactsCanonPropertyIds());
}

int ContactsDomainDefinition::richnessRank(const Shape::Shape &s) const
{
    if (s == canonicalShape())
        return 100;
    if (s.encoding == EncodingId{QStringLiteral("vcard4")})
        return 50;
    return 10;
}

} // namespace Kalburator::Contacts
