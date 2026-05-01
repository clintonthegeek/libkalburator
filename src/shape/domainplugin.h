#pragma once

#include <QList>
#include <memory>

#include "propertycatalogue.h"
#include "shape.h"

namespace Kalburator::Shape {

class IRecordDiffer;     // defined in irecorddiffer.h (Task 11)
class IRecordMerger;     // defined in irecordmerger.h (Task 11)
class TransformationRegistry;

/// Domain plugins own the canonical shape for a domain plus the
/// edges that connect peer shapes to the canonical hub. Stock
/// plugins (calendar, contacts, memo, todo) are registered with
/// the DomainRegistry at static-init time; user code can register
/// additional plugins before using the engine.
class DomainPlugin {
public:
    virtual ~DomainPlugin() = default;

    virtual DomainId domain() const = 0;
    virtual Shape canonicalShape() const = 0;

    /// Other shapes this domain plugin knows about. Edges to/from
    /// the canonical shape are registered for each.
    virtual QList<Shape> peerShapes() const = 0;

    virtual PropertyCatalogue canonicalCatalogue() const = 0;
    virtual PropertyCatalogue catalogueFor(const Shape&) const = 0;

    /// Differ / merger over the canonical shape. The engine uses
    /// these on records that have been promoted to canonical.
    virtual std::unique_ptr<IRecordDiffer> createCanonicalDiffer() const = 0;
    virtual std::unique_ptr<IRecordMerger> createCanonicalMerger() const = 0;

    /// Populate the TransformationRegistry with this plugin's
    /// shapes, canonical declaration, and edges. Called once per
    /// process by DomainRegistry::initialize().
    virtual void registerEdges(TransformationRegistry& registry) = 0;

    /// Intra-domain richness rank. Domains with multiple peer shapes
    /// declare a partial order so first-sync `RicherSideWins` can
    /// pick the more expressive side. Higher = richer.
    virtual int richnessRank(const Shape&) const = 0;
};

}  // namespace Kalburator::Shape
