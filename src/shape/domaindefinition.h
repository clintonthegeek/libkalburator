#ifndef KALBURATOR_SHAPE_DOMAINDEFINITION_H
#define KALBURATOR_SHAPE_DOMAINDEFINITION_H

#include <QStringList>
#include <memory>

#include "propertycatalogue.h"
#include "shape.h"

namespace Kalburator::Shape {

class RecordDiffer;
class RecordMerger;

/// Read-only descriptor of one domain's canonical geometry: its shape,
/// property catalogue, differ/merger factories, and richness ordering.
///
/// DomainDefinition is the "what does this domain look like?" half of
/// the plugin contract. It is split from DomainOperations ("how do I
/// read/write records for this domain?") so that consumers that only
/// need structural information (shape graph builders, UI catalogues,
/// transcoding registry setup) can depend on a minimal interface without
/// pulling in backend or writer machinery.
class DomainDefinition {
public:
    virtual ~DomainDefinition() = default;

    virtual DomainId domain() const = 0;
    virtual Shape canonicalShape() const = 0;
    virtual PropertyCatalogue canonicalCatalogue() const = 0;
    virtual std::unique_ptr<RecordDiffer> createCanonicalDiffer() const = 0;
    virtual std::unique_ptr<RecordMerger> createCanonicalMerger() const = 0;
    virtual int richnessRank(const Shape &) const = 0;
    virtual QStringList baselineProperties() const { return {}; }

    /// Ordered canonical spine for versioned-canon domains: oldest node first,
    /// current head last. `canonicalShape()` must equal `spine().last()` when
    /// the spine is non-empty; the default returns `{ canonicalShape() }`.
    ///
    /// PluginManager uses this to build the full spine via `declareCanonical`
    /// (first entry) + `appendCanonicalVersion` (subsequent entries), so that
    /// N-hop peer routing works (e.g. vcard3 → vcard4 → canon).
    ///
    /// Returns (shape, catalogue) pairs.  The head's catalogue must equal
    /// `canonicalCatalogue()`.  Intermediate entries may return an empty
    /// catalogue if the plugin does not need to expose that version's fields
    /// (the PluginManager will still register the shape so edges can reference
    /// it).
    virtual QList<std::pair<Shape, PropertyCatalogue>> canonicalSpine() const
    {
        return { { canonicalShape(), canonicalCatalogue() } };
    }
};

} // namespace Kalburator::Shape

#endif // KALBURATOR_SHAPE_DOMAINDEFINITION_H
