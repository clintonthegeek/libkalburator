#include "icalcanonstages.h"

#include "canonenvelope.h"
#include "eventcanonfields.h"

#include <KCalendarCore/Attendee>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

namespace {

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

} // namespace

namespace Kalburator::Calendar {

// ---------------------------------------------------------------------------
// ICalToCanonStage — VEVENT iCal bytes → canon JSON (lossless)
// ---------------------------------------------------------------------------

QByteArray ICalToCanonStage::transform(const QByteArray& icalBytes) const
{
    if (icalBytes.isEmpty())
        return {};
    const auto event = parseEvent(icalBytes);
    if (!event)
        return {};
    QJsonObject obj = eventFieldsToCanon(event, icalBytes);
    stampEnvelope(obj, QStringLiteral("calendar"), event->uid());
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// CanonToICalStage — canon JSON → VEVENT iCal bytes (lossy)
// ---------------------------------------------------------------------------

QByteArray CanonToICalStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    return canonObjectToEventBytes(parse(canonBytes));
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
