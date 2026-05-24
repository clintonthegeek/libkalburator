#include "tododomaindefinition.h"
#include "icalvtodoproperties.h"
#include "todocanonproperties.h"
#include "canonjsondiffer.h"
#include "canonjsonmerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Todo {

Shape::DomainId TodoDomainDefinition::domain() const
{
    return DomainId{QStringLiteral("todo")};
}

Shape::Shape TodoDomainDefinition::canonicalShape() const
{
    return { DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("canon")} };
}

Shape::PropertyCatalogue TodoDomainDefinition::canonicalCatalogue() const
{
    return makeTodoCanonCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> TodoDomainDefinition::createCanonicalDiffer() const
{
    return std::make_unique<Kalburator::Shape::CanonJsonDiffer>(todoCanonPropertyIds());
}

std::unique_ptr<Shape::RecordMerger> TodoDomainDefinition::createCanonicalMerger() const
{
    return std::make_unique<Kalburator::Shape::CanonJsonMerger>(
        QStringLiteral("todo"), todoCanonPropertyIds());
}

int TodoDomainDefinition::richnessRank(const Shape::Shape &s) const
{
    if (s == canonicalShape())
        return 100;
    if (s.encoding == EncodingId{QStringLiteral("ical-vtodo")})
        return 50;
    if (s.encoding == EncodingId{QStringLiteral("todotxt")})
        return 3;
    return 0;
}

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>>
TodoDomainDefinition::canonicalSpine() const
{
    // Spine: [ical-vtodo (root), canon (head)]
    // PluginManager calls declareCanonical(todo, ical-vtodo) then
    // appendCanonicalVersion(todo, canon), so that todotxt→ical-vtodo→canon
    // N-hop routing works (todotxt peer attaches at spine[0]=ical-vtodo).
    const Shape::Shape vtodo{ DomainId{QStringLiteral("todo")},
                              EncodingId{QStringLiteral("ical-vtodo")} };
    return {
        { vtodo,          makeVTodoCatalogue() },
        { canonicalShape(), canonicalCatalogue() },
    };
}

} // namespace Kalburator::Todo
