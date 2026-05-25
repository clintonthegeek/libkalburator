#include "notedomaindefinition.h"
#include "noteproperties.h"
#include "textdiffer.h"
#include "textmerger.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;

namespace Kalburator::Note {

Shape::DomainId NoteDomainDefinition::domain() const { return DomainId{"note"}; }

Shape::Shape NoteDomainDefinition::canonicalShape() const {
    return { DomainId{"note"}, EncodingId{"canon"} };
}

Shape::PropertyCatalogue NoteDomainDefinition::canonicalCatalogue() const {
    return makeNoteCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> NoteDomainDefinition::createCanonicalDiffer() const {
    return std::make_unique<TextDiffer>();
}

std::unique_ptr<Shape::RecordMerger> NoteDomainDefinition::createCanonicalMerger() const {
    return std::make_unique<TextMerger>();
}

int NoteDomainDefinition::richnessRank(const Shape::Shape &s) const {
    if (s == canonicalShape())
        return 100;
    if (s.encoding == EncodingId{"markdown"})
        return 50;
    return 0;
}

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>>
NoteDomainDefinition::canonicalSpine() const {
    return { { canonicalShape(), canonicalCatalogue() } };
}

} // namespace Kalburator::Note
