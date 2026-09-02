#pragma once

#include <QByteArray>
#include <QList>

#include "lossprofile.h"
#include "shape.h"
#include "transformationedge.h"

namespace Kalburator::Shape {

/// Compiled chain of TransformationEdges from `inputShape()` to
/// `outputShape()`. Constructed by TransformationRegistry::compile()
/// (lands in Task 9). Identity (zero-edge) Pipelines are valid and
/// represent same-shape passthrough.
class Pipeline {
public:
    /// Identity Pipeline (no edges). input == output == s.
    explicit Pipeline(Shape s);

    /// Composed Pipeline. Validates that consecutive edges chain
    /// correctly (edges[i].to == edges[i+1].from). The whole list
    /// determines inputShape() (edges.front().from) and outputShape()
    /// (edges.back().to). Empty list yields an identity Pipeline
    /// over Shape::Any() — prefer the single-arg ctor.
    explicit Pipeline(QList<TransformationEdge> edges);

    Shape inputShape() const { return m_inputShape; }
    Shape outputShape() const { return m_outputShape; }

    /// Composition of all edge LossProfiles, folded left-to-right. IP.9:
    /// `kind` selects, per edge, the kind-scoped override from
    /// TransformationEdge::lossByKind when present (TransformationEdge::
    /// lossFor()), falling back to the edge's default `loss` otherwise.
    /// Passing the default empty `kind` reproduces pre-IP.9 behaviour
    /// exactly for every edge that never populates `lossByKind` (i.e.
    /// every edge outside the calendar domain's kind-polymorphic leg).
    LossProfile composedLoss(const QString& kind = QString()) const;

    /// Apply each stage's transform in order. Throws std::logic_error
    /// if a stage is null (defensive — TransformationRegistry never
    /// constructs Pipelines with null stages).
    QByteArray apply(const QByteArray& input) const;

    /// True iff there are no edges (same-shape passthrough).
    bool isIdentity() const noexcept { return m_edges.isEmpty(); }

    const QList<TransformationEdge>& edges() const { return m_edges; }

private:
    Shape m_inputShape;
    Shape m_outputShape;
    QList<TransformationEdge> m_edges;
};

}  // namespace Kalburator::Shape
