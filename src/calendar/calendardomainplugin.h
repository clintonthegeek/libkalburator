#pragma once

#include "domainplugin.h"

namespace Kalburator::Calendar {

class KalburatorDomainCalendar : public Kalburator::Shape::DomainPlugin {
public:
    Kalburator::Shape::DomainId domain() const override;
    Kalburator::Shape::Shape canonicalShape() const override;
    QList<Kalburator::Shape::Shape> peerShapes() const override;
    Kalburator::Shape::PropertyCatalogue canonicalCatalogue() const override;
    Kalburator::Shape::PropertyCatalogue catalogueFor(const Kalburator::Shape::Shape&) const override;
    std::unique_ptr<Kalburator::Shape::IRecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Kalburator::Shape::IRecordMerger> createCanonicalMerger() const override;
    void registerEdges(Kalburator::Shape::TransformationRegistry& registry) override;
    int richnessRank(const Kalburator::Shape::Shape&) const override;
};

} // namespace Kalburator::Calendar
