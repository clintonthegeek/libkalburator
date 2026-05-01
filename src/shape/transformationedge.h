#pragma once

#include <QByteArray>
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

    /// "<from-shape> → <to-shape> [<loss-summary>]"
    QString toString() const;
};

}  // namespace Kalburator::Shape
