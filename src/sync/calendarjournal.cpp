#include "calendarjournal.h"
#include <KCalendarCore/ICalFormat>
#include <QJsonObject>

namespace Kalburator::Sync {

CalendarJournal::CalendarJournal(const QString &journalDirectory)
    : m_journal(journalDirectory, QStringLiteral(".calendar.journal"))
{
}

void CalendarJournal::appendCreation(const QString &calendarId,
                                     const KCalendarCore::Incidence::Ptr &item)
{
    if (!item) return;

    KCalendarCore::ICalFormat format;
    QJsonObject entry;
    entry[QStringLiteral("op")] = QStringLiteral("create");
    entry[QStringLiteral("uid")] = item->uid();
    entry[QStringLiteral("ical")] = format.toICalString(item);
    m_journal.append(calendarId, entry);
}

void CalendarJournal::appendUpdate(const QString &calendarId,
                                   const KCalendarCore::Incidence::Ptr &item)
{
    if (!item) return;

    KCalendarCore::ICalFormat format;
    QJsonObject entry;
    entry[QStringLiteral("op")] = QStringLiteral("update");
    entry[QStringLiteral("uid")] = item->uid();
    entry[QStringLiteral("ical")] = format.toICalString(item);
    m_journal.append(calendarId, entry);
}

void CalendarJournal::appendDeletion(const QString &calendarId,
                                     const QString &uid,
                                     const QString &recurrenceId)
{
    QJsonObject entry;
    entry[QStringLiteral("op")] = QStringLiteral("delete");
    entry[QStringLiteral("uid")] = uid;
    if (!recurrenceId.isEmpty()) {
        entry[QStringLiteral("recurrenceId")] = recurrenceId;
    }
    m_journal.append(calendarId, entry);
}

void CalendarJournal::truncate(const QString &calendarId)
{
    m_journal.truncate(calendarId);
}

bool CalendarJournal::hasJournal(const QString &calendarId) const
{
    return m_journal.hasJournal(calendarId);
}

QStringList CalendarJournal::calendarsWithJournals() const
{
    return m_journal.entitiesWithJournals();
}

int CalendarJournal::replay(const QString &calendarId,
                            const std::function<void(const QJsonObject &)> &handler) const
{
    return m_journal.replay(calendarId, handler);
}


} // namespace Kalburator::Sync
