#include "isynchost.h"

#include "../sync/backendregistry.h"
#include "syncbackend.h"

namespace Kalburator::Sync {

void ISyncHost::setBackendRegistry(BackendRegistry *registry)
{
    m_backendRegistry = registry;
}

SyncBackend* ISyncHost::backendById(const QString &id)
{
    if (!m_backendRegistry) {
        return nullptr;
    }
    // dynamic_cast (not static_cast): a non-calendar instance is a clean
    // nullptr, never UB — the hazard the Plan 8 RFC retires.
    return dynamic_cast<SyncBackend*>(m_backendRegistry->backendInstance(id));
}

QHash<QString, SyncBackend*> ISyncHost::backends()
{
    QHash<QString, SyncBackend*> result;
    if (!m_backendRegistry) {
        return result;
    }
    const QStringList ids = m_backendRegistry->registeredInstanceIds();
    for (const QString &id : ids) {
        if (auto *calendarBackend = dynamic_cast<SyncBackend*>(
                m_backendRegistry->backendInstance(id))) {
            result.insert(id, calendarBackend);
        }
    }
    return result;
}

} // namespace Kalburator::Sync
