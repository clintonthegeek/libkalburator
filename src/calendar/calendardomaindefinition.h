#pragma once
#include "domaindefinition.h"

namespace Kalburator::Calendar {

class CalendarDomainDefinition : public Kalburator::Shape::DomainDefinition {
public:
    Kalburator::Shape::DomainId domain() const override;
    Kalburator::Shape::Shape canonicalShape() const override;
    Kalburator::Shape::PropertyCatalogue canonicalCatalogue() const override;
    std::unique_ptr<Kalburator::Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Kalburator::Shape::RecordMerger> createCanonicalMerger() const override;
    int richnessRank(const Kalburator::Shape::Shape &s) const override;
    QStringList baselineProperties() const override;
};

} // namespace Kalburator::Calendar
