#include "calendardomaindefinition.h"

#include "icalproperties.h"
#include "icalrecorddiffer.h"
#include "icalrecordmerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::RecordDiffer;
using Kalburator::Shape::RecordMerger;

namespace Kalburator::Calendar {

DomainId CalendarDomainDefinition::domain() const
{
    return DomainId{"calendar"};
}

Kalburator::Shape::Shape CalendarDomainDefinition::canonicalShape() const
{
    return { DomainId{"calendar"}, EncodingId{"ical"} };
}

PropertyCatalogue CalendarDomainDefinition::canonicalCatalogue() const
{
    return makeICalCatalogue();
}

std::unique_ptr<RecordDiffer> CalendarDomainDefinition::createCanonicalDiffer() const
{
    return std::make_unique<RecordDifferICal>();
}

std::unique_ptr<RecordMerger> CalendarDomainDefinition::createCanonicalMerger() const
{
    return std::make_unique<RecordMergerICal>();
}

int CalendarDomainDefinition::richnessRank(const Kalburator::Shape::Shape &s) const
{
    return s == canonicalShape() ? 10 : 0;
}

QStringList CalendarDomainDefinition::baselineProperties() const
{
    return { QStringLiteral("color"), QStringLiteral("description") };
}

} // namespace Kalburator::Calendar
