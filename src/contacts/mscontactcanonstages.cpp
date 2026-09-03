#include "mscontactcanonstages.h"

#include "canonenvelope.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;
using Kalburator::Shape::CanonEnvelope::stampProviderExtrasDigest;

constexpr auto kCarrierPrefix = "x-canon-";
constexpr auto kExtensionName = "kalburator.canon";

QString carrierKey(const QString& propId)
{
    QString kebab;
    for (const QChar c : propId) {
        if (c.isUpper()) {
            kebab += QLatin1Char('-');
            kebab += c.toLower();
        } else {
            kebab += c;
        }
    }
    return QLatin1String(kCarrierPrefix) + kebab;
}

QString propFromCarrierKey(const QString& key)
{
    if (!key.startsWith(QLatin1String(kCarrierPrefix)))
        return {};
    const QString suffix = key.mid(int(qstrlen(kCarrierPrefix)));
    QString camel;
    bool upper = false;
    for (const QChar c : suffix) {
        if (c == QLatin1Char('-')) {
            upper = true;
        } else if (upper) {
            camel += c.toUpper();
            upper = false;
        } else {
            camel += c;
        }
    }
    return camel;
}

QString valueToCarrierString(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (d == qint64(d))
            return QString::number(qint64(d));
        return QString::number(d);
    }
    case QJsonValue::String: return v.toString();
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    default:                 return {};
    }
}

QJsonValue carrierStringToValue(const QJsonValue& v)
{
    if (v.type() != QJsonValue::String)
        return v;
    const QString s = v.toString();
    if (s == QStringLiteral("true"))   return QJsonValue(true);
    if (s == QStringLiteral("false"))  return QJsonValue(false);
    bool ok = false;
    const qlonglong i = s.toLongLong(&ok);
    if (ok && QString::number(i) == s)
        return QJsonValue(double(i));
    const QChar first = s.isEmpty() ? QChar() : s.at(0);
    if (first == QLatin1Char('{') || first == QLatin1Char('[')) {
        const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
        if (doc.isObject())
            return QJsonValue(doc.object());
        if (doc.isArray())
            return QJsonValue(doc.array());
    }
    return QJsonValue(s);
}

/// Non-empty string read (O60: never signal absence with a default-typed
/// QJsonValue; explicit is-string + non-empty checks).
QString strValue(const QJsonObject& o, const QString& key)
{
    const QJsonValue v = o.value(key);
    if (v.type() != QJsonValue::String)
        return {};
    return v.toString();
}

bool nonEmptyString(const QJsonObject& o, const QString& key)
{
    return !strValue(o, key).isEmpty();
}

} // namespace

