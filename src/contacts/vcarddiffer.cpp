#include "vcarddiffer.h"

#include <KContacts/VCardConverter>
#include <KContacts/Addressee>

using namespace Kalburator::Shape;

namespace {

KContacts::Addressee parseVCard(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KContacts::VCardConverter conv;
    const auto list = conv.parseVCards(data);
    return list.isEmpty() ? KContacts::Addressee{} : list.first();
}

} // namespace

namespace Kalburator::Contacts {

QSet<PropertyId> IRecordDifferVCard::diff(
    const CanonicalRecord &source,
    const CanonicalRecord &baseline) const
{
    if (source.data == baseline.data)
        return {};

    const auto src  = parseVCard(source.data);
    const auto base = parseVCard(baseline.data);

    QSet<PropertyId> changed;

    if (src.isEmpty() && base.isEmpty())
        return {};

    if (src.isEmpty() || base.isEmpty()) {
        changed.insert(PropertyId{"fn"});
        return changed;
    }

    if (src.uid()              != base.uid())              changed.insert(PropertyId{"uid"});
    if (src.formattedName()    != base.formattedName())    changed.insert(PropertyId{"fn"});
    if (src.realName()         != base.realName())         changed.insert(PropertyId{"n"});
    if (src.nickName()         != base.nickName())         changed.insert(PropertyId{"nickname"});
    if (src.organization()     != base.organization())     changed.insert(PropertyId{"org"});
    if (src.title()            != base.title())            changed.insert(PropertyId{"title"});
    if (src.role()             != base.role())             changed.insert(PropertyId{"role"});
    if (src.note()             != base.note())             changed.insert(PropertyId{"note"});
    if (src.birthday()         != base.birthday())         changed.insert(PropertyId{"bday"});
    if (src.revision()         != base.revision())         changed.insert(PropertyId{"rev"});
    if (src.categories()       != base.categories())       changed.insert(PropertyId{"categories"});

    // vCard 4.0 properties (RFC 6350).
    // KContacts::Addressee API surface for these:
    //   gender:      addr.gender() — KContacts::Gender (operator!= defined)
    //   lang:        addr.langs()  — KContacts::Lang::List (preferred languages)
    //   anniversary: addr.anniversary() — QDate (X-Anniversary extension)
    //   kind:        addr.kind() — QString (RFC 6350 §6.1.4)
    if (src.gender()           != base.gender())           changed.insert(PropertyId{"gender"});
    if (src.langs()            != base.langs())            changed.insert(PropertyId{"lang"});
    if (src.anniversary()      != base.anniversary())      changed.insert(PropertyId{"anniversary"});
    if (src.kind()             != base.kind())             changed.insert(PropertyId{"kind"});

    // Multi-value fields: email, phone, address, url
    const auto srcEmails = src.emailList();
    const auto baseEmails = base.emailList();
    if (srcEmails.size() != baseEmails.size() ||
        [&]() {
            for (int i = 0; i < srcEmails.size(); ++i)
                if (srcEmails.at(i).mail() != baseEmails.at(i).mail())
                    return true;
            return false;
        }())
        changed.insert(PropertyId{"email"});

    const auto srcPhones = src.phoneNumbers();
    const auto basePhones = base.phoneNumbers();
    if (srcPhones.size() != basePhones.size())
        changed.insert(PropertyId{"tel"});

    const auto srcAddrs = src.addresses();
    const auto baseAddrs = base.addresses();
    if (srcAddrs.size() != baseAddrs.size())
        changed.insert(PropertyId{"adr"});

    const auto srcUrls = src.url();
    const auto baseUrls = base.url();
    if (srcUrls != baseUrls)
        changed.insert(PropertyId{"url"});

    return changed;
}

bool IRecordDifferVCard::equal(
    const CanonicalRecord &a,
    const CanonicalRecord &b) const
{
    if (a.data == b.data)
        return true;
    return diff(a, b).isEmpty();
}

} // namespace Kalburator::Contacts
