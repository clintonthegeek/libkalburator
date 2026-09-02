#include "eventcanonfields.h"

#include "canonenvelope.h"
#include "icalcomponentscan.h"
#include "incidencecommonfields.h"

#include <KCalendarCore/Attendee>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

namespace {

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;
using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;

KCalendarCore::Event::Ptr parseEvent(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(data));
    return inc.dynamicCast<KCalendarCore::Event>();
}

QByteArray serializeEvent(const KCalendarCore::Event::Ptr &event)
{
    if (!event)
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(event).toUtf8();
}

/// Encode a QDateTime (from KCalendarCore) to a JSON object:
///   date-only: { "date": "YYYY-MM-DD", "allDay": true }
///   floating:  { "dateTime": "...", "floating": true }
///   with tz:   { "dateTime": "...", "tz": "<iana-id>", "floating": false }
QJsonObject dateTimeToJson(const QDateTime &dt, bool allDay = false)
{
    if (!dt.isValid())
        return {};
    QJsonObject obj;
    if (allDay) {
        obj.insert(QStringLiteral("date"),   dt.date().toString(Qt::ISODate));
        obj.insert(QStringLiteral("allDay"), true);
    } else {
        obj.insert(QStringLiteral("dateTime"), dt.toUTC().toString(Qt::ISODate));
        const bool floating = (dt.timeSpec() == Qt::LocalTime);
        obj.insert(QStringLiteral("floating"), floating);
        if (!floating && dt.timeSpec() == Qt::TimeZone) {
            const QString tzId = QString::fromLatin1(dt.timeZone().id());
            if (!tzId.isEmpty())
                obj.insert(QStringLiteral("tz"), tzId);
        }
    }
    return obj;
}

/// Reverse of dateTimeToJson.
QDateTime jsonToDateTime(const QJsonObject &obj)
{
    if (obj.isEmpty())
        return {};
    if (obj.value(QStringLiteral("allDay")).toBool() ||
        obj.contains(QStringLiteral("date"))) {
        const QDate d = QDate::fromString(
            obj.value(QStringLiteral("date")).toString(), Qt::ISODate);
        return d.isValid() ? QDateTime(d, QTime(0,0,0), QTimeZone::utc()) : QDateTime{};
    }
    const QString dtStr = obj.value(QStringLiteral("dateTime")).toString();
    if (dtStr.isEmpty())
        return {};
    const QDateTime dt = QDateTime::fromString(dtStr, Qt::ISODate);
    if (!dt.isValid())
        return {};
    const QString tzId = obj.value(QStringLiteral("tz")).toString();
    if (tzId.isEmpty())
        return dt;
    const QTimeZone tz(tzId.toLatin1());
    return tz.isValid() ? dt.toTimeZone(tz) : dt;
}

/// Convert status enum to string.
QString statusToString(KCalendarCore::Incidence::Status s)
{
    switch (s) {
    case KCalendarCore::Incidence::StatusTentative:  return QStringLiteral("tentative");
    case KCalendarCore::Incidence::StatusConfirmed:  return QStringLiteral("confirmed");
    case KCalendarCore::Incidence::StatusCanceled:   return QStringLiteral("cancelled");
    default:                                         return {};
    }
}

/// Reverse of statusToString.
KCalendarCore::Incidence::Status statusFromString(const QString &s)
{
    if (s == QStringLiteral("tentative"))  return KCalendarCore::Incidence::StatusTentative;
    if (s == QStringLiteral("confirmed"))  return KCalendarCore::Incidence::StatusConfirmed;
    if (s == QStringLiteral("cancelled"))  return KCalendarCore::Incidence::StatusCanceled;
    return KCalendarCore::Incidence::StatusNone;
}

/// Convert Attendee::PartStat enum to string.
QString partStatToString(KCalendarCore::Attendee::PartStat ps)
{
    switch (ps) {
    case KCalendarCore::Attendee::Accepted:      return QStringLiteral("accepted");
    case KCalendarCore::Attendee::Declined:      return QStringLiteral("declined");
    case KCalendarCore::Attendee::Tentative:     return QStringLiteral("tentative");
    case KCalendarCore::Attendee::Delegated:     return QStringLiteral("delegated");
    case KCalendarCore::Attendee::NeedsAction:   return QStringLiteral("needsAction");
    default:                                     return QStringLiteral("needsAction");
    }
}

