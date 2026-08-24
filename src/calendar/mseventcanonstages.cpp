#include "mseventcanonstages.h"

#include "canonenvelope.h"
#include "recurrencepatternconverter.h"
#include "windowszonesmap.h"

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
// (eventcanonfields.cpp stores the raw enum int) — same discipline as the
// Google edge.
constexpr int kAlarmTypeDisplay = 1;

// Campaign-pinned extended-property GUID (declared loss profile, "Carrier
// channels"): singleValueExtendedProperties carry x-canon-* values as
//   id: "String {66f5926c-9c3e-4c14-9e4b-7a2f0d1c9eee} Name x-canon-<kebab>"
constexpr auto kSvepGuid = "{66f5926c-9c3e-4c14-9e4b-7a2f0d1c9eee}";
constexpr auto kCarrierPrefix = "x-canon-";

// ---------------------------------------------------------------------------
// Carrier helpers — same valueToCarrierString/carrierStringToValue discipline
// as googlecanonstages.cpp (string-typed carrier values, JSON-stringified
// composites). Kept local to mirror the Google edge until a shared internal
// header is justified by a third consumer.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

/// .NET year-1 sentinel (O57(d)) — never manufactured into stamps.
bool isYearOneSentinel(const QString& iso)
{
    return iso.startsWith(QLatin1String("0001-"));
}

