#ifndef KALBURATOR_TEST_STUBCALENDARCOLLECTION_H
#define KALBURATOR_TEST_STUBCALENDARCOLLECTION_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

#include <KCalendarCore/MemoryCalendar>

#include "icalendarcollection.h"

namespace Kalburator::Sync::Test {

/**
 * @brief In-memory ICalendarCollection for libkalburator integration tests.
 *
 * Owns the MemoryCalendar pointers passed to addCalendar(). Calendars
 * are keyed by an explicit id supplied via addCalendarWithId(); plain
 * addCalendar() falls back to KCalendarCore::Calendar::productId() and
 * finally to a pointer-derived stable string. Property setters
 * (color, visible) record into inspectable hashes.
 */
class StubCalendarCollection : public ICalendarCollection
{
public:
    explicit StubCalendarCollection(QString id = QStringLiteral("stub-collection"));
    ~StubCalendarCollection() override;

    // ICalendarCollection
    QString id() const override { return m_id; }
    KCalendarCore::MemoryCalendar* calendar(const QString &calendarId) const override;
    QList<KCalendarCore::MemoryCalendar*> calendars() const override;
    void addCalendar(KCalendarCore::MemoryCalendar *cal) override;
    void setCalendarColor(const QString &calendarId, const QColor &color) override;
    void setCalendarVisible(const QString &calendarId, bool visible) override;

    // Test helpers
    void addCalendarWithId(const QString &id, KCalendarCore::MemoryCalendar *cal);
    QColor recordedColor(const QString &calendarId) const { return m_colors.value(calendarId); }
    bool   recordedVisible(const QString &calendarId) const { return m_visibles.value(calendarId, true); }
    QStringList ids() const { return m_calendars.keys(); }

private:
    static QString deriveKey(KCalendarCore::MemoryCalendar *cal);

    QString m_id;
    QHash<QString, KCalendarCore::MemoryCalendar*> m_calendars; // owns the pointers
    QHash<QString, QColor> m_colors;
    QHash<QString, bool>   m_visibles;
};

} // namespace Kalburator::Sync::Test

#endif // KALBURATOR_TEST_STUBCALENDARCOLLECTION_H
