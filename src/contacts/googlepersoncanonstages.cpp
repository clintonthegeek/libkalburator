#include "googlepersoncanonstages.h"

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

constexpr auto kCarrierPrefix = "x-canon-";

// Same carrier-string discipline as the calendar edges (clientData values
// are string-typed on the wire).
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

/// Copy a repeated Google field's rows through a key-renaming map.
/// `metadata` rows are bookkeeping and drop; any OTHER unmapped key flags
/// leftovers so the caller can stash the verbatim original array.
QJsonArray mapRows(const QJsonArray& in,
                   const QList<QPair<QString, QString>>& keyMap,
                   bool* anyLeftovers)
{
    QJsonArray out;
    for (const auto& rv : in) {
        const QJsonObject row = rv.toObject();
        QJsonObject entry;
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            if (it.key() == QStringLiteral("metadata"))
                continue;
            bool mapped = false;
            for (const auto& [from, to] : keyMap) {
                if (it.key() == from) {
                    entry.insert(to, it.value());
                    mapped = true;
                    break;
                }
            }
            if (!mapped)
                *anyLeftovers = true;   // unknown field on this row
        }
        out.append(entry);
    }
    return out;
}

/// True when any row of `in` carries keys beyond `known` (+ metadata).
bool rowsHaveUnknownFields(const QJsonArray& in, const QSet<QString>& known)
{
    for (const auto& rv : in) {
        for (auto it = rv.toObject().constBegin();
             it != rv.toObject().constEnd(); ++it) {
            if (it.key() == QStringLiteral("metadata"))
                continue;
            if (!known.contains(it.key()))
                return true;
        }
    }
    return false;
}

bool rowPrimary(const QJsonObject& row)
{
    const QJsonObject meta = row.value(QStringLiteral("metadata")).toObject();
    return meta.value(QStringLiteral("primary")).toBool(false)
        || meta.value(QStringLiteral("sourcePrimary")).toBool(false);
}

} // namespace

