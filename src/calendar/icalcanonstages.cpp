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
    // IP.9 / O88 — before this item, a VTODO demoted through this edge was
    // warned with canonToIcalLoss() above (entirely event-shaped: onlineMeeting,
    // guestsCan*, ...), none of which a VTODO ever carries, so the warning
    // mechanism was silently vacuous for VTODOs even though real properties
    // were (and, per O83/O91, still are — this declares, it does not fix)
    // being dropped. Populated with TODAY's actual drops, cross-checked
    // against Kalburator::Todo::vtodoCanonContributedIds()
    // (vtodocanonfields.cpp never touches these accessors on ANY kind's
    // demote path — same emitter as {todo,canon}, see O78/O83).
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;

    // Dropped (O83): the seven properties vtodocanonfields.cpp never reads
    // on ANY kind's demote path, despite all seven being catalogued canon
    // properties (contributed by eventcanonfields.cpp and/or
    // journalcanonfields.cpp for the other two kinds).
    p.affected.insert(PropertyId{QStringLiteral("attachments")},   LossKind::Dropped); // ATTACH
    p.affected.insert(PropertyId{QStringLiteral("attendees")},     LossKind::Dropped); // ATTENDEE
    p.affected.insert(PropertyId{QStringLiteral("classification")}, LossKind::Dropped); // CLASS — total drop for
                                                                                          // VTODO, unlike VEVENT's
                                                                                          // value-dependent Degraded above.
    p.affected.insert(PropertyId{QStringLiteral("color")},         LossKind::Dropped); // COLOR
    p.affected.insert(PropertyId{QStringLiteral("organizer")},     LossKind::Dropped); // ORGANIZER
    p.affected.insert(PropertyId{QStringLiteral("sequence")},      LossKind::Dropped); // SEQUENCE
    p.affected.insert(PropertyId{QStringLiteral("url")},           LossKind::Dropped); // URL

    // Dropped (O91 — new, filed by IP.8, declared by IP.9): no canon
    // PropertyId exists for these — no emitter of any kind ever produces
    // them, so none reached the contributed-id union. Declared anyway;
    // LossProfile.affected does not require its keys to be catalogued
    // (see IP.9 return receipt).
    p.affected.insert(PropertyId{QStringLiteral("comments")},      LossKind::Dropped); // COMMENT
    p.affected.insert(PropertyId{QStringLiteral("contacts")},      LossKind::Dropped); // CONTACT
    p.affected.insert(PropertyId{QStringLiteral("resources")},     LossKind::Dropped); // RESOURCES
    p.affected.insert(PropertyId{QStringLiteral("requestStatus")}, LossKind::Dropped); // REQUEST-STATUS (upstream)

    // Degraded (O86): GEO's NAME survives the round trip (vtodocanonfields.cpp
    // DOES promote/demote it — see :443-447/:793-798) but its VALUE is
    // corrupted by kcalendarcore's GEO serializer, so promote→demote→promote
    // is not a fixpoint. None of the four LossKind values describes "name
    // survives, value corrupted" precisely: Dropped is wrong (the property
    // is not absent), Simplified/Reversible both imply the original is
    // recoverable (it is not — the corruption happens on the library's own
    // round trip, there is nothing to reverse to). Degraded — "mapped
    // through a lossy path; original kept verbatim" — is the closest
    // available fit, even though the mechanism is a serializer bug rather
    // than a many-to-one vocabulary mapping; see the IP.9 return receipt.
    // NOT in IP.8's expectedLossTable()["vtodo"] (property-NAME loss list)
    // by design — its damage shows up there as the fixpoint failure
    // instead. IP.6 owns the actual fix (O86: drop geo entirely, per
    // PlanStan's ratified answer).
    p.affected.insert(PropertyId{QStringLiteral("geo")}, LossKind::Degraded);

    return p;
}

}  // namespace Kalburator::Calendar
