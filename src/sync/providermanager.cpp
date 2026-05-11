#include "providermanager.h"

#include "iprovider.h"
#include "iblobbackend.h"
#include "backendregistry.h"
#include "backendcontribution.h"
#include "syncbackend.h"
#include "backendconfiguration.h"
#include "caldavprovider.h"
#include "carddavprovider.h"   // Phase Ib

#include <KConfigGroup>

#include <QDebug>
#include <QtConcurrent>
#include <QFutureSynchronizer>

#include <algorithm>

namespace {

// Built-in contributions for the two production provider kinds.
// Registered into the BackendRegistry by ProviderManager's constructor
// when no contribution for those kinds already exists.

class CalDavBackendContribution : public Kalburator::Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("caldav"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IProvider>
    createProvider(QObject *parent) const override {
        return std::make_unique<Kalburator::Sync::CalDavProvider>(parent);
    }
};

class CardDavBackendContribution : public Kalburator::Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("carddav"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IProvider>
    createProvider(QObject *parent) const override {
        return std::make_unique<Kalburator::Sync::CardDavProvider>(parent);
    }
};

} // anon namespace

namespace Kalburator::Sync {

ProviderManager::ProviderManager(BackendRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    Q_ASSERT(m_registry);

    // Register built-in contributions for caldav and carddav if not already
    // present. A plugin or test can pre-register its own contribution for
    // these kinds before constructing ProviderManager to override these.
    if (!m_registry->contributionFor(QStringLiteral("caldav"))) {
        m_registry->registerContribution(
            std::make_shared<CalDavBackendContribution>());
    }
    if (!m_registry->contributionFor(QStringLiteral("carddav"))) {
        m_registry->registerContribution(
            std::make_shared<CardDavBackendContribution>());
    }
}

ProviderManager::~ProviderManager()
{
    disconnectAll();
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
        auto *contribution = m_registry->contributionFor(kind);
        if (!contribution) {
            qWarning() << "[ProviderManager] no contribution for kind" << kind;
            continue;
        }
        auto provider = contribution->createProvider();
        if (!provider) {
            qWarning() << "[ProviderManager] contribution returned null provider for kind" << kind;
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
