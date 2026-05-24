#pragma once
#include "domaindefinition.h"

namespace Kalburator::Todo {

class TodoDomainDefinition : public Shape::DomainDefinition {
public:
    Shape::DomainId domain() const override;
    Shape::Shape canonicalShape() const override;
    Shape::PropertyCatalogue canonicalCatalogue() const override;
    std::unique_ptr<Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Shape::RecordMerger> createCanonicalMerger() const override;
    int richnessRank(const Shape::Shape &) const override;

    /// Versioned canonical spine: [ical-vtodo, canon]. PluginManager uses this
    /// to build the spine via declareCanonical (root) + appendCanonicalVersion
    /// (upgrades) so that todotxt→ical-vtodo→canon N-hop routing works.
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> canonicalSpine() const override;
};

} // namespace Kalburator::Todo