/// Reverse of partStatToString.
KCalendarCore::Attendee::PartStat partStatFromString(const QString &s)
{
    if (s == QStringLiteral("accepted"))    return KCalendarCore::Attendee::Accepted;
    if (s == QStringLiteral("declined"))    return KCalendarCore::Attendee::Declined;
    if (s == QStringLiteral("tentative"))   return KCalendarCore::Attendee::Tentative;
    if (s == QStringLiteral("delegated"))   return KCalendarCore::Attendee::Delegated;
    return KCalendarCore::Attendee::NeedsAction;
}

/// Convert Attendee::Role enum to string.
QString roleToString(KCalendarCore::Attendee::Role r)
{
    switch (r) {
    case KCalendarCore::Attendee::Chair:           return QStringLiteral("chair");
    case KCalendarCore::Attendee::ReqParticipant:  return QStringLiteral("required");
    case KCalendarCore::Attendee::OptParticipant:  return QStringLiteral("optional");
    case KCalendarCore::Attendee::NonParticipant:  return QStringLiteral("nonParticipant");
    default:                                       return QStringLiteral("required");
    }
}

/// Reverse of roleToString.
KCalendarCore::Attendee::Role roleFromString(const QString &s)
{
    if (s == QStringLiteral("chair"))          return KCalendarCore::Attendee::Chair;
    if (s == QStringLiteral("optional"))       return KCalendarCore::Attendee::OptParticipant;
    if (s == QStringLiteral("nonParticipant")) return KCalendarCore::Attendee::NonParticipant;
    return KCalendarCore::Attendee::ReqParticipant;
}

} // namespace

