#include "backendregistry.h"
#include "backendcontribution.h"
#include "syncbackend.h"

namespace Kalburator::Sync {

BackendRegistry::BackendRegistry(QObject *parent)
    : QObject(parent)
{
}

void BackendRegistry::registerBackendType(const QString &typeName, BackendFactory factory)
{
    m_factories[typeName] = factory;
    emit backendTypeRegistered(typeName);
}

void BackendRegistry::unregisterBackendType(const QString &typeName)
{
    m_factories.remove(typeName);
}

bool BackendRegistry::hasBackendType(const QString &typeName) const
{
    return m_factories.contains(typeName);
}

QStringList BackendRegistry::registeredTypes() const
{
    return m_factories.keys();
}

SyncBackend* BackendRegistry::createBackend(const QString &typeName,
                                             const QVariantMap &config,
                                             QObject *parent) const
{
    auto it = m_factories.find(typeName);
    if (it == m_factories.end()) {
        return nullptr;
    }
    return it.value()(config, parent);
}

void BackendRegistry::registerBackendInstance(const QString &backendId, SyncBackend *backend)
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

SyncBackend* BackendRegistry::backendInstance(const QString &backendId) const
{
    return m_instances.value(backendId, nullptr);
}

QStringList BackendRegistry::registeredInstanceIds() const
{
    return m_instances.keys();
}

QString BackendRegistry::backendDisplayName(const QString &typeName)
{
    static const QMap<QString, QString> displayNames = {
        {QStringLiteral("local"), QObject::tr("Local ICS Calendar")},
        {QStringLiteral("orgmode"), QObject::tr("Org Mode Files")},
        {QStringLiteral("caldav"), QObject::tr("CalDAV Server")},
        {QStringLiteral("subscription"), QObject::tr("Subscription Calendar (Holidays)")},
        {QStringLiteral("akonadi"), QObject::tr("Akonadi (KDE PIM)")},
    };

    return displayNames.value(typeName, typeName);  // Fallback to type name if unknown
}

QList<BackendRegistry::BackendTypeInfo> BackendRegistry::availableBackendTypes() const
{
    QList<BackendTypeInfo> types;
    for (const QString &typeName : registeredTypes()) {
        BackendTypeInfo info;
        info.typeName = typeName;
        info.displayName = backendDisplayName(typeName);
        types.append(info);
    }
    return types;
}


bool BackendRegistry::registerContribution(std::shared_ptr<BackendContribution> contrib) {
    if (!contrib) return false;
    const QString type = contrib->backendType();
    if (m_contributions.contains(type)) return false;
    m_contributions.insert(type, std::move(contrib));
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

} // namespace Kalburator::Sync
