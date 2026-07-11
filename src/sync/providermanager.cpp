#include "providermanager.h"

#include "iprovider.h"
#include "iblobbackend.h"
#include "backendregistry.h"
#include "backendcontribution.h"
#include "syncbackendbase.h"
#include "backendconfiguration.h"

#include <KConfigGroup>

#include <QDebug>
#include <QtConcurrent>
#include <QFutureSynchronizer>
#include <QFutureWatcher>

#include <algorithm>


namespace Kalburator::Sync {

ProviderManager::ProviderManager(BackendRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    Q_ASSERT(m_registry);
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
    // m_providerStates is lazily populated — entry appears on first
    // connectionStateChanged from this provider, not at addProvider time.
    wireProviderSignals(provider.get());

    // A provider handed over already-connected (the Add Account flow:
    // ProviderConfigDialog drives connect() for calendar discovery before
    // the manager ever sees the provider) emitted connectionStateChanged(true)
    // with no subscribers. Re-run the work that emission would have driven —
    // otherwise connectAll()'s idempotency fast-path (providers return a
    // finished future without re-emitting) leaves the backends unregistered
    // and the state stuck at Connecting.
    if (provider->isConnected()) {
        registerProviderBackends(provider.get());
        m_providerStates[provider->id()] = ProviderConnectionState::Connected;
        emit providerStateChanged(provider->id(),
                                  ProviderConnectionState::Connected);
    }

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
    m_providerStates.remove(providerId);
    emit providersChanged();
}

QFuture<void> ProviderManager::connectAll()
{
    auto sync = std::make_shared<QFutureSynchronizer<bool>>();
    for (const auto &p : m_providers) {
        // Skip providers that are already connected or whose async connect is
        // in-flight (Connecting). m_providerStates is updated synchronously to
        // Connecting here so that a second connectAll() call (e.g. from
        // AccountController::addProvider()) does not re-launch discovery for
        // providers already being handled.
        const auto st = m_providerStates.value(
            p->id(), ProviderConnectionState::Disconnected);
        if (st == ProviderConnectionState::Connecting
            || st == ProviderConnectionState::Connected)
            continue;

        m_providerStates[p->id()] = ProviderConnectionState::Connecting;
        emit providerStateChanged(p->id(), ProviderConnectionState::Connecting);

        // If p->connect() fails (returns false), the provider will remain in
        // Connecting state forever because it won't emit connectionStateChanged(false)
        // (that signal only fires on explicit disconnect()). Attach a per-provider
        // future watcher to reset the state if the connect operation fails.
        // Parented to 'this' so Qt's parent-child mechanism cleans it up if
        // ProviderManager is destroyed before the future completes.
        auto future = p->connect();
        auto *watcher = new QFutureWatcher<bool>(this);
        const QString pid = p->id();
        QObject::connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, pid]() {
                // Defensive: a future canceled before a result was reported has
                // no entry in its result store; calling result() in that state
                // is UB and crashes inside QFutureInterface::resultReference.
                // This can happen if a second connect() call on the same
                // provider replaced its QPromise unfinished — providers must
                // be idempotent (return the in-flight future on re-entry), but
                // we still guard here so a buggy provider can't take the
                // manager down.
                const bool ok = !watcher->future().isCanceled()
                             && watcher->result();
                if (!ok) {
                    // Connect failed — reset state so retries are possible.
                    m_providerStates[pid] = ProviderConnectionState::Disconnected;
                    emit providerStateChanged(pid, ProviderConnectionState::Disconnected);
                }
                watcher->deleteLater();
            });
        watcher->setFuture(future);
        sync->addFuture(future);
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
        }
        p->disconnect();
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

ProviderConnectionState ProviderManager::providerState(const QString &id) const
{
    return m_providerStates.value(id, ProviderConnectionState::Disconnected);
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
    const ProviderConnectionState newState = connected
        ? ProviderConnectionState::Connected
        : ProviderConnectionState::Disconnected;
    m_providerStates[provider->id()] = newState;
    emit providerStateChanged(provider->id(), newState);
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

        // BackendRegistry stores SyncBackendBase* (which inherits IBlobBackend).
        // All provider-produced backends must derive SyncBackendBase (calendar
        // backends via SyncBackend; non-calendar backends directly).
        auto *asSync = dynamic_cast<SyncBackendBase*>(backend.get());
        if (!asSync) {
            qWarning() << "[ProviderManager] provider" << provider->id()
                       << "produced a non-SyncBackendBase for collection" << col.id
                       << "— cannot register with BackendRegistry. Skipping.";
            continue;
        }
        m_registry->registerBackendInstance(backendId, asSync);
        m_ownedBackends.insert_or_assign(backendId, std::move(backend));
    }
}

// PHASE1-TASK1.1 -> PHASE3-TASK3.1 — v2 wiring hook. Phase 1 only collected
// descriptors and logged a "Phase 1 stub" message; Phase 2 made real
// per-collection specs live; Phase 3 settled on the dual-pipeline
// configuration: v1 registerProviderBackends() continues to be the
// registration entry point (per-collection, 2-segment ids stable for
// engine + downstream callers), and the v2 entry returns the
// descriptors without taking over registration. The provider fanout
// collapse to per-domain specs (one backend per (provider, domain)
// with the new `<uuid>:cal` / `<uuid>:contacts` registry ids, no fanout)
// lands together with the PlanStan adoption sweep in Phase 4
// (per design §C + spec §B).
QList<ProviderBackendSpec>
ProviderManager::createBackendsForCollection(const QString &collectionId)
{
    // Find the provider that owns this collection. There is no global
    // cross-provider index yet — we walk the providers and ask each
    // whether collectionId appears in its collections() list.
    IProvider *owner = nullptr;
    for (const auto &p : m_providers) {
        if (!p->isConnected()) continue;
        const auto cols = p->collections();
        for (const auto &c : cols) {
            if (c.id == collectionId) {
                owner = p.get();
                break;
            }
        }
        if (owner) break;
    }
    if (!owner) {
        qDebug() << "[ProviderManager] createBackendsForCollection:"
                 << "no connected provider owns collectionId" << collectionId
                 << "— returning empty list.";
        return {};
    }

    const auto specs = owner->createBackends(collectionId);
    qDebug() << "[ProviderManager] createBackendsForCollection: provider"
             << owner->id() << "produced" << specs.size()
             << "spec(s) for collection" << collectionId;
    return specs;
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
