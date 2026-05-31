#pragma once

// LogicalCalendar JSON codec. Extracted from src/types/logicalcalendar.h in Plan 5
// (AUDIT B2: the type vocabulary must not carry JSON). The LogicalCalendar value
// type stays in types/; only its serialization lives here, in the light TypeSupport
// target. Valid downward dependency: typesupport/ -> types/.

#include "logicalcalendar.h"   // BackendRole, CalendarBackendBinding, LogicalCalendar

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace Kalburator::Sync {

QString backendRoleToString(BackendRole role);
BackendRole backendRoleFromString(const QString &str);
QJsonObject calendarBindingToJson(const CalendarBackendBinding &binding);
CalendarBackendBinding calendarBindingFromJson(const QJsonObject &obj);
QJsonObject logicalCalendarToJson(const LogicalCalendar &cal);
LogicalCalendar logicalCalendarFromJson(const QJsonObject &obj);
QJsonArray logicalCalendarsToJson(const QList<LogicalCalendar> &calendars);
QList<LogicalCalendar> logicalCalendarsFromJson(const QJsonArray &arr);

} // namespace Kalburator::Sync
