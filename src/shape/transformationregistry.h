#pragma once

#include <QHash>
#include <QList>
#include <QMultiHash>
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

    /// Look up the canonical shape for a domain. Returns
    /// `Shape::Any()` if the domain has no canonical declared.
    Shape canonicalFor(const DomainId&) const;

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

private:
    TransformationRegistry() = default;

    /// Find the single edge from `a` to `b`, or nullptr if absent.
    const TransformationEdge* findEdge(const Shape& a, const Shape& b) const;

    QHash<Shape, PropertyCatalogue> m_catalogues;
    QMultiHash<Shape, TransformationEdge> m_edgesFrom;
    QHash<DomainId, Shape> m_canonicalByDomain;
};

}  // namespace Kalburator::Shape
