#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Todo {

/// Transforms a VTODO iCalendar byte-string to canon JSON (lossless).
class VTodoToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& vtodoBytes) const override;
};

/// Transforms canon JSON bytes to a VTODO iCalendar string (lossy).
class CanonToVTodoStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → ical-vtodo demote direction.
Kalburator::Shape::LossProfile canonToVtodoLoss();

}  // namespace Kalburator::Todo
