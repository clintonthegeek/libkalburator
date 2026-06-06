#pragma once

#include <QList>

#include "propertycatalogue.h"
#include "recordmerger.h"

namespace Kalburator::Shape {

/// RecordMerger for canon JSON records. Per-PropertyId 3-way merge over the
/// domain's canon catalogue property ids (coarse; STATUS decision 6).
/// `providerExtras` follows the merge result's origin record rather than being
/// three-way merged (schema §5: never a conflict axis). `recurrence`, like any
/// composite, is merged as one opaque whole.
class CanonJsonMerger : public RecordMerger {
public:
    /// `domain` is the canon domain string for the merged record's envelope.
    CanonJsonMerger(QString domain, QList<PropertyId> properties);

    CanonicalRecord merge(const CanonicalRecord& source,
                          const CanonicalRecord& target,
                          const CanonicalRecord& baseline,
                          AutoResolveStrategy strategy) const override;

private:
    QString m_domain;
    QList<PropertyId> m_properties;
};

}  // namespace Kalburator::Shape
