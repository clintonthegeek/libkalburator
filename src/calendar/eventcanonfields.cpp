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

    // ---- start / end (IP.7b — malformed DTSTART/DTEND coercion, O81) -------
    // Probe-confirmed (2026-09-02, KCalendarCore::Event, mirrors the
    // vtodocanonfields.cpp W6.2 probe result for Todo): dtStart()/dtEnd()
    // come back as two independently-typed QDateTimes after a malformed
    // source round-trips through ICalFormat's parser, each individually
    // detectable as date-only via the same heuristic
    // (dt.time()==QTime(0,0) && dt.timeSpec()==Qt::LocalTime).
    // event->allDay() reflects only ONE side's date-only-ness (empirically,
    // DTEND's — see the probe transcript in the IP.7 return receipt), not a
    // fused view of both, so it is NOT used for mismatch detection here —
    // same mechanism as VTODO's, applied with OPPOSITE polarity: Amendment 2
    // §B.2 (ratified by PlanStan 2026-09-02) makes DTSTART — the mandatory
    // temporal anchor — win, coercing the optional, derived DTEND to match,
    // rather than the reverse.
    {
        QDateTime start = event->dtStart();
        QDateTime end   = event->dtEnd();

        const auto isDateOnly = [](const QDateTime &dt) {
            return dt.time() == QTime(0, 0) && dt.timeSpec() == Qt::LocalTime;
        };

        // KCalendarCore quirk (probe-confirmed, IP.7 return receipt §3):
        // for an all-day DTEND (wire VALUE=DATE), Event::dtEnd() does NOT
        // return the literal wire date — RFC 5545's DTEND is EXCLUSIVE for
        // a DATE range, and the getter returns the INCLUSIVE last day
        // (wire date minus one), while setDtEnd()+serialize apply the
        // exact inverse (+1 day) on write. This getter/setter pair is
        // symmetric and transparent as long as a date-only value is
        // always passed through unmodified between the two — canon's
        // "end" date field therefore stores THIS inclusive/getter-space
        // value (matching how a native, uncoerced all-day pair has always
        // round-tripped through this code), and the two `addDays()` calls
        // below exist ONLY to keep a coerced value in that same space when
        // this item's coercion crosses the DATE/DATE-TIME boundary.
        const bool startDateOnly = start.isValid() && isDateOnly(start);
        const bool endDateOnlyOriginally = end.isValid() && isDateOnly(end);

        // Rule 1: coerce DTEND to DTSTART's value type. Never the reverse.
        if (start.isValid() && end.isValid()) {
            if (startDateOnly != endDateOnlyOriginally) {
                if (startDateOnly) {
                    // DTSTART DATE + DTEND DATE-TIME ⇒ take DTEND's date
                    // part. The raw dtEnd() here is NOT getter-adjusted
                    // (the original wire DTEND was DATE-TIME, never
                    // subject to the all-day inclusive/exclusive
                    // convention), so end.date() IS the true wire date the
                    // author wrote. Store it one day EARLY (getter/canon
                    // space) so demote's automatic +1-day re-serialization
                    // reproduces that same true date on the wire.
                    end = QDateTime(end.date().addDays(-1), QTime(0, 0), Qt::LocalTime);
                } else {
                    // DTSTART DATE-TIME + DTEND DATE ⇒ DTEND at 00:00 in
                    // DTSTART's timezone (house rule O60: construct the wall
                    // time directly IN the target zone, never build
                    // elsewhere and convert). The raw dtEnd() here IS
                    // getter-adjusted (original wire DTEND was DATE, one
                    // day less than the true wire value per the quirk
                    // above) — add the day back first to reconstruct the
                    // true calendar date before constructing the 00:00
                    // moment on it. Branches explicitly on DTSTART's own
                    // timeSpec rather than calling start.timeZone()
                    // unconditionally the way vtodocanonfields.cpp's rule
                    // (a) does for its DUE-wins case: QDateTime::timeZone()
                    // on a floating (Qt::LocalTime) datetime returns the
                    // SYSTEM timezone, not an invalid/floating marker
                    // (probe-confirmed), so a blind call would silently
                    // anchor a floating DTSTART to whichever machine runs
                    // the code — filed as FINDINGS O98 (VTODO's rule (a)
                    // shares this latent bug; out of this item's scope to
                    // fix there).
                    const QDate trueEndDate = end.date().addDays(1);
                    if (start.timeSpec() == Qt::TimeZone)
                        end = QDateTime(trueEndDate, QTime(0, 0), start.timeZone());
                    else
                        end = QDateTime(trueEndDate, QTime(0, 0), Qt::LocalTime);
                }
            }
        }

        // Rule 2: coerced DTEND <= DTSTART ⇒ drop DTEND entirely and let
        // RFC 5545 §3.6.1's default stand (Amendment 2 §B.2: a non-
        // conforming pair has no valid value to clamp to, and the absent-
        // DTEND default is already defined — dropping falls back to a
        // defined behaviour, clamping would invent a bound the author
        // never wrote).
        //
        // For a DATE-only end (native, or Rule 1's bullet-1 coercion, both
        // now consistently in getter/canon space per the comment above),
        // `end.date() < start.date()` is the correctly-shifted equivalent
        // of "true wire DTEND <= DTSTART" — algebraically, (trueEnd - 1) <
        // start  ⇔  trueEnd <= start (see the IP.7 return receipt for the
        // derivation). Critically this means an EQUAL date-only pair
        // (end.date() == start.date()) is NOT dropped: that is the
        // getter-space representation of a perfectly valid one-day all-day
        // event, confirmed by direct probe of setDtEnd()+serialize.
        // For a DATE-TIME end (both sides timed, no coercion), no
        // inclusive/exclusive concept applies — compare directly.
        bool dropEnd = false;
        if (start.isValid() && end.isValid()) {
            if (isDateOnly(end))
                dropEnd = end.date() < start.date();
            else
                dropEnd = end <= start;
        }

        // Rule 3: DURATION present instead of DTEND ⇒ nothing to coerce,
        // leave it. Probe-confirmed (2026-09-02): unlike Todo (no DURATION
        // accessor at all), KCalendarCore::Event exposes hasDuration(), and
        // a DURATION-derived dtEnd() is already type-consistent with
        // dtStart() by construction (DTSTART DATE + DURATION in
        // day/week units ⇒ dtEnd() date-only; DTSTART DATE-TIME + DURATION
        // in any unit ⇒ dtEnd() timed) — so this rule needs no dedicated
        // code, the same zero-code-no-op shape as VTODO's rule (c).

        if (start.isValid()) {
            const QJsonObject startObj = dateTimeToJson(start, startDateOnly);
            if (!startObj.isEmpty()) {
                obj.insert(QStringLiteral("start"),  startObj);
                obj.insert(QStringLiteral("allDay"), startDateOnly);
            }
        }
        if (end.isValid() && !dropEnd) {
            const bool endAllDay = isDateOnly(end);
            const QJsonObject endObj = dateTimeToJson(end, endAllDay);
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

    // ---- recurrenceId / recurrenceRange (IP.7a — O82) -----------------------
    // Mirrors vtodocanonfields.cpp's W3 safety rule (VP.e), applied to the
    // event side: RANGE=THISANDFUTURE is NEVER re-emitted on write,
    // unconditionally, regardless of what canon's `recurrenceRange` carries.
    // Re-emitting RANGE=THISANDFUTURE is write-hostile on real CalDAV
    // servers; `recurrenceRange` in canon is therefore purely a READ-SIDE
    // fact (an already-existing foreign producer's write, captured
    // losslessly by promote above), a hard safety backstop independent of
    // any split mechanism. The bare exception identity (RECURRENCE-ID with
    // no RANGE) is unaffected and still fully Reversible.
    {
        const QJsonObject recIdObj = obj.value(QStringLiteral("recurrenceId")).toObject();
        if (!recIdObj.isEmpty()) {
            const QString dtStr = recIdObj.value(QStringLiteral("dateTime")).toString();
            if (!dtStr.isEmpty()) {
                const QDateTime dt = QDateTime::fromString(dtStr, Qt::ISODate);
                if (dt.isValid()) {
                    event->setRecurrenceId(dt);
                    event->setThisAndFuture(false);
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

    // ---- Strip KCalendarCore-injected heap-derived ATTENDEE X-UID (O90) ---
    // IP.12: incidencecommonfields.
    icalBytes = stripAttendeeXUid(icalBytes);

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
