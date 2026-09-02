#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <memory>

#include "lossprofile.h"
#include "shape.h"

namespace Kalburator::Shape {

/// Pure transformation step. Domain plugins implement subclasses to
/// adapt record bytes from one shape to another (e.g. ical →
/// palm-datebook). Implementations should be free of I/O.
class TransformationStage {
public:
    virtual ~TransformationStage() = default;
    virtual QByteArray transform(const QByteArray& sourceBytes) const = 0;
};

/// Identity stage: returns its input unchanged. Used for canonical-
/// hub identity edges and for assembling Pipelines that span
/// transparent steps.
class IdentityStage : public TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override {
        return sourceBytes;
    }
};

struct TransformationEdge {
    Shape from;
    Shape to;
    LossProfile loss;
    std::shared_ptr<TransformationStage> stage;

    /// IP.9 / O88 — kind-scoped loss overrides. Some encodings are a union
    /// of more than one record schema on the SAME shape pair (the calendar
    /// domain's {calendar,canon} → {calendar,ical} edge carries VEVENT,
    /// VTODO and VJOURNAL, discriminated at runtime by `_canon.kind` —
    /// see CanonEnvelope::kind()). `loss` above remains the profile for
    /// every kind NOT present here (this includes the domain's default/
    /// untagged kind — "vevent" for calendar, by icalcanonstages.cpp's own
    /// convention of omitting the kind key for that case). A kind found in
    /// this map OVERRIDES `loss` outright for that kind; profiles are never
    /// merged/composed between the two. Every edge outside the calendar
    /// domain leaves this empty and behaves exactly as it did before IP.9.
    QHash<QString, LossProfile> lossByKind;

    /// The effective loss profile when demoting/materializing a record
    /// whose `_canon.kind` is `kind` (empty string for the untagged/
    /// default case). Looks up `lossByKind` first, falls back to `loss`.
    LossProfile lossFor(const QString& kind) const {
        const auto it = lossByKind.constFind(kind);
        return it != lossByKind.constEnd() ? it.value() : loss;
    }

    /// "<from-shape> → <to-shape> [<loss-summary>]"
    QString toString() const;
};

}  // namespace Kalburator::Shape
