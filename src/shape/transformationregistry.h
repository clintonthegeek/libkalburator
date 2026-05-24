#pragma once

#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QPair>
#include <QSet>
#include <optional>

#include "lossprofile.h"
#include "pipeline.h"
#include "propertycatalogue.h"
#include "shape.h"
#include "transformationedge.h"

namespace Kalburator::Shape {

/// Process-wide registry of (shape, catalogue) pairs and
/// (shape→shape) transformation edges. Populated at static-init by
/// domain plugins; queried by the engine to compile per-mapping
/// pipelines.
///
/// Topology: hub-and-spoke per domain. Each domain declares one
/// canonical shape. Edges connect native shapes ↔ canonical
/// (intra-domain, possibly lossy) and identity within canonical.
/// In v1, no cross-domain edges are registered.
class TransformationRegistry {
public:
    static TransformationRegistry& instance();

    /// Register a property catalogue for a shape. Required before
    /// any edge involving that shape can be registered. Idempotent
    /// on re-registration of the same shape (the new catalogue
    /// replaces the old).
    void registerShape(Shape shape, PropertyCatalogue catalogue);

    /// Register the canonical hub shape for a domain. Required
    /// before compile() can produce non-identity pipelines for that
    /// domain. Idempotent on re-registration.
    void declareCanonical(DomainId domain, Shape canonical);

    /// Append a newer canonical version to a domain's spine, making it the
    /// new head (current canonical). Requires the spine to already exist
    /// (declareCanonical first) and the domain not yet frozen. The bridge
    /// edges between the previous head and `newCanonical` must be registered
    /// separately. Idempotent if `newCanonical` is already the head.
    void appendCanonicalVersion(DomainId domain, Shape newCanonical);

    /// Look up the canonical shape for a domain. Returns
    /// `Shape::Any()` if the domain has no canonical declared.
    Shape canonicalFor(const DomainId&) const;

    /// The full ordered canonical spine for a domain (oldest → current).
    /// Empty if no canonical declared.
    QList<Shape> canonicalSpine(const DomainId&) const;

    /// True if compile() has been called against any shape in this
    /// domain. After that, registerEdge / registerShape for shapes
    /// in this domain are rejected. Test introspection for the
    /// post-init dynamic-registration contract.
    bool isFrozen(const DomainId&) const;

    /// Register a transformation edge. Both endpoints must already
    /// be registered shapes. Asserts on conflicting re-registration
    /// of the same (from, to) pair; idempotent on identical
    /// re-registration.
    void registerEdge(TransformationEdge edge);

    /// Look up the catalogue for a shape. Returns nullptr if
    /// unregistered.
    const PropertyCatalogue* catalogueFor(const Shape&) const;

    /// Compile a pipeline from `from` to `to`.
    ///
    /// - `to.isAny()` → identity Pipeline (universal sink semantics).
    /// - `from.isAny()` → nullopt (can't compile from unknown shape).
    /// - `from == to` → identity Pipeline.
    /// - `from.domain != to.domain` → nullopt (no cross-domain in v1).
    /// - Otherwise: compose `from → canonical(from.domain) → to`
    ///   when both legs exist; nullopt otherwise.
    std::optional<Pipeline> compile(Shape from, Shape to) const;

    /// Like compile(), but only returns the loss profile (no
    /// Pipeline allocation). Returns lossless when no path exists
    /// (caller should compile() to detect that).
    LossProfile inspect(Shape from, Shape to) const;

    /// All registered shapes (for debugging / UX).
    QList<Shape> registeredShapes() const;

    /// All edges leaving a shape.
    QList<TransformationEdge> edgesFrom(const Shape&) const;

    /// Test-only escape hatch: drop everything. Use in test cleanup
    /// to avoid singleton leakage between cases.
    void clear();

    /// Remove the given shapes from the catalogues map and from the
    /// canonical spine if the shape appears there.
    void unregisterShapes(const QList<Shape> &shapes);

    /// Remove edges from the edge graph. For each (from, to) pair,
    /// remove all edges in m_edgesFrom[from] where edge.to == to.
    void unregisterEdges(const QList<QPair<Shape, Shape>> &edges);

public:
    /// Default-constructible so it can be a member of ShapeRegistries (DI).
    /// Use ShapeRegistries / dependency injection, not instance(), in new code.
    TransformationRegistry() = default;

private:
    /// Find the single edge from `a` to `b`, or nullptr if absent.
    const TransformationEdge* findEdge(const Shape& a, const Shape& b) const;

    /// Internal: like compile() but without the freeze side-effect.
    /// compile() freezes the source domain; inspect() must not.
    std::optional<Pipeline> compileImpl(Shape from, Shape to) const;

    /// Internal: mark a domain frozen. Called by compile() on its
    /// successful non-identity branch. The frozen set is logically a
    /// "has-been-queried" cache: once a compile() consults the edge
    /// graph for a domain, that graph is fixed. Hence `freeze()` is
    /// `const` and `m_frozenDomains` is `mutable`, in the standard
    /// pattern of caching the result of a logically-pure query.
    void freeze(const DomainId& d) const;

    QHash<Shape, PropertyCatalogue> m_catalogues;
    QMultiHash<Shape, TransformationEdge> m_edgesFrom;
    QHash<DomainId, QList<Shape>> m_spineByDomain;

    /// Domains for which compile() has produced a non-identity Pipeline.
    /// Once a domain is frozen, registerEdge / registerShape on shapes
    /// in that domain are rejected.
    mutable QSet<DomainId> m_frozenDomains;
};

}  // namespace Kalburator::Shape
