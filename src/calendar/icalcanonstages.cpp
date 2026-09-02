#include "icalcanonstages.h"

#include "canonenvelope.h"
#include "eventcanonfields.h"
#include "journalcanonfields.h"
#include "vtodocanonfields.h"

#include <KCalendarCore/Attendee>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/Journal>
#include <KCalendarCore/Todo>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

namespace {

using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;

KCalendarCore::Incidence::Ptr parseIncidence(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    return fmt.fromString(QString::fromUtf8(data));
}

} // namespace

namespace Kalburator::Calendar {

// ---------------------------------------------------------------------------
// ICalToCanonStage — iCal bytes → canon JSON (kind-dispatched)
// ---------------------------------------------------------------------------

QByteArray ICalToCanonStage::transform(const QByteArray& icalBytes) const
{
    if (icalBytes.isEmpty())
        return {};
    const auto inc = parseIncidence(icalBytes);
    if (!inc)
        return {};

    QJsonObject obj;
    QString kind;
    if (auto ev = inc.dynamicCast<KCalendarCore::Event>()) {
        obj  = eventFieldsToCanon(ev, icalBytes);
        kind = QStringLiteral("vevent");
    } else if (auto td = inc.dynamicCast<KCalendarCore::Todo>()) {
        obj  = Kalburator::Todo::todoFieldsToCanon(td, icalBytes);
        kind = QStringLiteral("vtodo");
    } else if (auto jr = inc.dynamicCast<KCalendarCore::Journal>()) {
        obj  = journalFieldsToCanon(jr, icalBytes);
        kind = QStringLiteral("vjournal");
    } else {
        return {};   // unknown component kind — guarded loudly by the engine.
    }
    // vevent kind is the default; omit it so existing v1 baselines stay byte-stable.
    stampEnvelope(obj, QStringLiteral("calendar"), inc->uid(),
                  kind == QStringLiteral("vevent") ? QString() : kind);
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// CanonToICalStage — canon JSON → iCal bytes (kind-dispatched)
// ---------------------------------------------------------------------------

QByteArray CanonToICalStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    const QJsonObject obj = parse(canonBytes);
    if (obj.isEmpty())
        return {};
    const QString kind = Kalburator::Shape::CanonEnvelope::kind(obj);
    if (kind == QStringLiteral("vtodo"))
        return Kalburator::Todo::canonObjectToVtodoBytes(obj);
    if (kind == QStringLiteral("vjournal"))
        return canonObjectToJournalBytes(obj);
    if (kind.isEmpty() || kind == QStringLiteral("vevent"))
        return canonObjectToEventBytes(obj);   // absent kind ⇒ vevent (back-compat)
    return {};   // unknown kind — guarded loudly by the engine.
}

// ---------------------------------------------------------------------------
// canonToIcalLoss — LossProfile for the canon → ical demote direction
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToIcalLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Dropped: no iCal representation at all
    p.affected.insert(PropertyId{QStringLiteral("onlineMeeting")},   LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("eventType")},       LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("typedProperties")}, LossKind::Dropped);

    // Dropped (IP.6 / O86, ratified Amendment 2 §B.5): GEO is corrupt at the
    // KCalendarCore layer (upstream) — VEVENT never promoted it before this
    // item and still does not; declared here now instead of silently
    // undeclared (was O86's VEVENT half; the VTODO half already promoted
    // and demoted the corrupt value — that leg is fixed by removing GEO
    // from vtodocanonfields.cpp entirely, see canonToVtodoIcalLoss() below).
    p.affected.insert(PropertyId{QStringLiteral("geo")}, LossKind::Dropped);

    // Dropped (IP.6 / O91, upstream): RFC 5545 permits REQUEST-STATUS on
    // VEVENT but KCalendarCore exposes no accessor for it anywhere in its
    // public API — no emitter can ever promote what the toolkit does not
    // expose. Permanent, not fixable in an emitter.
    p.affected.insert(PropertyId{QStringLiteral("requestStatus")}, LossKind::Dropped);

    // Dropped (IP.6 / O94, new, upstream): RESOURCES is correct on the
    // KCalendarCore OBJECT MODEL (resources()/setResources() work exactly
    // as documented, verified directly) but ICalFormat 6.29.0 never reads
    // OR writes a RESOURCES line on the wire at all — this contradicts
    // O91's claim that resources() "round-trips fine through KCalendarCore's
    // own ICalFormat" (verified wrong for RESOURCES specifically; COMMENT/
    // CONTACT DO round-trip correctly through the same ICalFormat call).
    // promoteResources()/demoteResources() are kept (correct against the
    // object model, forward-compatible with a future kcalendarcore fix);
    // this profile declares today's actual wire behaviour honestly.
    p.affected.insert(PropertyId{QStringLiteral("resources")}, LossKind::Dropped);

    // Simplified: multi-location → first entry as LOCATION string
    p.affected.insert(PropertyId{QStringLiteral("locations")},       LossKind::Simplified);

    // Reversible: vendor-specific fields stashed in providerExtras or X- props
    p.affected.insert(PropertyId{QStringLiteral("descriptionHtml")},          LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("freeBusyStatus")},           LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("guestsCanModify")},          LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("guestsCanInviteOthers")},    LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("guestsCanSeeOtherGuests")},  LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("allowNewTimeProposals")},    LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("hideAttendees")},            LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("locked")},                   LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("privateCopy")},              LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("responseRequested")},        LossKind::Reversible);

    // Degraded: MS "personal" classification → iCal "PRIVATE" with original kept.
    // Only "personal" actually degrades; public/private/confidential map to the
    // exact iCal CLASS value, so the warning path must not flag those.
    p.affected.insert(PropertyId{QStringLiteral("classification")},   LossKind::Degraded);
    p.losslessValues.insert(PropertyId{QStringLiteral("classification")},
                            {QStringLiteral("public"), QStringLiteral("private"),
                             QStringLiteral("confidential")});

    return p;
}

