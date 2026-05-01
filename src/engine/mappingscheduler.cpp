#include "mappingscheduler.h"

namespace Kalburator::Sync {

void MappingScheduler::enqueue(const QStringList &mappingIds,
                                const QHash<QString, QSet<QString>> &resourceMap,
                                DispatchFn dispatch)
{
    m_dispatch = std::move(dispatch);
    for (const QString &id : mappingIds) {
        m_pending.enqueue(id);
        if (resourceMap.contains(id))
            m_resources[id] = resourceMap[id];
    }
    tryDispatch();
}

void MappingScheduler::onCompleted(const QString &mappingId)
{
    if (m_active == mappingId) {
        m_resources.remove(m_active);
        m_active.clear();
    }
    tryDispatch();
}

QStringList MappingScheduler::cancelMappingsTouchingResource(const QString &resourceId)
{
    QStringList cancelled;
    QQueue<QString> remaining;
    for (const QString &id : std::as_const(m_pending)) {
        const QSet<QString> &res = m_resources.value(id);
        if (res.contains(resourceId)) {
            cancelled << id;
            m_resources.remove(id);
        } else {
            remaining.enqueue(id);
        }
    }
    m_pending = std::move(remaining);
    return cancelled;
}

QSet<QString> MappingScheduler::activeResources() const
{
    if (m_active.isEmpty())
        return {};
    return m_resources.value(m_active);
}

void MappingScheduler::tryDispatch()
{
    if (m_pending.isEmpty() || !m_active.isEmpty())
        return;
    m_active = m_pending.dequeue();
    if (m_dispatch)
        m_dispatch(m_active);
}

} // namespace Kalburator::Sync
