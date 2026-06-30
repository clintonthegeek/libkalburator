#pragma once

#include <KCalendarCore/Todo>
#include <QByteArray>
#include <QJsonObject>

namespace Kalburator::Todo {

/// Map a parsed VTODO to canon JSON fields (NO envelope). `originalBytes` is the
/// source iCal, used to extract RRULE/RDATE/EXDATE verbatim (invariant 3).
QJsonObject todoFieldsToCanon(const KCalendarCore::Todo::Ptr& todo,
                              const QByteArray& originalBytes);

/// Build full VTODO iCal bytes from canon JSON (reads "uid" etc.).
QByteArray canonObjectToVtodoBytes(const QJsonObject& obj);

}  // namespace Kalburator::Todo