/// Graph dateTimeTimeZone dateTime: usually offset-less wall time with a
/// 7-digit fraction ("2026-08-23T09:00:00.0000000"), sometimes a full
/// offset/Z form. Returns a valid QDateTime either way; zone interpretation
/// is the caller's job for the wall-time form.
QDateTime parseGraphDateTime(const QString& raw)
{
    if (raw.isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(raw, Qt::ISODate);
    if (dt.isValid())
        return dt;
    static const QRegularExpression frac(
        QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d+)?$"));
    const auto m = frac.match(raw);
    if (!m.hasMatch())
        return {};
    return QDateTime::fromString(m.captured(1), QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

/// Graph wire dateTime (wall time in `zone`) → canon start/end object
/// ({dateTime UTC ISO, tz, floating:false} [, tzOriginal]).
/// Windows-vocabulary zones resolve through the vendored CLDR map; the
/// ORIGINAL id rides along in `tzOriginal` when it differs (O57(b)
/// split-brain requirement: author's zone survives round-trips).
QJsonObject msTimeToCanon(const QJsonObject& g)
{
    const QString raw = g.value(QStringLiteral("dateTime")).toString();
    const QString tzRaw = g.value(QStringLiteral("timeZone")).toString();

    QString iana = tzRaw;
    bool splitBrain = false;
    // Only resolve through the CLDR map when the id is NOT already valid
    // IANA vocabulary ("UTC", "America/New_York", … pass through verbatim;
    // Windows ids like "Eastern Standard Time" resolve + preserve original).
    if (!tzRaw.isEmpty()
        && !QTimeZone(tzRaw.toLatin1()).isValid()) {
        const QString resolved =
            Kalburator::Calendar::WindowsZones::windowsZoneToIana(tzRaw);
        if (!resolved.isEmpty()) {
            iana = resolved;
            splitBrain = (iana != tzRaw);
        }
    }

    // Offset/Z forms parse as absolute instants; offset-less wall time (the
    // common O57 wire form) must be INTERPRETED IN the target zone — never
    // through the process-local zone.
    QDateTime dt = QDateTime::fromString(raw, Qt::ISODate);
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime) {
        dt = {};
        static const QRegularExpression frac(
            QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d+)?$"));
        const auto m = frac.match(raw);
        if (m.hasMatch()) {
            const QDateTime wall = QDateTime::fromString(
                m.captured(1), QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
            if (wall.isValid()) {
                const QTimeZone zone(iana.isEmpty()
                                         ? QByteArrayLiteral("UTC")
                                         : iana.toLatin1());
                dt = zone.isValid() ? QDateTime(wall.date(), wall.time(), zone)
                                    : QDateTime(wall.date(), wall.time(), Qt::UTC);
            }
        }
    }
    if (!dt.isValid())
        return {};

    QJsonObject obj;
    obj.insert(QStringLiteral("dateTime"), dt.toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("floating"), false);
    if (!iana.isEmpty())
        obj.insert(QStringLiteral("tz"), iana);
    if (splitBrain)
        obj.insert(QStringLiteral("tzOriginal"), tzRaw);
    return obj;
}

/// Canon start/end/recurrenceId object → Graph dateTimeTimeZone.
/// Emits the zone's LOCAL wall time with a 7-digit fraction (the wire form
/// observed in O57 payloads); `tzOriginal` wins over `tz` so split-brained
/// events re-emit their author's vocabulary verbatim. Windows-vocabulary ids
/// resolve through the vendored CLDR map for the WALL-TIME INTERPRETATION
/// only — the emitted timeZone string stays verbatim.
QJsonObject canonTimeToMs(const QJsonObject& c)
{
    if (c.contains(QStringLiteral("date")))
        return {};
    QDateTime utc = QDateTime::fromString(
        c.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
    if (!utc.isValid())
        return {};

    QString tzId = QStringLiteral("UTC");
    if (c.contains(QStringLiteral("tzOriginal")))
        tzId = c.value(QStringLiteral("tzOriginal")).toString();
    else if (!c.value(QStringLiteral("tz")).toString().isEmpty())
        tzId = c.value(QStringLiteral("tz")).toString();

    QString interpretAs = tzId;
    if (const QString resolved =
            Kalburator::Calendar::WindowsZones::windowsZoneToIana(tzId);
        !resolved.isEmpty())
        interpretAs = resolved;
    const QTimeZone tz(interpretAs.toLatin1());
    const QDateTime local = tz.isValid() ? utc.toTimeZone(tz) : utc;

    QJsonObject obj;
    obj.insert(QStringLiteral("dateTime"),
               local.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
                   + QStringLiteral(".0000000"));
    obj.insert(QStringLiteral("timeZone"), tzId);
    return obj;
}

/// Date portion ("yyyy-MM-dd") of an ISO string.
QString datePortion(const QString& iso)
{
    return iso.size() >= 10 ? iso.left(10) : iso;
}

/// All-day midnight pair for one endpoint ({date:"…"} → timed midnight).
QJsonObject allDayMsTime(const QString& dateIso, const QString& tzId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("dateTime"), datePortion(dateIso)
        + QStringLiteral("T00:00:00.0000000"));
    obj.insert(QStringLiteral("timeZone"),
               tzId.isEmpty() ? QStringLiteral("UTC") : tzId);
    return obj;
}

} // namespace

namespace Kalburator::Calendar {

// ---------------------------------------------------------------------------
// MsEventToCanonStage — Graph event JSON → canon JSON (lossless promote)
// ---------------------------------------------------------------------------

QByteArray MsEventToCanonStage::transform(const QByteArray& msBytes) const
{
    if (msBytes.isEmpty())
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(msBytes);
    if (!doc.isObject())
        return {};
    const QJsonObject ev = doc.object();

    QJsonObject obj;
    QJsonObject extrasMs;   // providerExtras["msgraph"]

    // ---- uid ← top-level uid (= iCalUId, O57(a)); fallback transport id ----
    QString uid = ev.value(QStringLiteral("uid")).toString();
    if (uid.isEmpty())
        uid = ev.value(QStringLiteral("iCalUId")).toString();
    if (uid.isEmpty())
        uid = ev.value(QStringLiteral("id")).toString();

    // ---- createdDateTime / lastModifiedDateTime (normalized UTC ISO) -------
    {
        const QDateTime created = QDateTime::fromString(
            ev.value(QStringLiteral("createdDateTime")).toString(), Qt::ISODate);
        if (created.isValid() && !isYearOneSentinel(
                ev.value(QStringLiteral("createdDateTime")).toString()))
            obj.insert(QStringLiteral("created"), created.toUTC().toString(Qt::ISODate));
        const QDateTime lastMod = QDateTime::fromString(
            ev.value(QStringLiteral("lastModifiedDateTime")).toString(), Qt::ISODate);
        if (lastMod.isValid() && !isYearOneSentinel(
                ev.value(QStringLiteral("lastModifiedDateTime")).toString()))
            obj.insert(QStringLiteral("lastModified"),
                       lastMod.toUTC().toString(Qt::ISODate));
    }

    // ---- subject / body ------------------------------------------------------
    {
        const QString subject = ev.value(QStringLiteral("subject")).toString();
        if (!subject.isEmpty())
            obj.insert(QStringLiteral("summary"), subject);
        const QJsonObject body = ev.value(QStringLiteral("body")).toObject();
        const QString contentType =
            body.value(QStringLiteral("contentType")).toString().toLower();
        const QString content = body.value(QStringLiteral("content")).toString();
        if (!content.isEmpty()) {
            if (contentType == QLatin1String("html"))
                obj.insert(QStringLiteral("descriptionHtml"), content);
            else
                obj.insert(QStringLiteral("description"), content);
        }
    }

    // ---- location / locations -----------------------------------------------
    {
        const QJsonObject loc = ev.value(QStringLiteral("location")).toObject();
        const QString displayName = loc.value(QStringLiteral("displayName")).toString();
        if (!displayName.isEmpty())
            obj.insert(QStringLiteral("location"), displayName);
        QJsonObject locLeftovers;
        for (auto it = loc.constBegin(); it != loc.constEnd(); ++it)
            if (it.key() != QStringLiteral("displayName"))
                locLeftovers.insert(it.key(), it.value());
        if (!locLeftovers.isEmpty())
            extrasMs.insert(QStringLiteral("location"), locLeftovers);

        const QJsonArray locs = ev.value(QStringLiteral("locations")).toArray();
        if (!locs.isEmpty()) {
            QJsonArray arr;
            bool anyLeftovers = false;
            for (const auto& lv : locs) {
                const QJsonObject l = lv.toObject();
                QJsonObject entry;
                for (const QString keep : { QStringLiteral("displayName"),
                                            QStringLiteral("address"),
                                            QStringLiteral("coordinates") }) {
                    if (l.contains(keep))
                        entry.insert(keep, l.value(keep));
                }
                for (auto it = l.constBegin(); it != l.constEnd(); ++it)
                    if (it.key() != QStringLiteral("displayName")
                        && it.key() != QStringLiteral("address")
                        && it.key() != QStringLiteral("coordinates"))
                        anyLeftovers = true;   // Bing-resolved extras (O57(c))
                arr.append(entry);
            }
            obj.insert(QStringLiteral("locations"), arr);
            if (anyLeftovers)
                extrasMs.insert(QStringLiteral("locations"), locs);
        }
    }

    // ---- start / end (+ all-day detection) ------------------------------------
    {
        const bool isAllDay = ev.value(QStringLiteral("isAllDay")).toBool(false);
        const QJsonObject startObj = ev.value(QStringLiteral("start")).toObject();
        const QJsonObject endObj = ev.value(QStringLiteral("end")).toObject();
        if (isAllDay && !startObj.isEmpty()) {
            // Degraded: Graph holds no date-only form — detect the midnight
            // pair and promote to canon date forms.
            obj.insert(QStringLiteral("start"),
                       QJsonObject{
                           {QStringLiteral("date"),
                            datePortion(startObj.value(QStringLiteral("dateTime")).toString())},
                           {QStringLiteral("allDay"), true}});
            if (!endObj.isEmpty())
                obj.insert(QStringLiteral("end"),
                           QJsonObject{
                               {QStringLiteral("date"),
                                datePortion(endObj.value(QStringLiteral("dateTime")).toString())},
                               {QStringLiteral("allDay"), true}});
            obj.insert(QStringLiteral("allDay"), true);
        } else {
            const QJsonObject cs = msTimeToCanon(startObj);
            if (!cs.isEmpty()) {
                obj.insert(QStringLiteral("start"), cs);
                if (cs.value(QStringLiteral("allDay")).toBool())
                    obj.insert(QStringLiteral("allDay"), true);
            }
            const QJsonObject ce = msTimeToCanon(endObj);
            if (!ce.isEmpty())
                obj.insert(QStringLiteral("end"), ce);
        }
    }

    // ---- recurrence (via the converter; invariant 5) ---------------------------
    {
        const QJsonArray cancelled = ev.value(QStringLiteral("cancelledOccurrences")).toArray();
        QStringList unparsed;
        QStringList cancelIsos;
        for (const auto& cv : cancelled) {
            const QString s = cv.toString();
            if (QDateTime::fromString(s, Qt::ISODate).isValid()
                || QDateTime::fromString(s, QStringLiteral("yyyyMMddThhmmssZ")).isValid())
                cancelIsos << s;
            else
                unparsed << s;   // MAPI blob ids: no offline EXDATE mapping
        }
        const QJsonObject pr = ev.value(QStringLiteral("recurrence")).toObject();
        if (!pr.isEmpty()) {
            const QStringList lines = RecurrencePattern::patternedRecurrenceToRruleLines(
                pr, cancelIsos, &unparsed);
            if (!lines.isEmpty()) {
                obj.insert(QStringLiteral("recurrence"),
                           QJsonArray::fromStringList(lines));
            }
        }
        if (!unparsed.isEmpty())
            extrasMs.insert(QStringLiteral("cancelledOccurrences"),
                            QJsonArray::fromStringList(unparsed));
    }

    // ---- record topology: exception ⇒ recurrenceId keyed by originalStart ------
    {
        const QString type = ev.value(QStringLiteral("type")).toString();
        const QString originalStart =
            ev.value(QStringLiteral("originalStart")).toString();
        if (type == QLatin1String("exception") && !originalStart.isEmpty()
            && !isYearOneSentinel(originalStart)) {
            QJsonObject og;
            og.insert(QStringLiteral("dateTime"), originalStart);
            og.insert(QStringLiteral("timeZone"),
                      ev.value(QStringLiteral("originalStartTimeZone")).toString());
            const QJsonObject rid = msTimeToCanon(og);
            if (!rid.isEmpty()) {
                obj.insert(QStringLiteral("recurrenceId"), rid);
                extrasMs.remove(QStringLiteral("originalStart"));
                extrasMs.remove(QStringLiteral("originalStartTimeZone"));
                extrasMs.remove(QStringLiteral("originalEndTimeZone"));
            }
        }
    }

    // ---- status (isCancelled; tentative has no master-level form) ---------------
    {
        if (ev.value(QStringLiteral("isCancelled")).toBool(false))
            obj.insert(QStringLiteral("status"), QStringLiteral("cancelled"));
    }

    // ---- classification ⇄ sensitivity --------------------------------------------
    {
        // Promote keeps the vendor value verbatim (including "personal",
        // which is NOT in the losslessValues set); the demote side owns the
        // personal→private+carrier mapping. This keeps G→C→G diffs inside
        // the declared Degraded set only.
        const QString sens = ev.value(QStringLiteral("sensitivity")).toString();
        if (!sens.isEmpty()) {
            if (sens == QLatin1String("normal"))
                obj.insert(QStringLiteral("classification"), QStringLiteral("public"));
            else
                obj.insert(QStringLiteral("classification"), sens);
        }
    }

    // ---- free/busy ⇄ showAs ---------------------------------------------------------
    {
        const QString showAs = ev.value(QStringLiteral("showAs")).toString();
        if (!showAs.isEmpty()) {
            obj.insert(QStringLiteral("freeBusyStatus"), showAs);
            obj.insert(QStringLiteral("timeTransparency"),
                       showAs == QLatin1String("free") ? QStringLiteral("transparent")
                                                       : QStringLiteral("opaque"));
        }
    }

    // ---- categories (direct string array) ---------------------------------------------
    {
        const QJsonArray cats = ev.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty())
            obj.insert(QStringLiteral("categories"), cats);
    }

    // ---- participants -------------------------------------------------------------------
    if (ev.contains(QStringLiteral("organizer"))) {
        const QJsonObject org =
            ev.value(QStringLiteral("organizer")).toObject();
        const QJsonObject addr = org.value(QStringLiteral("emailAddress")).toObject();
        QJsonObject orgObj;
        const QString email = addr.value(QStringLiteral("address")).toString();
        if (!email.isEmpty())
            orgObj.insert(QStringLiteral("email"), email);
        const QString name = addr.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            orgObj.insert(QStringLiteral("name"), name);
        if (!orgObj.isEmpty())
            obj.insert(QStringLiteral("organizer"), orgObj);
        QJsonObject orgLeftovers;
        for (auto it = org.constBegin(); it != org.constEnd(); ++it)
            if (it.key() != QStringLiteral("emailAddress"))
                orgLeftovers.insert(it.key(), it.value());
        for (auto it = addr.constBegin(); it != addr.constEnd(); ++it)
            if (it.key() != QStringLiteral("address")
                && it.key() != QStringLiteral("name"))
                orgLeftovers.insert(QStringLiteral("emailAddress/") + it.key(),
                                    it.value());
        if (!orgLeftovers.isEmpty())
            extrasMs.insert(QStringLiteral("organizer"), orgLeftovers);
    }

    {
        const QJsonArray attendees = ev.value(QStringLiteral("attendees")).toArray();
        if (!attendees.isEmpty()) {
            QJsonArray arr;
            bool anyLeftovers = false;
            for (const auto& av : attendees) {
                const QJsonObject a = av.toObject();
                const QString email = a.value(QStringLiteral("emailAddress"))
                                          .toObject()
                                          .value(QStringLiteral("address"))
                                          .toString();
                if (email.isEmpty()) {
                    anyLeftovers = true;
                    continue;
                }
                QJsonObject entry;
                entry.insert(QStringLiteral("email"), email);
                const QString name = a.value(QStringLiteral("emailAddress"))
                                         .toObject()
                                         .value(QStringLiteral("name"))
                                         .toString();
                if (!name.isEmpty())
                    entry.insert(QStringLiteral("name"), name);
                const QString type = a.value(QStringLiteral("type")).toString();
                entry.insert(QStringLiteral("role"),
                             type == QLatin1String("resource")
                                 ? QStringLiteral("resource")
                                 : type == QLatin1String("optional")
                                       ? QStringLiteral("optional")
                                       : QStringLiteral("required"));
                const QString rs = a.value(QStringLiteral("status"))
                                       .toObject()
                                       .value(QStringLiteral("response"))
                                       .toString();
                // Graph response vocabulary → canon partstat:
                //   accepted/declined pass through; tentativelyAccepted →
                //   tentative; none (and anything foreign) → needsAction.
                QString partstat;
                if (rs == QLatin1String("accepted")
                    || rs == QLatin1String("declined"))
                    partstat = rs;
                else if (rs == QLatin1String("tentativelyAccepted"))
                    partstat = QStringLiteral("tentative");
                else
                    partstat = QStringLiteral("needsAction");
                entry.insert(QStringLiteral("partstat"), partstat);
                entry.insert(QStringLiteral("rsvp"), false);
                // O57(t): attendee rows may multiply per vendor-canonical
                // identity; the edge maps rows faithfully — convergence is an
                // engine/identity-layer concern.
                for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
                    if (it.key() == QStringLiteral("emailAddress")
                        || it.key() == QStringLiteral("type")
                        || it.key() == QStringLiteral("status"))
                        continue;
                    anyLeftovers = true;   // proposedNewTime etc.
                }
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("attendees"), arr);
            if (anyLeftovers)
                extrasMs.insert(QStringLiteral("attendees"), attendees);
        }
    }

    // ---- owner's own responseStatus (sentinels normalize ABSENT, O57(d)) --------
    {
        // LIVE-CHECKPOINT FINDING: organizer-self rows carry response:"organizer"
        // WITH the year-1 time sentinel. Dropping the whole object lost the
        // response VALUE — normalize only the sentinel time away.
        QJsonObject rs = ev.value(QStringLiteral("responseStatus")).toObject();
        if (!rs.isEmpty()) {
            if (isYearOneSentinel(rs.value(QStringLiteral("time")).toString()))
                rs.remove(QStringLiteral("time"));
            extrasMs.insert(QStringLiteral("responseStatus"), rs);
        }
    }

    // ---- priority ⇄ importance (bucket mapping) -----------------------------------
    {
        const QString imp = ev.value(QStringLiteral("importance")).toString();
        if (!imp.isEmpty()) {
            const int prio = imp == QLatin1String("high")     ? 1
                             : imp == QLatin1String("low")    ? 9
                                                              : 5;
            obj.insert(QStringLiteral("priority"), prio);
        }
    }

    // ---- alarms (first display alarm ⇄ reminder pair) --------------------------------
    {
        const bool on = ev.value(QStringLiteral("isReminderOn")).toBool(false);
        const int mins =
            ev.value(QStringLiteral("reminderMinutesBeforeStart")).toInt(-1);
        if (on && mins >= 0) {
            QJsonObject alarm;
            alarm.insert(QStringLiteral("type"), kAlarmTypeDisplay);
            alarm.insert(QStringLiteral("offset"), -(mins * 60));
            obj.insert(QStringLiteral("alarms"), QJsonArray{alarm});
        }
    }

    // ---- online meeting --------------------------------------------------------------
    {
        const QJsonObject om = ev.value(QStringLiteral("onlineMeeting")).toObject();
        const QString joinUrl = om.value(QStringLiteral("joinUrl")).toString();
        if (!joinUrl.isEmpty())
            obj.insert(QStringLiteral("onlineMeeting"),
                       QJsonObject{{QStringLiteral("joinUrl"), joinUrl}});
    }

    // ---- attachments -------------------------------------------------------------------
    {
        const QJsonArray attachments =
            ev.value(QStringLiteral("attachments")).toArray();
        if (!attachments.isEmpty()) {
            QJsonArray arr;
            bool anyLeftovers = false;
            for (const auto& av : attachments) {
                const QJsonObject a = av.toObject();
                QJsonObject entry;
                const QString sourceUrl =
                    a.value(QStringLiteral("sourceUrl")).toString();
                if (!sourceUrl.isEmpty())
                    entry.insert(QStringLiteral("url"), sourceUrl);
                const QString name = a.value(QStringLiteral("name")).toString();
                if (!name.isEmpty())
                    entry.insert(QStringLiteral("title"), name);
                const QString mime =
                    a.value(QStringLiteral("contentType")).toString();
                if (!mime.isEmpty())
                    entry.insert(QStringLiteral("mimeType"), mime);
                for (auto it = a.constBegin(); it != a.constEnd(); ++it)
                    if (it.key() != QStringLiteral("sourceUrl")
                        && it.key() != QStringLiteral("name")
                        && it.key() != QStringLiteral("contentType"))
                        anyLeftovers = true;   // item/file payload fields
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("attachments"), arr);
            if (anyLeftovers)
                extrasMs.insert(QStringLiteral("attachments"), attachments);
        }
    }

    // ---- webLink → url -----------------------------------------------------------------
    {
        const QString webLink = ev.value(QStringLiteral("webLink")).toString();
        if (!webLink.isEmpty())
            obj.insert(QStringLiteral("url"), webLink);
    }

    // ---- MS flags with canon homes -------------------------------------------------------
    for (const QString graphKey : { QStringLiteral("allowNewTimeProposals"),
                                    QStringLiteral("hideAttendees") }) {
        const QJsonValue v = ev.value(graphKey);
        if (v.isBool())
            obj.insert(graphKey, v.toBool());
    }

    // ---- extended-property carriers (x-canon-* → canon props) ----------------------------
    {
        const QJsonArray sveps =
            ev.value(QStringLiteral("singleValueExtendedProperties")).toArray();
        QJsonArray svepRemainder;
        for (const auto& sv : sveps) {
            const QJsonObject p = sv.toObject();
            const QString id = p.value(QStringLiteral("id")).toString();
            if (!id.contains(QLatin1String(kSvepGuid))) {
                svepRemainder.append(sv);
                continue;
            }
            // "String {guid} Name x-canon-<kebab>" → kebab suffix after "Name ".
            const int nameIdx = id.indexOf(QLatin1String("Name "));
            const QString key = nameIdx >= 0 ? id.mid(nameIdx + 5) : QString();
            const QString prop = propFromCarrierKey(key);
            if (prop.isEmpty()) {
                svepRemainder.append(sv);
                continue;
            }
            const QJsonValue value = carrierStringToValue(
                p.value(QStringLiteral("value")));
            if (prop == QStringLiteral("floating")) {
                const bool floating = value.isBool() ? value.toBool()
                                                     : value.toDouble() != 0.0;
                if (floating) {
                    for (const QString endpoint :
                         { QStringLiteral("start"), QStringLiteral("end") }) {
                        QJsonObject t = obj.value(endpoint).toObject();
                        if (t.contains(QStringLiteral("dateTime"))) {
                            t.remove(QStringLiteral("tz"));
                            t.remove(QStringLiteral("tzOriginal"));
                            t.insert(QStringLiteral("floating"), true);
                            obj.insert(endpoint, t);
                        }
                    }
                }
            } else if (prop == QStringLiteral("alarms")) {
                QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
                for (const auto& a : value.toArray())
                    alarms.append(a);
                if (!value.toArray().isEmpty())
                    obj.insert(QStringLiteral("alarms"), alarms);
            } else if (prop == QStringLiteral("classification")) {
                // Carrier overrides the sensitivity-derived twin (restores e.g.
                // canon "personal" verbatim).
                obj.insert(prop, value);
            } else if (prop == QStringLiteral("recurrence")) {
                // Carried lines replace the RRULE re-derived from
                // patternedRecurrence (they hold the ORIGINAL rule set) but
                // keep EXDATE lines materialized from
                // cancelledOccurrences on this promote.
                QJsonArray rec = obj.value(QStringLiteral("recurrence")).toArray();
                QJsonArray merged = value.toArray();
                for (const auto& lv : rec)
                    if (!lv.toString().startsWith(QLatin1String("RRULE"),
                                                  Qt::CaseInsensitive))
                        merged.append(lv);
                if (!merged.isEmpty())
                    obj.insert(QStringLiteral("recurrence"), merged);
            } else {
                obj.insert(prop, value);
            }
        }
        if (!svepRemainder.isEmpty())
            extrasMs.insert(QStringLiteral("singleValueExtendedProperties"),
                            svepRemainder);
        const QJsonArray mveps =
            ev.value(QStringLiteral("multiValueExtendedProperties")).toArray();
        if (!mveps.isEmpty())
            extrasMs.insert(QStringLiteral("multiValueExtendedProperties"), mveps);
    }

    // ---- everything unmapped → providerExtras["msgraph"] verbatim --------------------------
    {
        static const QSet<QString> consumed = {
            // NOTE: id/changeKey deliberately NOT consumed — transport-local
            // identity stashes under providerExtras["msgraph"] (O55-style
            // aliasing anchor lives in `uid`, which IS consumed above).
            // iCalUId deliberately NOT consumed: live wires carry BOTH
            // uid AND iCalUId (identical values, O57(a)); stashing keeps
            // G→C→G byte-equal via passthrough.
            QStringLiteral("uid"),
            QStringLiteral("createdDateTime"),
            QStringLiteral("lastModifiedDateTime"),
            QStringLiteral("subject"), QStringLiteral("body"),
            QStringLiteral("location"), QStringLiteral("locations"),
            QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("isAllDay"),
            QStringLiteral("recurrence"), QStringLiteral("cancelledOccurrences"),
            QStringLiteral("categories"), QStringLiteral("organizer"),
            QStringLiteral("attendees"), QStringLiteral("responseRequested"),
            QStringLiteral("importance"), QStringLiteral("sensitivity"),
            QStringLiteral("showAs"), QStringLiteral("isCancelled"),
            QStringLiteral("isReminderOn"),
            QStringLiteral("reminderMinutesBeforeStart"),
            QStringLiteral("onlineMeeting"), QStringLiteral("weblink"),
            QStringLiteral("webLink"),
            QStringLiteral("isOnlineMeeting"), QStringLiteral("onlineMeetingProvider"),
            QStringLiteral("allowNewTimeProposals"), QStringLiteral("hideAttendees"),
            QStringLiteral("singleValueExtendedProperties"),
            QStringLiteral("multiValueExtendedProperties"),
            QStringLiteral("responseStatus")
        };
        for (auto it = ev.constBegin(); it != ev.constEnd(); ++it)
            if (!consumed.contains(it.key()))
                extrasMs.insert(it.key(), it.value());

        // Redundant-topology suppression: `type` is reconstructed
        // structurally by the demote side, so when the wire value equals the
        // derivation it must NOT double-stash (keeps C→G→C byte-equal).
        // Surprising topologies stash verbatim and re-emit untouched.
        {
            QString derived;
            if (obj.contains(QStringLiteral("recurrenceId")))
                derived = QStringLiteral("exception");
            else if (!ev.value(QStringLiteral("recurrence")).toObject().isEmpty())
                derived = QStringLiteral("seriesMaster");
            else if (!ev.value(QStringLiteral("seriesMasterId")).toString().isEmpty())
                derived = QStringLiteral("occurrence");
            else
                derived = QStringLiteral("singleInstance");
            if (ev.value(QStringLiteral("type")).toString() == derived)
                extrasMs.remove(QStringLiteral("type"));
        }
    }

    // responseRequested — Graph natively has it (unlike Google)
    {
        const QJsonValue rr = ev.value(QStringLiteral("responseRequested"));
        if (rr.isBool())
            obj.insert(QStringLiteral("responseRequested"), rr.toBool());
    }

    if (!extrasMs.isEmpty()) {
        QJsonObject extras;
        extras.insert(QStringLiteral("msgraph"), extrasMs);
        obj.insert(providerExtrasKey(), extras);
    }
    stampEnvelope(obj, QStringLiteral("calendar"), uid);
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// CanonToMsEventStage — canon JSON → Graph event JSON (lossy demote)
// ---------------------------------------------------------------------------

QByteArray CanonToMsEventStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};

    QJsonObject out;
    QJsonObject carriers;   // singleValueExtendedProperties entries by key

    const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
    const QJsonObject extrasMs = extras.value(QStringLiteral("msgraph")).toObject();

    // Vendor passthrough first (id, changeKey, bodyPreview, type, …) minus
    // the keys rebuilt below.
    static const QSet<QString> kRebuilt = {
        QStringLiteral("createdDateTime"), QStringLiteral("lastModifiedDateTime"),
        QStringLiteral("subject"), QStringLiteral("body"),
        QStringLiteral("location"), QStringLiteral("locations"),
        QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("isAllDay"),
        QStringLiteral("recurrence"), QStringLiteral("cancelledOccurrences"),
        // originalStart IS rebuilt for exceptions; the two original*TimeZone
        // fields are NOT rebuilt — they are master-level creation metadata
        // and ride the passthrough verbatim (LIVE-CHECKPOINT FINDING: they
        // carry the author's zone even when endpoints were re-homed to UTC).
        QStringLiteral("originalStart"), QStringLiteral("type"),
        QStringLiteral("categories"), QStringLiteral("organizer"),
        QStringLiteral("attendees"), QStringLiteral("responseRequested"),
        QStringLiteral("importance"), QStringLiteral("sensitivity"),
        QStringLiteral("showAs"), QStringLiteral("isCancelled"),
        QStringLiteral("isReminderOn"), QStringLiteral("reminderMinutesBeforeStart"),
        QStringLiteral("onlineMeeting"), QStringLiteral("webLink"),
        QStringLiteral("allowNewTimeProposals"), QStringLiteral("hideAttendees"),
        QStringLiteral("singleValueExtendedProperties"),
        QStringLiteral("multiValueExtendedProperties")
        // responseStatus deliberately NOT rebuilt: the promote stash
        // (sentinel-time-stripped) rides the passthrough back verbatim.
    };
    for (auto it = extrasMs.constBegin(); it != extrasMs.constEnd(); ++it)
        if (!kRebuilt.contains(it.key()))
            out.insert(it.key(), it.value());

    // ---- uid → top-level uid ------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            out.insert(QStringLiteral("uid"), uid);
    }

    // ---- timestamps -----------------------------------------------------------
    {
        // Graph wire form: offset-less UTC with 7-digit fraction
        // ("2026-08-23T09:00:00.0000000") — never "…Z.0000000".
        const QDateTime created = QDateTime::fromString(
            obj.value(QStringLiteral("created")).toString(), Qt::ISODate);
        if (created.isValid())
            out.insert(QStringLiteral("createdDateTime"),
                       created.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
                           + QStringLiteral(".0000000Z"));
        const QDateTime lastMod = QDateTime::fromString(
            obj.value(QStringLiteral("lastModified")).toString(), Qt::ISODate);
        if (lastMod.isValid())
            out.insert(QStringLiteral("lastModifiedDateTime"),
                       lastMod.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
                           + QStringLiteral(".0000000Z"));
    }

    // ---- summary / body ----------------------------------------------------------
    {
        const QString summary = obj.value(QStringLiteral("summary")).toString();
        if (!summary.isEmpty())
            out.insert(QStringLiteral("subject"), summary);
        const QString html = obj.value(QStringLiteral("descriptionHtml")).toString();
        const QString text = obj.value(QStringLiteral("description")).toString();
        if (!html.isEmpty()) {
            out.insert(QStringLiteral("body"),
                       QJsonObject{{QStringLiteral("contentType"),
                                    QStringLiteral("HTML")},
                                   {QStringLiteral("content"), html}});
        } else if (!text.isEmpty()) {
            out.insert(QStringLiteral("body"),
                       QJsonObject{{QStringLiteral("contentType"),
                                    QStringLiteral("text")},
                                   {QStringLiteral("content"), text}});
        }
    }

    // ---- location / locations --------------------------------------------------------
    {
        const QJsonObject locStash = extrasMs.value(QStringLiteral("location")).toObject();
        if (!locStash.isEmpty()) {
            out.insert(QStringLiteral("location"), locStash);
        } else {
            const QString loc = obj.value(QStringLiteral("location")).toString();
            if (!loc.isEmpty())
                // displayName ONLY: extra fields would land in the promote
                // leftovers stash and break C→G→C byte-equality.
                out.insert(QStringLiteral("location"),
                           QJsonObject{{QStringLiteral("displayName"), loc}});
        }

        const QJsonArray locsStash =
            extrasMs.value(QStringLiteral("locations")).toArray();
        if (!locsStash.isEmpty()) {
            out.insert(QStringLiteral("locations"), locsStash);
        } else {
            const QJsonArray locs = obj.value(QStringLiteral("locations")).toArray();
            if (!locs.isEmpty())
                out.insert(QStringLiteral("locations"), locs);
        }
    }

    // ---- start / end (+ all-day emission) -----------------------------------------------
    {
        const QString tzForAllDay = [&] {
            const QJsonObject s = obj.value(QStringLiteral("start")).toObject();
            if (s.contains(QStringLiteral("tzOriginal")))
                return s.value(QStringLiteral("tzOriginal")).toString();
            return s.value(QStringLiteral("tz")).toString();
        }();

        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        if (obj.value(QStringLiteral("allDay")).toBool()
            || startObj.contains(QStringLiteral("date"))) {
            // Degraded: no date-only Graph form → midnight-to-midnight pair.
            out.insert(QStringLiteral("start"),
                       allDayMsTime(startObj.value(QStringLiteral("date")).toString(),
                                    tzForAllDay));
            const QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();
            out.insert(QStringLiteral("end"),
                       allDayMsTime(endObj.value(QStringLiteral("date")).toString(),
                                    tzForAllDay));
            out.insert(QStringLiteral("isAllDay"), true);
        } else {
            bool anyFloating = false;
            auto emitEndpoint = [&](const QString& key) {
                QJsonObject c = obj.value(key).toObject();
                if (c.isEmpty())
                    return;
                if (c.value(QStringLiteral("floating")).toBool()) {
                    // Floating time has no Graph form → UTC pin (carrier
                    // restores it on re-promote).
                    c.remove(QStringLiteral("tz"));
                    c.remove(QStringLiteral("tzOriginal"));
                    anyFloating = true;
                }
                QJsonObject g = canonTimeToMs(c);
                if (!g.isEmpty())
                    out.insert(key, g);
            };
            emitEndpoint(QStringLiteral("start"));
            emitEndpoint(QStringLiteral("end"));
            if (anyFloating)
                carriers.insert(carrierKey(QStringLiteral("floating")),
                                QStringLiteral("true"));
        }
    }

    // ---- recurrence (via the converter; invariant 5) --------------------------------------
    {
        QStringList lines;
        for (const auto& lv : obj.value(QStringLiteral("recurrence")).toArray())
            lines << lv.toString();

        QString dtstart;
        {
            const QJsonObject s = obj.value(QStringLiteral("start")).toObject();
            dtstart = s.value(QStringLiteral("dateTime")).toString();
            if (dtstart.isEmpty())
                dtstart = s.value(QStringLiteral("date")).toString();
        }
        // NOTE: carried recurrence lines (x-canon-recurrence, written when
        // this canon previously demoted an unrepresentable rule) are restored
        // on the PROMOTE side, not here: the demote emits the reduced pattern
        // plus the carrier; promote merges them back byte-identically.

        const auto conv =
            RecurrencePattern::rruleLinesToPatternedRecurrence(lines, dtstart);
        if (!conv.patternedRecurrence.isEmpty())
            out.insert(QStringLiteral("recurrence"), conv.patternedRecurrence);

        // Unrepresentable features: the FULL original RRULE rides the
        // x-canon-recurrence carrier so re-promote is byte-identical.
        if (!conv.carriedLines.isEmpty()) {
            QJsonArray carriedArr;
            for (const QString& l : conv.carriedLines)
                carriedArr.append(l);
            carriers.insert(carrierKey(QStringLiteral("recurrence")),
                            valueToCarrierString(carriedArr));
        }

        // cancelledOccurrences: EXDATE-derived ISO datetimes + stashed MAPI
        // blobs from the promote side.
        QJsonArray cancelled;
        for (const QString& ex : conv.exdates)
            cancelled.append(ex);
        const QJsonArray stashCancel =
            extrasMs.value(QStringLiteral("cancelledOccurrences")).toArray();
        for (const auto& cv : stashCancel)
            cancelled.append(cv);
        if (!cancelled.isEmpty())
            out.insert(QStringLiteral("cancelledOccurrences"), cancelled);
    }

    // ---- record topology: reconstructed structurally, never stored ----------
    {
        const bool hasRecurrence = out.contains(QStringLiteral("recurrence"));
        QString type;
        if (obj.contains(QStringLiteral("recurrenceId")))
            type = QStringLiteral("exception");
        else if (hasRecurrence)
            type = QStringLiteral("seriesMaster");
        else if (!extrasMs.value(QStringLiteral("seriesMasterId")).toString().isEmpty())
            type = QStringLiteral("occurrence");
        else
            type = QStringLiteral("singleInstance");
        out.insert(QStringLiteral("type"), type);

        // Exception records re-key on originalStart (from the promoted
        // recurrenceId); the originalStart stash was consumed on promote.
        if (type == QLatin1String("exception")) {
            const QJsonObject rid =
                obj.value(QStringLiteral("recurrenceId")).toObject();
            // Emit the canon UTC instant directly (master's
            // originalStartTimeZone is unknown at this point — it stayed
            // behind on the master record).
            const QDateTime utc = QDateTime::fromString(
                rid.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
            if (utc.isValid()) {
                out.insert(QStringLiteral("originalStart"),
                           utc.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))
                               + QStringLiteral(".0000000Z"));
            }
        } else if (extrasMs.contains(QStringLiteral("originalStart"))) {
            out.insert(QStringLiteral("originalStart"),
                       extrasMs.value(QStringLiteral("originalStart")));
        }
    }

    // ---- status (Degraded: tentative carries) -----------------------------------------------
    {
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("cancelled")) {
            out.insert(QStringLiteral("isCancelled"), true);
        } else if (!status.isEmpty() && status != QLatin1String("confirmed")) {
            // Tentative and any vendor-foreign value: no master-level form.
            carriers.insert(carrierKey(QStringLiteral("status")),
                            valueToCarrierString(status));
        }
    }

    // ---- classification ⇄ sensitivity (Degraded + losslessValues) -----------------------------
    {
        const QString cls = obj.value(QStringLiteral("classification")).toString();
        if (!cls.isEmpty()) {
            if (cls == QLatin1String("public")) {
                out.insert(QStringLiteral("sensitivity"), QStringLiteral("normal"));
            } else if (cls == QLatin1String("private")
                       || cls == QLatin1String("confidential")) {
                out.insert(QStringLiteral("sensitivity"), cls);
            } else {
                out.insert(QStringLiteral("sensitivity"), QStringLiteral("private"));
                carriers.insert(carrierKey(QStringLiteral("classification")), cls);
            }
        }
    }

    // ---- free/busy ⇄ showAs ----------------------------------------------------------------------
    {
        const QString fbs = obj.value(QStringLiteral("freeBusyStatus")).toString();
        const QString tt = obj.value(QStringLiteral("timeTransparency")).toString();
        static const QSet<QString> kShowAsVocab = {
            QStringLiteral("busy"), QStringLiteral("free"),
            QStringLiteral("tentative"), QStringLiteral("oof"),
            QStringLiteral("workingElsewhere")
        };
        if (!fbs.isEmpty() && kShowAsVocab.contains(fbs)) {
            out.insert(QStringLiteral("showAs"), fbs);
            // A transparency that re-promote would NOT derive from this
            // showAs must ride the carrier.
            const QString ttDerived =
                fbs == QLatin1String("free") ? QStringLiteral("transparent")
                                             : QStringLiteral("opaque");
            if (!tt.isEmpty() && tt != ttDerived)
                carriers.insert(carrierKey(QStringLiteral("timeTransparency")), tt);
        } else if (!fbs.isEmpty() || !tt.isEmpty()) {
            out.insert(QStringLiteral("showAs"),
                       tt == QLatin1String("transparent") ? QStringLiteral("free")
                                                          : QStringLiteral("busy"));
            if (!fbs.isEmpty())
                carriers.insert(carrierKey(QStringLiteral("freeBusyStatus")), fbs);
        }
    }

    // ---- categories ---------------------------------------------------------------------------------
    {
        const QJsonArray cats = obj.value(QStringLiteral("categories")).toArray();
        if (!cats.isEmpty())
            out.insert(QStringLiteral("categories"), cats);
    }

    // ---- organizer -------------------------------------------------------------------------------------
    {
        const QJsonObject orgObj = obj.value(QStringLiteral("organizer")).toObject();
        const QJsonObject orgStash = extrasMs.value(QStringLiteral("organizer")).toObject();
        QJsonObject orgOut = orgStash;
        QJsonObject addr = orgStash.value(QStringLiteral("emailAddress")).toObject();
        const QString email = orgObj.value(QStringLiteral("email")).toString();
        if (!email.isEmpty())
            addr.insert(QStringLiteral("address"), email);
        const QString name = orgObj.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            addr.insert(QStringLiteral("name"), name);
        if (!addr.isEmpty())
            orgOut.insert(QStringLiteral("emailAddress"), addr);
        if (!orgOut.isEmpty())
            out.insert(QStringLiteral("organizer"), orgOut);
    }

    // ---- attendees -----------------------------------------------------------------------------------------
    {
        const QJsonArray attStash = extrasMs.value(QStringLiteral("attendees")).toArray();
        if (!attStash.isEmpty()) {
            // Wire-fidelity preference: the promote stash round-trips MAPI
            // fields (status.time, proposedNewTime) that the canon encoding
            // cannot hold. Canonical edits flow through the engine baseline
            // diff, which sees canon attendees change and re-demotes with a
            // refreshed stash on the next promote.
            out.insert(QStringLiteral("attendees"), attStash);
        } else {
            const QJsonArray attendees =
                obj.value(QStringLiteral("attendees")).toArray();
            if (!attendees.isEmpty()) {
                QJsonArray arr;
                for (const auto& av : attendees) {
                    const QJsonObject a = av.toObject();
                    const QString email = a.value(QStringLiteral("email")).toString();
                    if (email.isEmpty())
                        continue;
                    QJsonObject entry;
                    QJsonObject addr;
                    addr.insert(QStringLiteral("address"), email);
                    const QString name = a.value(QStringLiteral("name")).toString();
                    if (!name.isEmpty())
                        addr.insert(QStringLiteral("name"), name);
                    entry.insert(QStringLiteral("emailAddress"), addr);
                    const QString role = a.value(QStringLiteral("role")).toString();
                    entry.insert(QStringLiteral("type"),
                                 role == QLatin1String("optional")
                                     ? QStringLiteral("optional")
                                     : role == QLatin1String("resource")
                                           ? QStringLiteral("resource")
                                           : QStringLiteral("required"));
                    const QString ps = a.value(QStringLiteral("partstat")).toString();
                    // canon partstat → Graph response vocabulary.
                    if (ps == QLatin1String("accepted")
                        || ps == QLatin1String("declined"))
                        entry.insert(QStringLiteral("status"),
                                     QJsonObject{{QStringLiteral("response"), ps}});
                    else if (ps == QLatin1String("tentative"))
                        entry.insert(
                            QStringLiteral("status"),
                            QJsonObject{{QStringLiteral("response"),
                                         QStringLiteral("tentativelyAccepted")}});
                    // needsAction ≡ absent (Graph "none") — emit nothing.
                    arr.append(entry);
                }
                if (!arr.isEmpty())
                    out.insert(QStringLiteral("attendees"), arr);
            }
        }
    }

    // ---- responseRequested / MS flags -------------------------------------------------------------------------
    {
        const QJsonValue rr = obj.value(QStringLiteral("responseRequested"));
        if (rr.isBool())
            out.insert(QStringLiteral("responseRequested"), rr.toBool());
        for (const QString key : { QStringLiteral("allowNewTimeProposals"),
                                   QStringLiteral("hideAttendees") }) {
            const QJsonValue v = obj.value(key);
            if (v.isBool())
                out.insert(key, v.toBool());
        }
    }

    // ---- priority ⇄ importance (Simplified bucket) ---------------------------------------------------------------
    {
        const int prio = obj.value(QStringLiteral("priority")).toInt(-1);
        if (prio >= 0) {
            const char* imp = (prio >= 1 && prio <= 4)  ? "high"
                              : (prio >= 6 && prio <= 9) ? "low"
                                                          : "normal";
            out.insert(QStringLiteral("importance"), QLatin1String(imp));
            // Exact buckets need no carrier; lossy ones carry the original.
            if (prio != 1 && prio != 5 && prio != 9)
                carriers.insert(carrierKey(QStringLiteral("priority")),
                                QString::number(prio));
        }
    }

    // ---- alarms (first mappable VALARM ⇄ reminder pair) ------------------------------------------------------------
    {
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        if (!alarms.isEmpty()) {
            QJsonArray carried;
            bool emitted = false;
            for (const auto& av : alarms) {
                const QJsonObject a = av.toObject();
                const int type = a.value(QStringLiteral("type")).toInt();
                const int offsetSecs = a.value(QStringLiteral("offset")).toInt();
                if (!emitted && type == kAlarmTypeDisplay && offsetSecs <= 0
                    && offsetSecs % 60 == 0) {
                    out.insert(QStringLiteral("isReminderOn"), true);
                    out.insert(QStringLiteral("reminderMinutesBeforeStart"),
                               -offsetSecs / 60);
                    emitted = true;
                } else {
                    carried.append(av);
                }
            }
            if (!carried.isEmpty())
                carriers.insert(carrierKey(QStringLiteral("alarms")),
                                valueToCarrierString(carried));
        }
    }

    // ---- online meeting ----------------------------------------------------------------------------------------------
    {
        const QJsonObject om = obj.value(QStringLiteral("onlineMeeting")).toObject();
        const QString joinUrl = om.value(QStringLiteral("joinUrl")).toString();
        if (!joinUrl.isEmpty()) {
            out.insert(QStringLiteral("onlineMeeting"),
                       QJsonObject{{QStringLiteral("joinUrl"), joinUrl}});
            if (!out.contains(QStringLiteral("isOnlineMeeting")))
                out.insert(QStringLiteral("isOnlineMeeting"), true);
        }
    }

    // ---- attachments ------------------------------------------------------------------------------------------------------
    {
        const QJsonArray attStash =
            extrasMs.value(QStringLiteral("attachments")).toArray();
        if (!attStash.isEmpty()) {
            out.insert(QStringLiteral("attachments"), attStash);
        } else {
            const QJsonArray attachments =
                obj.value(QStringLiteral("attachments")).toArray();
            if (!attachments.isEmpty()) {
                QJsonArray arr;
                for (const auto& av : attachments) {
                    const QJsonObject a = av.toObject();
                    QJsonObject entry;
                    const QString url = a.value(QStringLiteral("url")).toString();
                    if (!url.isEmpty()) {
                        entry.insert(QStringLiteral("@odata.type"),
                                     QStringLiteral("#microsoft.graph.referenceAttachment"));
                        entry.insert(QStringLiteral("sourceUrl"), url);
                    } else {
                        entry.insert(QStringLiteral("@odata.type"),
                                     QStringLiteral("#microsoft.graph.fileAttachment"));
                    }
                    const QString title = a.value(QStringLiteral("title")).toString();
                    if (!title.isEmpty())
                        entry.insert(QStringLiteral("name"), title);
                    const QString mime =
                        a.value(QStringLiteral("mimeType")).toString();
                    if (!mime.isEmpty())
                        entry.insert(QStringLiteral("contentType"), mime);
                    arr.append(entry);
                }
                if (!arr.isEmpty())
                    out.insert(QStringLiteral("attachments"), arr);
            }
        }
    }

    // ---- url ⇄ webLink (read-only upstream: emitted when present) ------------
    {
        const QString url = obj.value(QStringLiteral("url")).toString();
        if (!url.isEmpty())
            out.insert(QStringLiteral("webLink"), url);
        else if (!extrasMs.value(QStringLiteral("webLink")).toString().isEmpty())
            out.insert(QStringLiteral("webLink"),
                       extrasMs.value(QStringLiteral("webLink")));
    }

    // ---- unhandled canon props → carriers (never silently dropped) ------------
    {
        static const QSet<QString> handled = {
            // NOTE: sequence + recurrenceRange deliberately NOT handled —
            // no Graph form; the generic x-canon-* carrier below takes them.
            QStringLiteral("uid"), QStringLiteral("created"),
            QStringLiteral("lastModified"), QStringLiteral("summary"),
            QStringLiteral("description"), QStringLiteral("descriptionHtml"),
            QStringLiteral("location"), QStringLiteral("locations"),
            QStringLiteral("status"), QStringLiteral("classification"),
            QStringLiteral("timeTransparency"), QStringLiteral("freeBusyStatus"),
            QStringLiteral("categories"), QStringLiteral("url"),
            QStringLiteral("organizer"), QStringLiteral("attendees"),
            QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("allDay"),
            QStringLiteral("recurrence"), QStringLiteral("recurrenceId"),
            QStringLiteral("responseRequested"), QStringLiteral("priority"),
            QStringLiteral("alarms"), QStringLiteral("onlineMeeting"),
            QStringLiteral("attachments"),
            // eventType: canon holds the GOOGLE vocab — deliberately untouched
            // by this edge (declared decision); Graph `type` is reconstructed
            // structurally above. Values pass both directions in canon only.
            QStringLiteral("eventType"),
            QStringLiteral("allowNewTimeProposals"),
            QStringLiteral("hideAttendees")
        };
        // NOT in `handled` ⇒ generic x-canon-* carrier below picks them up:
        //   sequence, recurrenceRange, typedProperties, guestsCanModify,
        //   guestsCanInviteOthers, guestsCanSeeOtherGuests, locked,
        //   privateCopy — Graph has no native form for any of them.
        static const QSet<QString> dropped = {
            // color: Dropped (Graph events carry no color — calendar-level only).
            // geo + cross-kind fields: same ruling as the Google edge.
            QStringLiteral("color"), QStringLiteral("geo"), QStringLiteral("due"),
            QStringLiteral("completed"), QStringLiteral("percentComplete"),
            QStringLiteral("relatedTo")
        };
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (handled.contains(it.key()) || dropped.contains(it.key()))
                continue;
            if (it.key() == Kalburator::Shape::CanonEnvelope::canonKey()
                || it.key() == Kalburator::Shape::CanonEnvelope::uidKey()
                || it.key() == providerExtrasKey())
                continue;
            carriers.insert(carrierKey(it.key()),
                            valueToCarrierString(it.value()));
        }
    }

    // ---- assemble singleValueExtendedProperties ----------------------------------
    if (!carriers.isEmpty()) {
        QJsonArray sveps;
        for (auto it = carriers.constBegin(); it != carriers.constEnd(); ++it) {
            QJsonObject p;
            p.insert(QStringLiteral("id"),
                     QStringLiteral("String %1 Name %2")
                         .arg(QLatin1String(kSvepGuid), it.key()));
            p.insert(QStringLiteral("value"), it.value().toString());
            sveps.append(p);
        }
        out.insert(QStringLiteral("singleValueExtendedProperties"), sveps);
    }

    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------------------
