#include "calendardomaindefinition.h"

#include "icalproperties.h"
#include "calendarcanonproperties.h"
#include "canonjsondiffer.h"
#include "canonjsonmerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::RecordDiffer;
using Kalburator::Shape::RecordMerger;

namespace Kalburator::Calendar {

DomainId CalendarDomainDefinition::domain() const
{
    return DomainId{QStringLiteral("calendar")};
}

Kalburator::Shape::Shape CalendarDomainDefinition::canonicalShape() const
{
    return { DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("canon")} };
}

PropertyCatalogue CalendarDomainDefinition::canonicalCatalogue() const
{
    return makeCalendarCanonCatalogue();
}

std::unique_ptr<RecordDiffer> CalendarDomainDefinition::createCanonicalDiffer() const
{
    return std::make_unique<Kalburator::Shape::CanonJsonDiffer>(calendarCanonPropertyIds());
}

std::unique_ptr<RecordMerger> CalendarDomainDefinition::createCanonicalMerger() const
{
    return std::make_unique<Kalburator::Shape::CanonJsonMerger>(
        QStringLiteral("calendar"), calendarCanonPropertyIds());
}

int CalendarDomainDefinition::richnessRank(const Kalburator::Shape::Shape &s) const
{
    if (s == canonicalShape())
        return 100;
    if (s.encoding == EncodingId{QStringLiteral("ical")})
        return 50;
    return 0;
}

QStringList CalendarDomainDefinition::baselineProperties() const
{
    // Both "color" and "description" are PropertyIds in the canon catalogue.
    return { QStringLiteral("color"), QStringLiteral("description") };
}

QList<std::pair<Kalburator::Shape::Shape, Kalburator::Shape::PropertyCatalogue>>
CalendarDomainDefinition::canonicalSpine() const
{
    // Spine: [ical (root), canon (head)]
    // PluginManager calls declareCanonical(calendar, ical) then
    // appendCanonicalVersion(calendar, canon).
    const Kalburator::Shape::Shape icalShape{
        DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("ical")} };
    return {
        { icalShape,       makeICalCatalogue() },
        { canonicalShape(), canonicalCatalogue() },
    };
}

} // namespace Kalburator::Calendar
