#pragma once

#include "lossprofile.h"
#include "transformationedge.h"

namespace Kalburator::Calendar {

/// Transforms a VEVENT iCalendar byte-string to canon JSON (lossless).
class ICalToCanonStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& icalBytes) const override;
};

/// Transforms canon JSON bytes to a VEVENT iCalendar string (lossy).
class CanonToICalStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& canonBytes) const override;
};

/// LossProfile for the canon → ical demote direction when the record's
/// `_canon.kind` is the default/untagged case (VEVENT, per
/// icalcanonstages.cpp's own convention of omitting the kind key for it).
Kalburator::Shape::LossProfile canonToIcalLoss();

/// IP.9 / O88 — LossProfile for the canon → ical demote direction when
/// the record's `_canon.kind` is "vtodo". The calendar domain's
/// {calendar,canon}→{calendar,ical} edge is kind-polymorphic
/// (CanonToICalStage::transform() dispatches on kind to three different
/// emitters); canonToIcalLoss() above is entirely event-shaped and must
/// NOT be applied to a VTODO. Declared here, next to the kind dispatch
/// that needs it, rather than in vtodocanonfields.{h,cpp} — this is a
/// DIFFERENT profile from Kalburator::Todo::canonToVtodoLoss()
/// (src/todo/vtodocanonstages.h), which governs the {todo,canon}→
/// {todo,vtodo} edge; see the IP.9 return receipt for why the two legs'
/// declared losses currently differ even though they share one emitter.
Kalburator::Shape::LossProfile canonToVtodoIcalLoss();

}  // namespace Kalburator::Calendar
