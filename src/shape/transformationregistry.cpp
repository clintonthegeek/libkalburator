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
    if (m_frozenDomains.contains(domain)) {
        qWarning("TransformationRegistry::declareCanonical: domain is frozen — redeclaration ignored");
        return;
    }
    auto it = m_spineByDomain.find(domain);
    if (it != m_spineByDomain.end() && !it->isEmpty()) {
        if (it->first() != canonical) {
            qCritical("TransformationRegistry::declareCanonical: "
                      "conflicting canonical for same domain — "
                      "second plugin must not redeclare; declaration ignored");
        }
        return;  // idempotent same-value (compares the v1 root)
    }
    m_spineByDomain.insert(domain, QList<Shape>{ canonical });
}

void TransformationRegistry::appendCanonicalVersion(DomainId domain, Shape newCanonical) {
    if (m_frozenDomains.contains(domain)) {
        qWarning("TransformationRegistry::appendCanonicalVersion: domain is frozen — ignored");
        return;
    }
    auto it = m_spineByDomain.find(domain);
    if (it == m_spineByDomain.end() || it->isEmpty()) {
        qCritical("TransformationRegistry::appendCanonicalVersion: no canonical declared yet");
        return;
    }
    if (it->last() == newCanonical) return;  // idempotent
    it->append(newCanonical);
}

Shape TransformationRegistry::canonicalFor(const DomainId& d) const {
    auto it = m_spineByDomain.constFind(d);
    if (it == m_spineByDomain.constEnd() || it->isEmpty()) return Shape::Any();
    return it->last();  // head = current canonical
}

QList<Shape> TransformationRegistry::canonicalSpine(const DomainId& d) const {
    return m_spineByDomain.value(d);
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
    // We freeze only the source domain in compile(), but defensively
    // reject edges whose target domain is also frozen — preserves a
    // clean contract should v2 add cross-domain edges.
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
        const bool sameAffected = existing->loss.affected == edge.loss.affected;
        Q_ASSERT_X(sameAffected,
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

std::optional<Pipeline> TransformationRegistry::compileImpl(Shape from, Shape to) const {
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
            return Pipeline{ {*e} };
        }
        return std::nullopt;
    }
    if (to == hub) {
        // Single-leg from → canonical.
        if (const auto* e = findEdge(from, to)) {
            return Pipeline{ {*e} };
        }
        return std::nullopt;
    }

    // Two-leg: from → hub → to.
    const auto* legA = findEdge(from, hub);
    const auto* legB = findEdge(hub, to);
    if (!legA || !legB) return std::nullopt;
    return Pipeline{ {*legA, *legB} };
}

std::optional<Pipeline> TransformationRegistry::compile(Shape from, Shape to) const {
    auto result = compileImpl(from, to);
    // Only freeze on a non-identity successful Pipeline. Identity cases
    // (from == to, to.isAny()) compile to a Pipeline whose result is
    // logically a passthrough — those don't depend on the edge graph
    // and shouldn't freeze the domain.
    if (result.has_value() && from.domain == to.domain
        && !(from == to) && !to.isAny() && !from.isAny()) {
        freeze(from.domain);
    }
    return result;
}

LossProfile TransformationRegistry::inspect(Shape from, Shape to) const {
    auto p = compileImpl(from, to);
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
    m_spineByDomain.clear();
    m_frozenDomains.clear();
}

void TransformationRegistry::unregisterShapes(const QList<Shape> &shapes) {
    for (const auto &s : shapes) {
        m_catalogues.remove(s);
        auto it = m_spineByDomain.find(s.domain);
        if (it != m_spineByDomain.end()) {
            it->removeAll(s);
            if (it->isEmpty()) m_spineByDomain.erase(it);
        }
    }
}

void TransformationRegistry::unregisterEdges(const QList<QPair<Shape, Shape>> &edges) {
    for (const auto &pair : edges) {
        auto range = m_edgesFrom.equal_range(pair.first);
        for (auto it = range.first; it != range.second; ) {
            if (it.value().to == pair.second) {
                it = m_edgesFrom.erase(it);
            } else {
                ++it;
            }
        }
    }
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
