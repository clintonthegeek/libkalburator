#include "vcardcanonstages.h"

#include "canonenvelope.h"

#include <KContacts/VCardConverter>
#include <KContacts/Addressee>
#include <KContacts/Email>
#include <KContacts/PhoneNumber>
#include <KContacts/Address>
#include <KContacts/Impp>
#include <KContacts/Related>
#include <KContacts/Picture>
#include <KContacts/Gender>
#include <KContacts/Lang>

#include <QJsonArray>
#include <QJsonObject>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;

/// Convert KContacts::PhoneNumber::Type flags to a human-readable string.
/// We use the canonical vCard property names for portability.
QString phoneTypeString(KContacts::PhoneNumber::Type t)
{
    // Build a comma-separated list from the flag bits we recognise.
    QStringList parts;
    if (t & KContacts::PhoneNumber::Cell)  parts << QStringLiteral("cell");
    if (t & KContacts::PhoneNumber::Home)  parts << QStringLiteral("home");
    if (t & KContacts::PhoneNumber::Work)  parts << QStringLiteral("work");
    if (t & KContacts::PhoneNumber::Fax)   parts << QStringLiteral("fax");
    if (t & KContacts::PhoneNumber::Voice) parts << QStringLiteral("voice");
    return parts.join(QLatin1Char(','));
}

/// Convert KContacts::Address::Type flags to a human-readable string.
QString addressTypeString(KContacts::Address::Type t)
{
    QStringList parts;
    if (t & KContacts::Address::Home) parts << QStringLiteral("home");
    if (t & KContacts::Address::Work) parts << QStringLiteral("work");
    return parts.join(QLatin1Char(','));
}

} // namespace

namespace Kalburator::Contacts {

QByteArray VCard4ToCanonStage::transform(const QByteArray& vcardBytes) const
{
    if (vcardBytes.isEmpty())
        return {};

    KContacts::VCardConverter conv;
    const auto list = conv.parseVCards(vcardBytes);
    if (list.isEmpty())
        return {};

    const KContacts::Addressee& addr = list.first();
    if (addr.isEmpty())
        return {};

    QJsonObject obj;

    // ---- uid ---------------------------------------------------------------
    const QString uid = addr.uid();

    // Stash original vCard uid in providerExtras["x-vcard"]["uid"]
    if (!uid.isEmpty()) {
        QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        QJsonObject xvcard = extras.value(QStringLiteral("x-vcard")).toObject();
        xvcard.insert(QStringLiteral("uid"), uid);
        extras.insert(QStringLiteral("x-vcard"), xvcard);
        obj.insert(providerExtrasKey(), extras);
    }

    // ---- names -------------------------------------------------------------
    {
        QJsonObject nameObj;
        const QString fn = addr.formattedName();
        const QString given  = addr.givenName();
        const QString family = addr.familyName();
        const QString middle = addr.additionalName();
        const QString pfx    = addr.prefix();
        const QString sfx    = addr.suffix();
        if (!fn.isEmpty())     nameObj.insert(QStringLiteral("formatted"), fn);
        if (!given.isEmpty())  nameObj.insert(QStringLiteral("given"),     given);
        if (!family.isEmpty()) nameObj.insert(QStringLiteral("family"),    family);
        if (!middle.isEmpty()) nameObj.insert(QStringLiteral("middle"),    middle);
        if (!pfx.isEmpty())    nameObj.insert(QStringLiteral("prefix"),    pfx);
        if (!sfx.isEmpty())    nameObj.insert(QStringLiteral("suffix"),    sfx);
        if (!nameObj.isEmpty()) {
            QJsonArray names;
            names.append(nameObj);
            obj.insert(QStringLiteral("names"), names);
        }
    }

    // ---- nicknames ---------------------------------------------------------
    {
        const QString nick = addr.nickName();
        if (!nick.isEmpty()) {
            QJsonArray arr;
            QJsonObject entry;
            entry.insert(QStringLiteral("value"), nick);
            arr.append(entry);
            obj.insert(QStringLiteral("nicknames"), arr);
        }
    }

    // ---- emails ------------------------------------------------------------
    {
        const auto emails = addr.emailList();
        if (!emails.isEmpty()) {
            QJsonArray arr;
            for (const auto& e : emails) {
                const QString mail = e.mail();
                if (mail.isEmpty())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("value"),   mail);
                entry.insert(QStringLiteral("primary"), e.isPreferred());
                // KContacts::Email::Type is a QFlags; check bit-flags manually.
                // We encode as a comma-separated string of type labels.
                const auto t = e.type();
                QStringList typeParts;
                if (t & KContacts::Email::Work)  typeParts << QStringLiteral("work");
                if (t & KContacts::Email::Home)  typeParts << QStringLiteral("home");
                if (t & KContacts::Email::Other) typeParts << QStringLiteral("other");
                if (!typeParts.isEmpty())
                    entry.insert(QStringLiteral("type"), typeParts.join(QLatin1Char(',')));
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("emails"), arr);
        }
    }

