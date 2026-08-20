#include "backendregistry.h"
#include "backendcontribution.h"

namespace Kalburator::Sync {

BackendRegistry::BackendRegistry(QObject *parent)
    : QObject(parent)
{
}

void BackendRegistry::registerBackendInstance(const QString &backendId, SyncBackendBase *backend)
{
    {
        QMutexLocker locker(&m_instancesMutex);
        m_instances[backendId] = backend;
    }
    // Emit outside the lock — a connected slot re-entering the registry
    // (e.g. backendInstance()) must never deadlock on its own mutex.
    emit backendInstanceRegistered(backendId);
}

void BackendRegistry::unregisterBackendInstance(const QString &backendId)
{
    bool removed = false;
    {
        QMutexLocker locker(&m_instancesMutex);
        removed = m_instances.remove(backendId) > 0;
    }
    if (removed) {
        emit backendInstanceUnregistered(backendId);
    }
}

SyncBackendBase* BackendRegistry::backendInstance(const QString &backendId) const
{
    QMutexLocker locker(&m_instancesMutex);
    return m_instances.value(backendId, nullptr);
}

QStringList BackendRegistry::registeredInstanceIds() const
{
    QMutexLocker locker(&m_instancesMutex);
    return m_instances.keys();
}

bool BackendRegistry::registerContribution(std::shared_ptr<BackendContribution> contrib) {
    if (!contrib) return false;
    const QString type = contrib->backendType();
    if (m_contributions.contains(type)) return false;
    m_contributions.insert(type, std::move(contrib));
    emit contributionRegistered(type);
    return true;
}

BackendContribution* BackendRegistry::contributionFor(const QString &type) const {
    auto it = m_contributions.find(type);
    return (it == m_contributions.end()) ? nullptr : it.value().get();
}

QList<BackendContribution*> BackendRegistry::contributions() const {
    QList<BackendContribution*> out;
    out.reserve(m_contributions.size());
    for (const auto &c : m_contributions) out.append(c.get());
    return out;
}

void BackendRegistry::unregisterContribution(const QString &typeName) {
    if (m_contributions.remove(typeName) > 0) {
        emit contributionUnregistered(typeName);
    }
}

void BackendRegistry::clear() {
    {
        QMutexLocker locker(&m_instancesMutex);
        m_instances.clear();
    }
    m_contributions.clear();
}

} // namespace Kalburator::Sync
