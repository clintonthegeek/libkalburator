#include "memodomaindefinition.h"
#include "memoproperties.h"
#include "textdiffer.h"
#include "textmerger.h"

namespace Kalburator::Memo {

Shape::DomainId MemoDomainDefinition::domain() const { return Shape::DomainId{"memo"}; }

Shape::Shape MemoDomainDefinition::canonicalShape() const {
    return { Shape::DomainId{"memo"}, Shape::EncodingId{"text"} };
}

Shape::PropertyCatalogue MemoDomainDefinition::canonicalCatalogue() const {
    return makeMemoCatalogue();
}

std::unique_ptr<Shape::RecordDiffer> MemoDomainDefinition::createCanonicalDiffer() const {
    return std::make_unique<TextDiffer>();
}

std::unique_ptr<Shape::RecordMerger> MemoDomainDefinition::createCanonicalMerger() const {
    return std::make_unique<TextMerger>();
}

int MemoDomainDefinition::richnessRank(const Shape::Shape &s) const {
    return s == canonicalShape() ? 10 : 0;
}

} // namespace Kalburator::Memo