namespace Kalburator::Calendar {

QJsonObject eventFieldsToCanon(const KCalendarCore::Event::Ptr& event,
                               const QByteArray& originalBytes)
{
    QJsonObject obj;

    // ---- sequence ----------------------------------------------------------
    {
        const int seq = event->revision();
        if (seq > 0)
            obj.insert(QStringLiteral("sequence"), seq);
    }

    // ---- created / lastModified (IP.6: incidencecommonfields, O41 guard) ---
    promoteTimestamps(obj, originalBytes);

    // ---- summary / description (IP.6: incidencecommonfields) ---------------
    promoteSummaryDescription(obj, event);

    // ---- descriptionHtml (X-ALT-DESC) — Reversible carrier -----------------
    {
        const QString altDesc = event->nonKDECustomProperty("X-ALT-DESC");
        if (!altDesc.isEmpty())
            obj.insert(QStringLiteral("descriptionHtml"), altDesc);
    }

    // ---- location ----------------------------------------------------------
    {
        const QString location = event->location();
        if (!location.isEmpty())
            obj.insert(QStringLiteral("location"), location);
    }

    // ---- status ------------------------------------------------------------
    {
        const QString status = statusToString(event->status());
        if (!status.isEmpty())
            obj.insert(QStringLiteral("status"), status);
    }

    // ---- classification ----------------------------------------------------
    {
        const auto cls = event->secrecy();
        QString clsStr;
        switch (cls) {
        case KCalendarCore::Incidence::SecrecyPublic:      clsStr = QStringLiteral("public");       break;
        case KCalendarCore::Incidence::SecrecyPrivate:     clsStr = QStringLiteral("private");      break;
        case KCalendarCore::Incidence::SecrecyConfidential: clsStr = QStringLiteral("confidential"); break;
        default: break;
        }
        if (!clsStr.isEmpty())
            obj.insert(QStringLiteral("classification"), clsStr);
    }

    // ---- timeTransparency --------------------------------------------------
    {
        const auto transp = event->transparency();
        const QString transpStr = (transp == KCalendarCore::Event::Transparent)
            ? QStringLiteral("transparent") : QStringLiteral("opaque");
        obj.insert(QStringLiteral("timeTransparency"), transpStr);
    }

    // ---- freeBusyStatus (X-MICROSOFT-CDO-BUSYSTATUS) -----------------------
    {
        const QString fbs = event->nonKDECustomProperty("X-MICROSOFT-CDO-BUSYSTATUS");
        if (!fbs.isEmpty())
            obj.insert(QStringLiteral("freeBusyStatus"), fbs);
    }

    // ---- start / end -------------------------------------------------------
    {
        const QDateTime start = event->dtStart();
        const bool allDay     = event->allDay();
        if (start.isValid()) {
            const QJsonObject startObj = dateTimeToJson(start, allDay);
            if (!startObj.isEmpty()) {
                obj.insert(QStringLiteral("start"),  startObj);
                obj.insert(QStringLiteral("allDay"), allDay);
            }
        }
    }
    {
        const QDateTime end = event->dtEnd();
        const bool allDay   = event->allDay();
        if (end.isValid()) {
            const QJsonObject endObj = dateTimeToJson(end, allDay);
            if (!endObj.isEmpty())
                obj.insert(QStringLiteral("end"), endObj);
        }
    }

    // ---- recurrence (verbatim lines — invariant 3) -------------------------
    {
        const QStringList recLines =
            extractComponentRecurrenceLines(originalBytes, "VEVENT", event->uid());
        if (!recLines.isEmpty()) {
            QJsonArray arr;
            for (const auto& l : recLines)
                arr.append(l);
            obj.insert(QStringLiteral("recurrence"), arr);
        }
    }

    // ---- recurrenceId / recurrenceRange ------------------------------------
    {
        const QDateTime recId = event->recurrenceId();
        if (recId.isValid()) {
            QJsonObject recIdObj;
            recIdObj.insert(QStringLiteral("dateTime"), recId.toUTC().toString(Qt::ISODate));
            obj.insert(QStringLiteral("recurrenceId"), recIdObj);

            // RANGE=THISANDFUTURE → recurrenceRange
            if (event->thisAndFuture())
                obj.insert(QStringLiteral("recurrenceRange"), QStringLiteral("thisAndFuture"));
        }
    }

    // ---- color -------------------------------------------------------------
    {
        const QString color = event->color();
        if (!color.isEmpty())
            obj.insert(QStringLiteral("color"), color);
    }

    // ---- categories (IP.6: incidencecommonfields) --------------------------
    promoteCategories(obj, event);

    // ---- url ---------------------------------------------------------------
    {
        const QUrl url = event->url();
        if (url.isValid())
            obj.insert(QStringLiteral("url"), url.toString());
    }

    // ---- organizer ---------------------------------------------------------
    {
        const auto org = event->organizer();
        if (!org.email().isEmpty() || !org.name().isEmpty()) {
            QJsonObject orgObj;
            if (!org.email().isEmpty()) orgObj.insert(QStringLiteral("email"), org.email());
            if (!org.name().isEmpty())  orgObj.insert(QStringLiteral("name"),  org.name());
            obj.insert(QStringLiteral("organizer"), orgObj);
        }
    }

    // ---- attendees ---------------------------------------------------------
    {
        const auto attendees = event->attendees();
        if (!attendees.isEmpty()) {
            QJsonArray arr;
            for (const auto& a : attendees) {
                if (a.email().isEmpty())
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("email"),   a.email());
                if (!a.name().isEmpty())
                    entry.insert(QStringLiteral("name"), a.name());
                entry.insert(QStringLiteral("role"),    roleToString(a.role()));
                entry.insert(QStringLiteral("partstat"), partStatToString(a.status()));
                entry.insert(QStringLiteral("rsvp"),    a.RSVP());
                arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("attendees"), arr);
        }
    }

    // ---- priority ----------------------------------------------------------
    {
        const int pri = event->priority();
        if (pri > 0)
            obj.insert(QStringLiteral("priority"), pri);
    }

    // ---- alarms (VALARM) ---------------------------------------------------
    {
        const auto alarms = event->alarms();
        if (!alarms.isEmpty()) {
            QJsonArray arr;
            for (const auto& alarm : alarms) {
                QJsonObject a;
                a.insert(QStringLiteral("type"),   int(alarm->type()));
                a.insert(QStringLiteral("offset"), alarm->startOffset().asSeconds());
                if (!alarm->text().isEmpty())
                    a.insert(QStringLiteral("text"), alarm->text());
                arr.append(a);
            }
            obj.insert(QStringLiteral("alarms"), arr);
        }
    }

