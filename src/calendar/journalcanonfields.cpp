#include "journalcanonfields.h"

#include "canonenvelope.h"
#include "icalcomponentscan.h"
#include "incidencecommonfields.h"

#include <KCalendarCore/ICalFormat>

#include <QJsonArray>
#include <QTimeZone>

using Kalburator::Shape::CanonEnvelope::providerExtrasKey;

namespace {

QString journalStatusToString(KCalendarCore::Incidence::Status s)
{
    switch (s) {
    case KCalendarCore::Incidence::StatusDraft:      return QStringLiteral("draft");
    case KCalendarCore::Incidence::StatusFinal:      return QStringLiteral("final");
    case KCalendarCore::Incidence::StatusCanceled:   return QStringLiteral("cancelled");
    default:                                         return {};
    }
}

KCalendarCore::Incidence::Status journalStatusFromString(const QString &s)
{
    if (s == QStringLiteral("draft"))     return KCalendarCore::Incidence::StatusDraft;
    if (s == QStringLiteral("final"))     return KCalendarCore::Incidence::StatusFinal;
    if (s == QStringLiteral("cancelled")) return KCalendarCore::Incidence::StatusCanceled;
    return KCalendarCore::Incidence::StatusNone;
}

} // namespace

namespace Kalburator::Calendar {

QJsonObject journalFieldsToCanon(const KCalendarCore::Journal::Ptr& journal,
                                 const QByteArray& originalBytes)
{
    QJsonObject obj;
    // ---- created / lastModified (IP.6: incidencecommonfields, O41 guard) --
    // journalcanonfields.cpp used to carry its own copy of this guard (see
    // the IP.6 return receipt for the history of it lagging the other two
    // kinds' fix); now shared.
    promoteTimestamps(obj, originalBytes);
    if (journal->revision() > 0)
        obj.insert(QStringLiteral("sequence"), journal->revision());
    // ---- summary / description (IP.6: incidencecommonfields) --------------
    promoteSummaryDescription(obj, journal);
    if (journal->dtStart().isValid()) {
        QJsonObject start;
        const bool allDay = journal->allDay();
        if (allDay) {
            start.insert(QStringLiteral("date"),
                         journal->dtStart().date().toString(Qt::ISODate));
            start.insert(QStringLiteral("allDay"), true);
            obj.insert(QStringLiteral("allDay"), true);
        } else {
            start.insert(QStringLiteral("dateTime"),
                         journal->dtStart().toUTC().toString(Qt::ISODate));
            start.insert(QStringLiteral("floating"),
                         journal->dtStart().timeSpec() == Qt::LocalTime);
        }
        obj.insert(QStringLiteral("start"), start);
    }
    {
        const QString st = journalStatusToString(journal->status());
        if (!st.isEmpty())
            obj.insert(QStringLiteral("status"), st);
    }
    // ---- classification (IP.10: incidencecommonfields — closes the
    // phantom-key bug: journalFieldsToCanon() used to insert `classification`
    // unconditionally, defaulting to "public" even when no CLASS property
    // was present at all, a catalogue/emitter asymmetry of exactly the kind
    // this campaign exists to remove. promoteClassification() is guarded —
    // same shared function VEVENT/VTODO already use.) --------------------
    promoteClassification(obj, journal);
    if (!journal->color().isEmpty())
        obj.insert(QStringLiteral("color"), journal->color());
    if (journal->url().isValid())
        obj.insert(QStringLiteral("url"), journal->url().toString());
    // ---- categories (IP.6: incidencecommonfields) -------------------------
    promoteCategories(obj, journal);
    // ---- comments / contacts (IP.6 commit 2: O91 — judgment call, see the
    // return receipt: RFC 5545 §3.6.3's jourprop grammar permits both on
    // VJOURNAL, and the fix is the same one-line common-module call VEVENT
    // and VTODO use, so VJOURNAL is wired here too rather than left to
    // IP.10) -----------------------------------------------------------
    promoteComments(obj, journal);
    promoteContacts(obj, journal);

    // ---- recurrence (verbatim lines — invariant 3, IP.10 / O87) -----------
    // Mirrors eventcanonfields.cpp / vtodocanonfields.cpp exactly — the
    // verbatim-RFC5545-lines convention is shared across all three kinds via
    // the SAME canon key "recurrence" (covers RRULE/RDATE/EXDATE at once).
    {
        const QStringList recLines =
            extractComponentRecurrenceLines(originalBytes, "VJOURNAL", journal->uid());
        if (!recLines.isEmpty()) {
            QJsonArray arr;
            for (const auto& l : recLines)
                arr.append(l);
            obj.insert(QStringLiteral("recurrence"), arr);
        }
    }

    // ---- recurrenceId / recurrenceRange (IP.10 / O87 — the identity fix) --
    // Model: VTODO's W1 composite-exception-identity shape (vtodocanonfields.cpp),
    // NOT VEVENT's — VEVENT's demote still carries the O82 bug (unconditional
    // RANGE=THISANDFUTURE re-emission), IP.7's to fix, not copied here.
    // Promoting recurrenceId is what makes a detached VJOURNAL instance
    // distinguishable from its master in canon (O87's identity-corruption
    // finding: before this, both promoted to canon objects differing in no
    // identifying way).
    {
        const QDateTime recId = journal->recurrenceId();
        if (recId.isValid()) {
            QJsonObject recIdObj;
            recIdObj.insert(QStringLiteral("dateTime"), recId.toUTC().toString(Qt::ISODate));
            obj.insert(QStringLiteral("recurrenceId"), recIdObj);

            // RANGE=THISANDFUTURE → recurrenceRange (read-side fact only —
            // see the demote-side comment for why it is never re-emitted).
            if (journal->thisAndFuture())
                obj.insert(QStringLiteral("recurrenceRange"), QStringLiteral("thisAndFuture"));
        }
    }

    // ---- organizer / attendees / relatedTo (IP.10 — free via IP.6's
    // incidencecommonfields; see the IP.10 return receipt for verification
    // that this is genuinely wiring-only, no new field-specific logic) -----
    promoteOrganizer(obj, journal);
    promoteAttendees(obj, journal);
    promoteRelatedTo(obj, journal);

    // ---- descriptionHtml (X-ALT-DESC) — Reversible carrier (IP.10) --------
    // Same generic nonKDECustomProperty convention VEVENT/VTODO already use
    // (KOrganizer-family X-ALT-DESC, not RFC-standard, but not tied to any
    // one component kind either — VJOURNAL's DESCRIPTION is RFC 5545
    // jourprop-legal, so a rich-text journal entry from any client that
    // already emits X-ALT-DESC on other kinds could plausibly emit it here
    // too). See the IP.10 return receipt for the full justification.
    {
        const QString altDesc = journal->nonKDECustomProperty("X-ALT-DESC");
        if (!altDesc.isEmpty())
            obj.insert(QStringLiteral("descriptionHtml"), altDesc);
    }

    // ---- attachments (IP.10 — free via IP.6's incidencecommonfields) ------
    promoteAttachments(obj, journal);

    // providerExtras["x-ical"] — unmapped X- custom properties.
    // IP.6: incidencecommonfields. IP.10: X-ALT-DESC added to the skip list
    // (mirrors eventcanonfields.cpp) now that it is promoted by name above —
    // otherwise it would double-ride both descriptionHtml and this passthrough.
    {
        static const QSet<QByteArray> kSkip = { "X-ALT-DESC" };
        const QJsonObject xical = promoteCustomPropertyPassthrough(journal, kSkip);
        if (!xical.isEmpty()) {
            QJsonObject extras;
            extras.insert(QStringLiteral("x-ical"), xical);
            obj.insert(providerExtrasKey(), extras);
        }
    }
    return obj;
}

QByteArray canonObjectToJournalBytes(const QJsonObject& obj)
{
    if (obj.isEmpty())
        return {};
    KCalendarCore::Journal::Ptr journal(new KCalendarCore::Journal);
    const QString uid = obj.value(QStringLiteral("uid")).toString();
    if (!uid.isEmpty())
        journal->setUid(uid);
    // ---- created / lastModified (IP.6: incidencecommonfields, O41 guard) --
    const auto timestampPresence = demoteTimestamps(obj, journal);
    if (const QJsonValue seq = obj.value(QStringLiteral("sequence")); !seq.isUndefined())
        journal->setRevision(seq.toInt());
    // ---- summary / description (IP.6: incidencecommonfields) --------------
    demoteSummaryDescription(obj, journal);
    {
        const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
        const bool allDay = obj.value(QStringLiteral("allDay")).toBool();
        if (!start.isEmpty()) {
            if (start.contains(QStringLiteral("date"))) {
                const QDate dd = QDate::fromString(
                    start.value(QStringLiteral("date")).toString(), Qt::ISODate);
                if (dd.isValid()) {
                    journal->setDtStart(QDateTime(dd, QTime(0,0,0), QTimeZone::utc()));
                    journal->setAllDay(true);
                }
            } else {
                const QDateTime dt = QDateTime::fromString(
                    start.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
                if (dt.isValid()) {
                    journal->setDtStart(dt);
                    journal->setAllDay(allDay);
                }
            }
        }
    }
    if (const QString st = obj.value(QStringLiteral("status")).toString(); !st.isEmpty()) {
        const auto status = journalStatusFromString(st);
        if (status != KCalendarCore::Incidence::StatusNone)
            journal->setStatus(status);
    }
    // ---- classification (IP.10: incidencecommonfields) --------------------
    demoteClassification(obj, journal);
    if (const QString c = obj.value(QStringLiteral("color")).toString(); !c.isEmpty())
        journal->setColor(c);
    if (const QString u = obj.value(QStringLiteral("url")).toString(); !u.isEmpty())
        journal->setUrl(QUrl(u));
    // ---- categories (IP.6: incidencecommonfields) -------------------------
    demoteCategories(obj, journal);
    // ---- comments / contacts (IP.6 commit 2: O91) --------------------------
    demoteComments(obj, journal);
    demoteContacts(obj, journal);

    // ---- recurrence — re-inject verbatim RRULE/RDATE/EXDATE lines (IP.10) -
    // Store for post-serialization injection (same approach as
    // eventcanonfields.cpp / vtodocanonfields.cpp).
    const QJsonArray recurrenceArr = obj.value(QStringLiteral("recurrence")).toArray();

    // ---- recurrenceId / recurrenceRange (IP.10 / O87) ----------------------
    // Model: VTODO's W1 shape, including its W3 safety fix — RANGE=
    // THISANDFUTURE is NEVER re-emitted on write, unconditionally, regardless
    // of what canon's `recurrenceRange` carries (write-hostile on real
    // CalDAV servers; see vtodocanonfields.cpp's identical comment). VEVENT's
    // demote block still has the O82 bug (unconditional re-emission) —
    // deliberately NOT copied here; that is IP.7's fix, not this item's.
    {
        const QJsonObject recIdObj = obj.value(QStringLiteral("recurrenceId")).toObject();
        if (!recIdObj.isEmpty()) {
            const QString dtStr = recIdObj.value(QStringLiteral("dateTime")).toString();
            if (!dtStr.isEmpty()) {
                const QDateTime dt = QDateTime::fromString(dtStr, Qt::ISODate);
                if (dt.isValid()) {
                    journal->setRecurrenceId(dt);
                    journal->setThisAndFuture(false);
                }
            }
        }
    }

    // ---- organizer / attendees / relatedTo (IP.10) -------------------------
    demoteOrganizer(obj, journal);
    demoteAttendees(obj, journal);
    demoteRelatedTo(obj, journal);

    // ---- descriptionHtml → X-ALT-DESC (Reversible, IP.10) ------------------
    {
        const QString html = obj.value(QStringLiteral("descriptionHtml")).toString();
        if (!html.isEmpty())
            journal->setNonKDECustomProperty("X-ALT-DESC", html);
    }

    // ---- attachments (IP.10) ------------------------------------------------
    demoteAttachments(obj, journal);

    // IP.6: incidencecommonfields.
    {
        const QJsonObject extras = obj.value(providerExtrasKey()).toObject();
        const QJsonObject xical  = extras.value(QStringLiteral("x-ical")).toObject();
        demoteCustomPropertyPassthrough(xical, journal);
    }
    KCalendarCore::ICalFormat fmt;
    QByteArray icalBytes = fmt.toICalString(journal).toUtf8();

    // ---- Strip KCalendarCore-injected created/lastModified defaults -------
    // IP.6: incidencecommonfields.
    icalBytes = stripInjectedTimestamps(icalBytes, timestampPresence);

    // ---- Inject verbatim recurrence lines (IP.10) --------------------------
    if (!recurrenceArr.isEmpty() && !icalBytes.isEmpty()) {
        const QByteArray marker = "END:VJOURNAL";
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

Kalburator::Shape::LossProfile canonToVjournalLoss()
{
    // IP.9 / O88 — this was dead code (declared, zero call sites) carrying
    // a false "no loss" comment; the calendar domain's canon→ical edge ran
    // the event-shaped canonToIcalLoss() over every VJOURNAL instead. Now
    // wired via CalendarStockShapes::edges()'s lossByKind (see
    // calendarstockshapes.cpp).
    //
    // IP.10 / O87 CLOSED: `attachments`, `attendees`, `organizer`,
    // `relatedTo`, `recurrence` (RRULE/RDATE/EXDATE) and `recurrenceId` are
    // ALL REMOVED from this profile — journalFieldsToCanon()/
    // canonObjectToJournalBytes() now wire all six via IP.6's
    // incidencecommonfields.cpp (organizer/attendees/attachments/relatedTo,
    // genuinely free — no new field-specific logic needed) plus new
    // journal-specific recurrence/recurrenceId handling modeled on VTODO's
    // W1 shape. What remains, honestly:
    //
    // - `recurrenceRange`: Degraded, not Dropped — RANGE=THISANDFUTURE is
    //   captured on promote (a foreign producer's existing write, read-side
    //   only) but demote never re-emits it, by the same W3 safety rule
    //   VTODO uses (write-hostile on real CalDAV servers); the bare
    //   RECURRENCE-ID identity itself survives losslessly. Declared here
    //   even though the sibling `canonToVtodoIcalLoss()` doesn't yet
    //   declare the identical degradation on VTODO's own demote path — see
    //   FINDINGS O96 (logged, not this item's to fix).
    // - `requestStatus`: Dropped, permanently — upstream, no KCalendarCore
    //   accessor exists at all (O91).
    //
    // `relatedTo` belongs in the REMOVED list above, not here, because THIS
    // profile is scoped to the canon->vjournal DEMOTE direction (see the
    // function's own doc comment on the .h declaration) and demote/write
    // genuinely round-trips relatedTo losslessly (probe-verified). The
    // separate, real defect is upstream and on the OPPOSITE (promote/ical->
    // canon) direction — KCalendarCore::ICalFormat's VJOURNAL parser never
    // populates relatedTo() from a source RELATED-TO line even though
    // VEVENT/VTODO's parsers do — which this function structurally cannot
    // express (no promote-direction loss-profile mechanism exists in the
    // shape graph at all). See FINDINGS O95 and
    // tests/calendar/tst_incidence_rfc5545_fidelity.cpp's
    // vjournalExpectedLostList().
    //
    // `comments`/`contacts` were closed early by IP.6 commit 2 (judgment
    // call, see its receipt). RESOURCES is correctly absent from this
    // profile — RFC 5545 §3.6.3's jourprop grammar does not permit it on
    // VJOURNAL at all, so its absence is RFC-correct, not a drop.
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    p.affected.insert(PropertyId{QStringLiteral("recurrenceRange")}, LossKind::Degraded); // RANGE=THISANDFUTURE never re-emitted (W3-shaped safety rule)
    p.affected.insert(PropertyId{QStringLiteral("requestStatus")},   LossKind::Dropped);  // REQUEST-STATUS (upstream: no KCalendarCore accessor exists at all)

    return p;
}

QList<Kalburator::Shape::PropertyId> journalCanonContributedIds()
{
    using Kalburator::Shape::PropertyId;
    // Order mirrors journalFieldsToCanon's own field-by-field body above.
    // Envelope keys (_canon/uid/providerExtras) are deliberately excluded.
    // IP.6 commit 2: `comments`/`contacts` ADDED (O91 — judgment call, see
    // the return receipt). IP.10: `recurrence`/`recurrenceId`/
    // `recurrenceRange`/`organizer`/`attendees`/`descriptionHtml`/
    // `attachments`/`relatedTo` ADDED (O87).
    return {
        PropertyId{QStringLiteral("created")},
        PropertyId{QStringLiteral("lastModified")},
        PropertyId{QStringLiteral("sequence")},
        PropertyId{QStringLiteral("summary")},
        PropertyId{QStringLiteral("description")},
        PropertyId{QStringLiteral("start")},
        PropertyId{QStringLiteral("allDay")},
        PropertyId{QStringLiteral("status")},
        PropertyId{QStringLiteral("classification")},
        PropertyId{QStringLiteral("color")},
        PropertyId{QStringLiteral("url")},
        PropertyId{QStringLiteral("categories")},
        PropertyId{QStringLiteral("comments")},
        PropertyId{QStringLiteral("contacts")},
        PropertyId{QStringLiteral("recurrence")},
        PropertyId{QStringLiteral("recurrenceId")},
        PropertyId{QStringLiteral("recurrenceRange")},
        PropertyId{QStringLiteral("organizer")},
        PropertyId{QStringLiteral("attendees")},
        PropertyId{QStringLiteral("relatedTo")},
        PropertyId{QStringLiteral("descriptionHtml")},
        PropertyId{QStringLiteral("attachments")},
    };
}

}  // namespace Kalburator::Calendar