namespace Kalburator::Contacts {

// ---------------------------------------------------------------------------
// Promote — Graph contact JSON → canon JSON (lossless by construction)
// ---------------------------------------------------------------------------

QByteArray MsContactToCanonStage::transform(const QByteArray& msBytes) const
{
    if (msBytes.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(msBytes);
    if (!doc.isObject())
        return {};
    const QJsonObject contact = doc.object();

    QJsonObject obj;
    QJsonObject extras;

    // ---- uid ← id -----------------------------------------------------------
    const QString id = strValue(contact, QStringLiteral("id"));

    // ---- flat single name → canon names[] -----------------------------------
    {
        static const QList<QPair<QString, QString>> kMap = {
            { QStringLiteral("displayName"),    QStringLiteral("formatted") },
            { QStringLiteral("givenName"),      QStringLiteral("given") },
            { QStringLiteral("surname"),        QStringLiteral("family") },
            { QStringLiteral("middleName"),     QStringLiteral("middle") },
            { QStringLiteral("title"),          QStringLiteral("prefix") },
            { QStringLiteral("generation"),     QStringLiteral("suffix") },
            { QStringLiteral("yomiGivenName"),  QStringLiteral("phoneticGiven") },
            { QStringLiteral("yomiSurname"),    QStringLiteral("phoneticFamily") },
            { QStringLiteral("fileAs"),         QStringLiteral("fileAs") },
        };
        QJsonObject entry;
        for (const auto& [from, to] : kMap) {
            const QString v = strValue(contact, from);
            if (!v.isEmpty())
                entry.insert(to, v);
        }
        if (!entry.isEmpty())
            obj.insert(QStringLiteral("names"), QJsonArray{ entry });
    }

    // ---- nickName → nicknames[{value}] ---------------------------------------
    {
        const QString nick = strValue(contact, QStringLiteral("nickName"));
        if (!nick.isEmpty())
            obj.insert(QStringLiteral("nicknames"),
                       QJsonArray{ QJsonObject{ { QStringLiteral("value"), nick } } });
    }

    // ---- emails: rows + positional primary typing -----------------------------
    {
        QJsonArray emails;
        QString primaryAddress;
        const QJsonObject primaryRow =
            contact.value(QStringLiteral("primaryEmailAddress")).toObject();
        primaryAddress = strValue(primaryRow, QStringLiteral("address"));

        bool sawPrimary = false;
        for (const auto& rv :
             contact.value(QStringLiteral("emailAddresses")).toArray()) {
            const QJsonObject row = rv.toObject();
            QJsonObject e;
            const QString address = strValue(row, QStringLiteral("address"));
            if (address.isEmpty())
                continue;
            e.insert(QStringLiteral("value"), address);
            const QString name = strValue(row, QStringLiteral("name"));
            if (!name.isEmpty())
                e.insert(QStringLiteral("name"), name);
            if (!primaryAddress.isEmpty() && address == primaryAddress) {
                e.insert(QStringLiteral("primary"), true);
                sawPrimary = true;
            }
            emails.append(e);
        }
        // primaryEmailAddress present but not mirrored in emailAddresses:
        // keep it as its own flagged row.
        if (!primaryAddress.isEmpty() && !sawPrimary) {
            QJsonObject e;
            e.insert(QStringLiteral("value"), primaryAddress);
            const QString name = strValue(primaryRow, QStringLiteral("name"));
            if (!name.isEmpty())
                e.insert(QStringLiteral("name"), name);
            e.insert(QStringLiteral("primary"), true);
            emails.append(e);
        }
        if (!emails.isEmpty())
            obj.insert(QStringLiteral("emails"), emails);
    }

    // ---- phones: fixed buckets → typed rows ------------------------------------
    {
        QJsonArray phones;
        auto bucket = [&phones](const QJsonArray& src, const QString& type) {
            for (const auto& pv : src) {
                const QString v = pv.toString();
                if (v.isEmpty())
                    continue;
                phones.append(QJsonObject{ { QStringLiteral("value"), v },
                                           { QStringLiteral("type"), type } });
            }
        };
        bucket(contact.value(QStringLiteral("homePhones")).toArray(),
               QStringLiteral("home"));
        bucket(contact.value(QStringLiteral("businessPhones")).toArray(),
               QStringLiteral("work"));
        const QString mobile = strValue(contact, QStringLiteral("mobilePhone"));
        if (!mobile.isEmpty())
            phones.append(QJsonObject{ { QStringLiteral("value"), mobile },
                                       { QStringLiteral("type"), QStringLiteral("mobile") } });
        if (!phones.isEmpty())
            obj.insert(QStringLiteral("phones"), phones);
    }

    // ---- imAddresses StringList → imClients[{username}] -------------------------
    {
        QJsonArray imClients;
        for (const auto& iv :
             contact.value(QStringLiteral("imAddresses")).toArray()) {
            const QString v = iv.toString();
            if (v.isEmpty())
                continue;
            imClients.append(QJsonObject{ { QStringLiteral("username"), v } });
        }
        if (!imClients.isEmpty())
            obj.insert(QStringLiteral("imClients"), imClients);
    }

    // ---- addresses: three fixed slots → typed rows -------------------------------
    {
        static const QList<QPair<QString, QString>> kSlots = {
            { QStringLiteral("homeAddress"),     QStringLiteral("home") },
            { QStringLiteral("businessAddress"), QStringLiteral("work") },
            { QStringLiteral("otherAddress"),    QStringLiteral("other") },
        };
        static const QList<QPair<QString, QString>> kKeyMap = {
            { QStringLiteral("street"),          QStringLiteral("street") },
            { QStringLiteral("city"),            QStringLiteral("city") },
            { QStringLiteral("state"),           QStringLiteral("region") },
            { QStringLiteral("countryOrRegion"), QStringLiteral("country") },
            { QStringLiteral("postalCode"),      QStringLiteral("postalCode") },
        };
        QJsonArray addresses;
        for (const auto& [slot, type] : kSlots) {
            const QJsonObject a =
                contact.value(slot).toObject();
            if (a.isEmpty())
                continue;
            QJsonObject entry;
            for (const auto& [from, to] : kKeyMap) {
                const QString v = strValue(a, from);
                if (!v.isEmpty())
                    entry.insert(to, v);
            }
            if (!entry.isEmpty()) {
                entry.insert(QStringLiteral("type"), type);
                addresses.append(entry);
            }
        }
        if (!addresses.isEmpty())
            obj.insert(QStringLiteral("addresses"), addresses);
    }

    // ---- organization (single scalar cluster) + profession ------------------------
    {
        QJsonObject org;
        const QString company = strValue(contact, QStringLiteral("companyName"));
        if (!company.isEmpty())
            org.insert(QStringLiteral("name"), company);
        const QString jobTitle = strValue(contact, QStringLiteral("jobTitle"));
        if (!jobTitle.isEmpty())
            org.insert(QStringLiteral("title"), jobTitle);
        const QString department = strValue(contact, QStringLiteral("department"));
        if (!department.isEmpty())
            org.insert(QStringLiteral("department"), department);
        const QString office = strValue(contact, QStringLiteral("officeLocation"));
        if (!office.isEmpty())
            org.insert(QStringLiteral("location"), office);
        if (!org.isEmpty())
            obj.insert(QStringLiteral("organizations"), QJsonArray{ org });

        const QString profession = strValue(contact, QStringLiteral("profession"));
        if (!profession.isEmpty())
            obj.insert(QStringLiteral("occupations"), QJsonArray{ profession });
    }

    // ---- urls / relations / notes / birthday / categories ---------------------------
    {
        const QString homePage = strValue(contact, QStringLiteral("businessHomePage"));
        if (!homePage.isEmpty())
            obj.insert(QStringLiteral("urls"),
                       QJsonArray{ QJsonObject{ { QStringLiteral("value"), homePage } } });

        QJsonArray relations;
        auto nameOnlyRelation = [&relations](const QString& wireKey,
                                             const QString& type) {
            // caller checks presence
            Q_UNUSED(wireKey);
            Q_UNUSED(type);
        };
        Q_UNUSED(nameOnlyRelation);
        const QString assistant = strValue(contact, QStringLiteral("assistantName"));
        if (!assistant.isEmpty())
            relations.append(QJsonObject{ { QStringLiteral("person"), assistant },
                                          { QStringLiteral("type"), QStringLiteral("assistant") } });
        const QString manager = strValue(contact, QStringLiteral("manager"));
        if (!manager.isEmpty())
            relations.append(QJsonObject{ { QStringLiteral("person"), manager },
                                          { QStringLiteral("type"), QStringLiteral("manager") } });
        const QString spouse = strValue(contact, QStringLiteral("spouseName"));
        if (!spouse.isEmpty())
            relations.append(QJsonObject{ { QStringLiteral("person"), spouse },
                                          { QStringLiteral("type"), QStringLiteral("spouse") } });
        for (const auto& cv : contact.value(QStringLiteral("children")).toArray()) {
            const QString v = cv.toString();
            if (v.isEmpty())
                continue;
            relations.append(QJsonObject{ { QStringLiteral("person"), v },
                                          { QStringLiteral("type"), QStringLiteral("child") } });
        }
        if (!relations.isEmpty())
            obj.insert(QStringLiteral("relations"), relations);

        const QString notes = strValue(contact, QStringLiteral("personalNotes"));
        if (!notes.isEmpty())
            obj.insert(QStringLiteral("notes"), notes);

        const QString birthday = strValue(contact, QStringLiteral("birthday"));
        if (!birthday.isEmpty())
            obj.insert(QStringLiteral("birthday"),
                       QJsonObject{ { QStringLiteral("dateTime"), birthday } });

        const QJsonArray cats = contact.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty())
            obj.insert(QStringLiteral("categories"), cats);
    }

    // ---- open-extension carriers → canon props --------------------------------------
    {
        QJsonArray remainder;
        for (const auto& ev :
             contact.value(QStringLiteral("extensions")).toArray()) {
            const QJsonObject ext = ev.toObject();
            if (strValue(ext, QStringLiteral("extensionName"))
                    != QLatin1String(kExtensionName)) {
                remainder.append(ext);
                continue;
            }
            for (auto it = ext.constBegin(); it != ext.constEnd(); ++it) {
                if (it.key() == QLatin1String("extensionName")
                    || it.key().startsWith(QLatin1String("@odata")))
                    continue;
                const QString prop = propFromCarrierKey(it.key());
                if (prop.isEmpty()) {
                    remainder.append(ext);
                    break;
                }
                obj.insert(prop, carrierStringToValue(it.value()));
            }
        }
        if (!remainder.isEmpty())
            extras.insert(QStringLiteral("extensions"), remainder);
    }

    // ---- everything unmapped → providerExtras["msgraph"] verbatim --------------------
    {
        static const QSet<QString> consumed = {
            QStringLiteral("id"),
            QStringLiteral("displayName"), QStringLiteral("givenName"),
            QStringLiteral("surname"), QStringLiteral("middleName"),
            QStringLiteral("title"), QStringLiteral("generation"),
            QStringLiteral("yomiGivenName"), QStringLiteral("yomiSurname"),
            QStringLiteral("fileAs"), QStringLiteral("nickName"),
            QStringLiteral("emailAddresses"),
            QStringLiteral("primaryEmailAddress"),
            QStringLiteral("secondaryEmailAddress"),
            QStringLiteral("tertiaryEmailAddress"),
            QStringLiteral("homePhones"), QStringLiteral("businessPhones"),
            QStringLiteral("mobilePhone"), QStringLiteral("imAddresses"),
            QStringLiteral("homeAddress"), QStringLiteral("businessAddress"),
            QStringLiteral("otherAddress"), QStringLiteral("companyName"),
            QStringLiteral("jobTitle"), QStringLiteral("department"),
            QStringLiteral("officeLocation"), QStringLiteral("profession"),
            QStringLiteral("businessHomePage"), QStringLiteral("assistantName"),
            QStringLiteral("manager"), QStringLiteral("spouseName"),
            QStringLiteral("children"), QStringLiteral("personalNotes"),
            QStringLiteral("birthday"), QStringLiteral("categories"),
            QStringLiteral("extensions")
        };
        for (auto it = contact.constBegin(); it != contact.constEnd(); ++it)
            if (!consumed.contains(it.key()))
                extras.insert(it.key(), it.value());
    }

    if (!extras.isEmpty()) {
        QJsonObject wrap;
        wrap.insert(QStringLiteral("msgraph"), extras);
        obj.insert(providerExtrasKey(), wrap);

        // ---- providerExtrasDigest (IP.5/O80) — FILTERED, excludes
        // `@odata.etag`/`changeKey`/`lastModifiedDateTime` ------------------
        // Confirmed against real captured /me/contacts samples
        // (msgraph/captured/20260823-011727-…json and
        // …-020405-…json — the SAME ten contacts fetched ~50 minutes apart,
        // no edits made): `@odata.etag`/`changeKey`/`lastModifiedDateTime`
        // stayed byte-identical across both fetches in this sample, but
        // both fields are the identical OData change-tracking mechanism
        // this file's event-leg sibling (mseventcanonstages.cpp) directly
        // observed bumping on every write for the SAME resource family
        // (Outlook items) — filtered defensively for consistency, not
        // because this narrow no-edit sample happened to catch it churning.
        // `createdDateTime` is deliberately KEPT (MS To-Do's own
        // precedent: set once at creation, no false-dirty risk).
        // `parentFolderId`/`initials`/`yomiCompanyName`/`id` stay hashed:
        // stable identity/content.
        stampProviderExtrasDigest(obj, extras,
                                   { QStringLiteral("@odata.etag"),
                                     QStringLiteral("changeKey"),
                                     QStringLiteral("lastModifiedDateTime") });
    }
    stampEnvelope(obj, QStringLiteral("contacts"), id);
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// Demote — canon JSON → Graph contact JSON (lossy per the declared profile)
// ---------------------------------------------------------------------------

QByteArray CanonToMsContactStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};