// canonToMsEventLoss — LossProfile for the canon → ms-event demote
// (mirrors docs/2026-08-23-ms-event-edge-loss-profile.md exactly)
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToMsEventLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Dropped: color (calendar-level only), geo, cross-kind fields
    p.affected.insert(PropertyId{QStringLiteral("color")},             LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("geo")},               LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("due")},               LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("completed")},         LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("percentComplete")},   LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("relatedTo")},         LossKind::Dropped);

    // Simplified
    p.affected.insert(PropertyId{QStringLiteral("sequence")},      LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("recurrence")},    LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("attendees")},     LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("alarms")},        LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("attachments")},   LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("priority")},      LossKind::Simplified);

    // Degraded
    p.affected.insert(PropertyId{QStringLiteral("location")},        LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("locations")},       LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("start")},           LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("end")},             LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("allDay")},          LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("recurrenceId")},    LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("recurrenceRange")}, LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("status")},          LossKind::Degraded);
    p.losslessValues.insert(PropertyId{QStringLiteral("status")},
                            {QStringLiteral("confirmed"), QStringLiteral("tentative"),
                             QStringLiteral("cancelled")});
    p.affected.insert(PropertyId{QStringLiteral("classification")},  LossKind::Degraded);
    p.losslessValues.insert(PropertyId{QStringLiteral("classification")},
                            {QStringLiteral("public"), QStringLiteral("private"),
                             QStringLiteral("confidential")});
    p.affected.insert(PropertyId{QStringLiteral("freeBusyStatus")},  LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("responseStatus")},  LossKind::Degraded);
    p.affected.insert(PropertyId{QStringLiteral("eventType")},       LossKind::Degraded);
    p.losslessValues.insert(PropertyId{QStringLiteral("eventType")},
                            {QStringLiteral("singleInstance"),
                             QStringLiteral("occurrence"),
                             QStringLiteral("exception"),
                             QStringLiteral("seriesMaster")});

    // Reversible (carried via singleValueExtendedProperties x-canon-*)
    p.affected.insert(PropertyId{QStringLiteral("categories")},           LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("url")},                  LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("typedProperties")},      LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("guestsCanModify")},      LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("guestsCanInviteOthers")}, LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("guestsCanSeeOtherGuests")}, LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("locked")},               LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("privateCopy")},          LossKind::Reversible);

    return p;
}

}  // namespace Kalburator::Calendar
