#pragma once

#include "domainplugin.h"

namespace Kalburator::Blob {

/// DomainPlugin for the blob domain.
///
/// Canonical shape: (blob, raw) — opaque byte payload; no structural
/// properties. Differ is hash-equality (data bytes identical). Merger
/// is whole-record-replace (source wins on conflict by default).
///
/// No peer shapes and no transformation edges for now; the canonical
/// shape is the only registered form. The identity edge is the only
/// edge in the transformation registry for this domain.
class KalburatorDomainBlob : public Kalburator::Shape::DomainPlugin {
public:
    Kalburator::Shape::DomainId domain() const override;
    Kalburator::Shape::Shape canonicalShape() const override;
    QList<Kalburator::Shape::Shape> peerShapes() const override;
    Kalburator::Shape::PropertyCatalogue canonicalCatalogue() const override;
    Kalburator::Shape::PropertyCatalogue catalogueFor(const Kalburator::Shape::Shape&) const override;
    std::unique_ptr<Kalburator::Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Kalburator::Shape::RecordMerger> createCanonicalMerger() const override;
    void registerEdges(Kalburator::Shape::TransformationRegistry& registry) override;
    int richnessRank(const Kalburator::Shape::Shape&) const override;
};

} // namespace Kalburator::Blob