    QJsonObject out;
    QJsonArray extensionRows;

    const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
    const QJsonObject extrasMs = extras.value(QStringLiteral("msgraph")).toObject();

    // Vendor passthrough first (@odata.etag, changeKey, parentFolderId,
    // timestamps, initials, …) minus keys rebuilt below.
    static const QSet<QString> kRebuilt = {
        QStringLiteral("id"),
        QStringLiteral("displayName"), QStringLiteral("givenName"),
        QStringLiteral("surname"), QStringLiteral("middleName"),
        QStringLiteral("title"), QStringLiteral("generation"),
        QStringLiteral("yomiGivenName"), QStringLiteral("yomiSurname"),
        QStringLiteral("fileAs"), QStringLiteral("nickName"),
        QStringLiteral("emailAddresses"),
        QStringLiteral("primaryEmailAddress"),
        QStringLiteral("secondaryEmailAddress"),
        QStringLiteral("tertiaryEmailAddress"),
        QStringLiteral("homePhones"), QStringLiteral("businessPhones"),
        QStringLiteral("mobilePhone"), QStringLiteral("imAddresses"),
        QStringLiteral("homeAddress"), QStringLiteral("businessAddress"),
        QStringLiteral("otherAddress"), QStringLiteral("companyName"),
        QStringLiteral("jobTitle"), QStringLiteral("department"),
        QStringLiteral("officeLocation"), QStringLiteral("profession"),
        QStringLiteral("businessHomePage"), QStringLiteral("assistantName"),
        QStringLiteral("manager"), QStringLiteral("spouseName"),
        QStringLiteral("children"), QStringLiteral("personalNotes"),
        QStringLiteral("birthday"), QStringLiteral("categories"),
        QStringLiteral("extensions")
    };
    for (auto it = extrasMs.constBegin(); it != extrasMs.constEnd(); ++it)
        if (!kRebuilt.contains(it.key()) && it.key() != QStringLiteral("id"))
            out.insert(it.key(), it.value());

