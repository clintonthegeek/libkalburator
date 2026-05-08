#include "vcard3to4transformation.h"

#include <KContacts/Addressee>
#include <KContacts/VCardConverter>

using namespace Kalburator::Shape;

namespace {

KContacts::Addressee parseFirstAddressee(const QByteArray &bytes)
{
    if (bytes.isEmpty()) return {};
    KContacts::VCardConverter conv;
    const auto list = conv.parseVCards(bytes);
    return list.isEmpty() ? KContacts::Addressee{} : list.first();
}

QByteArray serialize(const KContacts::Addressee &a,
                     KContacts::VCardConverter::Version v)
{
    if (a.isEmpty()) return {};
    KContacts::VCardConverter conv;
    return conv.createVCard(a, v);
}

} // namespace

namespace Kalburator::Contacts {

QByteArray VCard3To4Stage::transform(const QByteArray &sourceBytes) const
{
    return serialize(parseFirstAddressee(sourceBytes),
                     KContacts::VCardConverter::v4_0);
}

QByteArray VCard4To3Stage::transform(const QByteArray &sourceBytes) const
{
    return serialize(parseFirstAddressee(sourceBytes),
                     KContacts::VCardConverter::v3_0);
}

LossProfile vcard4ToVcard3Loss()
{
    LossProfile p;
    p.level = LossLevel::IntraDomainLossy;
    // Initial conservative list — refine after Task 9's test exercises
    // KContacts round-trip behavior with a representative vCard.
    p.dropped.insert(PropertyId{QStringLiteral("gender")});
    p.dropped.insert(PropertyId{QStringLiteral("kind")});
    p.dropped.insert(PropertyId{QStringLiteral("anniversary")});
    p.dropped.insert(PropertyId{QStringLiteral("lang")});
    p.dropped.insert(PropertyId{QStringLiteral("member")});
    return p;
}

} // namespace Kalburator::Contacts
