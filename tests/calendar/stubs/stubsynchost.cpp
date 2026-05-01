#include "stubsynchost.h"

#include "backendregistry.h"

namespace Kalburator::Sync::Test {

StubSyncHost::StubSyncHost(BackendRegistry *registry)
    : m_backendRegistry(registry)
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

void StubSyncHost::recordChanged(const QString &mappingId,
                                 const QString &recordId,
                                 ChangeKind kind)
{
    Q_UNUSED(mappingId)
    Q_UNUSED(recordId)
    ++m_recordChangedCount;
    switch (kind) {
        case ChangeKind::Created: ++m_createdCount; break;
        case ChangeKind::Updated: ++m_updatedCount; break;
        case ChangeKind::Deleted: ++m_deletedCount; break;
    }
}

int StubSyncHost::recordChangedCount(ChangeKind kind) const
{
    switch (kind) {
        case ChangeKind::Created: return m_createdCount;
        case ChangeKind::Updated: return m_updatedCount;
        case ChangeKind::Deleted: return m_deletedCount;
    }
    return 0;
}

} // namespace Kalburator::Sync::Test