    // ---- uid → id -------------------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            out.insert(QStringLiteral("id"), uid);
    }

    // ---- canon names[0] → flat scalars ------------------------------------------
    {
        const QJsonArray names = obj.value(QStringLiteral("names")).toArray();
        if (!names.isEmpty()) {
            const QJsonObject n = names.at(0).toObject();
            static const QList<QPair<QString, QString>> kMap = {
                { QStringLiteral("formatted"),        QStringLiteral("displayName") },
                { QStringLiteral("given"),            QStringLiteral("givenName") },
                { QStringLiteral("family"),           QStringLiteral("surname") },
                { QStringLiteral("middle"),           QStringLiteral("middleName") },
                { QStringLiteral("prefix"),           QStringLiteral("title") },
                { QStringLiteral("suffix"),           QStringLiteral("generation") },
                { QStringLiteral("phoneticGiven"),    QStringLiteral("yomiGivenName") },
                { QStringLiteral("phoneticFamily"),   QStringLiteral("yomiSurname") },
                { QStringLiteral("fileAs"),           QStringLiteral("fileAs") },
            };
            for (const auto& [from, to] : kMap) {
                const QString v = n.value(from).toString();
                if (!v.isEmpty())
                    out.insert(to, v);
            }
        }
    }

    // ---- nickName -------------------------------------------------------------
    {
        const QJsonArray nick = obj.value(QStringLiteral("nicknames")).toArray();
        if (!nick.isEmpty()) {
            const QString v = nick.at(0).toObject()
                                  .value(QStringLiteral("value")).toString();
            if (!v.isEmpty())
                out.insert(QStringLiteral("nickName"), v);
        }
    }

    // ---- emails → rows + primaryEmailAddress -------------------------------------
    {
        const QJsonArray emails = obj.value(QStringLiteral("emails")).toArray();
        if (!emails.isEmpty()) {
            QJsonArray rows;
            QJsonObject primaryRow;
            for (const auto& ev : emails) {
                const QJsonObject e = ev.toObject();
                const QString value = e.value(QStringLiteral("value")).toString();
                if (value.isEmpty())
                    continue;
                QJsonObject row;
                row.insert(QStringLiteral("address"), value);
                const QString name = e.value(QStringLiteral("name")).toString();
                if (!name.isEmpty())
                    row.insert(QStringLiteral("name"), name);
                rows.append(row);
                if (e.value(QStringLiteral("primary")).toBool(false))
                    primaryRow = row;
            }
            out.insert(QStringLiteral("emailAddresses"), rows);
            if (!primaryRow.isEmpty())
                out.insert(QStringLiteral("primaryEmailAddress"), primaryRow);
        }
    }

    // ---- phones → fixed buckets -----------------------------------------------------
    {
        const QJsonArray phones = obj.value(QStringLiteral("phones")).toArray();
        QJsonArray home;
        QJsonArray work;
        QString mobile;
        for (const auto& pv : phones) {
            const QJsonObject p = pv.toObject();
            const QString v = p.value(QStringLiteral("value")).toString();
            if (v.isEmpty())
                continue;
            const QString t = p.value(QStringLiteral("type")).toString();
            if (t == QLatin1String("home"))
                home.append(v);
            else if (t == QLatin1String("work"))
                work.append(v);
            else if (t == QLatin1String("mobile") && mobile.isEmpty())
                mobile = v;
        }
        if (!home.isEmpty())
            out.insert(QStringLiteral("homePhones"), home);
        if (!work.isEmpty())
            out.insert(QStringLiteral("businessPhones"), work);
        if (!mobile.isEmpty())
            out.insert(QStringLiteral("mobilePhone"), mobile);
    }

    // ---- imClients → imAddresses --------------------------------------------------------
    {
        QJsonArray ims;
        for (const auto& iv :
             obj.value(QStringLiteral("imClients")).toArray()) {
            const QString v = iv.toObject()
                                  .value(QStringLiteral("username")).toString();
            if (!v.isEmpty())
                ims.append(v);
        }
        if (!ims.isEmpty())
            out.insert(QStringLiteral("imAddresses"), ims);
    }

    // ---- addresses → three slots -----------------------------------------------------------
    {
        static const QList<QPair<QString, QString>> kSlots = {
            { QStringLiteral("home"),  QStringLiteral("homeAddress") },
            { QStringLiteral("work"),  QStringLiteral("businessAddress") },
            { QStringLiteral("other"), QStringLiteral("otherAddress") },
        };
        static const QList<QPair<QString, QString>> kKeyMap = {
            { QStringLiteral("street"),     QStringLiteral("street") },
            { QStringLiteral("city"),       QStringLiteral("city") },
            { QStringLiteral("region"),     QStringLiteral("state") },
            { QStringLiteral("country"),    QStringLiteral("countryOrRegion") },
            { QStringLiteral("postalCode"), QStringLiteral("postalCode") },
        };
        for (const auto& [type, slot] : kSlots) {
            for (const auto& av :
                 obj.value(QStringLiteral("addresses")).toArray()) {
                const QJsonObject a = av.toObject();
                if (a.value(QStringLiteral("type")).toString() != type)
                    continue;
                QJsonObject slotObj;
                for (const auto& [from, to] : kKeyMap) {
                    const QString v = a.value(from).toString();
                    if (!v.isEmpty())
                        slotObj.insert(to, v);
                }
                if (!slotObj.isEmpty())
                    out.insert(slot, slotObj);
                break;
            }
        }
    }

    // ---- organization + profession ------------------------------------------------------------
    {
        const QJsonArray orgs =
            obj.value(QStringLiteral("organizations")).toArray();
        if (!orgs.isEmpty()) {
            const QJsonObject o = orgs.at(0).toObject();
            const QString company = o.value(QStringLiteral("name")).toString();
            if (!company.isEmpty())
                out.insert(QStringLiteral("companyName"), company);
            const QString title = o.value(QStringLiteral("title")).toString();
            if (!title.isEmpty())
                out.insert(QStringLiteral("jobTitle"), title);
            const QString dept = o.value(QStringLiteral("department")).toString();
            if (!dept.isEmpty())
                out.insert(QStringLiteral("department"), dept);
            const QString loc = o.value(QStringLiteral("location")).toString();
            if (!loc.isEmpty())
                out.insert(QStringLiteral("officeLocation"), loc);
        }
        const QJsonArray occupations =
            obj.value(QStringLiteral("occupations")).toArray();
        if (!occupations.isEmpty()) {
            const QString v = occupations.at(0).toString();
            if (!v.isEmpty())
                out.insert(QStringLiteral("profession"), v);
        }
    }

    // ---- urls / relations / notes / birthday / categories ----------------------------------------
    {
        const QJsonArray urls = obj.value(QStringLiteral("urls")).toArray();
        if (!urls.isEmpty()) {
            const QString v = urls.at(0).toObject()
                                  .value(QStringLiteral("value")).toString();
            if (!v.isEmpty())
                out.insert(QStringLiteral("businessHomePage"), v);
        }

        const QJsonArray relations =
            obj.value(QStringLiteral("relations")).toArray();
        QJsonArray children;
        for (const auto& rv : relations) {
            const QJsonObject r = rv.toObject();
            const QString person = r.value(QStringLiteral("person")).toString();
            const QString type = r.value(QStringLiteral("type")).toString();
            if (person.isEmpty())
                continue;
            if (type == QLatin1String("assistant"))
                out.insert(QStringLiteral("assistantName"), person);
            else if (type == QLatin1String("manager"))
                out.insert(QStringLiteral("manager"), person);
            else if (type == QLatin1String("spouse"))
                out.insert(QStringLiteral("spouseName"), person);
            else if (type == QLatin1String("child"))
                children.append(person);
        }
        if (!children.isEmpty())
            out.insert(QStringLiteral("children"), children);

        const QString notes = obj.value(QStringLiteral("notes")).toString();
        if (!notes.isEmpty())
            out.insert(QStringLiteral("personalNotes"), notes);

        const QJsonObject b = obj.value(QStringLiteral("birthday")).toObject();
        if (!b.isEmpty()) {
            QString birthdayStr = b.value(QStringLiteral("dateTime")).toString();
            if (birthdayStr.isEmpty()) {
                // Google-style {date:{y,m,d}} input degrades to midnight UTC
                const QJsonObject d = b.value(QStringLiteral("date")).toObject();
                if (!d.isEmpty()) {
                    birthdayStr =
                        QStringLiteral("%1-%2-%3T00:00:00Z")
                            .arg(d.value(QStringLiteral("year")).toInt(), 4, 10,
                                 QLatin1Char('0'))
                            .arg(d.value(QStringLiteral("month")).toInt(), 2, 10,
                                 QLatin1Char('0'))
                            .arg(d.value(QStringLiteral("day")).toInt(), 2, 10,
                                 QLatin1Char('0'));
                }
            }
            if (!birthdayStr.isEmpty())
                out.insert(QStringLiteral("birthday"), birthdayStr);
        }

        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty())
            out.insert(QStringLiteral("categories"), cats);
    }

    // ---- unhandled canon props → open-extension carriers (never dropped) -------------
    {
        static const QSet<QString> handled = {
            QStringLiteral("uid"), QStringLiteral("names"),
            QStringLiteral("nicknames"), QStringLiteral("emails"),
            QStringLiteral("phones"), QStringLiteral("addresses"),
            QStringLiteral("organizations"), QStringLiteral("occupations"),
            QStringLiteral("urls"), QStringLiteral("imClients"),
            QStringLiteral("relations"), QStringLiteral("notes"),
            QStringLiteral("birthday"), QStringLiteral("categories"),
            // providerExtrasDigest (IP.5/O80): purely derived/meta, no wire
            // representation by design — deliberately excluded from the
            // open-extension carrier below (matching MS To-Do's O74
            // precedent) rather than auto-carried as a stale extension row.
            QStringLiteral("providerExtrasDigest")
            // gender / anniversary / significantDates / timeZone / languages /
            // interests / skills / calendarUrls / sipAddresses / memberships /
            // externalIds are deliberately NOT handled — no GA `contact`
            // home; the carrier row below takes them (declared Reversible).
        };
        bool any = false;
        QJsonObject carrierRow;
        carrierRow.insert(QStringLiteral("@odata.type"),
                          QStringLiteral("microsoft.graph.openTypeExtension"));
        carrierRow.insert(QStringLiteral("extensionName"),
                          QLatin1String(kExtensionName));
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (handled.contains(it.key()))
                continue;
            if (it.key() == Kalburator::Shape::CanonEnvelope::canonKey()
                || it.key() == Kalburator::Shape::CanonEnvelope::uidKey()
                || it.key() == providerExtrasKey())
                continue;
            carrierRow.insert(carrierKey(it.key()),
                              valueToCarrierString(it.value()));
            any = true;
        }
        if (any)
            extensionRows.append(carrierRow);
    }

    // merge with stashed non-carrier extension rows
    {
        QJsonArray stashed =
            extrasMs.value(QStringLiteral("extensions")).toArray();
        if (!extensionRows.isEmpty()) {
            QJsonArray merged = extensionRows;
            for (const auto& sv : stashed) {
                const QJsonObject so = sv.toObject();
                if (so.value(QStringLiteral("extensionName")).toString()
                    == QLatin1String(kExtensionName))
                    continue;  // rebuilt fresh above
                merged.append(so);
            }
            out.insert(QStringLiteral("extensions"), merged);
        } else if (!stashed.isEmpty()) {
            out.insert(QStringLiteral("extensions"), stashed);
        }
    }

    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------------------
// LossProfile — canon → ms-contact demote
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToMsContactLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Simplified: survive in reduced form (flat/bucketed/single-slot collapse)
    for (const char* prop : { "names", "nicknames", "emails", "phones",
                              "addresses", "organizations", "occupations",
                              "urls", "relations", "imClients", "birthday" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(prop)},
                          LossKind::Simplified);
    }
    // Reversible: carried via kalburator.canon open extension x-canon-* rows
    for (const char* prop : { "gender", "anniversary", "significantDates",
                              "timeZone", "languages", "interests", "skills",
                              "calendarUrls", "sipAddresses", "memberships",
                              "externalIds" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(prop)},
                          LossKind::Reversible);
    }
    // Dropped: fetch-only nav resource, never inline payload
    p.affected.insert(PropertyId{QStringLiteral("photos")}, LossKind::Dropped);
    // Dropped (IP.5/O80): providerExtrasDigest is purely derived/meta, no
    // Graph contact wire form by design, and deliberately excluded from
    // the open-extension carrier loop (see the promote-side comment).
    p.affected.insert(PropertyId{QStringLiteral("providerExtrasDigest")}, LossKind::Dropped);
    return p;
}

}  // namespace Kalburator::Contacts
