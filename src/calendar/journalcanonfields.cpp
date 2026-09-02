#include "journalcanonfields.h"

#include "canonenvelope.h"
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

QString classToString(KCalendarCore::Incidence::Secrecy cls)
{
    switch (cls) {
    case KCalendarCore::Incidence::SecrecyPrivate:      return QStringLiteral("private");
    case KCalendarCore::Incidence::SecrecyConfidential: return QStringLiteral("confidential");
    default:                                            return QStringLiteral("public");
    }
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
    obj.insert(QStringLiteral("classification"), classToString(journal->secrecy()));
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
    // providerExtras["x-ical"] — unmapped X- custom properties.
    // IP.6: incidencecommonfields (no skip list on this leg).
    {
        const QJsonObject xical = promoteCustomPropertyPassthrough(journal);
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
    {
        const QString cls = obj.value(QStringLiteral("classification")).toString();
        if (cls == QStringLiteral("private"))
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);
        else if (cls == QStringLiteral("confidential"))
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyConfidential);
        else
            journal->setSecrecy(KCalendarCore::Incidence::SecrecyPublic);
    }
    if (const QString c = obj.value(QStringLiteral("color")).toString(); !c.isEmpty())
        journal->setColor(c);
    if (const QString u = obj.value(QStringLiteral("url")).toString(); !u.isEmpty())
        journal->setUrl(QUrl(u));
    // ---- categories (IP.6: incidencecommonfields) -------------------------
    demoteCategories(obj, journal);
    // ---- comments / contacts (IP.6 commit 2: O91) --------------------------
    demoteComments(obj, journal);
    demoteContacts(obj, journal);
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

    return icalBytes;
}

Kalburator::Shape::LossProfile canonToVjournalLoss()
{
    // IP.9 / O88 — this was dead code (declared, zero call sites) carrying
    // a false "no loss" comment; the calendar domain's canon→ical edge ran
    // the event-shaped canonToIcalLoss() over every VJOURNAL instead. Now
    // wired via CalendarStockShapes::edges()'s lossByKind (see
    // calendarstockshapes.cpp) and populated honestly with TODAY's actual
    // drops — O87/O91, cross-checked against journalCanonContributedIds()
    // above (journalFieldsToCanon() never touches attachments, attendees,
    // organizer, relatedTo, recurrence, recurrenceId at all). This declares
    // the loss; it does NOT fix it — that is IP.10's job (O87).
    //
    // IP.6 commit 2: `comments`/`contacts` REMOVED from this profile —
    // journalFieldsToCanon() now calls promoteComments()/promoteContacts()
    // (O91's judgment call: RFC 5545 permits both on VJOURNAL and the fix
    // was a one-line common-module call, so it landed here instead of
    // waiting for IP.10). `requestStatus` stays declared Dropped —
    // permanently unfixable, no KCalendarCore accessor exists at all.
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Dropped: no representation at all — journalFieldsToCanon() has zero
    // references to any of these KCalendarCore accessors (grep-confirmed).
    p.affected.insert(PropertyId{QStringLiteral("attachments")}, LossKind::Dropped);   // ATTACH
    p.affected.insert(PropertyId{QStringLiteral("attendees")},   LossKind::Dropped);   // ATTENDEE
    p.affected.insert(PropertyId{QStringLiteral("organizer")},   LossKind::Dropped);   // ORGANIZER
    p.affected.insert(PropertyId{QStringLiteral("relatedTo")},   LossKind::Dropped);   // RELATED-TO
    // recurrenceId: O87's identity-corruption finding — a detached VJOURNAL
    // instance and its master are indistinguishable in canon (they collapse
    // onto one uid). LossKind has no "identity corruption" verdict; Dropped
    // is the closest honest fit (the property is, in fact, entirely absent
    // from the demoted output) — the identity-corruption severity itself is
    // recorded in FINDINGS O87, not expressible here.
    p.affected.insert(PropertyId{QStringLiteral("recurrenceId")}, LossKind::Dropped);  // RECURRENCE-ID
    // recurrence: journalFieldsToCanon() has NO recurrence handling of any
    // kind (zero RRULE/RDATE/EXDATE support), so this single canon
    // PropertyId — the verbatim-RFC5545-lines carrier shared with VEVENT/
    // VTODO (invariant 3) — covers all three RFC properties at once.
    p.affected.insert(PropertyId{QStringLiteral("recurrence")}, LossKind::Dropped);    // RRULE, RDATE, EXDATE

    // Dropped, permanently — O91, upstream: RFC 5545 jourprop permits
    // REQUEST-STATUS on VJOURNAL but KCalendarCore exposes no accessor for
    // it anywhere in its public API, so no emitter can ever promote it.
    // RESOURCES is correctly absent from this profile — RFC 5545 jourprop
    // does not permit it on VJOURNAL at all, so its absence is RFC-correct,
    // not a drop. `comments`/`contacts` — fixed, see the comment above;
    // removed from here.
    p.affected.insert(PropertyId{QStringLiteral("requestStatus")}, LossKind::Dropped); // REQUEST-STATUS (upstream: no KCalendarCore accessor exists at all)

    return p;
}

QList<Kalburator::Shape::PropertyId> journalCanonContributedIds()
{
    using Kalburator::Shape::PropertyId;
    // Order mirrors journalFieldsToCanon's own field-by-field body above.
    // Envelope keys (_canon/uid/providerExtras) are deliberately excluded.
    // IP.6 commit 2: `comments`/`contacts` ADDED (O91 — judgment call, see
    // the return receipt).
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
    };
}

}  // namespace Kalburator::Calendar