namespace Kalburator::Contacts {

// ---------------------------------------------------------------------------
// Promote — Google Person JSON → canon JSON (lossless by construction)
// ---------------------------------------------------------------------------

QByteArray GooglePersonToCanonStage::transform(const QByteArray& googleBytes) const
{
    if (googleBytes.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(googleBytes);
    if (!doc.isObject())
        return {};
    const QJsonObject person = doc.object();

    QJsonObject obj;
    QJsonObject extras;

    // ---- uid ← resourceName -------------------------------------------------
    const QString resourceName =
        person.value(QStringLiteral("resourceName")).toString();

    // ---- names ------------------------------------------------------------------
    {
        QJsonArray names;
        for (const auto& nv :
             person.value(QStringLiteral("names")).toArray()) {
            const QJsonObject n = nv.toObject();
            QJsonObject entry;
            static const QList<QPair<QString, QString>> kMap = {
                { QStringLiteral("displayName"),     QStringLiteral("formatted") },
                { QStringLiteral("givenName"),       QStringLiteral("given") },
                { QStringLiteral("familyName"),      QStringLiteral("family") },
                { QStringLiteral("middleName"),      QStringLiteral("middle") },
                { QStringLiteral("honorificPrefix"), QStringLiteral("prefix") },
                { QStringLiteral("honorificSuffix"), QStringLiteral("suffix") },
                { QStringLiteral("phoneticFullName"),   QStringLiteral("phoneticFormatted") },
                { QStringLiteral("phoneticGivenName"),  QStringLiteral("phoneticGiven") },
                { QStringLiteral("phoneticFamilyName"), QStringLiteral("phoneticFamily") },
            };
            for (const auto& [from, to] : kMap) {
                const QString v = n.value(from).toString();
                if (!v.isEmpty())
                    entry.insert(to, v);
            }
            const QString fileAs = n.value(QStringLiteral("unstructuredName")).toString();
            Q_UNUSED(fileAs);
            if (rowPrimary(n))
                entry.insert(QStringLiteral("primary"), true);
            if (!entry.isEmpty())
                names.append(entry);
        }
        if (!names.isEmpty())
            obj.insert(QStringLiteral("names"), names);
    }

    // ---- simple repeated rows with direct key mappings ---------------------------
    {
        // emails: value/type map; primary lives in metadata; unknown row
        // fields ⇒ verbatim stash (wire-fidelity preference on demote).
        const auto emailSrc =
            person.value(QStringLiteral("emailAddresses")).toArray();
        if (!emailSrc.isEmpty()) {
            bool leftovers = false;
            const auto mapped = mapRows(
                emailSrc,
                { { QStringLiteral("value"), QStringLiteral("value") },
                  { QStringLiteral("type"), QStringLiteral("type") },
                  { QStringLiteral("displayName"), QStringLiteral("name") } },
                &leftovers);
            QJsonArray fixed;
            for (int i = 0; i < mapped.size(); ++i) {
                QJsonObject e = mapped.at(i).toObject();
                if (rowPrimary(emailSrc.at(i).toObject()))
                    e.insert(QStringLiteral("primary"), true);
                fixed.append(e);
            }
            obj.insert(QStringLiteral("emails"), fixed);
            if (leftovers)
                extras.insert(QStringLiteral("emailAddresses"), emailSrc);
        }

        // phones (canonicalForm rides the entry — rich Json superset)
        const auto phoneSrc =
            person.value(QStringLiteral("phoneNumbers")).toArray();
        if (!phoneSrc.isEmpty()) {
            bool leftovers = false;
            const auto phones = mapRows(
                phoneSrc,
                { { QStringLiteral("value"), QStringLiteral("value") },
                  { QStringLiteral("type"), QStringLiteral("type") },
                  { QStringLiteral("canonicalForm"),
                    QStringLiteral("canonicalForm") } },
                &leftovers);
            obj.insert(QStringLiteral("phones"), phones);
            if (leftovers)
                extras.insert(QStringLiteral("phoneNumbers"), phoneSrc);
        }

        // addresses
        const auto addrSrc = person.value(QStringLiteral("addresses")).toArray();
        if (!addrSrc.isEmpty()) {
            bool leftovers = false;
            const auto addresses = mapRows(
                addrSrc,
                { { QStringLiteral("streetAddress"), QStringLiteral("street") },
                  { QStringLiteral("city"),          QStringLiteral("city") },
                  { QStringLiteral("region"),        QStringLiteral("region") },
                  { QStringLiteral("postalCode"),    QStringLiteral("postalCode") },
                  { QStringLiteral("country"),       QStringLiteral("country") },
                  { QStringLiteral("countryCode"),   QStringLiteral("countryCode") },
                  { QStringLiteral("formattedValue"), QStringLiteral("formatted") },
                  { QStringLiteral("type"),          QStringLiteral("type") } },
                &leftovers);
            obj.insert(QStringLiteral("addresses"), addresses);
            if (leftovers)
                extras.insert(QStringLiteral("addresses"), addrSrc);
        }

        // urls / relations / externalIds / memberships pass near-verbatim
        for (const QString gk : { QStringLiteral("urls"),
                                  QStringLiteral("relations"),
                                  QStringLiteral("externalIds"),
                                  QStringLiteral("memberships"),
                                  QStringLiteral("imClients"),
                                  QStringLiteral("calendarUrls") }) {
            const QJsonArray arr = person.value(gk).toArray();
            if (!arr.isEmpty())
                obj.insert(gk, arr);
        }
    }

    // ---- StringList-shaped groups ({value} rows → strings) -----------------------
    for (const QString gk : { QStringLiteral("interests"),
                              QStringLiteral("skills"),
                              QStringLiteral("occupations") }) {
        QJsonArray strs;
        for (const auto& rv : person.value(gk).toArray()) {
            const QString v = rv.toObject().value(QStringLiteral("value")).toString();
            if (!v.isEmpty())
                strs.append(v);
        }
        if (!strs.isEmpty())
            obj.insert(gk, strs);
    }
    {
        QJsonArray langs;
        for (const auto& lv : person.value(QStringLiteral("locales")).toArray()) {
            const QString v = lv.toObject().value(QStringLiteral("value")).toString();
            if (!v.isEmpty())
                langs.append(v);
        }
        if (!langs.isEmpty())
            obj.insert(QStringLiteral("languages"), langs);
    }
    {
        QJsonArray sips;
        for (const auto& sv :
             person.value(QStringLiteral("sipAddresses")).toArray()) {
            const QString v = sv.toObject().value(QStringLiteral("value")).toString();
            if (!v.isEmpty())
                sips.append(v);
        }
        if (!sips.isEmpty())
            obj.insert(QStringLiteral("sipAddresses"), sips);
    }

    // ---- nicknames -----------------------------------------------------------------
    {
        QJsonArray arr;
        for (const auto& nv :
             person.value(QStringLiteral("nicknames")).toArray()) {
            const QString v = nv.toObject().value(QStringLiteral("value")).toString();
            if (!v.isEmpty()) {
                QJsonObject e;
                e.insert(QStringLiteral("value"), v);
                arr.append(e);
            }
        }
        if (!arr.isEmpty())
            obj.insert(QStringLiteral("nicknames"), arr);
    }

    // ---- birthday (first primary wins; all stashed when >1) -------------------------
    {
        const auto birthdays =
            person.value(QStringLiteral("birthdays")).toArray();
        if (!birthdays.isEmpty()) {
            const QJsonObject b = birthdays.at(0).toObject();
            QJsonObject entry;
            if (b.contains(QStringLiteral("date")))
                entry.insert(QStringLiteral("date"),
                             b.value(QStringLiteral("date")));
            if (b.contains(QStringLiteral("text")))
                entry.insert(QStringLiteral("text"),
                             b.value(QStringLiteral("text")));
            obj.insert(QStringLiteral("birthday"), entry);
            if (birthdays.size() > 1)
                extras.insert(QStringLiteral("birthdays"), birthdays);
        }
    }

    // ---- gender / notes(biographies) / photos ------------------------------------------
    {
        const auto genders = person.value(QStringLiteral("genders")).toArray();
        if (!genders.isEmpty()) {
            const QJsonObject g = genders.at(0).toObject();
            QJsonObject entry;
            for (const QString k : { QStringLiteral("value"),
                                     QStringLiteral("formattedValue"),
                                     QStringLiteral("addressMeAs") }) {
                const QString v = g.value(k).toString();
                if (!v.isEmpty())
                    entry.insert(k, v);
            }
            if (!entry.isEmpty())
                obj.insert(QStringLiteral("gender"), entry);
        }

        const auto bios =
            person.value(QStringLiteral("biographies")).toArray();
        if (!bios.isEmpty()) {
            const QString note = bios.at(0).toObject()
                                     .value(QStringLiteral("value"))
                                     .toString();
            if (!note.isEmpty())
                obj.insert(QStringLiteral("notes"), note);
            if (bios.size() > 1)
                extras.insert(QStringLiteral("biographies"), bios);
        }

        const auto photos = person.value(QStringLiteral("photos")).toArray();
        if (!photos.isEmpty()) {
            QJsonArray arr;
            const auto src = photos;
            for (int i = 0; i < src.size(); ++i) {
                const QJsonObject p = src.at(i).toObject();
                QJsonObject e;
                const QString url = p.value(QStringLiteral("url")).toString();
                if (url.isEmpty())
                    continue;
                e.insert(QStringLiteral("url"), url);
                if (rowPrimary(p))
                    e.insert(QStringLiteral("primary"), true);
                arr.append(e);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("photos"), arr);
        }
    }

    // ---- organizations (rich rows, near-verbatim) ----------------------------------------
    {
        const auto orgs =
            person.value(QStringLiteral("organizations")).toArray();
        if (!orgs.isEmpty()) {
            QJsonArray arr;
            for (const auto& ov : orgs) {
                const QJsonObject o = ov.toObject();
                QJsonObject entry;
                for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
                    if (it.key() == QStringLiteral("metadata")
                        || it.key() == QStringLiteral("formattedType"))
                        continue;
                    entry.insert(it.key(), it.value());
                }
                if (!entry.isEmpty())
                    arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("organizations"), arr);
        }
    }

    // ---- fileAses (first row mirrors names[].fileAs convention) ----------------------------
    {
        const QJsonArray fa =
            person.value(QStringLiteral("fileAses")).toArray();
        if (!fa.isEmpty()) {
            const QString v = fa.at(0).toObject().value(QStringLiteral("value")).toString();
            if (!v.isEmpty()) {
                // canon keeps fileAs inside each name entry; attach to first name
                QJsonArray names = obj.value(QStringLiteral("names")).toArray();
                if (!names.isEmpty()) {
                    QJsonObject n0 = names.at(0).toObject();
                    n0.insert(QStringLiteral("fileAs"), v);
                    names.replace(0, n0);
                    obj.insert(QStringLiteral("names"), names);
                }
            }
        }
    }

    // ---- clientData carriers → canon props ---------------------------------------------------
    {
        const QJsonArray cd =
            person.value(QStringLiteral("clientData")).toArray();
        QJsonArray remainder;
        for (const auto& cv : cd) {
            const QJsonObject row = cv.toObject();
            const QString key = row.value(QStringLiteral("key")).toString();
            const QString prop = propFromCarrierKey(key);
            if (prop.isEmpty()) {
                remainder.append(row);
                continue;
            }
            obj.insert(prop, carrierStringToValue(row.value(QStringLiteral("value"))));
        }
        if (!remainder.isEmpty())
            extras.insert(QStringLiteral("clientData"), remainder);
    }

    // ---- everything unmapped → providerExtras["google"] verbatim ------------------------------
    {
        static const QSet<QString> consumed = {
            QStringLiteral("resourceName"),
            QStringLiteral("names"), QStringLiteral("nicknames"),
            QStringLiteral("emailAddresses"), QStringLiteral("phoneNumbers"),
            QStringLiteral("addresses"), QStringLiteral("organizations"),
            QStringLiteral("urls"), QStringLiteral("relations"),
            QStringLiteral("birthdays"), QStringLiteral("genders"),
            QStringLiteral("biographies"), QStringLiteral("photos"),
            QStringLiteral("externalIds"), QStringLiteral("memberships"),
            QStringLiteral("interests"), QStringLiteral("skills"),
            QStringLiteral("occupations"), QStringLiteral("locales"),
            QStringLiteral("sipAddresses"), QStringLiteral("calendarUrls"),
            QStringLiteral("imClients"), QStringLiteral("fileAses"),
            QStringLiteral("clientData")
        };
        for (auto it = person.constBegin(); it != person.constEnd(); ++it)
            if (!consumed.contains(it.key()))
                extras.insert(it.key(), it.value());
    }

    if (!extras.isEmpty()) {
        QJsonObject wrap;
        wrap.insert(QStringLiteral("google"), extras);
        obj.insert(providerExtrasKey(), wrap);
    }
    stampEnvelope(obj, QStringLiteral("contacts"), resourceName);
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// Demote — canon JSON → Google Person JSON (lossy per the declared profile)
// ---------------------------------------------------------------------------

QByteArray CanonToGooglePersonStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};