    // ---- attachments -------------------------------------------------------
    {
        const auto attachments = event->attachments();
        if (!attachments.isEmpty()) {
            QJsonArray arr;
            for (const auto& att : attachments) {
                QJsonObject entry;
                if (att.isUri())
                    entry.insert(QStringLiteral("url"), att.uri());
                if (!att.mimeType().isEmpty())
                    entry.insert(QStringLiteral("mimeType"), att.mimeType());
                if (!entry.isEmpty())
                    arr.append(entry);
            }
            if (!arr.isEmpty())
                obj.insert(QStringLiteral("attachments"), arr);
        }
    }

    // ---- providerExtras["x-ical"] — unmapped X- custom properties ----------
    // IP.6: incidencecommonfields, parameterized by the skip-list of keys
    // already promoted above by name.
    {
        static const QSet<QByteArray> kSkip = {
            "X-ALT-DESC", "X-MICROSOFT-CDO-BUSYSTATUS",
        };
        const QJsonObject xical = promoteCustomPropertyPassthrough(event, kSkip);
        if (!xical.isEmpty()) {
            QJsonObject extras;
            extras.insert(QStringLiteral("x-ical"), xical);
            obj.insert(providerExtrasKey(), extras);
        }
    }

    return obj;
}

QByteArray canonObjectToEventBytes(const QJsonObject& obj)
{
    if (obj.isEmpty())
        return {};

    KCalendarCore::Event::Ptr event(new KCalendarCore::Event);

    // ---- uid ---------------------------------------------------------------
    {
        const QString uid = obj.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty())
            event->setUid(uid);
    }

    // ---- sequence ----------------------------------------------------------
    {
        const QJsonValue seq = obj.value(QStringLiteral("sequence"));
        if (!seq.isUndefined())
            event->setRevision(seq.toInt());
    }

    // ---- created / lastModified (IP.6: incidencecommonfields, O41 guard) ---
    // O41 write-side fix: KCalendarCore::Incidence always carries a valid
    // construction-time "now" for created()/lastModified() — there is no
    // API to leave them unset — so toICalString() below will stamp them
    // into the outbound bytes regardless of whether canon has these keys.
    // demoteTimestamps() tracks absence so the injected default can be
    // stripped post-serialization below.
    const auto timestampPresence = demoteTimestamps(obj, event);

    // ---- summary / description (IP.6: incidencecommonfields) ---------------
    demoteSummaryDescription(obj, event);

    // ---- descriptionHtml → X-ALT-DESC (Reversible) ------------------------
    {
        const QString html = obj.value(QStringLiteral("descriptionHtml")).toString();
        if (!html.isEmpty())
            event->setNonKDECustomProperty("X-ALT-DESC", html);
    }

    // ---- location ----------------------------------------------------------
    {
        const QString location = obj.value(QStringLiteral("location")).toString();
        if (!location.isEmpty())
            event->setLocation(location);

        // locations (multi) → Simplified: only first → LOCATION (already done above)
        // (if location is empty but locations has entries, use first entry)
        if (location.isEmpty()) {
            const QJsonArray locs = obj.value(QStringLiteral("locations")).toArray();
            if (!locs.isEmpty()) {
                const QString firstLoc = locs.at(0).toObject()
                    .value(QStringLiteral("displayName")).toString();
                if (!firstLoc.isEmpty())
                    event->setLocation(firstLoc);
            }
        }
    }

    // ---- status ------------------------------------------------------------
    {
        const QString statusStr = obj.value(QStringLiteral("status")).toString();
        if (!statusStr.isEmpty()) {
            const auto status = statusFromString(statusStr);
            if (status != KCalendarCore::Incidence::StatusNone)
                event->setStatus(status);
        }
    }

    // ---- classification ----------------------------------------------------
    {
        const QString cls = obj.value(QStringLiteral("classification")).toString();
        KCalendarCore::Incidence::Secrecy secrecy = KCalendarCore::Incidence::SecrecyPublic;
        if (cls == QStringLiteral("private")) {
            secrecy = KCalendarCore::Incidence::SecrecyPrivate;
        } else if (cls == QStringLiteral("confidential")) {
            secrecy = KCalendarCore::Incidence::SecrecyConfidential;
        } else if (cls == QStringLiteral("personal")) {
            // Degraded: MS "personal" has no iCal CLASS; map to PRIVATE but keep the
            // original verbatim (invariant 4) so it is recoverable — emit as an X-
            // property the forward stage round-trips into providerExtras["x-ical"].
            secrecy = KCalendarCore::Incidence::SecrecyPrivate;
            event->setNonKDECustomProperty("X-CANON-CLASSIFICATION", cls);
        }
        event->setSecrecy(secrecy);
    }

    // ---- timeTransparency --------------------------------------------------
    {
        const QString transp = obj.value(QStringLiteral("timeTransparency")).toString();
        if (transp == QStringLiteral("transparent"))
            event->setTransparency(KCalendarCore::Event::Transparent);
        else
            event->setTransparency(KCalendarCore::Event::Opaque);
    }

    // ---- freeBusyStatus → X-MICROSOFT-CDO-BUSYSTATUS (Reversible) ----------
    {
        const QString fbs = obj.value(QStringLiteral("freeBusyStatus")).toString();
        if (!fbs.isEmpty())
            event->setNonKDECustomProperty("X-MICROSOFT-CDO-BUSYSTATUS", fbs);
    }

    // ---- start / end -------------------------------------------------------
    {
        const QJsonObject startObj = obj.value(QStringLiteral("start")).toObject();
        const bool allDay = obj.value(QStringLiteral("allDay")).toBool();
        if (!startObj.isEmpty()) {
            const QDateTime dt = jsonToDateTime(startObj);
            if (dt.isValid()) {
                event->setDtStart(dt);
                event->setAllDay(allDay);
            }
        }
    }
    {
        const QJsonObject endObj = obj.value(QStringLiteral("end")).toObject();
        if (!endObj.isEmpty()) {
            const QDateTime dt = jsonToDateTime(endObj);
            if (dt.isValid())
                event->setDtEnd(dt);
        }
    }

    // ---- recurrence — re-inject verbatim RRULE/RDATE/EXDATE lines ----------
    // Store for post-serialization injection (same approach as vtodo stages).
    const QJsonArray recurrenceArr = obj.value(QStringLiteral("recurrence")).toArray();

    // ---- recurrenceId / recurrenceRange ------------------------------------
    {
        const QJsonObject recIdObj = obj.value(QStringLiteral("recurrenceId")).toObject();
        if (!recIdObj.isEmpty()) {
            const QString dtStr = recIdObj.value(QStringLiteral("dateTime")).toString();
            if (!dtStr.isEmpty()) {
                const QDateTime dt = QDateTime::fromString(dtStr, Qt::ISODate);
                if (dt.isValid()) {
                    const QString range = obj.value(QStringLiteral("recurrenceRange")).toString();
                    event->setRecurrenceId(dt);
                    event->setThisAndFuture(range == QStringLiteral("thisAndFuture"));
                }
            }
        }
    }

    // ---- color -------------------------------------------------------------
    {
        const QString color = obj.value(QStringLiteral("color")).toString();
        if (!color.isEmpty())
            event->setColor(color);
    }

    // ---- categories (IP.6: incidencecommonfields) --------------------------
    demoteCategories(obj, event);

    // ---- url ---------------------------------------------------------------
    {
        const QString url = obj.value(QStringLiteral("url")).toString();
        if (!url.isEmpty())
            event->setUrl(QUrl(url));
    }

    // ---- organizer ---------------------------------------------------------
    {
        const QJsonObject orgObj = obj.value(QStringLiteral("organizer")).toObject();
        if (!orgObj.isEmpty()) {
            const QString email = orgObj.value(QStringLiteral("email")).toString();
            const QString name  = orgObj.value(QStringLiteral("name")).toString();
            if (!email.isEmpty())
                event->setOrganizer(KCalendarCore::Person(name, email));
        }
    }

    // ---- attendees ---------------------------------------------------------
    {
        const QJsonArray attendees = obj.value(QStringLiteral("attendees")).toArray();
        for (const auto& av : attendees) {
            const QJsonObject a = av.toObject();
            const QString email = a.value(QStringLiteral("email")).toString();
            if (email.isEmpty())
                continue;
            const QString name    = a.value(QStringLiteral("name")).toString();
            const auto role       = roleFromString(a.value(QStringLiteral("role")).toString());
            const auto partstat   = partStatFromString(a.value(QStringLiteral("partstat")).toString());
            const bool rsvp       = a.value(QStringLiteral("rsvp")).toBool();
            KCalendarCore::Attendee att(name, email, rsvp, partstat, role);
            event->addAttendee(att);
        }
    }

    // ---- priority ----------------------------------------------------------
    {
        const QJsonValue pri = obj.value(QStringLiteral("priority"));
        if (!pri.isUndefined())
            event->setPriority(pri.toInt());
    }

    // ---- alarms (VALARM) ---------------------------------------------------
    {
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        for (const auto& av : alarms) {
            const QJsonObject a = av.toObject();
            KCalendarCore::Alarm::Ptr alarm(new KCalendarCore::Alarm(event.data()));
            const int typeInt = a.value(QStringLiteral("type")).toInt();
            alarm->setType(static_cast<KCalendarCore::Alarm::Type>(typeInt));
            const int offsetSecs = a.value(QStringLiteral("offset")).toInt();
            alarm->setStartOffset(KCalendarCore::Duration(offsetSecs));
            const QString text = a.value(QStringLiteral("text")).toString();
            if (!text.isEmpty())
                alarm->setText(text);
            event->addAlarm(alarm);
        }
    }

    // ---- attachments -------------------------------------------------------
    {
        const QJsonArray attachments = obj.value(QStringLiteral("attachments")).toArray();
        for (const auto& av : attachments) {
            const QJsonObject a = av.toObject();
            const QString url = a.value(QStringLiteral("url")).toString();
            if (!url.isEmpty()) {
                KCalendarCore::Attachment att;
                att.setUri(url);
                const QString mime = a.value(QStringLiteral("mimeType")).toString();
                if (!mime.isEmpty())
                    att.setMimeType(mime);
                event->addAttachment(att);
            }
        }
    }

    // ---- providerExtras["x-ical"] — re-emit custom/X- properties ----------
    // IP.6: incidencecommonfields.
    {
        const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        const QJsonObject xical  = extras.value(QStringLiteral("x-ical")).toObject();
        demoteCustomPropertyPassthrough(xical, event);
    }

    // ---- Serialize to iCal -------------------------------------------------
    QByteArray icalBytes = serializeEvent(event);

    // ---- Strip KCalendarCore-injected created/lastModified defaults -------
    // IP.6: incidencecommonfields.
    icalBytes = stripInjectedTimestamps(icalBytes, timestampPresence);

    // ---- Inject verbatim recurrence lines ----------------------------------
    if (!recurrenceArr.isEmpty() && !icalBytes.isEmpty()) {
        const QByteArray marker = "END:VEVENT";
        const int pos = icalBytes.indexOf(marker);
        if (pos >= 0) {
            QByteArray recBytes;
            for (const auto& rv : recurrenceArr) {
                recBytes += rv.toString().toUtf8();
                recBytes += '\n';
            }
            icalBytes.insert(pos, recBytes);
        }
    }

    return icalBytes;
}

