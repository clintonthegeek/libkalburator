#pragma once

#include <KCalendarCore/Event>
#include <QByteArray>
#include <QJsonObject>

namespace Kalburator::Calendar {

/// Map a parsed VEVENT to canon JSON fields (NO envelope). `originalBytes` is the
/// source iCal, used to extract RRULE/RDATE/EXDATE verbatim (invariant 3).
QJsonObject eventFieldsToCanon(const KCalendarCore::Event::Ptr& event,
                               const QByteArray& originalBytes);

/// Build full VEVENT iCal bytes from canon JSON (reads "uid" etc.).
QByteArray canonObjectToEventBytes(const QJsonObject& obj);

}  // namespace Kalburator::Calendar
