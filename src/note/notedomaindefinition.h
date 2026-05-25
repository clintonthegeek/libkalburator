#pragma once

#include "domaindefinition.h"

namespace Kalburator::Note {

class NoteDomainDefinition : public Shape::DomainDefinition {
public:
    Shape::DomainId domain() const override;
    Shape::Shape canonicalShape() const override;
    Shape::PropertyCatalogue canonicalCatalogue() const override;
    std::unique_ptr<Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Shape::RecordMerger> createCanonicalMerger() const override;
    int richnessRank(const Shape::Shape &) const override;

    /// Single-node spine: [(note, canon)]. PluginManager calls
    /// declareCanonical(note, canon) only. The (note, markdown) peer attaches
    /// to canon via the NoteStockShapes edges.
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> canonicalSpine() const override;
};

} // namespace Kalburator::Note
