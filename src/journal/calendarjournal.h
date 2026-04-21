#ifndef CALENDARJOURNAL_H
#define CALENDARJOURNAL_H

#include "crashjournal.h"
#include <KCalendarCore/Incidence>
#include <QString>

namespace Kalburator::Sync {

/**
 * @brief Crash-recovery journal for calendar data.
 *
 * Journals staging operations (create/update/delete) at edit time using
 * full iCalendar serialization. On startup, replayed entries are re-staged
 * into StagingController so the next autosave tick flushes them to disk.
 *
 * Per-calendar journal files, truncated after successful backend write.
 */
class CalendarJournal {
public:
    explicit CalendarJournal(const QString &journalDirectory);

    /** Journal a creation — stores full iCal text. */
    void appendCreation(const QString &calendarId,
                        const KCalendarCore::Incidence::Ptr &item);

    /** Journal an update — stores full iCal text. */
    void appendUpdate(const QString &calendarId,
                      const KCalendarCore::Incidence::Ptr &item);

    /** Journal a deletion — stores UID and optional recurrenceId. */
    void appendDeletion(const QString &calendarId,
                        const QString &uid,
                        const QString &recurrenceId = {});

    /** Delete the journal file for @p calendarId after successful sync. */
    void truncate(const QString &calendarId);

    /** True if a non-empty journal exists for @p calendarId. */
    bool hasJournal(const QString &calendarId) const;

    /** Return calendar IDs that have non-empty journal files. */
    QStringList calendarsWithJournals() const;

    /**
     * Replay journal entries for @p calendarId.
     * The handler receives raw JSON entries with fields: op, uid, ical, recurrenceId.
     * Returns the number of entries replayed.
     */
    int replay(const QString &calendarId,
               const std::function<void(const QJsonObject &)> &handler) const;

private:
    CrashJournal m_journal;
};

} // namespace Kalburator::Sync

#endif // CALENDARJOURNAL_H
