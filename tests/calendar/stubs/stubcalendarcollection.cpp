#include "stubcalendarcollection.h"

namespace Kalburator::Sync::Test {

StubCalendarCollection::StubCalendarCollection(QString id)
    : m_id(std::move(id))
{
}

StubCalendarCollection::~StubCalendarCollection()
{
    qDeleteAll(m_calendars);
}

KCalendarCore::MemoryCalendar*
StubCalendarCollection::calendar(const QString &calendarId) const
{
    return m_calendars.value(calendarId, nullptr);
}

QList<KCalendarCore::MemoryCalendar*> StubCalendarCollection::calendars() const
{
    return m_calendars.values();
}

void StubCalendarCollection::addCalendar(KCalendarCore::MemoryCalendar *cal)
{
    if (!cal) return;
    const QString key = deriveKey(cal);
    m_calendars.insert(key, cal);
}

void StubCalendarCollection::addCalendarWithId(const QString &id,
                                               KCalendarCore::MemoryCalendar *cal)
{
    if (!cal || id.isEmpty()) return;
    m_calendars.insert(id, cal);
}

void StubCalendarCollection::setCalendarColor(const QString &calendarId,
                                              const QColor &color)
{
    m_colors[calendarId] = color;
}

void StubCalendarCollection::setCalendarVisible(const QString &calendarId,
                                                bool visible)
{
    m_visibles[calendarId] = visible;
}

QString StubCalendarCollection::deriveKey(KCalendarCore::MemoryCalendar *cal)
{
    QString key = cal->productId();
    if (key.isEmpty()) {
        key = QStringLiteral("cal-%1")
                  .arg(reinterpret_cast<quintptr>(cal), 0, 16);
    }
    return key;
}

} // namespace Kalburator::Sync::Test
