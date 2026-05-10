#pragma once
#include "canonicalrecord.h"
#include "shape.h"
#include <QString>

inline Kalburator::Shape::CanonicalRecord calendarTestRec(const QString &uid,
                                                          const QString &ical)
{
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")}};
    rec.data     = ical.toUtf8();
    return rec;
}
