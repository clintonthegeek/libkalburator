#include "stubsyncconfigstore.h"

namespace Kalburator::Sync::Test {

void StubSyncConfigStore::addLogicalCalendar(const LogicalCalendar &logCal)
{
    m_logicalCalendars.insert(logCal.id, logCal);
}

void StubSyncConfigStore::updateLogicalCalendar(const LogicalCalendar &logCal)
{
    m_logicalCalendars.insert(logCal.id, logCal);
}

void StubSyncConfigStore::removeLogicalCalendar(const QString &logicalCalendarId)
{
    m_logicalCalendars.remove(logicalCalendarId);
}

LogicalCalendar
StubSyncConfigStore::logicalCalendar(const QString &logicalCalendarId) const
{
    return m_logicalCalendars.value(logicalCalendarId);
}

QVariantMap StubSyncConfigStore::backendConfig(const QString &backendId) const
{
    return m_backendConfigs.value(backendId);
}

} // namespace Kalburator::Sync::Test
