#pragma once

#include <QList>

#include "propertycatalogue.h"
#include "recorddiffer.h"

namespace Kalburator::Shape {

/// RecordDiffer for canon JSON records. Coarse, one PropertyId per row
/// (STATUS decision 6): a change anywhere inside a composite (Json) property
/// marks the whole property changed. Ignores the `_canon` envelope and
/// `providerExtras` (schema §5: providerExtras is never a conflict axis).
/// Compares only the PropertyIds it is constructed with (the domain's canon
/// catalogue property ids).
class CanonJsonDiffer : public RecordDiffer {
public:
    explicit CanonJsonDiffer(QList<PropertyId> properties);

    QSet<PropertyId> diff(const CanonicalRecord& source,
                          const CanonicalRecord& baseline) const override;
    bool equal(const CanonicalRecord& a, const CanonicalRecord& b) const override;

private:
    QList<PropertyId> m_properties;
};

}  // namespace Kalburator::Shape
