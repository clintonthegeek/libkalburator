#include "providermanager.h"

#include "iprovider.h"
#include "iblobbackend.h"
#include "backendregistry.h"
#include "syncbackend.h"
#include "backendconfiguration.h"

#include <KConfigGroup>

#include <QDebug>
#include <QtConcurrent>
#include <QFutureSynchronizer>

#include <algorithm>

namespace Kalburator::Sync {

ProviderManager::ProviderManager(BackendRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    Q_ASSERT(m_registry);

    // Default factory: handles built-in provider kinds. CalDavProvider
    // is wired in Phase H Task 4 — until then, the default factory
    // returns nullptr for "caldav" and tests inject their own factory.
    m_factory = [](const QString & /*kind*/) -> std::unique_ptr<IProvider> {
        return nullptr;
    };
}

ProviderManager::~ProviderManager()
{
    disconnectAll();
}

void ProviderManager::setFactoryForTest(Factory factory)
{
    m_factory = std::move(factory);
}

void ProviderManager::wireProviderSignals(IProvider *provider)
{
    QObject::connect(provider, &IProvider::connectionStateChanged,
                     this, &ProviderManager::onProviderConnectionStateChanged);
    QObject::connect(provider, &IProvider::collectionsChanged,
                     this, &ProviderManager::onProviderCollectionsChanged);
}

void ProviderManager::loadFromProfile(const KConfigGroup &providersGroup)
{
    const QStringList ids = providersGroup.groupList();
    for (const QString &id : ids) {
        const KConfigGroup sub = providersGroup.group(id);
        const QString kind = sub.readEntry("kind", QString());
        if (kind.isEmpty()) {
            qWarning() << "[ProviderManager] skipping" << id
                       << "— missing 'kind' key";
            continue;
        }
        auto provider = m_factory(kind);
        if (!provider) {
            qWarning() << "[ProviderManager] no factory for kind"
                       << kind << "— skipping" << id;
            continue;
        }

        BackendConfiguration cfg;
        cfg.id = id;
        cfg.displayName = sub.readEntry("displayName", QString());
        const QStringList keys = sub.keyList();
        for (const QString &k : keys) {
            if (k == QLatin1String("kind") || k == QLatin1String("displayName")) continue;
            cfg.connectionParams[k] = sub.readEntry(k, QString());
        }
        provider->load(cfg);

        wireProviderSignals(provider.get());
        m_providers.push_back(std::move(provider));
    }
    emit providersChanged();
}

void ProviderManager::saveToProfile(KConfigGroup &providersGroup) const
{
    const QStringList existing = providersGroup.groupList();
    for (const QString &id : existing) {
        providersGroup.deleteGroup(id);
    }

    for (const auto &p : m_providers) {
        KConfigGroup sub = providersGroup.group(p->id());
        sub.writeEntry("kind", p->kind());
        sub.writeEntry("displayName", p->displayName());
        const auto cfg = p->save();
        for (auto it = cfg.connectionParams.constBegin();
             it != cfg.connectionParams.constEnd(); ++it) {
            sub.writeEntry(it.key(), it.value().toString());
        }
    }
    providersGroup.sync();
}

void ProviderManager::addProvider(std::unique_ptr<IProvider> provider)
{
    if (!provider) return;
    wireProviderSignals(provider.get());
    m_providers.push_back(std::move(provider));
    emit providersChanged();
}

void ProviderManager::removeProvider(const QString &providerId)
{
    auto it = std::find_if(m_providers.begin(), m_providers.end(),
        [&](const auto &p) { return p->id() == providerId; });
    if (it == m_providers.end()) return;

    if ((*it)->isConnected()) {
        unregisterProviderBackends(it->get());
        (*it)->disconnect();
    }
    m_providers.erase(it);
    emit providersChanged();
}

QFuture<void> ProviderManager::connectAll()
{
    auto sync = std::make_shared<QFutureSynchronizer<bool>>();
    for (const auto &p : m_providers) {
        if (!p->isConnected()) {
            sync->addFuture(p->connect());
        }
    }
    sync->setCancelOnWait(false);
    return QtConcurrent::run([sync]() {
        sync->waitForFinished();
    });
}

void ProviderManager::disconnectAll()
{
    for (const auto &p : m_providers) {
        if (p->isConnected()) {
            unregisterProviderBackends(p.get());
            p->disconnect();
        }
    }
}

QList<IProvider*> ProviderManager::providers() const
{
    QList<IProvider*> out;
    out.reserve(m_providers.size());
    for (const auto &p : m_providers) out.append(p.get());
    return out;
}

IProvider *ProviderManager::providerById(const QString &id) const
{
    for (const auto &p : m_providers) {
        if (p->id() == id) return p.get();
    }
    return nullptr;
}

void ProviderManager::onProviderConnectionStateChanged(bool connected)
{
    auto *provider = qobject_cast<IProvider*>(sender());
    if (!provider) return;
    if (connected) {
        registerProviderBackends(provider);
    } else {
        unregisterProviderBackends(provider);
    }
    emit providerConnectionStateChanged(provider->id(), connected);
}

void ProviderManager::onProviderCollectionsChanged()
{
    auto *provider = qobject_cast<IProvider*>(sender());
    if (!provider) return;
    unregisterProviderBackends(provider);
    if (provider->isConnected()) {
        registerProviderBackends(provider);
    }
    emit providersChanged();
}

void ProviderManager::registerProviderBackends(IProvider *provider)
{
    const auto cols = provider->collections();
    for (const auto &col : cols) {
        const QString backendId =
            QStringLiteral("%1:%2").arg(provider->id(), col.id);
        auto backend = provider->createBackend(col.id);
        if (!backend) continue;

        // BackendRegistry stores SyncBackend* (which inherits IBlobBackend).
        // For Phase H, all provider-produced backends derive SyncBackend.
        // dynamic_cast verifies that contract; if a future provider returns
        // a pure-IBlobBackend, registry needs an IBlobBackend-flavored API
        // — defer that until it actually happens.
        auto *asSync = dynamic_cast<SyncBackend*>(backend.get());
        if (!asSync) {
            qWarning() << "[ProviderManager] provider" << provider->id()
                       << "produced a non-SyncBackend for collection" << col.id
                       << "— cannot register with BackendRegistry. Skipping.";
            continue;
        }
        m_registry->registerBackendInstance(backendId, asSync);
        m_ownedBackends.insert_or_assign(backendId, std::move(backend));
    }
}

void ProviderManager::unregisterProviderBackends(IProvider *provider)
{
    const QString prefix = provider->id() + QLatin1Char(':');
    QStringList toRemove;
    for (const auto &kv : m_ownedBackends) {
        if (kv.first.startsWith(prefix)) {
            toRemove.append(kv.first);
        }
    }
    for (const QString &id : toRemove) {
        m_registry->unregisterBackendInstance(id);
        m_ownedBackends.erase(id);
    }
}

} // namespace Kalburator::Sync
