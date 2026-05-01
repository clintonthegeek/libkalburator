#pragma once

#include <QSet>
#include <QString>

#include "propertycatalogue.h"

namespace Kalburator::Shape {

enum class LossLevel {
    Lossless,            // pure encoding round-trip; no information lost
    IntraDomainLossy,    // same domain; capability gap
    InterDomainProjection,  // different domain; structural reduction
    Degenerate,          // different domain; only name-like field preserved
};

struct LossProfile {
    LossLevel level = LossLevel::Lossless;
    QSet<PropertyId> dropped;        // properties this pipeline drops

    bool isLossless() const noexcept { return level == LossLevel::Lossless; }

    /// Composes self with a downstream profile when stacking edges
    /// into a pipeline. Result level is the max of the two; dropped
    /// is the union. Composition is associative.
    LossProfile compose(const LossProfile& downstream) const;

    /// Human-readable summary, e.g. "lossless" or
    /// "intra-lossy: drops attendees, attachments".
    QString summary() const;
};

}  // namespace Kalburator::Shape
