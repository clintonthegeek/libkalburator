#include "vtodocanonstages.h"

#include "canonenvelope.h"
#include "vtodocanonfields.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Todo>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

namespace {

using Kalburator::Shape::CanonEnvelope::stampEnvelope;
using Kalburator::Shape::CanonEnvelope::serialize;
using Kalburator::Shape::CanonEnvelope::parse;

KCalendarCore::Todo::Ptr parseTodo(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    KCalendarCore::ICalFormat fmt;
    auto inc = fmt.fromString(QString::fromUtf8(data));
    return inc.dynamicCast<KCalendarCore::Todo>();
}

} // namespace

namespace Kalburator::Todo {

// ---------------------------------------------------------------------------
// VTodoToCanonStage — VTODO iCal bytes → canon JSON (lossless)
// ---------------------------------------------------------------------------

QByteArray VTodoToCanonStage::transform(const QByteArray& vtodoBytes) const
{
    if (vtodoBytes.isEmpty())
        return {};
    const auto todo = parseTodo(vtodoBytes);
    if (!todo)
        return {};
    QJsonObject obj = todoFieldsToCanon(todo, vtodoBytes);
    stampEnvelope(obj, QStringLiteral("todo"), todo->uid());
    return serialize(obj);
}

// ---------------------------------------------------------------------------
// CanonToVTodoStage — canon JSON → VTODO iCal bytes (lossy)
// ---------------------------------------------------------------------------

QByteArray CanonToVTodoStage::transform(const QByteArray& canonBytes) const
{
    if (canonBytes.isEmpty())
        return {};
    return canonObjectToVtodoBytes(parse(canonBytes));
}

// ---------------------------------------------------------------------------
// canonToVtodoLoss — LossProfile for the canon → ical-vtodo demote direction
// ---------------------------------------------------------------------------

Kalburator::Shape::LossProfile canonToVtodoLoss()
{
    using Kalburator::Shape::LossProfile;
    using Kalburator::Shape::LossKind;
    using Kalburator::Shape::PropertyId;

    LossProfile p;
    // Dropped: no VTODO representation at all
    p.affected.insert(PropertyId{QStringLiteral("linkedResources")}, LossKind::Dropped);

    // Reversible: stashed in providerExtras or mapped via X- (round-trippable)
    p.affected.insert(PropertyId{QStringLiteral("descriptionHtml")},  LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("checklistItems")},   LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("sortOrder")},        LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("parentUid")},        LossKind::Reversible);
    // completionAnchor (W4): the verbatim org repeater marker rides
    // providerExtras["x-vtodo"] (generic custom-prop channel) and the
    // derived standard form additionally re-emits as a real RRULE —
    // round-trippable, not lost.
    p.affected.insert(PropertyId{QStringLiteral("completionAnchor")}, LossKind::Reversible);

    // Degraded: vendor-specific status values not representable; mapped to
    // NEEDS-ACTION with original stashed in providerExtras
    p.affected.insert(PropertyId{QStringLiteral("status")},           LossKind::Degraded);

    return p;
}

}  // namespace Kalburator::Todo
