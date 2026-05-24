#pragma once

#include <QHash>
#include <QSet>
#include <QString>

#include "propertycatalogue.h"

namespace Kalburator::Shape {

/// How a single property is affected when a pipeline transforms a record.
enum class LossKind {
    Dropped,      // target cannot represent the property; information is gone
    Simplified,   // property survives in a reduced form (e.g. RRULE -> patternedRecurrence)
    Reversible,   // moved into an extension/X- property; a round-trip is lossless
    Degraded,     // mapped through a lossy many-to-one vocabulary; original kept verbatim
};

/// Severity ordering used when composing two profiles that touch the same
/// property: Reversible(0) < Degraded(1) < Simplified(2) < Dropped(3).
int lossKindSeverity(LossKind) noexcept;

struct LossProfile {
    QHash<PropertyId, LossKind> affected;

    /// Optional warning-path refinement: for a property listed here, the loss
    /// does NOT materialize when the record's (string) value is in the set.
    /// A property absent from this map loses for every value (the default).
    /// Lets a value-dependent Degraded edge (e.g. classification: only the MS
    /// "personal" value degrades; public/private/confidential map exactly)
    /// avoid crying wolf without making the static `affected` map value-aware.
    QHash<PropertyId, QSet<QString>> losslessValues;

    bool isLossless() const noexcept { return affected.isEmpty(); }

    /// Composes self with a downstream profile when stacking edges into a
    /// pipeline. Result is the union of affected maps; on a key collision the
    /// more severe kind wins. Composition is associative.
    LossProfile compose(const LossProfile& downstream) const;

    /// Human-readable summary, e.g. "lossless" or "drops gender; simplifies rrule".
    QString summary() const;

    /// Keys whose loss kind is Dropped (policy / compatibility helper).
    QSet<PropertyId> droppedProperties() const;
};

}  // namespace Kalburator::Shape
