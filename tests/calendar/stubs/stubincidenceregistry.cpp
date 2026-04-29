#include "stubincidenceregistry.h"

namespace Kalburator::Sync::Test {

bool StubIncidenceRegistry::addIncidence(const KCalendarCore::Incidence::Ptr &inc,
                                         const QString &calendarId,
                                         const QString & /*backendType*/,
                                         KCalendarCore::MemoryCalendar * /*sourceCal*/,
                                         DataDomain /*dataDomain*/)
{
    if (!inc) return false;
    ++m_callsAdd;
    m_byKey.insert(qMakePair(calendarId, inc->uid()), inc);
    return true;
}

bool StubIncidenceRegistry::removeIncidenceFromCalendar(const QString &uid,
                                                        const QString &calendarId)
{
    ++m_callsRemove;
    return m_byKey.remove(qMakePair(calendarId, uid)) > 0;
}

bool StubIncidenceRegistry::removeIncidence(const QString &uid,
                                            const QString &calendarId,
                                            const QDateTime & /*recurrenceId*/)
{
    ++m_callsRemove;
    return m_byKey.remove(qMakePair(calendarId, uid)) > 0;
}

bool StubIncidenceRegistry::updateIncidenceForCalendar(const KCalendarCore::Incidence::Ptr &inc,
                                                       const QString &calendarId)
{
    if (!inc) return false;
    ++m_callsUpdate;
    m_byKey.insert(qMakePair(calendarId, inc->uid()), inc);
    return true;
}

void StubIncidenceRegistry::setIncidencesForCalendar(const QString &calendarId,
                                                     const QString & /*backendType*/,
                                                     KCalendarCore::MemoryCalendar * /*sourceCalendar*/,
                                                     const QVector<KCalendarCore::Incidence::Ptr> &incidences,
                                                     DataDomain /*dataDomain*/)
{
    ++m_callsBulkSet;
    // Drop existing entries for this calendar before bulk-loading.
    for (auto it = m_byKey.begin(); it != m_byKey.end(); ) {
        if (it.key().first == calendarId) {
            it = m_byKey.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto &inc : incidences) {
        if (!inc) continue;
        m_byKey.insert(qMakePair(calendarId, inc->uid()), inc);
    }
}

void StubIncidenceRegistry::clear()
{
    m_byKey.clear();
    m_callsAdd = m_callsUpdate = m_callsRemove = m_callsBulkSet = 0;
}

KCalendarCore::Incidence::Ptr
StubIncidenceRegistry::lookup(const QString &calendarId, const QString &uid) const
{
    return m_byKey.value(qMakePair(calendarId, uid));
}

} // namespace Kalburator::Sync::Test