    QJsonObject out;
    QJsonArray clientDataCarriers;

    const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
    const QJsonObject extrasGoogle = extras.value(QStringLiteral("google")).toObject();

    // Vendor passthrough first (etag, metadata, coverPhotos, …) minus keys
    // rebuilt below.
    static const QSet<QString> kRebuilt = {
        QStringLiteral("names"), QStringLiteral("nicknames"),
        QStringLiteral("emailAddresses"), QStringLiteral("phoneNumbers"),
        QStringLiteral("addresses"), QStringLiteral("organizations"),
        QStringLiteral("urls"), QStringLiteral("relations"),
        QStringLiteral("birthdays"), QStringLiteral("genders"),
        QStringLiteral("biographies"), QStringLiteral("photos"),
        QStringLiteral("externalIds"), QStringLiteral("memberships"),
        QStringLiteral("interests"), QStringLiteral("skills"),
        QStringLiteral("occupations"), QStringLiteral("locales"),
        QStringLiteral("sipAddresses"), QStringLiteral("calendarUrls"),
        QStringLiteral("imClients"), QStringLiteral("fileAses"),
        QStringLiteral("clientData")
    };
    for (auto it = extrasGoogle.constBegin(); it != extrasGoogle.constEnd(); ++it)
        if (!kRebuilt.contains(it.key()))
            out.insert(it.key(), it.value());

