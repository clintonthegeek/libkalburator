#include "transformationregistry.h"

#include <QtGlobal>

namespace Kalburator::Shape {

TransformationRegistry& TransformationRegistry::instance() {
    static TransformationRegistry s_instance;
    return s_instance;
}

void TransformationRegistry::registerShape(Shape shape, PropertyCatalogue catalogue) {
    if (m_frozenDomains.contains(shape.domain)) {
        qWarning("TransformationRegistry::registerShape: shape's domain is frozen — register before first compile()");
        return;
    }
    m_catalogues.insert(shape, std::move(catalogue));
}

void TransformationRegistry::declareCanonical(DomainId domain, Shape canonical) {
    m_canonicalByDomain.insert(domain, canonical);
}

Shape TransformationRegistry::canonicalFor(const DomainId& d) const {
    auto it = m_canonicalByDomain.constFind(d);
    if (it == m_canonicalByDomain.constEnd()) return Shape::Any();
    return *it;
}

bool TransformationRegistry::isFrozen(const DomainId& d) const
{
    return m_frozenDomains.contains(d);
}

void TransformationRegistry::freeze(const DomainId& d) const
{
    m_frozenDomains.insert(d);
}

void TransformationRegistry::registerEdge(TransformationEdge edge) {
    if (m_frozenDomains.contains(edge.from.domain)
        || m_frozenDomains.contains(edge.to.domain)) {
        qWarning("TransformationRegistry::registerEdge: edge endpoint domain is frozen — register before first compile()");
        return;
    }
    Q_ASSERT_X(m_catalogues.contains(edge.from),
               "TransformationRegistry::registerEdge",
               "from-shape not registered");
    Q_ASSERT_X(m_catalogues.contains(edge.to),
               "TransformationRegistry::registerEdge",
               "to-shape not registered");

    if (const auto* existing = findEdge(edge.from, edge.to)) {
        // Idempotent on identical re-registration; assert on conflict.
        const bool sameLevel = existing->loss.level == edge.loss.level;
        const bool sameDropped = existing->loss.dropped == edge.loss.dropped;
        Q_ASSERT_X(sameLevel && sameDropped,
                   "TransformationRegistry::registerEdge",
                   "conflicting re-registration of (from, to) edge");
        return;
    }
    m_edgesFrom.insert(edge.from, std::move(edge));
}

const PropertyCatalogue* TransformationRegistry::catalogueFor(const Shape& s) const {
    auto it = m_catalogues.constFind(s);
    if (it == m_catalogues.constEnd()) return nullptr;
    return &(*it);
}

std::optional<Pipeline> TransformationRegistry::compile(Shape from, Shape to) const {
    if (to.isAny()) {
        // Universal sink: identity over the source shape so apply()
        // is a passthrough; backend stores bytes plus shape metadata.
        return Pipeline{from};
    }
    if (from.isAny()) {
        return std::nullopt;
    }
    if (from == to) {
        return Pipeline{from};
    }
    if (from.domain != to.domain) {
        // Cross-domain not in v1.
        return std::nullopt;
    }

    const Shape hub = canonicalFor(from.domain);
    if (hub.isAny()) {
        // Domain has no canonical declared.
        return std::nullopt;
    }

    if (from == hub) {
        // Single-leg from canonical → to.
        if (const auto* e = findEdge(from, to)) {
            // Freeze the source domain so subsequent edge/shape registration
            // is rejected; later compile() calls for the same domain remain
            // valid because the registry data is unchanged.
            freeze(from.domain);
            return Pipeline{ {*e} };
        }
        return std::nullopt;
    }
    if (to == hub) {
        // Single-leg from → canonical.
        if (const auto* e = findEdge(from, to)) {
            // Freeze the source domain so subsequent edge/shape registration
            // is rejected; later compile() calls for the same domain remain
            // valid because the registry data is unchanged.
            freeze(from.domain);
            return Pipeline{ {*e} };
        }
        return std::nullopt;
    }

    // Two-leg: from → hub → to.
    const auto* legA = findEdge(from, hub);
    const auto* legB = findEdge(hub, to);
    if (!legA || !legB) return std::nullopt;
    // Freeze the source domain so subsequent edge/shape registration
    // is rejected; later compile() calls for the same domain remain
    // valid because the registry data is unchanged.
    freeze(from.domain);
    return Pipeline{ {*legA, *legB} };
}

LossProfile TransformationRegistry::inspect(Shape from, Shape to) const {
    auto p = compile(from, to);
    if (!p.has_value()) return LossProfile{};
    return p->composedLoss();
}

QList<Shape> TransformationRegistry::registeredShapes() const {
    return m_catalogues.keys();
}

QList<TransformationEdge> TransformationRegistry::edgesFrom(const Shape& s) const {
    return m_edgesFrom.values(s);
}

void TransformationRegistry::clear() {
    m_catalogues.clear();
    m_edgesFrom.clear();
    m_canonicalByDomain.clear();
    m_frozenDomains.clear();
}

const TransformationEdge*
TransformationRegistry::findEdge(const Shape& a, const Shape& b) const {
    auto range = m_edgesFrom.equal_range(a);
    for (auto it = range.first; it != range.second; ++it) {
        if (it.value().to == b) {
            return &it.value();
        }
    }
    return nullptr;
}

}  // namespace Kalburator::Shape
