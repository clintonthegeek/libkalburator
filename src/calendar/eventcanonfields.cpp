#include "eventcanonfields.h"

#include "alarmshape.h"
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
using Kalburator::Shape::CanonEnvelope::stampProviderExtrasDigest;

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

// IP.6 commit 2: attendee role/partstat string<->enum helpers relocated to
// incidencecommonfields.cpp (promoteAttendees/demoteAttendees now live
// there, shared with VTODO — see O83).

} // namespace

namespace Kalburator::Calendar {

QJsonObject eventFieldsToCanon(const KCalendarCore::Event::Ptr& event,
                               const QByteArray& originalBytes)
{
    QJsonObject obj;

    // ---- sequence (IP.6 commit 2: incidencecommonfields) --------------------
    promoteSequence(obj, event);

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

    // ---- classification (IP.6 commit 2: incidencecommonfields) --------------
    promoteClassification(obj, event);

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

    // ---- color / url (IP.6 commit 2: incidencecommonfields) -----------------
    promoteColor(obj, event);

    // ---- categories (IP.6: incidencecommonfields) --------------------------
    promoteCategories(obj, event);

    promoteUrl(obj, event);

    // ---- organizer / attendees / attachments (IP.6 commit 2: O83) ----------
    promoteOrganizer(obj, event);
    promoteAttendees(obj, event);

    // ---- relatedTo (IP.6 commit 2: Amendment 1 §A.3.2) ----------------------
    promoteRelatedTo(obj, event);

    // ---- comments / contacts / resources (IP.6 commit 2: O91) ---------------
    promoteComments(obj, event);
    promoteContacts(obj, event);
    promoteResources(obj, event);

    // ---- priority ----------------------------------------------------------
    {
        const int pri = event->priority();
        if (pri > 0)
            obj.insert(QStringLiteral("priority"), pri);
    }

    // ---- alarms (VALARM, IP.4: shared alarmshape module) -------------------
    // Was: unconditional startOffset() (O79 — corrupted absolute-trigger and
    // END-related alarms to a bogus "offset: 0"). Now routes through the
    // same alarmToJson() vtodocanonfields.cpp uses, which branches on the
    // alarm's actual trigger form.
    {
        const auto alarms = event->alarms();
        if (!alarms.isEmpty()) {
            QJsonArray arr;
            for (const auto& alarm : alarms)
                arr.append(alarmToJson(alarm));
            obj.insert(QStringLiteral("alarms"), arr);
        }
    }

    // ---- attachments (IP.6 commit 2: incidencecommonfields, O83) -----------
    promoteAttachments(obj, event);

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

            // ---- providerExtrasDigest (IP.5/O80) --------------------------
            // No filtering needed on this leg: the CalDAV x-ical passthrough
            // is genuine X- custom properties only — no vendor bookkeeping
            // (etag-equivalents, server timestamps) rides this channel the
            // way it does on the MS/Google legs. Matches the reasoning
            // vtodocanonfields.cpp already used for its own CalDAV leg.
            stampProviderExtrasDigest(obj, xical);
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

    // ---- sequence (IP.6 commit 2: incidencecommonfields) --------------------
    demoteSequence(obj, event);

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
    demoteClassification(obj, event);

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

    // ---- color / url (IP.6 commit 2: incidencecommonfields) -----------------
    demoteColor(obj, event);

    // ---- categories (IP.6: incidencecommonfields) --------------------------
    demoteCategories(obj, event);

    demoteUrl(obj, event);

    // ---- organizer / attendees (IP.6 commit 2: O83) -------------------------
    demoteOrganizer(obj, event);
    demoteAttendees(obj, event);

    // ---- relatedTo (IP.6 commit 2: Amendment 1 §A.3.2) -----------------------
    demoteRelatedTo(obj, event);

    // ---- comments / contacts / resources (IP.6 commit 2: O91) ---------------
    demoteComments(obj, event);
    demoteContacts(obj, event);
    demoteResources(obj, event);

    // ---- priority ----------------------------------------------------------
    {
        const QJsonValue pri = obj.value(QStringLiteral("priority"));
        if (!pri.isUndefined())
            event->setPriority(pri.toInt());
    }

    // ---- alarms (VALARM, IP.4: shared alarmshape module) -------------------
    // Was: unconditional setStartOffset() (O79 — ignored "at"/"related"/
    // "repeatCount", so an "at"-carrying row demoted to an alarm with no
    // trigger at all). Now routes through alarmFromJson(), which honours
    // every row form and (O85) always enables the resulting alarm.
    {
        const QJsonArray alarms = obj.value(QStringLiteral("alarms")).toArray();
        for (const auto& av : alarms)
            event->addAlarm(alarmFromJson(av.toObject(), event.data()));
    }

    // ---- attachments (IP.6 commit 2: incidencecommonfields, O83) -----------
    demoteAttachments(obj, event);

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
        // IP.6 commit 2 additions:
        PropertyId{QStringLiteral("relatedTo")},     // Amendment 1 §A.3.2
        PropertyId{QStringLiteral("comments")},      // O91
        PropertyId{QStringLiteral("contacts")},      // O91
        PropertyId{QStringLiteral("resources")},     // O91
        // IP.5 addition: VEVENT's x-ical passthrough now stamps
        // providerExtrasDigest too (O80) — was previously contributed to
        // this catalogue only via the shared VTODO emitter
        // (vtodoCanonContributedIds()), which happened to cover it
        // implicitly for the calendar domain's union; VEVENT now honestly
        // declares its own production of the key.
        PropertyId{QStringLiteral("providerExtrasDigest")},
    };
}

}  // namespace Kalburator::Calendar
