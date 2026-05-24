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
#include <KContacts/ResourceLocatorUrl>
#include <KContacts/TimeZone>

#include <QJsonArray>
#include <QJsonDocument>
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
// CanonToVCard4Stage — canon JSON → vCard4 (Task A3)
// ---------------------------------------------------------------------------

QByteArray CanonToVCard4Stage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};

    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};

    KContacts::Addressee addr;

    // ---- uid ---------------------------------------------------------------
    // Prefer the uid stashed in providerExtras["x-vcard"]["uid"]; fall back to
    // the top-level "uid" field written by stampEnvelope.
    {
        const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        const QJsonObject xvcard = extras.value(QStringLiteral("x-vcard")).toObject();
        const QString storedUid  = xvcard.value(QStringLiteral("uid")).toString();
        if (!storedUid.isEmpty())
            addr.setUid(storedUid);
        else {
            const QString topUid = obj.value(QStringLiteral("uid")).toString();
            if (!topUid.isEmpty())
                addr.setUid(topUid);
        }
    }

    // ---- names -------------------------------------------------------------
    {
        const QJsonArray names = obj.value(QStringLiteral("names")).toArray();
        if (!names.isEmpty()) {
            const QJsonObject n = names.at(0).toObject();
            const QString fn     = n.value(QStringLiteral("formatted")).toString();
            const QString given  = n.value(QStringLiteral("given")).toString();
            const QString family = n.value(QStringLiteral("family")).toString();
            const QString middle = n.value(QStringLiteral("middle")).toString();
            const QString pfx    = n.value(QStringLiteral("prefix")).toString();
            const QString sfx    = n.value(QStringLiteral("suffix")).toString();
            if (!fn.isEmpty())     addr.setFormattedName(fn);
            if (!given.isEmpty())  addr.setGivenName(given);
            if (!family.isEmpty()) addr.setFamilyName(family);
            if (!middle.isEmpty()) addr.setAdditionalName(middle);
            if (!pfx.isEmpty())    addr.setPrefix(pfx);
            if (!sfx.isEmpty())    addr.setSuffix(sfx);
        }
    }

    // ---- nicknames ---------------------------------------------------------
    {
        const QJsonArray nicks = obj.value(QStringLiteral("nicknames")).toArray();
        if (!nicks.isEmpty()) {
            const QString nick = nicks.at(0).toObject()
                                      .value(QStringLiteral("value")).toString();
            if (!nick.isEmpty())
                addr.setNickName(nick);
        }
    }

    // ---- emails ------------------------------------------------------------
    {
        const QJsonArray emails = obj.value(QStringLiteral("emails")).toArray();
        for (const auto& ev : emails) {
            const QJsonObject e  = ev.toObject();
            const QString mail   = e.value(QStringLiteral("value")).toString();
            if (mail.isEmpty())
                continue;
            KContacts::Email email(mail);
            email.setPreferred(e.value(QStringLiteral("primary")).toBool());
            const QString typeStr = e.value(QStringLiteral("type")).toString();
            KContacts::Email::Type flags{};
            if (typeStr.contains(QStringLiteral("work")))  flags |= KContacts::Email::Work;
            if (typeStr.contains(QStringLiteral("home")))  flags |= KContacts::Email::Home;
            if (typeStr.contains(QStringLiteral("other"))) flags |= KContacts::Email::Other;
            if (flags != KContacts::Email::Type{})
                email.setType(flags);
            addr.addEmail(email);
        }
    }

    // ---- phones ------------------------------------------------------------
    {
        const QJsonArray phones = obj.value(QStringLiteral("phones")).toArray();
        for (const auto& pv : phones) {
            const QJsonObject p = pv.toObject();
            const QString num   = p.value(QStringLiteral("value")).toString();
            if (num.isEmpty())
                continue;
            KContacts::PhoneNumber::Type flags{};
            const QString typeStr = p.value(QStringLiteral("type")).toString();
            if (typeStr.contains(QStringLiteral("cell")))  flags |= KContacts::PhoneNumber::Cell;
            if (typeStr.contains(QStringLiteral("home")))  flags |= KContacts::PhoneNumber::Home;
            if (typeStr.contains(QStringLiteral("work")))  flags |= KContacts::PhoneNumber::Work;
            if (typeStr.contains(QStringLiteral("fax")))   flags |= KContacts::PhoneNumber::Fax;
            if (typeStr.contains(QStringLiteral("voice"))) flags |= KContacts::PhoneNumber::Voice;
            if (p.value(QStringLiteral("primary")).toBool())
                flags |= KContacts::PhoneNumber::Pref;
            KContacts::PhoneNumber phone(num, flags);
            addr.insertPhoneNumber(phone);
        }
    }

    // ---- addresses ---------------------------------------------------------
    {
        const QJsonArray addrs = obj.value(QStringLiteral("addresses")).toArray();
        for (const auto& av : addrs) {
            const QJsonObject a = av.toObject();
            KContacts::Address address;
            const QString street  = a.value(QStringLiteral("street")).toString();
            const QString loc     = a.value(QStringLiteral("locality")).toString();
            const QString region  = a.value(QStringLiteral("region")).toString();
            const QString postal  = a.value(QStringLiteral("postalCode")).toString();
            const QString country = a.value(QStringLiteral("country")).toString();
            if (!street.isEmpty())  address.setStreet(street);
            if (!loc.isEmpty())     address.setLocality(loc);
            if (!region.isEmpty())  address.setRegion(region);
            if (!postal.isEmpty())  address.setPostalCode(postal);
            if (!country.isEmpty()) address.setCountry(country);
            const QString typeStr = a.value(QStringLiteral("type")).toString();
            KContacts::Address::Type flags{};
            if (typeStr.contains(QStringLiteral("home"))) flags |= KContacts::Address::Home;
            if (typeStr.contains(QStringLiteral("work"))) flags |= KContacts::Address::Work;
            if (flags != KContacts::Address::Type{})
                address.setType(flags);
            addr.insertAddress(address);
        }
    }

    // ---- organizations -----------------------------------------------------
    {
        const QJsonArray orgs = obj.value(QStringLiteral("organizations")).toArray();
        if (!orgs.isEmpty()) {
            const QJsonObject o = orgs.at(0).toObject();
            const QString name  = o.value(QStringLiteral("name")).toString();
            const QString dept  = o.value(QStringLiteral("department")).toString();
            const QString title = o.value(QStringLiteral("title")).toString();
            const QString role  = o.value(QStringLiteral("role")).toString();
            if (!name.isEmpty())  addr.setOrganization(name);
            if (!dept.isEmpty())  addr.setDepartment(dept);
            if (!title.isEmpty()) addr.setTitle(title);
            if (!role.isEmpty())  addr.setRole(role);
        }
    }

    // ---- urls --------------------------------------------------------------
    {
        const QJsonArray urls = obj.value(QStringLiteral("urls")).toArray();
        bool first = true;
        for (const auto& uv : urls) {
            const QString urlStr = uv.toObject()
                                     .value(QStringLiteral("value")).toString();
            if (urlStr.isEmpty())
                continue;
            KContacts::ResourceLocatorUrl rlu;
            rlu.setUrl(QUrl(urlStr));
            if (first) {
                addr.setUrl(rlu);
                first = false;
            } else {
                addr.insertExtraUrl(rlu);
            }
        }
    }

    // ---- imClients ---------------------------------------------------------
    {
        const QJsonArray impps = obj.value(QStringLiteral("imClients")).toArray();
        for (const auto& iv : impps) {
            const QJsonObject i = iv.toObject();
            const QString val   = i.value(QStringLiteral("value")).toString();
            if (val.isEmpty())
                continue;
            addr.insertImpp(KContacts::Impp(QUrl(val)));
        }
    }

    // ---- birthday ----------------------------------------------------------
    {
        const QJsonObject bday = obj.value(QStringLiteral("birthday")).toObject();
        if (!bday.isEmpty()) {
            const QString dateStr = bday.value(QStringLiteral("date")).toString();
            if (!dateStr.isEmpty()) {
                // Try ISO date with time first, then date-only
                const QDateTime dt = QDateTime::fromString(dateStr, Qt::ISODate);
                if (dt.isValid())
                    addr.setBirthday(dt, false);
                else {
                    const QDate d = QDate::fromString(dateStr, Qt::ISODate);
                    if (d.isValid())
                        addr.setBirthday(d);
                }
            }
        }
    }

    // ---- anniversary -------------------------------------------------------
    {
        const QJsonObject ann = obj.value(QStringLiteral("anniversary")).toObject();
        if (!ann.isEmpty()) {
            const QString dateStr = ann.value(QStringLiteral("date")).toString();
            if (!dateStr.isEmpty()) {
                const QDate d = QDate::fromString(dateStr, Qt::ISODate);
                if (d.isValid())
                    addr.setAnniversary(d);
            }
        }
    }

    // ---- gender ------------------------------------------------------------
    {
        const QJsonObject g = obj.value(QStringLiteral("gender")).toObject();
        if (!g.isEmpty()) {
            const QString sex      = g.value(QStringLiteral("sex")).toString();
            const QString identity = g.value(QStringLiteral("identity")).toString();
            KContacts::Gender gender;
            if (!sex.isEmpty())      gender.setGender(sex);
            if (!identity.isEmpty()) gender.setComment(identity);
            addr.setGender(gender);
        }
    }

    // ---- notes -------------------------------------------------------------
    {
        const QString note = obj.value(QStringLiteral("notes")).toString();
        if (!note.isEmpty())
            addr.setNote(note);
    }

    // ---- photos ------------------------------------------------------------
    {
        const QJsonArray photos = obj.value(QStringLiteral("photos")).toArray();
        if (!photos.isEmpty()) {
            const QJsonObject p = photos.at(0).toObject();
            KContacts::Picture pic;
            if (p.contains(QStringLiteral("data"))) {
                const QByteArray raw = QByteArray::fromBase64(
                    p.value(QStringLiteral("data")).toString().toLatin1());
                pic.setRawData(raw, p.value(QStringLiteral("mediaType")).toString());
            } else if (p.contains(QStringLiteral("url"))) {
                pic.setUrl(p.value(QStringLiteral("url")).toString());
            }
            if (!pic.isEmpty())
                addr.setPhoto(pic);
        }
    }

    // ---- categories --------------------------------------------------------
    {
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty()) {
            QStringList catList;
            for (const auto& c : cats)
                catList << c.toString();
            addr.setCategories(catList);
        }
    }

    // ---- languages ---------------------------------------------------------
    {
        const QJsonArray langs = obj.value(QStringLiteral("languages")).toArray();
        if (!langs.isEmpty()) {
            KContacts::Lang::List langList;
            for (const auto& lv : langs) {
                const QString lang = lv.toString();
                if (!lang.isEmpty())
                    langList << KContacts::Lang(lang);
            }
            addr.setLangs(langList);
        }
    }

    // ---- timeZone ----------------------------------------------------------
    {
        const QString tzStr = obj.value(QStringLiteral("timeZone")).toString();
        if (!tzStr.isEmpty()) {
            // tzStr is like "+05:30" or "-08:00"; parse back to minutes offset.
            const bool neg = tzStr.startsWith(QLatin1Char('-'));
            const QString body = tzStr.mid(1); // strip sign
            const QStringList parts = body.split(QLatin1Char(':'));
            if (parts.size() == 2) {
                const int h = parts[0].toInt();
                const int m = parts[1].toInt();
                int offset = h * 60 + m;
                if (neg) offset = -offset;
                addr.setTimeZone(KContacts::TimeZone(offset));
            }
        }
    }

    // ---- relations (RELATED) -----------------------------------------------
    {
        const QJsonArray rels = obj.value(QStringLiteral("relations")).toArray();
        if (!rels.isEmpty()) {
            KContacts::Related::List relList;
            for (const auto& rv : rels) {
                const QString val = rv.toObject()
                                      .value(QStringLiteral("value")).toString();
                if (!val.isEmpty())
                    relList << KContacts::Related(val);
            }
            addr.setRelationships(relList);
        }
    }

    // ---- memberships (MEMBER) ----------------------------------------------
    {
        const QJsonArray members = obj.value(QStringLiteral("memberships")).toArray();
        if (!members.isEmpty()) {
            QStringList memberList;
            for (const auto& mv : members) {
                const QString val = mv.toObject()
                                      .value(QStringLiteral("value")).toString();
                if (!val.isEmpty())
                    memberList << val;
            }
            addr.setMembers(memberList);
        }
    }

    // ---- Stash Reversible Google-only fields as custom props ---------------
    // sipAddresses, calendarUrls, externalIds are classified Reversible in the
    // loss profile (contactsstockshapes.cpp canonToVcard4Loss).  Honor that
    // contract: stash them as custom properties so VCard4ToCanonStage recovers
    // them verbatim into providerExtras["x-vcard"] on the next forward pass.
    // Key naming: "CANON-<FIELDNAME>" — split at first '-' yields
    // app="CANON", name="<FIELDNAME>", which round-trips through KContacts
    // customs() as "CANON-<FIELDNAME>:<jsonString>".
    {
        const QJsonValue sipVal  = obj.value(QStringLiteral("sipAddresses"));
        const QJsonValue calVal  = obj.value(QStringLiteral("calendarUrls"));
        const QJsonValue extVal  = obj.value(QStringLiteral("externalIds"));

        auto stashJson = [&](const QString& name, const QJsonValue& val) {
            if (val.isUndefined() || val.isNull())
                return;
            // Accept non-empty arrays or non-empty objects.
            const bool isArr = val.isArray()  && !val.toArray().isEmpty();
            const bool isObj = val.isObject() && !val.toObject().isEmpty();
            if (!isArr && !isObj)
                return;
            const QByteArray json = val.isArray()
                ? QJsonDocument(val.toArray()).toJson(QJsonDocument::Compact)
                : QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact);
            addr.insertCustom(QStringLiteral("CANON"), name,
                              QString::fromUtf8(json));
        };

        stashJson(QStringLiteral("SIPADDRESSES"), sipVal);
        stashJson(QStringLiteral("CALENDARURLS"), calVal);
        stashJson(QStringLiteral("EXTERNALIDS"),  extVal);
    }

    // ---- providerExtras["x-vcard"] — re-emit custom/X- props --------------
    // The forward stage stored customs as JSON keys "APP-NAME" → value.
    // Re-emit them using insertCustom(app, name, value) where app = everything
    // before the first '-' and name = the rest.  The special "uid" key was
    // already consumed above.  Well-known KContacts X- helpers (AssistantsName,
    // etc.) were stored under manually chosen keys and are restored via setters.
    {
        const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        const QJsonObject xvcard = extras.value(QStringLiteral("x-vcard")).toObject();

        // Helper keys that the forward stage stored via dedicated getters.
        // Restore via setters to avoid double-emission.
        static const QSet<QString> kWellKnown = {
            QStringLiteral("X-AssistantsName"),
            QStringLiteral("X-ManagersName"),
            QStringLiteral("X-Office"),
            QStringLiteral("X-Profession"),
            QStringLiteral("X-SpousesName"),
            QStringLiteral("X-BlogFeed"),
            QStringLiteral("uid"),
        };

        const QString assistants = xvcard.value(QStringLiteral("X-AssistantsName")).toString();
        const QString managers   = xvcard.value(QStringLiteral("X-ManagersName")).toString();
        const QString office     = xvcard.value(QStringLiteral("X-Office")).toString();
        const QString profession = xvcard.value(QStringLiteral("X-Profession")).toString();
        const QString spouses    = xvcard.value(QStringLiteral("X-SpousesName")).toString();
        const QString blogFeed   = xvcard.value(QStringLiteral("X-BlogFeed")).toString();
        if (!assistants.isEmpty()) addr.setAssistantsName(assistants);
        if (!managers.isEmpty())   addr.setManagersName(managers);
        if (!office.isEmpty())     addr.setOffice(office);
        if (!profession.isEmpty()) addr.setProfession(profession);
        if (!spouses.isEmpty())    addr.setSpousesName(spouses);
        if (!blogFeed.isEmpty())   addr.setBlogFeed(QUrl(blogFeed));

        // Remaining entries are raw customs: key = "APP-NAME", value = string.
        for (auto it = xvcard.constBegin(); it != xvcard.constEnd(); ++it) {
            if (kWellKnown.contains(it.key()))
                continue;
            const QString key   = it.key();
            const QString value = it.value().toString();
            // Split "APP-NAME" at the first '-' to recover app + name.
            const int dash = key.indexOf(QLatin1Char('-'));
            if (dash > 0) {
                const QString app  = key.left(dash);
                const QString name = key.mid(dash + 1);
                addr.insertCustom(app, name, value);
            } else {
                // No dash: store under a synthetic app name.
                addr.insertCustom(QStringLiteral("X"), key, value);
            }
        }
    }

    KContacts::VCardConverter conv;
    return conv.createVCard(addr, KContacts::VCardConverter::v4_0);
}

}  // namespace Kalburator::Contacts
