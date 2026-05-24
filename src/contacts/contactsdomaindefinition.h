#pragma once
#include "domaindefinition.h"

namespace Kalburator::Contacts {

class ContactsDomainDefinition : public Shape::DomainDefinition {
public:
    Shape::DomainId domain() const override;
    Shape::Shape canonicalShape() const override;
    Shape::PropertyCatalogue canonicalCatalogue() const override;
    std::unique_ptr<Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Shape::RecordMerger> createCanonicalMerger() const override;
    int richnessRank(const Shape::Shape &) const override;

    /// Spine: vcard4 (v1 root) → canon (head). Allows vcard3→vcard4→canon
    /// N-hop routing via the existing vcard3↔vcard4 peer edges.
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> canonicalSpine() const override;
};

} // namespace Kalburator::Contacts
