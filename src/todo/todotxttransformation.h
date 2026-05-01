#pragma once

#include "transformationedge.h"

namespace Kalburator::Todo {

/// TransformationStage: (todo, ical-vtodo) → (todo, todotxt).
///
/// todo.txt format: https://github.com/todotxt/todo.txt
/// Loss: drops description, attendees, RRULE, attachments, alarms,
/// custom properties; keeps at most one +project and one @context.
class ICalToTodoTxtStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// TransformationStage: (todo, todotxt) → (todo, ical-vtodo).
class TodoTxtToICalStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& sourceBytes) const override;
};

/// Loss profile for the (ical-vtodo → todotxt) direction.
Kalburator::Shape::LossProfile todoTxtLoss();

} // namespace Kalburator::Todo
