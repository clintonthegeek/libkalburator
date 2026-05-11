#include "tododomaindefinition.h"
#include "icalvtodoproperties.h"
#include "icalvtododiffer.h"
#include "icalvtodomerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Todo {

Shape::DomainId TodoDomainDefinition::domain() const
{
    return DomainId{QStringLiteral("todo")};
}

Shape::Shape TodoDomainDefinition::canonicalShape() const
{
    return { DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };
}

Shape::PropertyCatalogue TodoDomainDefinition::canonicalCatalogue() const
{
    return makeVTodoCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> TodoDomainDefinition::createCanonicalDiffer() const
{
    return std::make_unique<RecordDifferVTodo>();
}

std::unique_ptr<Shape::RecordMerger> TodoDomainDefinition::createCanonicalMerger() const
{
    return std::make_unique<RecordMergerVTodo>();
}

int TodoDomainDefinition::richnessRank(const Shape::Shape &s) const
{
    if (s == canonicalShape())
        return 10;
    if (s.encoding == EncodingId{QStringLiteral("todotxt")})
        return 3;
    return 0;
}

} // namespace Kalburator::Todo
