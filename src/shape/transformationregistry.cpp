#include "transformationregistry.h"

#include <QtGlobal>

namespace Kalburator::Shape {

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
    if (to.isAny()) return Pipeline{from};
    if (from.isAny()) return std::nullopt;
    if (from == to) return Pipeline{from};
    if (from.domain != to.domain) return std::nullopt;  // cross-domain not in v1

    const QList<Shape> spine = m_spineByDomain.value(from.domain);
    if (spine.isEmpty()) return std::nullopt;

    QList<TransformationEdge> edges;

    // 1. Source side: resolve `from` to a spine node `fromIdx`.
    int fromIdx = spine.indexOf(from);
    if (fromIdx < 0) {
        const TransformationEdge* lead = nullptr;
        for (int i = 0; i < spine.size(); ++i) {
            if (const auto* e = findEdge(from, spine[i])) { lead = e; fromIdx = i; break; }
        }
        if (!lead) return std::nullopt;
        edges.append(*lead);
    }

    // 2. Target side: resolve `to` to a spine node `toIdx` (tail edge applied last).
    int toIdx = spine.indexOf(to);
    const TransformationEdge* tail = nullptr;
    if (toIdx < 0) {
        for (int i = 0; i < spine.size(); ++i) {
            if (const auto* e = findEdge(spine[i], to)) { tail = e; toIdx = i; break; }
        }
        if (!tail) return std::nullopt;
    }

    // 3. Walk the spine between the two anchors via adjacent bridge edges.
    if (fromIdx < toIdx) {
        for (int i = fromIdx; i < toIdx; ++i) {
            const auto* e = findEdge(spine[i], spine[i + 1]);
            if (!e) return std::nullopt;
            edges.append(*e);
        }
    } else if (fromIdx > toIdx) {
        for (int i = fromIdx; i > toIdx; --i) {
            const auto* e = findEdge(spine[i], spine[i - 1]);
            if (!e) return std::nullopt;
            edges.append(*e);
        }
    }

    // 4. Apply the target tail edge last.
    if (tail) edges.append(*tail);

    if (edges.isEmpty()) return std::nullopt;  // defensive; unreachable when from != to
    return Pipeline{ edges };
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