    // ---- phones ------------------------------------------------------------
    {
        const auto phones = addr.phoneNumbers();
        if (!phones.isEmpty()) {
            QJsonArray arr;
            for (const auto& ph : phones) {
                const QString num = ph.number();
                if (num.isEmpty())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("value"),   num);
                entry.insert(QStringLiteral("primary"), ph.isPreferred());
                const QString ts = phoneTypeString(ph.type());
                if (!ts.isEmpty())
                    entry.insert(QStringLiteral("type"), ts);
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("phones"), arr);
        }
    }

    // ---- addresses ---------------------------------------------------------
    {
        const auto addrs = addr.addresses();
        if (!addrs.isEmpty()) {
            QJsonArray arr;
            for (const auto& a : addrs) {
                if (a.isEmpty())
                    continue;
                QJsonObject entry;
                if (!a.street().isEmpty())     entry.insert(QStringLiteral("street"),     a.street());
                if (!a.locality().isEmpty())   entry.insert(QStringLiteral("locality"),   a.locality());
                if (!a.region().isEmpty())     entry.insert(QStringLiteral("region"),     a.region());
                if (!a.postalCode().isEmpty()) entry.insert(QStringLiteral("postalCode"), a.postalCode());
                if (!a.country().isEmpty())    entry.insert(QStringLiteral("country"),    a.country());
                const QString ts = addressTypeString(a.type());
                if (!ts.isEmpty())
                    entry.insert(QStringLiteral("type"), ts);
                if (!entry.isEmpty())
                    arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("addresses"), arr);
        }
    }

    // ---- organizations -----------------------------------------------------
    {
        const QString org   = addr.organization();
        const QString dept  = addr.department();
        const QString title = addr.title();
        const QString role  = addr.role();
        if (!org.isEmpty() || !dept.isEmpty() || !title.isEmpty() || !role.isEmpty()) {
            QJsonObject entry;
            if (!org.isEmpty())   entry.insert(QStringLiteral("name"),       org);
            if (!dept.isEmpty())  entry.insert(QStringLiteral("department"), dept);
            if (!title.isEmpty()) entry.insert(QStringLiteral("title"),      title);
            if (!role.isEmpty())  entry.insert(QStringLiteral("role"),       role);
            QJsonArray arr;
            arr.append(entry);
            obj.insert(QStringLiteral("organizations"), arr);
        }
    }

    // ---- urls --------------------------------------------------------------
    {
        QJsonArray arr;
        // Primary URL
        const auto primaryUrl = addr.url();
        if (primaryUrl.url().isValid()) {
            const QString urlStr = primaryUrl.url().toString();
            if (!urlStr.isEmpty()) {
                QJsonObject entry;
                entry.insert(QStringLiteral("value"), urlStr);
                arr.append(entry);
            }
        }
        // Extra URLs
        for (const auto& ru : addr.extraUrlList()) {
            if (!ru.url().isValid())
                continue;
            const QString urlStr = ru.url().toString();
            if (urlStr.isEmpty())
                continue;
            QJsonObject entry;
            entry.insert(QStringLiteral("value"), urlStr);
            arr.append(entry);
        }
        if (!arr.isEmpty())
            obj.insert(QStringLiteral("urls"), arr);
    }

    // ---- imClients ---------------------------------------------------------
    {
        const auto impps = addr.imppList();
        if (!impps.isEmpty()) {
            QJsonArray arr;
            for (const auto& impp : impps) {
                const QUrl imppAddr = impp.address();
                if (!imppAddr.isValid())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("value"), imppAddr.toString());
                const QString scheme = imppAddr.scheme();
                if (!scheme.isEmpty())
                    entry.insert(QStringLiteral("protocol"), scheme);
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("imClients"), arr);
        }
    }

    // ---- birthday ----------------------------------------------------------
    {
        const QDateTime bday = addr.birthday();
        if (bday.isValid()) {
            QJsonObject entry;
            const bool hasTime = addr.birthdayHasTime();
            if (hasTime) {
                entry.insert(QStringLiteral("date"), bday.toString(Qt::ISODate));
                entry.insert(QStringLiteral("hasYear"), true);
            } else {
                entry.insert(QStringLiteral("date"), bday.date().toString(Qt::ISODate));
                entry.insert(QStringLiteral("hasYear"), true);
            }
            obj.insert(QStringLiteral("birthday"), entry);
        }
    }

    // ---- anniversary -------------------------------------------------------
    {
        const QDate ann = addr.anniversary();
        if (ann.isValid()) {
            QJsonObject entry;
            entry.insert(QStringLiteral("date"),    ann.toString(Qt::ISODate));
            entry.insert(QStringLiteral("hasYear"), true);
            obj.insert(QStringLiteral("anniversary"), entry);
        }
    }

    // ---- gender ------------------------------------------------------------
    {
        const KContacts::Gender g = addr.gender();
        if (g.isValid()) {
            QJsonObject entry;
            const QString sex = g.gender();
            if (!sex.isEmpty())
                entry.insert(QStringLiteral("sex"), sex);
            const QString identity = g.comment();
            if (!identity.isEmpty())
                entry.insert(QStringLiteral("identity"), identity);
            if (!entry.isEmpty())
                obj.insert(QStringLiteral("gender"), entry);
        }
    }

    // ---- notes -------------------------------------------------------------
    {
        const QString note = addr.note();
        if (!note.isEmpty())
            obj.insert(QStringLiteral("notes"), note);
    }

    // ---- photos ------------------------------------------------------------
    {
        const KContacts::Picture photo = addr.photo();
        if (!photo.isEmpty()) {
            QJsonObject entry;
            if (photo.isIntern()) {
                entry.insert(QStringLiteral("data"),
                             QString::fromLatin1(photo.rawData().toBase64()));
                if (!photo.type().isEmpty())
                    entry.insert(QStringLiteral("mediaType"), photo.type());
            } else {
                const QString urlStr = photo.url();
                if (!urlStr.isEmpty())
                    entry.insert(QStringLiteral("url"), urlStr);
                if (!photo.type().isEmpty())
                    entry.insert(QStringLiteral("mediaType"), photo.type());
            }
            if (!entry.isEmpty()) {
                QJsonArray arr;
                arr.append(entry);
                obj.insert(QStringLiteral("photos"), arr);
            }
        }
    }

    // ---- categories --------------------------------------------------------
    {
        const QStringList cats = addr.categories();
        if (!cats.isEmpty()) {
            QJsonArray arr;
            for (const auto& c : cats)
                arr.append(c);
            obj.insert(QStringLiteral("categories"), arr);
        }
    }

    // ---- languages ---------------------------------------------------------
    {
        const auto langs = addr.langs();
        if (!langs.isEmpty()) {
            QJsonArray arr;
            for (const auto& l : langs) {
                const QString lang = l.language();
                if (!lang.isEmpty())
                    arr.append(lang);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("languages"), arr);
        }
    }

    // ---- timeZone ----------------------------------------------------------
    {
        const KContacts::TimeZone tz = addr.timeZone();
        if (tz.isValid()) {
            // TimeZone stores an offset in minutes; encode as UTC offset string.
            const int offsetMin = tz.offset();
            const int h = qAbs(offsetMin) / 60;
            const int m = qAbs(offsetMin) % 60;
            const QString tzStr = QString::asprintf("%s%02d:%02d",
                offsetMin >= 0 ? "+" : "-", h, m);
            obj.insert(QStringLiteral("timeZone"), tzStr);
        }
    }

    // ---- relations (RELATED) -----------------------------------------------
    {
        const auto rels = addr.relationships();
        if (!rels.isEmpty()) {
            QJsonArray arr;
            for (const auto& r : rels) {
                if (!r.isValid())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("value"), r.related());
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("relations"), arr);
        }
    }

    // ---- memberships (MEMBER) ----------------------------------------------
    {
        const QStringList members = addr.members();
        if (!members.isEmpty()) {
            QJsonArray arr;
            for (const auto& m : members) {
                if (m.isEmpty())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("value"), m);
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("memberships"), arr);
        }
    }

    // ---- providerExtras["x-vcard"] — custom/X- properties -----------------
    // KContacts stores custom props as "APP-KEY:value" strings returned by
    // addr.customs(). We collect all of them into providerExtras["x-vcard"]
    // as a JSON object keyed by "APP-KEY" to preserve them verbatim for the
    // round-trip. KContacts also exposes certain well-known X- properties via
    // dedicated getters (anniversary, assistantsName, blogFeed, managersName,
    // office, profession, spousesName), so we stash those too if present.
    {
        QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        QJsonObject xvcard = extras.value(QStringLiteral("x-vcard")).toObject();

        // Raw custom entries
        const QStringList customs = addr.customs();
        for (const QString& entry : customs) {
            // Format is "APP-KEY:value"
            const int sep = entry.indexOf(QLatin1Char(':'));
            if (sep < 0)
                continue;
            const QString key   = entry.left(sep);
            const QString value = entry.mid(sep + 1);
            xvcard.insert(key, value);
        }

        // Well-known KContacts X- extensions not yet covered above
        const QString assistantsName = addr.assistantsName();
        const QString managersName   = addr.managersName();
        const QString office         = addr.office();
        const QString profession     = addr.profession();
        const QString spousesName    = addr.spousesName();
        const QUrl    blogFeed       = addr.blogFeed();
        if (!assistantsName.isEmpty())
            xvcard.insert(QStringLiteral("X-AssistantsName"), assistantsName);
        if (!managersName.isEmpty())
            xvcard.insert(QStringLiteral("X-ManagersName"), managersName);
        if (!office.isEmpty())
            xvcard.insert(QStringLiteral("X-Office"), office);
        if (!profession.isEmpty())
            xvcard.insert(QStringLiteral("X-Profession"), profession);
        if (!spousesName.isEmpty())
            xvcard.insert(QStringLiteral("X-SpousesName"), spousesName);
        if (!blogFeed.isEmpty())
            xvcard.insert(QStringLiteral("X-BlogFeed"), blogFeed.toString());

        if (!xvcard.isEmpty()) {
            extras.insert(QStringLiteral("x-vcard"), xvcard);
            obj.insert(providerExtrasKey(), extras);
        }
    }

    // ---- Stamp the envelope last (writes _canon + uid) ---------------------
    stampEnvelope(obj, QStringLiteral("contacts"), uid);

    return serialize(obj);
}

// ---------------------------------------------------------------------------
// CanonToVCard4Stage — stub; implemented in Task A3
// ---------------------------------------------------------------------------

QByteArray CanonToVCard4Stage::transform(const QByteArray& /*canonBytes*/) const
{
    return {};
}

}  // namespace Kalburator::Contacts
