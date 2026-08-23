#include "googlecanonstages.h"

#include "canonenvelope.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QTimeZone>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;

// KCalendarCore::Alarm::Type values used in the canon `alarms` encoding
// (eventcanonfields.cpp stores the raw enum int).
constexpr int kAlarmTypeDisplay  = 1;
constexpr int kAlarmTypeEmail    = 3;

constexpr auto kCarrierPrefix = "x-canon-";

QString carrierKey(const QString& propId)
{
    // camelCase PropertyId → kebab-case carrier suffix.
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
    // kebab-case carrier suffix → camelCase PropertyId.
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

/// Encode a canon property value as an extendedProperties.private string.
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

/// Reverse of valueToCarrierString.
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

/// Google start/end JSON → canon start/end object
/// ({date,allDay:true} | {dateTime UTC ISO, tz, floating:false}).
QJsonObject googleTimeToCanon(const QJsonObject& g)
{
    if (g.contains(QStringLiteral("date"))) {
        QJsonObject obj;
        obj.insert(QStringLiteral("date"), g.value(QStringLiteral("date")).toString());
        obj.insert(QStringLiteral("allDay"), true);
        return obj;
    }
    const QDateTime dt = QDateTime::fromString(
        g.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
    if (!dt.isValid())
        return {};
    QJsonObject obj;
    QDateTime normalized = dt;
    const QString tzId = g.value(QStringLiteral("timeZone")).toString();
    if (!tzId.isEmpty()) {
        const QTimeZone tz(tzId.toLatin1());
        if (tz.isValid())
            normalized = dt.toTimeZone(tz);
    }
    obj.insert(QStringLiteral("dateTime"), normalized.toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("floating"), false);
    if (!tzId.isEmpty())
        obj.insert(QStringLiteral("tz"), tzId);
    return obj;
}

/// Canon start/end/recurrenceId object → Google {date} | {dateTime,timeZone}.
QJsonObject canonTimeToGoogle(const QJsonObject& c)
{
    if (c.contains(QStringLiteral("date")) || c.value(QStringLiteral("allDay")).toBool()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("date"), c.value(QStringLiteral("date")).toString());
        return obj;
    }
    // Emit the canon UTC-ISO string VERBATIM plus the IANA zone. Rewriting
    // into the zone's local time here would break byte-equal demote→promote
    // round-trips (promote normalizes back to UTC); the Calendar API accepts
    // dateTime+timeZone with the offset carried by the Z suffix.
    QDateTime dt = QDateTime::fromString(
        c.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
    if (!dt.isValid())
        return {};
    QJsonObject obj;
    const QString tzId = c.value(QStringLiteral("tz")).toString();
    if (!tzId.isEmpty()) {
        const QTimeZone tz(tzId.toLatin1());
        if (tz.isValid())
            obj.insert(QStringLiteral("timeZone"), tzId);
    }
    obj.insert(QStringLiteral("dateTime"), dt.toUTC().toString(Qt::ISODate));
    return obj;
}

} // namespace

namespace Kalburator::Calendar {

// ---------------------------------------------------------------------------
// GoogleEventToCanonStage — Google event JSON → canon JSON (lossless)
// ---------------------------------------------------------------------------

QByteArray GoogleEventToCanonStage::transform(const QByteArray& googleBytes) const
{
    if (googleBytes.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(googleBytes);
    if (!doc.isObject())
        return {};
    const QJsonObject ev = doc.object();

    QJsonObject obj;
    QJsonObject extrasGoogle;   // providerExtras["google"]

    // ---- uid ← iCalUID (fallback: transport-local id) ----------------------
    QString uid = ev.value(QStringLiteral("iCalUID")).toString();
    if (uid.isEmpty())
        uid = ev.value(QStringLiteral("id")).toString();

    // ---- sequence ----------------------------------------------------------
    {
        const QJsonValue seq = ev.value(QStringLiteral("sequence"));
        if (seq.isDouble())
            obj.insert(QStringLiteral("sequence"), seq.toInt());
    }

    // ---- created / updated → created / lastModified (normalized UTC ISO) ---
    {
        const QDateTime created = QDateTime::fromString(
            ev.value(QStringLiteral("created")).toString(), Qt::ISODate);
        if (created.isValid())
            obj.insert(QStringLiteral("created"), created.toUTC().toString(Qt::ISODate));
        const QDateTime updated = QDateTime::fromString(
            ev.value(QStringLiteral("updated")).toString(), Qt::ISODate);
        if (updated.isValid())
            obj.insert(QStringLiteral("lastModified"), updated.toUTC().toString(Qt::ISODate));
    }

    // ---- summary / description / location ----------------------------------
    for (const QString googleKey : { QStringLiteral("summary"),
                                     QStringLiteral("description"),
                                     QStringLiteral("location") }) {
        const QString v = ev.value(googleKey).toString();
        if (!v.isEmpty())
            obj.insert(googleKey, v);
    }

    // ---- status / classification / transparency / color / eventType --------
    {
        const QString status = ev.value(QStringLiteral("status")).toString();
        if (!status.isEmpty())
            obj.insert(QStringLiteral("status"), status.toLower());
        const QString visibility = ev.value(QStringLiteral("visibility")).toString();
        if (!visibility.isEmpty())
            obj.insert(QStringLiteral("classification"), visibility);
        const QString transparency = ev.value(QStringLiteral("transparency")).toString();
        if (!transparency.isEmpty())
            obj.insert(QStringLiteral("timeTransparency"), transparency);
        const QString colorId = ev.value(QStringLiteral("colorId")).toString();
        if (!colorId.isEmpty())
            obj.insert(QStringLiteral("color"), colorId);
        const QString eventType = ev.value(QStringLiteral("eventType")).toString();
        if (!eventType.isEmpty())
            obj.insert(QStringLiteral("eventType"), eventType);
    }

    // ---- guest permission / lock booleans -----------------------------------
    for (const QString googleKey : { QStringLiteral("guestsCanModify"),
                                     QStringLiteral("guestsCanInviteOthers"),
                                     QStringLiteral("guestsCanSeeOtherGuests"),
                                     QStringLiteral("locked"),
                                     QStringLiteral("privateCopy") }) {
        const QJsonValue v = ev.value(googleKey);
        if (v.isBool())
            obj.insert(googleKey, v.toBool());
    }

    // ---- start / end --------------------------------------------------------
    {
        const QJsonObject startObj = googleTimeToCanon(ev.value(QStringLiteral("start")).toObject());
        if (!startObj.isEmpty()) {
            obj.insert(QStringLiteral("start"), startObj);
            // Only stamp allDay when true: an absent key means timed events
            // round-trip byte-equal through demote → promote.
            if (startObj.value(QStringLiteral("allDay")).toBool())
                obj.insert(QStringLiteral("allDay"), true);
        }
        const QJsonObject endObj = googleTimeToCanon(ev.value(QStringLiteral("end")).toObject());
        if (!endObj.isEmpty())
            obj.insert(QStringLiteral("end"), endObj);
    }

    // ---- recurrence — verbatim RFC5545 lines --------------------------------
    {
        const QJsonArray rec = ev.value(QStringLiteral("recurrence")).toArray();
        if (!rec.isEmpty())
            obj.insert(QStringLiteral("recurrence"), rec);
    }

    // ---- recurringEventId + originalStartTime → extras + recurrenceId -------
    {
        const QString recurringEventId =
            ev.value(QStringLiteral("recurringEventId")).toString();
        if (!recurringEventId.isEmpty())
            extrasGoogle.insert(QStringLiteral("recurringEventId"), recurringEventId);
        const QJsonObject originalStart =
            googleTimeToCanon(ev.value(QStringLiteral("originalStartTime")).toObject());
        if (!originalStart.isEmpty())
            obj.insert(QStringLiteral("recurrenceId"), originalStart);
    }

    // ---- organizer ----------------------------------------------------------
    if (ev.contains(QStringLiteral("organizer"))) {
        const QJsonObject org = ev.value(QStringLiteral("organizer")).toObject();
        QJsonObject orgObj;
        QJsonObject orgLeftovers;
        for (auto it = org.constBegin(); it != org.constEnd(); ++it) {
            if (it.key() == QStringLiteral("email"))
                orgObj.insert(QStringLiteral("email"), it.value().toString());
            else if (it.key() == QStringLiteral("displayName"))
                orgObj.insert(QStringLiteral("name"), it.value().toString());
            else
                orgLeftovers.insert(it.key(), it.value());
        }
        if (!orgObj.isEmpty())
            obj.insert(QStringLiteral("organizer"), orgObj);
        if (!orgLeftovers.isEmpty())
            extrasGoogle.insert(QStringLiteral("organizer"), orgLeftovers);
    }

    // ---- attendees ----------------------------------------------------------
    {
        const QJsonArray attendees = ev.value(QStringLiteral("attendees")).toArray();
        if (!attendees.isEmpty()) {
            QJsonArray arr;
            bool anyLeftovers = false;
            for (const auto& av : attendees) {
                const QJsonObject a = av.toObject();
                const QString email = a.value(QStringLiteral("email")).toString();
                if (email.isEmpty())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("email"), email);
                const QString name = a.value(QStringLiteral("displayName")).toString();
                if (!name.isEmpty())
                    entry.insert(QStringLiteral("name"), name);
                const bool optional = a.value(QStringLiteral("optional")).toBool();
                entry.insert(QStringLiteral("role"),
                             optional ? QStringLiteral("optional")
                                      : QStringLiteral("required"));
                const QString rs = a.value(QStringLiteral("responseStatus")).toString();
                entry.insert(QStringLiteral("partstat"),
                             rs.isEmpty() ? QStringLiteral("needsAction") : rs);
                entry.insert(QStringLiteral("rsvp"), false);
                for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
                    if (it.key() == QStringLiteral("email") ||
                        it.key() == QStringLiteral("displayName") ||
                        it.key() == QStringLiteral("optional") ||
                        it.key() == QStringLiteral("responseStatus"))
                        continue;
                    if (it.key().startsWith(QLatin1String(kCarrierPrefix))) {
                        // Per-entry canon carrier (reverse of the demote
                        // loop): re-promote into the attendee entry.
                        entry.insert(propFromCarrierKey(it.key()),
                                     carrierStringToValue(it.value()));
                        continue;
                    }
                    anyLeftovers = true;
                }
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("attendees"), arr);
            if (anyLeftovers)
                extrasGoogle.insert(QStringLiteral("attendees"), attendees);
        }
    }

    // ---- reminders → alarms (+ verbatim stash for re-emission) --------------
    if (ev.contains(QStringLiteral("reminders"))) {
        const QJsonObject rem = ev.value(QStringLiteral("reminders")).toObject();
        extrasGoogle.insert(QStringLiteral("reminders"), rem);
        const QJsonArray overrides = rem.value(QStringLiteral("overrides")).toArray();
        QJsonArray alarms;
        for (const auto& ov : overrides) {
            const QJsonObject o = ov.toObject();
            // Wire truth (Calendar API v3 events reference):
            // reminders.overrides[] entries are {method, minutes}.
            const QString method = o.value(QStringLiteral("method")).toString();
            const int minutes = o.value(QStringLiteral("minutes")).toInt();
            if ((method == QStringLiteral("popup") || method == QStringLiteral("email"))
                && o.value(QStringLiteral("minutes")).isDouble()) {
                QJsonObject alarm;
                alarm.insert(QStringLiteral("type"),
                             method == QStringLiteral("popup")
                                 ? kAlarmTypeDisplay : kAlarmTypeEmail);
                alarm.insert(QStringLiteral("offset"), -(minutes * 60));
                alarms.append(alarm);
            }
        }
        if (!alarms.isEmpty())
            obj.insert(QStringLiteral("alarms"), alarms);
    }

    // ---- conferenceData → onlineMeeting (+ verbatim stash) ------------------
    if (ev.contains(QStringLiteral("conferenceData"))) {
        const QJsonObject cd = ev.value(QStringLiteral("conferenceData")).toObject();
        extrasGoogle.insert(QStringLiteral("conferenceData"), cd);
        for (const auto& ev_ : cd.value(QStringLiteral("entryPoints")).toArray()) {
            const QJsonObject ep = ev_.toObject();
            if (ep.value(QStringLiteral("entryPointType")).toString()
                    == QStringLiteral("video")) {
                QString uri = ep.value(QStringLiteral("uri")).toString();
                if (uri.isEmpty())
                    uri = ep.value(QStringLiteral("label")).toString();
                if (!uri.isEmpty()) {
                    QJsonObject om;
                    om.insert(QStringLiteral("joinUrl"), uri);
                    obj.insert(QStringLiteral("onlineMeeting"), om);
                    break;
                }
            }
        }
    }

    // ---- attachments ---------------------------------------------------------
    {
        const QJsonArray attachments = ev.value(QStringLiteral("attachments")).toArray();
        if (!attachments.isEmpty()) {
            QJsonArray arr;
            bool anyLeftovers = false;
            for (const auto& av : attachments) {
                const QJsonObject a = av.toObject();
                QJsonObject entry;
                const QString fileUrl = a.value(QStringLiteral("fileUrl")).toString();
                if (!fileUrl.isEmpty())
                    entry.insert(QStringLiteral("url"), fileUrl);
                const QString title = a.value(QStringLiteral("title")).toString();
                if (!title.isEmpty())
                    entry.insert(QStringLiteral("title"), title);
                const QString mime = a.value(QStringLiteral("mimeType")).toString();
                if (!mime.isEmpty())
                    entry.insert(QStringLiteral("mimeType"), mime);
                for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
                    if (it.key() != QStringLiteral("fileUrl") &&
                        it.key() != QStringLiteral("title") &&
                        it.key() != QStringLiteral("mimeType"))
                        anyLeftovers = true;
                }
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("attachments"), arr);
            if (anyLeftovers)
                extrasGoogle.insert(QStringLiteral("attachments"), attachments);
        }
    }

    // ---- source → url (+ leftovers stash) ------------------------------------
    if (ev.contains(QStringLiteral("source"))) {
        const QJsonObject src = ev.value(QStringLiteral("source")).toObject();
        const QString url = src.value(QStringLiteral("url")).toString();
        if (!url.isEmpty())
            obj.insert(QStringLiteral("url"), url);
        QJsonObject srcLeftovers;
        for (auto it = src.constBegin(); it != src.constEnd(); ++it)
            if (it.key() != QStringLiteral("url"))
                srcLeftovers.insert(it.key(), it.value());
        if (!srcLeftovers.isEmpty())
            extrasGoogle.insert(QStringLiteral("source"), srcLeftovers);
    }

    // ---- extendedProperties: shared → typedProperties; x-canon-* carriers ---
    {
        const QJsonObject ep = ev.value(QStringLiteral("extendedProperties")).toObject();
        const QJsonObject shared = ep.value(QStringLiteral("shared")).toObject();
        if (!shared.isEmpty())
            obj.insert(QStringLiteral("typedProperties"), shared);

        const QJsonObject priv = ep.value(QStringLiteral("private")).toObject();
        QJsonObject privRemainder;
        for (auto it = priv.constBegin(); it != priv.constEnd(); ++it) {
            const QString prop = propFromCarrierKey(it.key());
            if (prop.isEmpty()) {
                privRemainder.insert(it.key(), it.value());
                continue;
            }
            const QJsonValue value = carrierStringToValue(it.value());
            if (prop == QStringLiteral("floating")) {
                // x-canon-floating: restore the floating form on start/end.
                // Carrier strings may encode bool as "true"/"false" or "1"/"0".
                const bool floating = value.isBool() ? value.toBool()
                                                    : value.toDouble() != 0.0;
                if (floating) {
                    QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
                    if (startObj.contains(QStringLiteral("dateTime"))) {
                        startObj.remove(QStringLiteral("tz"));
                        startObj.insert(QStringLiteral("floating"), true);
                        obj.insert(QStringLiteral("start"), startObj);
                    }
                    QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();
                    if (endObj.contains(QStringLiteral("dateTime"))) {
                        endObj.remove(QStringLiteral("tz"));
                        endObj.insert(QStringLiteral("floating"), true);
                        obj.insert(QStringLiteral("end"), endObj);
                    }
                }
            } else if (prop == QStringLiteral("alarms")) {
                // Merge carried VALARMs back into the mapped ones.
                QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
                const QJsonArray carried = value.toArray();
                for (const auto& a : carried)
                    alarms.append(a);
                if (!carried.isEmpty())
                    obj.insert(QStringLiteral("alarms"), alarms);
            } else {
                // Generic promotion (classification/color/eventType override
                // their visibility/colorId/eventType-derived twins).
                obj.insert(prop, value);
            }
        }

        QJsonObject epExtras;
        if (!privRemainder.isEmpty())
            epExtras.insert(QStringLiteral("private"), privRemainder);
        for (auto it = ep.constBegin(); it != ep.constEnd(); ++it)
            if (it.key() != QStringLiteral("shared") && it.key() != QStringLiteral("private"))
                epExtras.insert(it.key(), it.value());
        if (!epExtras.isEmpty())
            extrasGoogle.insert(QStringLiteral("extendedProperties"), epExtras);
    }

    // ---- everything unmapped → providerExtras["google"] verbatim -------------
    {
        static const QSet<QString> consumed = {
            // NOTE: "id" deliberately NOT consumed — transport-local identity
            // is stashed under providerExtras["google"]["id"] (it doubles as
            // the uid fallback only when iCalUID is absent).
            QStringLiteral("iCalUID"), QStringLiteral("sequence"),
            QStringLiteral("created"), QStringLiteral("updated"),
            QStringLiteral("summary"), QStringLiteral("description"), QStringLiteral("location"),
            QStringLiteral("status"), QStringLiteral("visibility"), QStringLiteral("transparency"),
            QStringLiteral("colorId"), QStringLiteral("eventType"),
            QStringLiteral("guestsCanModify"), QStringLiteral("guestsCanInviteOthers"),
            QStringLiteral("guestsCanSeeOtherGuests"), QStringLiteral("locked"),
            QStringLiteral("privateCopy"),
            QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("recurrence"),
            QStringLiteral("recurringEventId"), QStringLiteral("originalStartTime"),
            QStringLiteral("organizer"), QStringLiteral("attendees"), QStringLiteral("reminders"),
            QStringLiteral("conferenceData"), QStringLiteral("attachments"),
            QStringLiteral("source"), QStringLiteral("extendedProperties")
        };
        for (auto it = ev.constBegin(); it != ev.constEnd(); ++it)
            if (!consumed.contains(it.key()))
                extrasGoogle.insert(it.key(), it.value());
    }

    if (!extrasGoogle.isEmpty()) {
        QJsonObject extras;
        extras.insert(QStringLiteral("google"), extrasGoogle);
        obj.insert(providerExtrasKey(), extras);
    }
    stampEnvelope(obj, QStringLiteral("calendar"), uid);
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// CanonToGoogleEventStage — canon JSON → Google event JSON (lossy)
// ---------------------------------------------------------------------------

QByteArray CanonToGoogleEventStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};

    QJsonObject out;
    QJsonObject privateCarriers;   // extendedProperties.private["x-canon-*"]

    const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
    const QJsonObject extrasGoogle = extras.value(QStringLiteral("google")).toObject();

    // Vendor leftovers first (id, etag, htmlLink, creator, hangoutLink,
    // recurringEventId, kind, …). Keys rebuilt below are held aside.
    const QJsonObject extrasReminders = extrasGoogle.value(QStringLiteral("reminders")).toObject();
    const QJsonObject extrasConference = extrasGoogle.value(QStringLiteral("conferenceData")).toObject();
    const QJsonObject extrasOrganizer = extrasGoogle.value(QStringLiteral("organizer")).toObject();
    const QJsonObject extrasSource = extrasGoogle.value(QStringLiteral("source")).toObject();
    QJsonObject extrasExtended = extrasGoogle.value(QStringLiteral("extendedProperties")).toObject();
    for (auto it = extrasGoogle.constBegin(); it != extrasGoogle.constEnd(); ++it) {
        const QString key = it.key();
        if (key == QStringLiteral("reminders") || key == QStringLiteral("conferenceData")
            || key == QStringLiteral("organizer") || key == QStringLiteral("source")
            || key == QStringLiteral("extendedProperties"))
            continue;
        out.insert(key, it.value());
    }

    // ---- uid → iCalUID -------------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            out.insert(QStringLiteral("iCalUID"), uid);
    }

    // ---- sequence / created / lastModified → sequence/created/updated --------
    {
        const QJsonValue seq = obj.value(QStringLiteral("sequence"));
        if (seq.isDouble())
            out.insert(QStringLiteral("sequence"), seq.toInt());
        const QString created = obj.value(QStringLiteral("created")).toString();
        if (!created.isEmpty())
            out.insert(QStringLiteral("created"), created);
        const QString lastMod = obj.value(QStringLiteral("lastModified")).toString();
        if (!lastMod.isEmpty())
            out.insert(QStringLiteral("updated"), lastMod);
    }

    // ---- summary / description / location -------------------------------------
    for (const QString canonKey : { QStringLiteral("summary"),
                                    QStringLiteral("description"),
                                    QStringLiteral("location") }) {
        const QString v = obj.value(canonKey).toString();
        if (!v.isEmpty())
            out.insert(canonKey, v);
    }

    // ---- locations (Simplified): primary → location string, full carried -----
    {
        const QJsonArray locs = obj.value(QStringLiteral("locations")).toArray();
        if (!locs.isEmpty()) {
            if (obj.value(QStringLiteral("location")).toString().isEmpty()) {
                const QString primary = locs.at(0).toObject()
                    .value(QStringLiteral("displayName")).toString();
                if (!primary.isEmpty())
                    out.insert(QStringLiteral("location"), primary);
            }
            privateCarriers.insert(carrierKey(QStringLiteral("locations")),
                                   valueToCarrierString(locs));
        }
    }

    // ---- status ----------------------------------------------------------------
    {
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (!status.isEmpty())
            out.insert(QStringLiteral("status"), status.toLower());
    }

    // ---- classification → visibility (Degraded + losslessValues) ---------------
    {
        const QString cls = obj.value(QStringLiteral("classification")).toString();
        if (!cls.isEmpty()) {
            if (cls == QStringLiteral("public") || cls == QStringLiteral("private")
                || cls == QStringLiteral("confidential")) {
                out.insert(QStringLiteral("visibility"), cls);
            } else {
                out.insert(QStringLiteral("visibility"), QStringLiteral("private"));
                privateCarriers.insert(carrierKey(QStringLiteral("classification")), cls);
            }
        }
    }

    // ---- timeTransparency / freeBusyStatus → transparency (Degraded) ----------
    {
        const QString transp = obj.value(QStringLiteral("timeTransparency")).toString();
        const QString fbs = obj.value(QStringLiteral("freeBusyStatus")).toString();
        if (!transp.isEmpty()) {
            if (transp == QStringLiteral("opaque") || transp == QStringLiteral("transparent")) {
                out.insert(QStringLiteral("transparency"), transp);
            } else {
                out.insert(QStringLiteral("transparency"), QStringLiteral("opaque"));
                privateCarriers.insert(carrierKey(QStringLiteral("timeTransparency")), transp);
            }
        }
        if (!fbs.isEmpty()) {
            // Values beyond free/busy have no Google form → nearest transparency
            // (blocking semantics), original carried.
            if (out.value(QStringLiteral("transparency")).toString().isEmpty()) {
                out.insert(QStringLiteral("transparency"), QStringLiteral("opaque"));
            }
            privateCarriers.insert(carrierKey(QStringLiteral("freeBusyStatus")), fbs);
        }
    }

    // ---- color → colorId (Degraded: palette ids pass, others carried) ---------
    {
        const QString color = obj.value(QStringLiteral("color")).toString();
        if (!color.isEmpty()) {
            static const QRegularExpression numericOnly(QStringLiteral("^[0-9]+$"));
            if (numericOnly.match(color).hasMatch()) {
                out.insert(QStringLiteral("colorId"), color);
            } else {
                privateCarriers.insert(carrierKey(QStringLiteral("color")), color);
            }
        }
    }

    // ---- categories (Simplified → carrier) --------------------------------------
    {
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty())
            privateCarriers.insert(carrierKey(QStringLiteral("categories")),
                                   valueToCarrierString(cats));
    }

    // ---- url → source.url (+ source.title leftovers) -----------------------------
    {
        QJsonObject src = extrasSource;
        const QString url = obj.value(QStringLiteral("url")).toString();
        if (!url.isEmpty())
            src.insert(QStringLiteral("url"), url);
        if (!src.isEmpty())
            out.insert(QStringLiteral("source"), src);
    }

    // ---- organizer ----------------------------------------------------------------
    {
        const QJsonObject orgObj = obj.value(QStringLiteral("organizer")).toObject();
        QJsonObject gOrg = extrasOrganizer;
        const QString email = orgObj.value(QStringLiteral("email")).toString();
        if (!email.isEmpty())
            gOrg.insert(QStringLiteral("email"), email);
        const QString name = orgObj.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            gOrg.insert(QStringLiteral("displayName"), name);
        if (!gOrg.isEmpty())
            out.insert(QStringLiteral("organizer"), gOrg);
    }

    // ---- start / end ----------------------------------------------------------------
    {
        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        if (!startObj.isEmpty()) {
            QJsonObject g = canonTimeToGoogle(startObj);
            if (startObj.value(QStringLiteral("floating")).toBool()
                && !g.value(QStringLiteral("dateTime")).toString().isEmpty()) {
                // Degraded: floating has no Google form → pin to UTC, carry.
                g.insert(QStringLiteral("timeZone"), QStringLiteral("UTC"));
                privateCarriers.insert(carrierKey(QStringLiteral("floating")),
                                       QStringLiteral("1"));
            }
            out.insert(QStringLiteral("start"), g);
        }
        const QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();
        if (!endObj.isEmpty()) {
            QJsonObject g = canonTimeToGoogle(endObj);
            if (endObj.value(QStringLiteral("floating")).toBool()
                && !g.value(QStringLiteral("dateTime")).toString().isEmpty()) {
                g.insert(QStringLiteral("timeZone"), QStringLiteral("UTC"));
            }
            if (!g.isEmpty())
                out.insert(QStringLiteral("end"), g);
        }
    }

    // ---- recurrence — verbatim RFC5545 lines ------------------------------------
    {
        const QJsonArray rec = obj.value(QStringLiteral("recurrence")).toArray();
        if (!rec.isEmpty())
            out.insert(QStringLiteral("recurrence"), rec);
    }

    // ---- recurrenceId → originalStartTime ----------------------------------------
    {
        const QJsonObject recIdObj = obj.value(QStringLiteral("recurrenceId")).toObject();
        const QJsonObject g = canonTimeToGoogle(recIdObj);
        if (!g.isEmpty())
            out.insert(QStringLiteral("originalStartTime"), g);
    }

    // ---- attendees (Simplified) ----------------------------------------------------
    {
        const QJsonArray attendees = obj.value(QStringLiteral("attendees")).toArray();
        if (!attendees.isEmpty()) {
            QJsonArray arr;
            for (const auto& av : attendees) {
                const QJsonObject a = av.toObject();
                const QString email = a.value(QStringLiteral("email")).toString();
                if (email.isEmpty())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("email"), email);
                const QString name = a.value(QStringLiteral("name")).toString();
                if (!name.isEmpty())
                    entry.insert(QStringLiteral("displayName"), name);
                // role → optional boolean only (chair/required distinction lost)
                const bool optional =
                    a.value(QStringLiteral("role")).toString() == QStringLiteral("optional");
                entry.insert(QStringLiteral("optional"), optional);
                const QString partstat = a.value(QStringLiteral("partstat")).toString();
                entry.insert(QStringLiteral("responseStatus"),
                             partstat.isEmpty() ? QStringLiteral("needsAction") : partstat);
                // canon-only attendee fields carried per-entry
                for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
                    if (it.key() == QStringLiteral("email")
                        || it.key() == QStringLiteral("name")
                        || it.key() == QStringLiteral("role")
                        || it.key() == QStringLiteral("partstat"))
                        continue;
                    entry.insert(carrierKey(it.key()), valueToCarrierString(it.value()));
                }
                arr.append(entry);
            }
            out.insert(QStringLiteral("attendees"), arr);
        }
    }

    // ---- responseRequested / priority / descriptionHtml / MS flags → carriers --
    for (const QString prop : { QStringLiteral("responseRequested"),
                                QStringLiteral("priority"),
                                QStringLiteral("descriptionHtml"),
                                QStringLiteral("allowNewTimeProposals"),
                                QStringLiteral("hideAttendees") }) {
        const QJsonValue v = obj.value(prop);
        if (v.isUndefined() || v.isNull())
            continue;
        privateCarriers.insert(carrierKey(prop), valueToCarrierString(v));
    }

    // ---- alarms → reminders{useDefault,overrides[]} (+ carried remainder) --------
    {
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        if (!alarms.isEmpty()) {
            QJsonArray overrides;
            QJsonArray carried;
            for (const auto& av : alarms) {
                const QJsonObject a = av.toObject();
                const int type = a.value(QStringLiteral("type")).toInt();
                const int offsetSecs = a.value(QStringLiteral("offset")).toInt();
                const bool mappable =
                    (type == kAlarmTypeDisplay || type == kAlarmTypeEmail)
                    && offsetSecs < 0 && offsetSecs % 60 == 0;
                if (mappable) {
                    QJsonObject o;
                    o.insert(QStringLiteral("method"),
                             type == kAlarmTypeDisplay
                                 ? QStringLiteral("popup") : QStringLiteral("email"));
                    o.insert(QStringLiteral("minutes"), -offsetSecs / 60);
                    overrides.append(o);
                } else {
                    carried.append(av);
                }
            }
            if (!carried.isEmpty())
                privateCarriers.insert(carrierKey(QStringLiteral("alarms")),
                                       valueToCarrierString(carried));
            QJsonObject rem;
            rem.insert(QStringLiteral("useDefault"), false);
            rem.insert(QStringLiteral("overrides"), overrides);
            out.insert(QStringLiteral("reminders"), rem);
        } else if (!extrasReminders.isEmpty()) {
            // No canon alarms: re-emit the promoted verbatim reminders stash
            // (preserves useDefault and calendar-default reminders).
            out.insert(QStringLiteral("reminders"), extrasReminders);
        }
    }

    // ---- onlineMeeting → conferenceData (Degraded) --------------------------------
    {
        const QJsonObject om = obj.value(QStringLiteral("onlineMeeting")).toObject();
        if (!om.isEmpty()) {
            if (!extrasConference.isEmpty()) {
                out.insert(QStringLiteral("conferenceData"), extrasConference);
            } else {
                QJsonObject cd;
                QJsonArray entryPoints;
                QJsonObject ep;
                ep.insert(QStringLiteral("entryPointType"), QStringLiteral("video"));
                ep.insert(QStringLiteral("uri"), om.value(QStringLiteral("joinUrl")).toString());
                entryPoints.append(ep);
                cd.insert(QStringLiteral("entryPoints"), entryPoints);
                out.insert(QStringLiteral("conferenceData"), cd);
            }
        }
    }

    // ---- attachments (Simplified) ---------------------------------------------------
    {
        const QJsonArray attachments = obj.value(QStringLiteral("attachments")).toArray();
        if (!attachments.isEmpty()) {
            QJsonArray arr;
            for (const auto& av : attachments) {
                const QJsonObject a = av.toObject();
                QJsonObject entry;
                const QString url = a.value(QStringLiteral("url")).toString();
                if (!url.isEmpty())
                    entry.insert(QStringLiteral("fileUrl"), url);
                const QString title = a.value(QStringLiteral("title")).toString();
                if (!title.isEmpty())
                    entry.insert(QStringLiteral("title"), title);
                const QString mime = a.value(QStringLiteral("mimeType")).toString();
                if (!mime.isEmpty())
                    entry.insert(QStringLiteral("mimeType"), mime);
                arr.append(entry);
            }
            out.insert(QStringLiteral("attachments"), arr);
        }
    }

    // ---- eventType (Degraded + losslessValues) ---------------------------------------
    {
        const QString eventType = obj.value(QStringLiteral("eventType")).toString();
        if (!eventType.isEmpty()) {
            static const QSet<QString> googleVocab = {
                QStringLiteral("default"), QStringLiteral("birthday"),
                QStringLiteral("focusTime"), QStringLiteral("fromGmail"),
                QStringLiteral("outOfOffice"), QStringLiteral("workingLocation")
            };
            if (googleVocab.contains(eventType)) {
                out.insert(QStringLiteral("eventType"), eventType);
            } else {
                out.insert(QStringLiteral("eventType"), QStringLiteral("default"));
                privateCarriers.insert(carrierKey(QStringLiteral("eventType")), eventType);
            }
        }
    }

    // ---- guest permission / lock booleans ----------------------------------------------
    for (const QString canonKey : { QStringLiteral("guestsCanModify"),
                                    QStringLiteral("guestsCanInviteOthers"),
                                    QStringLiteral("guestsCanSeeOtherGuests"),
                                    QStringLiteral("locked"),
                                    QStringLiteral("privateCopy") }) {
        const QJsonValue v = obj.value(canonKey);
        if (v.isBool())
            out.insert(canonKey, v.toBool());
    }

    // ---- typedProperties ⇄ extendedProperties.shared ------------------------------------
    {
        const QJsonObject typed = obj.value(QStringLiteral("typedProperties")).toObject();
        if (!typed.isEmpty())
            extrasExtended.insert(QStringLiteral("shared"), typed);
    }

    // ---- unhandled canon props → carriers (never silently dropped) ----------------------
    {
        static const QSet<QString> handled = {
            QStringLiteral("uid"), QStringLiteral("sequence"), QStringLiteral("created"),
            QStringLiteral("lastModified"), QStringLiteral("summary"),
            QStringLiteral("description"), QStringLiteral("descriptionHtml"),
            QStringLiteral("location"), QStringLiteral("locations"), QStringLiteral("status"),
            QStringLiteral("classification"), QStringLiteral("timeTransparency"),
            QStringLiteral("freeBusyStatus"), QStringLiteral("color"), QStringLiteral("categories"),
            QStringLiteral("url"), QStringLiteral("organizer"), QStringLiteral("attendees"),
            QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("allDay"),
            QStringLiteral("recurrence"), QStringLiteral("recurrenceId"),
            QStringLiteral("responseRequested"), QStringLiteral("priority"),
            QStringLiteral("alarms"), QStringLiteral("onlineMeeting"),
            QStringLiteral("attachments"), QStringLiteral("eventType"),
            QStringLiteral("typedProperties"),
            QStringLiteral("guestsCanModify"), QStringLiteral("guestsCanInviteOthers"),
            QStringLiteral("guestsCanSeeOtherGuests"), QStringLiteral("locked"),
            QStringLiteral("privateCopy"), QStringLiteral("allowNewTimeProposals"),
            QStringLiteral("hideAttendees")
        };
        static const QSet<QString> dropped = {
            QStringLiteral("geo"), QStringLiteral("due"), QStringLiteral("completed"),
            QStringLiteral("percentComplete"), QStringLiteral("relatedTo")
        };
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (handled.contains(it.key()) || dropped.contains(it.key()))
                continue;
            if (it.key() == Kalburator::Shape::CanonEnvelope::canonKey()
                || it.key() == Kalburator::Shape::CanonEnvelope::uidKey()
                || it.key() == providerExtrasKey())
                continue;
            privateCarriers.insert(carrierKey(it.key()), valueToCarrierString(it.value()));
        }
    }

    // ---- assemble extendedProperties ------------------------------------------------------
    {
        QJsonObject epOut = extrasExtended;
        // Merge carried x-canon-* keys with any promoted private-key remainder.
        QJsonObject privOut = epOut.value(QStringLiteral("private")).toObject();
        for (auto it = privateCarriers.constBegin(); it != privateCarriers.constEnd(); ++it)
            privOut.insert(it.key(), it.value());
        epOut.remove(QStringLiteral("private"));
        if (!privOut.isEmpty())
            epOut.insert(QStringLiteral("private"), privOut);
        if (!epOut.isEmpty())
            out.insert(QStringLiteral("extendedProperties"), epOut);
    }

    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------------------