    // ---- uid → resourceName -----------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            out.insert(QStringLiteral("resourceName"), uid);
    }

    // ---- names (+ fileAs extraction) -----------------------------------------------
    {
        const QJsonArray names = obj.value(QStringLiteral("names")).toArray();
        if (!names.isEmpty()) {
            QJsonArray arr;
            for (const auto& nv : names) {
                const QJsonObject n = nv.toObject();
                QJsonObject entry;
                static const QList<QPair<QString, QString>> kMap = {
                    { QStringLiteral("formatted"),           QStringLiteral("displayName") },
                    { QStringLiteral("given"),               QStringLiteral("givenName") },
                    { QStringLiteral("family"),              QStringLiteral("familyName") },
                    { QStringLiteral("middle"),              QStringLiteral("middleName") },
                    { QStringLiteral("prefix"),              QStringLiteral("honorificPrefix") },
                    { QStringLiteral("suffix"),              QStringLiteral("honorificSuffix") },
                    { QStringLiteral("phoneticFormatted"),   QStringLiteral("phoneticFullName") },
                    { QStringLiteral("phoneticGiven"),       QStringLiteral("phoneticGivenName") },
                    { QStringLiteral("phoneticFamily"),      QStringLiteral("phoneticFamilyName") },
                };
                for (const auto& [from, to] : kMap) {
                    const QString v = n.value(from).toString();
                    if (!v.isEmpty())
                        entry.insert(to, v);
                }
                if (n.value(QStringLiteral("primary")).toBool(true))
                    entry.insert(QStringLiteral("metadata"),
                                 QJsonObject{ { QStringLiteral("sourcePrimary"), true } });
                if (!entry.isEmpty())
                    arr.append(entry);
            }
            out.insert(QStringLiteral("names"), arr);

            // fileAs from the first name entry → fileAses row.
            const QString fileAs =
                names.at(0).toObject().value(QStringLiteral("fileAs")).toString();
            if (!fileAs.isEmpty())
                out.insert(QStringLiteral("fileAses"),
                           QJsonArray{ QJsonObject{
                               { QStringLiteral("value"), fileAs } } });
        }
    }

    // ---- simple row groups ------------------------------------------------------------
    {
        auto renameRows = [](const QJsonArray& in,
                             const QList<QPair<QString, QString>>& keyMap) {
            QJsonArray outRows;
            for (const auto& rv : in) {
                const QJsonObject row = rv.toObject();
                QJsonObject entry;
                for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                    QString key = it.key();
                    for (const auto& [from, to] : keyMap)
                        if (key == from)
                            key = to;
                    entry.insert(key, it.value());
                }
                outRows.append(entry);
            }
            return outRows;
        };

        const auto emails = obj.value(QStringLiteral("emails")).toArray();
        if (!emails.isEmpty()) {
            QJsonArray rows;
            for (const auto& ev : emails) {
                const QJsonObject e = ev.toObject();
                QJsonObject entry;
                entry.insert(QStringLiteral("value"),
                             e.value(QStringLiteral("value")));
                const QString t = e.value(QStringLiteral("type")).toString();
                if (!t.isEmpty())
                    entry.insert(QStringLiteral("type"), t);
                const QString name = e.value(QStringLiteral("name")).toString();
                if (!name.isEmpty())
                    entry.insert(QStringLiteral("displayName"), name);
                if (e.value(QStringLiteral("primary")).toBool(false))
                    entry.insert(QStringLiteral("metadata"),
                                 QJsonObject{ { QStringLiteral("primary"), true } });
                rows.append(entry);
            }
            out.insert(QStringLiteral("emailAddresses"), rows);
        }

        const auto phones = obj.value(QStringLiteral("phones")).toArray();
        if (!phones.isEmpty())
            out.insert(QStringLiteral("phoneNumbers"),
                       renameRows(phones, { { QStringLiteral("value"), QStringLiteral("value") } }));

        const auto addresses = obj.value(QStringLiteral("addresses")).toArray();
        if (!addresses.isEmpty())
            out.insert(QStringLiteral("addresses"),
                       renameRows(addresses,
                                  { { QStringLiteral("street"), QStringLiteral("streetAddress") },
                                    { QStringLiteral("formatted"), QStringLiteral("formattedValue") } }));

        for (const QString ck : { QStringLiteral("urls"),
                                  QStringLiteral("relations"),
                                  QStringLiteral("externalIds"),
                                  QStringLiteral("memberships"),
                                  QStringLiteral("imClients"),
                                  QStringLiteral("calendarUrls"),
                                  QStringLiteral("organizations") }) {
            const QJsonArray arr = obj.value(ck).toArray();
            if (!arr.isEmpty())
                out.insert(ck, arr);
        }
    }

    // ---- nicknames ----------------------------------------------------------------------
    {
        const QJsonArray nick = obj.value(QStringLiteral("nicknames")).toArray();
        if (!nick.isEmpty()) {
            QJsonArray rows;
            for (const auto& nv : nick) {
                const QString v = nv.toObject().value(QStringLiteral("value")).toString();
                if (!v.isEmpty())
                    rows.append(QJsonObject{ { QStringLiteral("value"), v } });
            }
            if (!rows.isEmpty())
                out.insert(QStringLiteral("nicknames"), rows);
        }
    }

    // ---- StringList groups -----------------------------------------------------------------
    for (const QString ck : { QStringLiteral("interests"),
                              QStringLiteral("skills"),
                              QStringLiteral("occupations") }) {
        const QJsonArray strs = obj.value(ck).toArray();
        if (!strs.isEmpty()) {
            QJsonArray rows;
            for (const auto& sv : strs)
                rows.append(QJsonObject{ { QStringLiteral("value"), sv } });
            out.insert(ck, rows);
        }
    }
    {
        const QJsonArray langs = obj.value(QStringLiteral("languages")).toArray();
        if (!langs.isEmpty()) {
            QJsonArray rows;
            for (const auto& lv : langs)
                rows.append(QJsonObject{ { QStringLiteral("value"), lv } });
            out.insert(QStringLiteral("locales"), rows);
        }
        const QJsonArray sips =
            obj.value(QStringLiteral("sipAddresses")).toArray();
        if (!sips.isEmpty()) {
            QJsonArray rows;
            for (const auto& sv : sips)
                rows.append(QJsonObject{ { QStringLiteral("value"), sv } });
            out.insert(QStringLiteral("sipAddresses"), rows);
        }
    }

    // ---- birthday (singular → one row; >1 restored from stash) -----------------------------
    {
        const QJsonObject b = obj.value(QStringLiteral("birthday")).toObject();
        const QJsonArray stash =
            extrasGoogle.value(QStringLiteral("birthdays")).toArray();
        if (!b.isEmpty()) {
            QJsonObject row;
            if (b.contains(QStringLiteral("date")))
                row.insert(QStringLiteral("date"),
                           b.value(QStringLiteral("date")));
            if (b.contains(QStringLiteral("text")))
                row.insert(QStringLiteral("text"),
                           b.value(QStringLiteral("text")));
            if (stash.size() > 1) {
                // restore the FULL set, replacing our single row at slot 0
                out.insert(QStringLiteral("birthdays"), stash);
            } else if (!row.isEmpty()) {
                out.insert(QStringLiteral("birthdays"),
                           QJsonArray{ row });
            }
        } else if (!stash.isEmpty()) {
            out.insert(QStringLiteral("birthdays"), stash);
        }

        const QString notes = obj.value(QStringLiteral("notes")).toString();
        const QJsonArray bioStash =
            extrasGoogle.value(QStringLiteral("biographies")).toArray();
        if (!notes.isEmpty() && bioStash.size() <= 1) {
            out.insert(QStringLiteral("biographies"),
                       QJsonArray{ QJsonObject{
                           { QStringLiteral("value"), notes } } });
        } else if (!bioStash.isEmpty()) {
            out.insert(QStringLiteral("biographies"), bioStash);
        }
    }

    // ---- gender ------------------------------------------------------------------------------
    {
        const QJsonObject g = obj.value(QStringLiteral("gender")).toObject();
        if (!g.isEmpty()) {
            QJsonObject row;
            for (const QString k : { QStringLiteral("value"),
                                     QStringLiteral("formattedValue"),
                                     QStringLiteral("addressMeAs") }) {
                const QString v = g.value(k).toString();
                if (!v.isEmpty())
                    row.insert(k, v);
            }
            if (!row.isEmpty())
                out.insert(QStringLiteral("genders"), QJsonArray{ row });
        }
    }

    // ---- photos --------------------------------------------------------------------------------
    {
        const QJsonArray photos = obj.value(QStringLiteral("photos")).toArray();
        if (!photos.isEmpty()) {
            QJsonArray rows;
            for (const auto& pv : photos) {
                const QJsonObject p = pv.toObject();
                QJsonObject row;
                const QString url = p.value(QStringLiteral("url")).toString();
                if (url.isEmpty())
                    continue;
                row.insert(QStringLiteral("url"), url);
                if (p.value(QStringLiteral("primary")).toBool(false))
                    row.insert(QStringLiteral("metadata"),
                               QJsonObject{ { QStringLiteral("primary"), true } });
                rows.append(row);
            }
            if (!rows.isEmpty())
                out.insert(QStringLiteral("photos"), rows);
        }
    }

    // ---- unhandled canon props → clientData carriers (never dropped) ----------------------------
    {
        static const QSet<QString> handled = {
            QStringLiteral("uid"), QStringLiteral("names"),
            QStringLiteral("nicknames"), QStringLiteral("emails"),
            QStringLiteral("phones"), QStringLiteral("addresses"),
            QStringLiteral("organizations"), QStringLiteral("occupations"),
            QStringLiteral("urls"), QStringLiteral("imClients"),
            QStringLiteral("sipAddresses"), QStringLiteral("calendarUrls"),
            QStringLiteral("relations"), QStringLiteral("birthday"),
            // anniversary / significantDates / timeZone / categories are
            // deliberately NOT handled — no Google home; the generic
            // clientData-carrier loop below takes them (declared Reversible).
            QStringLiteral("gender"), QStringLiteral("notes"),
            QStringLiteral("photos"),
            QStringLiteral("languages"),
            QStringLiteral("externalIds"), QStringLiteral("memberships"),
            QStringLiteral("interests"), QStringLiteral("skills")
        };
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (handled.contains(it.key()))
                continue;
            if (it.key() == Kalburator::Shape::CanonEnvelope::canonKey()
                || it.key() == Kalburator::Shape::CanonEnvelope::uidKey()
                || it.key() == providerExtrasKey())
                continue;
            clientDataCarriers.append(QJsonObject{
                { QStringLiteral("key"), carrierKey(it.key()) },
                { QStringLiteral("value"),
                  valueToCarrierString(it.value()) } });
        }
    }

    if (!extrasGoogle.value(QStringLiteral("clientData")).isArray()
        && !clientDataCarriers.isEmpty())
        out.insert(QStringLiteral("clientData"), clientDataCarriers);
    else if (!clientDataCarriers.isEmpty()) {
        QJsonArray merged =
            extrasGoogle.value(QStringLiteral("clientData")).toArray();
        for (const auto& c : clientDataCarriers)
            merged.append(c);
        out.insert(QStringLiteral("clientData"), merged);
    }

    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------------------
// LossProfile — canon → google-person demote
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToGooglePersonLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Simplified: survive in reduced form
    for (const char* prop : { "names", "nicknames", "addresses",
                              "organizations", "birthday", "notes", "photos",
                              "languages", "memberships", "interests",
                              "skills", "occupations", "sipAddresses" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(prop)},
                          LossKind::Simplified);
    }
    // Degraded
    p.affected.insert(PropertyId{QStringLiteral("gender")}, LossKind::Degraded);
    // Reversible: carried via clientData x-canon-* rows
    for (const char* prop : { "categories", "timeZone", "anniversary",
                              "significantDates" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(prop)},
                          LossKind::Reversible);
    }
    return p;
}

}  // namespace Kalburator::Contacts
