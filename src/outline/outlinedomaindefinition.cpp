#include "outlinedomaindefinition.h"
#include "outlinecanonproperties.h"
#include "outlinediffer.h"
#include "outlinemerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Outline {

Shape::DomainId OutlineDomainDefinition::domain() const { return DomainId{"outline"}; }

Shape::Shape OutlineDomainDefinition::canonicalShape() const {
    return { DomainId{"outline"}, EncodingId{"canon"} };
}

Shape::PropertyCatalogue OutlineDomainDefinition::canonicalCatalogue() const {
    return makeOutlineCanonCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> OutlineDomainDefinition::createCanonicalDiffer() const {
    return std::make_unique<OutlineDiffer>();
}

std::unique_ptr<Shape::RecordMerger> OutlineDomainDefinition::createCanonicalMerger() const {
    return std::make_unique<OutlineMerger>();
}

int OutlineDomainDefinition::richnessRank(const Shape::Shape &s) const {
    if (s == canonicalShape())
        return 100;
    if (s.encoding == EncodingId{"org"})
        return 70;
    if (s.encoding == EncodingId{"opml"})
        return 40;
    return 0;
}

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>>
OutlineDomainDefinition::canonicalSpine() const {
    return { { canonicalShape(), canonicalCatalogue() } };
}

} // namespace Kalburator::Outline