// canonToGoogleEventLoss — LossProfile for the canon → google-event demote
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToGoogleEventLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Dropped: no Google form, no safe carrier semantics
    p.affected.insert(PropertyId{QStringLiteral("geo")},             LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("due")},             LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("completed")},       LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("percentComplete")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("relatedTo")},       LossKind::Dropped);

    // Simplified: survives in reduced form
    p.affected.insert(PropertyId{QStringLiteral("attendees")},    LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("alarms")},       LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("locations")},    LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("url")},          LossKind::Simplified);

    // Degraded: lossy many-to-one vocabulary mapping, original kept verbatim
    p.affected.insert(PropertyId{QStringLiteral("classification")},   LossKind::Degraded);
    p.losslessValues.insert(PropertyId{QStringLiteral("classification")},
                            {QStringLiteral("public"), QStringLiteral("private"),
                             QStringLiteral("confidential")});
    p.affected.insert(PropertyId{QStringLiteral("timeTransparency")}, LossKind::Degraded);
    p.losslessValues.insert(PropertyId{QStringLiteral("timeTransparency")},
                            {QStringLiteral("opaque"), QStringLiteral("transparent")});
    p.affected.insert(PropertyId{QStringLiteral("freeBusyStatus")},   LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("color")},            LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("onlineMeeting")},    LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("eventType")},        LossKind::Degraded);
    p.losslessValues.insert(PropertyId{QStringLiteral("eventType")},
                            {QStringLiteral("default"), QStringLiteral("birthday"),
                             QStringLiteral("focusTime"), QStringLiteral("fromGmail"),
                             QStringLiteral("outOfOffice"), QStringLiteral("workingLocation")});

    // Reversible: carried in extendedProperties.private["x-canon-*"]
    // start/end: lossless except floating:true (no Google form) — pinned to
    // UTC with the original carried in x-canon-floating and restored on
    // re-promote (pinned by floatingStartDegradesToUtcWithCarrier).
    p.affected.insert(PropertyId{QStringLiteral("start")},                  LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("end")},                    LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("categories")},             LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("priority")},               LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("responseRequested")},      LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("descriptionHtml")},        LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("allowNewTimeProposals")},  LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("hideAttendees")},          LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("typedProperties")},        LossKind::Reversible);

    return p;
}

}  // namespace Kalburator::Calendar
