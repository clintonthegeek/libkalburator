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

}  // namespace Kalburator::Calendar
