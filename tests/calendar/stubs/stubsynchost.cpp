#include "stubsynchost.h"

#include <algorithm>

#include "backendregistry.h"

namespace Kalburator::Sync::Test {

StubSyncHost::StubSyncHost(BackendRegistry *registry,
                           IIncidenceSource *source)
    : m_backendRegistry(registry)
    , m_source(source)
    , m_collection(std::make_unique<StubCalendarCollection>())
    , m_registry(std::make_unique<StubIncidenceRegistry>())
    , m_config(std::make_unique<StubSyncConfigStore>())
{
}

StubSyncHost::~StubSyncHost() = default;

SyncBackend* StubSyncHost::backendById(const QString &id)
{
    return m_backendRegistry ? m_backendRegistry->backendInstance(id) : nullptr;
}

QHash<QString, SyncBackend*> StubSyncHost::backends()
{
    QHash<QString, SyncBackend*> result;
    if (!m_backendRegistry) return result;
    for (const QString &id : m_backendRegistry->registeredInstanceIds()) {
        result.insert(id, m_backendRegistry->backendInstance(id));
    }
    return result;
}

bool StubSyncHost::applyIncidenceAddition(const QString &calendarId,
                                          const KCalendarCore::Incidence::Ptr &inc,
                                          bool stageForSync)
{
    m_appliedChanges.append({AppliedChange::Kind::Add, calendarId,
                             inc ? inc->uid() : QString(), inc, stageForSync});
    return true;
}

bool StubSyncHost::applyIncidenceRemoval(const QString &calendarId,
                                         const QString &uid,
                                         bool stageForSync,
                                         const QDateTime & /*recurrenceId*/)
{
    m_appliedChanges.append({AppliedChange::Kind::Remove, calendarId, uid,
                             {}, stageForSync});
    return true;
}

bool StubSyncHost::applyIncidenceUpdate(const QString &calendarId,
                                        const KCalendarCore::Incidence::Ptr &inc,
                                        bool stageForSync)
{
    m_appliedChanges.append({AppliedChange::Kind::Update, calendarId,
                             inc ? inc->uid() : QString(), inc, stageForSync});
    return true;
}

int StubSyncHost::appliedAdditionCount() const
{
    return static_cast<int>(std::count_if(m_appliedChanges.cbegin(),
                                          m_appliedChanges.cend(),
        [](const AppliedChange &c) { return c.kind == AppliedChange::Kind::Add; }));
}

int StubSyncHost::appliedRemovalCount() const
{
    return static_cast<int>(std::count_if(m_appliedChanges.cbegin(),
                                          m_appliedChanges.cend(),
        [](const AppliedChange &c) { return c.kind == AppliedChange::Kind::Remove; }));
}

int StubSyncHost::appliedUpdateCount() const
{
    return static_cast<int>(std::count_if(m_appliedChanges.cbegin(),
                                          m_appliedChanges.cend(),
        [](const AppliedChange &c) { return c.kind == AppliedChange::Kind::Update; }));
}

} // namespace Kalburator::Sync::Test
