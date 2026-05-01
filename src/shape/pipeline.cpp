#include "pipeline.h"

#include <stdexcept>

namespace Kalburator::Shape {

Pipeline::Pipeline(Shape s)
    : m_inputShape(s), m_outputShape(s) {}

Pipeline::Pipeline(QList<TransformationEdge> edges)
    : m_edges(std::move(edges)) {
    if (m_edges.isEmpty()) {
        m_inputShape = Shape::Any();
        m_outputShape = Shape::Any();
        return;
    }
    for (qsizetype i = 1; i < m_edges.size(); ++i) {
        if (m_edges.at(i - 1).to != m_edges.at(i).from) {
            throw std::logic_error(
                "Pipeline: non-matching edge chain (edges[i-1].to != edges[i].from)");
        }
    }
    m_inputShape = m_edges.front().from;
    m_outputShape = m_edges.back().to;
}

LossProfile Pipeline::composedLoss() const {
    LossProfile out;
    for (const auto& e : m_edges) {
        out = out.compose(e.loss);
    }
    return out;
}

QByteArray Pipeline::apply(const QByteArray& input) const {
    QByteArray cur = input;
    for (const auto& e : m_edges) {
        if (!e.stage) {
            throw std::logic_error("Pipeline: null TransformationStage");
        }
        cur = e.stage->transform(cur);
    }
    return cur;
}

}  // namespace Kalburator::Shape