// ---------------------------------------------------------------------------
// canonToVtodoIcalLoss — LossProfile for canon → ical when kind == "vtodo"
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToVtodoIcalLoss()
{
    // IP.9 / O88 — before IP.6, a VTODO demoted through this edge was
    // warned with canonToIcalLoss() above (entirely event-shaped: onlineMeeting,
    // guestsCan*, ...), none of which a VTODO ever carries, so the warning
    // mechanism was silently vacuous for VTODOs even though real properties
    // were being dropped (O83/O91). IP.6 commit 2 FIXED the underlying
    // emitter (vtodocanonfields.cpp now calls incidencecommonfields.cpp's
    // shared promote/demote for all seven O83 properties plus O91's
    // comments/contacts/resources), so this profile now declares only what
    // is genuinely, permanently lost: `geo` (dropped entirely, O86) and
    // `requestStatus` (upstream, no KCalendarCore accessor exists at all).
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Dropped (O86, ratified Amendment 2 §B.5): GEO is corrupt at the
    // KCalendarCore layer (upstream — swapped latitude/longitude,
    // uninitialized-memory bytes in the longitude slot). Before this item
    // vtodocanonfields.cpp promoted/demoted the corrupt value (a
    // fixpoint-breaking Degraded, not a Dropped, per the old comment here);
    // IP.6 commit 2 removed GEO promote/demote entirely, so it is now a
    // clean Dropped — the NAME no longer round-trips at all, and
    // promote→demote→promote is a fixpoint again (VTODO's O86 fixpoint
    // failure is resolved as a byproduct).
    p.affected.insert(PropertyId{QStringLiteral("geo")}, LossKind::Dropped);

    // Dropped (O91, upstream): RFC 5545 permits REQUEST-STATUS on VTODO but
    // KCalendarCore exposes no accessor for it anywhere in its public API —
    // no emitter can ever promote what the toolkit does not expose.
    // Permanent, not fixable in an emitter.
    p.affected.insert(PropertyId{QStringLiteral("requestStatus")}, LossKind::Dropped);

    // Dropped (O94, new, upstream): same ICalFormat wire gap as
    // canonToIcalLoss() above — see its comment for the full explanation.
    p.affected.insert(PropertyId{QStringLiteral("resources")}, LossKind::Dropped);

    return p;
}

}  // namespace Kalburator::Calendar
