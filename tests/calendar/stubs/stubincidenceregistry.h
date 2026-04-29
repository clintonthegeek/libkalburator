#ifndef KALBURATOR_TEST_STUBINCIDENCEREGISTRY_H
#define KALBURATOR_TEST_STUBINCIDENCEREGISTRY_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

#include "datadomain.h"
#include "iincidenceregistry.h"

namespace Kalburator::Sync::Test {

/**
 * @brief In-memory IIncidenceRegistry for libkalburator integration tests.
 *
 * Records every registry mutation in an inspectable log. Lookups
 * (`incidence(uid, calendarId)`) consult the per-calendar map. Tests
 * that need to seed pre-existing incidences use addIncidence() like
 * any host would.
 */
class StubIncidenceRegistry : public IIncidenceRegistry
{
public:
    using Key = QPair<QString, QString>;  // (calendarId, uid)

    StubIncidenceRegistry() = default;
    ~StubIncidenceRegistry() override = default;

    // IIncidenceRegistry
    bool addIncidence(const KCalendarCore::Incidence::Ptr &inc,
                      const QString &calendarId,
                      const QString &backendType,
                      KCalendarCore::MemoryCalendar *sourceCal,
                      DataDomain dataDomain = DataDomain::Calendar) override;

    bool removeIncidenceFromCalendar(const QString &uid,
                                     const QString &calendarId) override;

    bool removeIncidence(const QString &uid,
                         const QString &calendarId,
                         const QDateTime &recurrenceId) override;

    bool updateIncidenceForCalendar(const KCalendarCore::Incidence::Ptr &inc,
                                    const QString &calendarId) override;

    void setIncidencesForCalendar(const QString &calendarId,
                                  const QString &backendType,
                                  KCalendarCore::MemoryCalendar *sourceCalendar,
                                  const QVector<KCalendarCore::Incidence::Ptr> &incidences,
                                  DataDomain dataDomain = DataDomain::Calendar) override;

    void clear() override;

    // Test inspection
    int totalCount() const { return m_byKey.size(); }
    KCalendarCore::Incidence::Ptr lookup(const QString &calendarId,
                                          const QString &uid) const;
    int  callsToAdd() const    { return m_callsAdd; }
    int  callsToUpdate() const { return m_callsUpdate; }
    int  callsToRemove() const { return m_callsRemove; }
    int  callsToBulkSet() const { return m_callsBulkSet; }

private:
    QHash<Key, KCalendarCore::Incidence::Ptr> m_byKey;
    int m_callsAdd = 0;
    int m_callsUpdate = 0;
    int m_callsRemove = 0;
    int m_callsBulkSet = 0;
};

} // namespace Kalburator::Sync::Test

#endif // KALBURATOR_TEST_STUBINCIDENCEREGISTRY_H