QList<Kalburator::Shape::PropertyId> eventCanonContributedIds()
{
    using Kalburator::Shape::PropertyId;
    // Order mirrors eventFieldsToCanon's own field-by-field body above, for
    // a reader diffing the two. Envelope keys (_canon/uid/providerExtras)
    // are deliberately excluded — CanonEnvelope owns those.
    return {
        PropertyId{QStringLiteral("sequence")},
        PropertyId{QStringLiteral("created")},
        PropertyId{QStringLiteral("lastModified")},
        PropertyId{QStringLiteral("summary")},
        PropertyId{QStringLiteral("description")},
        PropertyId{QStringLiteral("descriptionHtml")},
        PropertyId{QStringLiteral("location")},
        PropertyId{QStringLiteral("status")},
        PropertyId{QStringLiteral("classification")},
        PropertyId{QStringLiteral("timeTransparency")},
        PropertyId{QStringLiteral("freeBusyStatus")},
        PropertyId{QStringLiteral("start")},
        PropertyId{QStringLiteral("allDay")},
        PropertyId{QStringLiteral("end")},
        PropertyId{QStringLiteral("recurrence")},
        PropertyId{QStringLiteral("recurrenceId")},
        PropertyId{QStringLiteral("recurrenceRange")},
        PropertyId{QStringLiteral("color")},
        PropertyId{QStringLiteral("categories")},
        PropertyId{QStringLiteral("url")},
        PropertyId{QStringLiteral("organizer")},
        PropertyId{QStringLiteral("attendees")},
        PropertyId{QStringLiteral("priority")},
        PropertyId{QStringLiteral("alarms")},
        PropertyId{QStringLiteral("attachments")},
    };
}

}  // namespace Kalburator::Calendar
