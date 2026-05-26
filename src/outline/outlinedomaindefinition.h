#pragma once

#include "domaindefinition.h"

namespace Kalburator::Outline {

class OutlineDomainDefinition : public Shape::DomainDefinition {
public:
    Shape::DomainId domain() const override;
    Shape::Shape canonicalShape() const override;
    Shape::PropertyCatalogue canonicalCatalogue() const override;
    std::unique_ptr<Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Shape::RecordMerger> createCanonicalMerger() const override;
    int richnessRank(const Shape::Shape &) const override;

    /// Single-node spine: [(outline, canon)]. PluginManager calls
    /// declareCanonical(outline, canon) only. The (outline, org) and
    /// (outline, opml) peers attach to canon via the OutlineStockShapes edges.
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> canonicalSpine() const override;
};

} // namespace Kalburator::Outline
