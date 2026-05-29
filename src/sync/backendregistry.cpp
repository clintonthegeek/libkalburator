#include "backendregistry.h"
#include "backendcontribution.h"

namespace Kalburator::Sync {

BackendRegistry::BackendRegistry(QObject *parent)
    : QObject(parent)
{
}

void BackendRegistry::registerBackendInstance(const QString &backendId, SyncBackendBase *backend)
{
    m_instances[backendId] = backend;
    emit backendInstanceRegistered(backendId);
}

void BackendRegistry::unregisterBackendInstance(const QString &backendId)
{
    if (m_instances.remove(backendId)) {
        emit backendInstanceUnregistered(backendId);
    }
}

SyncBackendBase* BackendRegistry::backendInstance(const QString &backendId) const
{
    return m_instances.value(backendId, nullptr);
}

QStringList BackendRegistry::registeredInstanceIds() const
{
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
    m_instances.clear();
    m_contributions.clear();
}

} // namespace Kalburator::Sync
