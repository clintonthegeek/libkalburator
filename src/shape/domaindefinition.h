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
};

} // namespace Kalburator::Shape

#endif // KALBURATOR_SHAPE_DOMAINDEFINITION_H
