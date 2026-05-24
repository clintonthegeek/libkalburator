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
    // Refined against KContacts reality by Task 9's
    // declaredDropsMatchKContactsReality test slot.
    //
    // NOTE: `anniversary` survives the v4→v3 round-trip via KContacts'
    // X-Anniversary extension — confirmed by the Task 9 test and by
    // KContacts source (Addressee::setAnniversary stores into a custom
    // X-Anniversary field that's serialized in v3 output). It is
    // therefore NOT in the dropped set, even though it's a v4-defined
    // property.
    p.affected.insert(PropertyId{QStringLiteral("gender")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("kind")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("lang")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("member")}, LossKind::Dropped);
    return p;
}

} // namespace Kalburator::Contacts
