#include <kalburator/runtime/collectionruntime.h>

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPromise>
#include <QSet>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/plugin/stock_plugins.h>
#include <kalburator/shape/shaperegistries.h>
#include <kalburator/storage/baselinestore.h>
#include <kalburator/sync/backendregistry.h>
#include <kalburator/calendar/isynchost.h>
#include <kalburator/types/isyncconfigstore.h>
#include <kalburator/types/logicalcalendar.h>
#include <kalburator/engine/syncengine.h>
#include <kalburator/engine/imassdeleteguard.h>
#include <kalburator/engine/syncrequest.h>
#include <kalburator/sync/providermanager.h>
#include <kalburator/sync/credentialredaction.h>
#include <kalburator/sync/backendexecutor.h>
#include <kalburator/calendar/syncconflictstore.h>
#include <kalburator/calendar/syncbackend.h>
#include <kalburator/conflict/conflictmanager.h>

namespace Kalburator::Runtime {

BackendEndpoint::BackendEndpoint(std::unique_ptr<QObject> backend)
    : m_backend(std::move(backend))
{
}

QObject *BackendEndpoint::backendObject() const
{
    return m_backend.get();
}

std::unique_ptr<Kalburator::Sync::IBlobBackend> BackendEndpoint::takeBackend()
{
    auto *backend = dynamic_cast<Kalburator::Sync::IBlobBackend *>(m_backend.get());
    if (!backend) return {};
    m_backend.release();
    return std::unique_ptr<Kalburator::Sync::IBlobBackend>(backend);
}

bool RunSelection::validate(QString &errorMessage) const
{
    switch (kind) {
    case Kind::AllEnabled:
        if (!mappingIds.isEmpty()) break;
        return errorMessage.clear(), true;
    case Kind::ExactSet:
        if (mappingIds.isEmpty()) {
            errorMessage = QStringLiteral("exact mapping selection must not be empty");
            return false;
        }
        if (std::any_of(mappingIds.cbegin(), mappingIds.cend(),
                        [](const QString &id) { return id.isEmpty(); })) {
            errorMessage = QStringLiteral("exact mapping selection contains an empty id");
            return false;
        }
        return errorMessage.clear(), true;
    case Kind::One:
        if (mappingIds.size() == 1 && !mappingIds.first().isEmpty())
            return errorMessage.clear(), true;
        errorMessage = QStringLiteral("one mapping selection requires one id");
        return false;
    case Kind::None:
        if (mappingIds.isEmpty())
            return errorMessage.clear(), true;
        break;
    }
    errorMessage = QStringLiteral("mapping ids are not valid for this selection");
    return false;
}

bool RunRequest::validate(QString &errorMessage) const
{
    return selection.validate(errorMessage);
}

namespace detail {

bool removeRuntimeDatabaseArtifacts(const QString &databasePath, QString &errorMessage)
{
    const QStringList artifacts{databasePath, databasePath + QStringLiteral("-wal"),
                                databasePath + QStringLiteral("-shm"),
                                databasePath + QStringLiteral("-journal")};
    for (const auto &artifact : artifacts) {
        if (QFile::exists(artifact) && !QFile::remove(artifact)) {
            errorMessage = QStringLiteral("runtime database: could not remove ") + artifact;
            return false;
        }
    }
    return true;
}

class RuntimeConfigStore final : public Kalburator::Sync::ISyncConfigStore
{
public:
    void addLogicalCalendar(const Kalburator::Sync::LogicalCalendar &calendar) override
    { m_calendars.insert(calendar.id, calendar); }
    void updateLogicalCalendar(const Kalburator::Sync::LogicalCalendar &calendar) override
    { m_calendars.insert(calendar.id, calendar); }
    void removeLogicalCalendar(const QString &id) override { m_calendars.remove(id); }
    Kalburator::Sync::LogicalCalendar logicalCalendar(const QString &id) const override
    { return m_calendars.value(id); }
    QVariantMap backendConfig(const QString &id) const override
    { return m_backendConfigs.value(id); }
    bool hasSyncMappings() const override { return !m_mappings.isEmpty(); }
    QList<Kalburator::Sync::SyncMapping> syncMappings() const override { return m_mappings; }
    void save() override {}

private:
    QHash<QString, Kalburator::Sync::LogicalCalendar> m_calendars;
    QHash<QString, QVariantMap> m_backendConfigs;
    QList<Kalburator::Sync::SyncMapping> m_mappings;
};

class RuntimeSyncHost final : public Kalburator::Sync::ISyncHost
{
public:
    using RecordCallback = std::function<void(const RecordChange &)>;

    RuntimeSyncHost(Kalburator::Sync::BackendRegistry *registry,
                    RecordCallback callback)
        : m_recordCallback(std::move(callback))
    { setBackendRegistry(registry); }

    Kalburator::Sync::ISyncConfigStore *configStore() override
    { return &m_config; }

    void recordChanged(const RecordChange &change) override
    {
        if (m_recordCallback) m_recordCallback(change);
    }

private:
    RuntimeConfigStore m_config;
    RecordCallback m_recordCallback;
};

struct EventSinkState {
    QMutex mutex;
    std::function<void(const RuntimeEvent &)> sink;
};

class RuntimeMassDeleteGuard final : public Kalburator::Conflict::IMassDeleteGuard
{
public:
    explicit RuntimeMassDeleteGuard(
        std::function<bool(const QString &, const QString &, int, int)> callback)
        : m_callback(std::move(callback)) {}

    bool confirmMassDelete(const QString &mappingId, const QString &targetBackendId,
                           int proposedDeletes, int baselineCount) override
    {
        return m_callback && m_callback(mappingId, targetBackendId,
                                         proposedDeletes, baselineCount);
    }

private:
    std::function<bool(const QString &, const QString &, int, int)> m_callback;
};

class CollectionRuntimeImpl final : public CollectionRuntime
{
public:
    explicit CollectionRuntimeImpl(RuntimeDefinition definition)
        : m_definition(std::move(definition))
        , m_backendRegistry()
        , m_shape()
        , m_pluginManager(&m_backendRegistry, m_shape)
    {
        auto plugins = stockPluginItems();
        plugins.append(m_definition.pluginExtensions);
        if (!m_pluginManager.loadInProcess(plugins)) {
            m_constructionError = QStringLiteral("plugin extension registration failed");
        }
        m_providerManager = std::make_unique<Kalburator::Sync::ProviderManager>(
            &m_backendRegistry);
        QObject::connect(m_providerManager.get(),
                         &Kalburator::Sync::ProviderManager::providerStateChanged,
                         m_providerManager.get(),
                         [this](const QString &providerId,
                                Kalburator::Sync::ProviderConnectionState state) {
            refreshProviderSnapshot(providerId);
            const QString prefix = providerId + QLatin1Char(':');
            m_snapshot.backendIds.erase(
                std::remove_if(m_snapshot.backendIds.begin(), m_snapshot.backendIds.end(),
                               [&prefix](const QString &id) { return id.startsWith(prefix); }),
                m_snapshot.backendIds.end());
            if (state == Kalburator::Sync::ProviderConnectionState::Connected)
                m_snapshot.backendIds.append(
                    m_providerManager->backendIdsForProvider(providerId));
            RuntimeEvent event;
            event.kind = RuntimeEvent::Kind::ProviderStateChanged;
            event.objectId = providerId;
            event.providerId = providerId;
            event.providerState = state;
            const auto it = std::find_if(m_snapshot.providers.cbegin(),
                                         m_snapshot.providers.cend(),
                                         [&providerId](const auto &p) {
                                             return p.id == providerId;
                                         });
            if (it != m_snapshot.providers.cend()) {
                event.providerError = it->errorMessage;
                event.providerWarning = it->warningMessage;
                event.providerCollections = it->collections;
            }
            emitEvent(event);
                         });
        QObject::connect(m_providerManager.get(),
                         &Kalburator::Sync::ProviderManager::providerCollectionsChanged,
                         m_providerManager.get(),
                         [this](const QString &providerId,
                                const QList<Kalburator::Sync::CollectionInfo> &collections) {
            refreshProviderSnapshot(providerId);
            RuntimeEvent event;
            event.kind = RuntimeEvent::Kind::ProviderStateChanged;
            event.objectId = providerId;
            event.providerId = providerId;
            event.providerState = m_providerManager->providerState(providerId);
            event.providerCollections = collections;
            const auto it = std::find_if(m_snapshot.providers.cbegin(),
                                         m_snapshot.providers.cend(),
                                         [&providerId](const auto &p) { return p.id == providerId; });
            if (it != m_snapshot.providers.cend()) {
                event.providerError = it->errorMessage;
                event.providerWarning = it->warningMessage;
            }
            emitEvent(event);
                         });
        QObject::connect(m_providerManager.get(),
                         &Kalburator::Sync::ProviderManager::providerErrorChanged,
                         m_providerManager.get(),
                         [this](const QString &providerId, const QString &message) {
            refreshProviderSnapshot(providerId);
            RuntimeEvent event;
            event.kind = RuntimeEvent::Kind::ProviderStateChanged;
            event.objectId = providerId;
            event.providerId = providerId;
            event.providerState = m_providerManager->providerState(providerId);
            event.providerError = Kalburator::Sync::redactCredentials(message);
            emitEvent(event);
                         });
        for (const auto &config : m_definition.providers) {
            auto *contribution = m_backendRegistry.contributionFor(config.type);
            if (!contribution) {
                m_constructionError = QStringLiteral("no provider contribution for type: ")
                    + config.type;
                break;
            }
            auto provider = contribution->createProvider();
            if (!provider) {
                m_constructionError = QStringLiteral("provider contribution returned null: ")
                    + config.type;
                break;
            }
            provider->load(config);
            m_providerManager->addProvider(std::move(provider));
            m_snapshot.providerIds.append(config.id);
            refreshProviderSnapshot(config.id);
        }
        for (const auto &resource : m_definition.resources) {
            if (!resource.lease) continue;
            const QString resourceId = resource.id;
            resource.lease->setLossHandler([this, resourceId]() {
                const auto handleLoss = [this, resourceId]() {
                    if (!m_running || !m_engine) return;
                    if (!m_lostResourceIds.contains(resourceId))
                        m_lostResourceIds.append(resourceId);
                    m_engine->cancelWithReason(
                        Kalburator::Engine::CancellationReason::ResourceLost,
                        resourceId);
                };
                if (QThread::currentThread() == m_eventContext.thread())
                    handleLoss();
                else
                    QMetaObject::invokeMethod(&m_eventContext, handleLoss,
                                              Qt::QueuedConnection);
            });
        }
        QString storeError;
        if (!openStores(storeError))
            m_constructionError = storeError;
        m_host = std::make_unique<RuntimeSyncHost>(
            &m_backendRegistry,
            [this](const Kalburator::Sync::ISyncHost::RecordChange &change) {
                RuntimeEvent event;
                event.kind = RuntimeEvent::Kind::RecordChanged;
                event.objectId = change.recordId;
                event.mappingId = change.mappingId;
                event.backendId = change.backendId;
                event.collectionId = change.calendarId;
                event.recordId = change.recordId;
                switch (change.kind) {
                case Kalburator::Sync::ISyncHost::ChangeKind::Created:
                    event.changeKind = RecordChangeKind::Created;
                    break;
                case Kalburator::Sync::ISyncHost::ChangeKind::Updated:
                    event.changeKind = RecordChangeKind::Updated;
                    break;
                case Kalburator::Sync::ISyncHost::ChangeKind::Deleted:
                    event.changeKind = RecordChangeKind::Deleted;
                    break;
                }
                event.canonicalDomain = change.shape.domain.toString();
                event.canonicalEncoding = change.shape.encoding.toString();
                event.canonicalPayload = change.data;
                emitEvent(event);
            });
        configureEngine();
    }

    const QString &constructionError() const { return m_constructionError; }

    ~CollectionRuntimeImpl() override
    {
        QObject::disconnect(m_providerManager.get(), nullptr,
                            m_providerManager.get(), nullptr);
        for (const auto &resource : m_definition.resources)
            if (resource.lease) resource.lease->setLossHandler({});
        if (m_running) {
            cancel();
            // A backend dispatch may be waiting synchronously for a result
            // whose cancellation/completion is delivered through the main
            // event loop.  Let that path unwind before joining the engine
            // worker; joining immediately would deadlock the loop against
            // the worker's BlockingQueuedConnection.
            QElapsedTimer shutdownTimer;
            shutdownTimer.start();
            while (m_engineWatcher && shutdownTimer.elapsed() < 1000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (m_engineWatcher)
                qWarning() << "CollectionRuntime: engine watcher did not finish during shutdown";
            RunResult result;
            result.cancelled = true;
            result.errorMessage = QStringLiteral("runtime destroyed during active run");
            for (const auto &mappingId : std::as_const(m_activeMappingIds))
                result.mappings.append({mappingId, false, true, result.errorMessage});
            finishResourcesOnce(result);
            if (m_runPromise) {
                m_runPromise->addResult(result);
                m_runPromise->finish();
                m_runPromise.reset();
            }
            m_running = false;
        }
        // The runtime owns this engine.  Stop its workers before destroying
        // the watcher; a watcher still attached to an unfinished engine
        // future can otherwise block teardown while waiting for a signal that
        // requires the very workers being destroyed.
        if (m_engine)
            m_engine->stopWorkerPool();
        if (m_engineWatcher) {
            QObject::disconnect(m_engineWatcher, nullptr, nullptr, nullptr);
            m_engineWatcher->setFuture(QFuture<QList<Kalburator::Sync::SyncResult>>());
            delete m_engineWatcher;
            m_engineWatcher = nullptr;
        }
        // Stop workers while the host callback and event context still exist;
        // member destruction order would otherwise destroy the later-declared
        // event context before the earlier-declared engine.
        m_engine.reset();
    }

    RuntimeSnapshot snapshot() const override { return m_snapshot; }

    QObject *backendObject(const QString &endpointId) const override
    {
        const auto it = m_endpointExecutors.find(endpointId);
        if (it == m_endpointExecutors.end() || !it->second)
            return nullptr;
        return it->second->backendObject();
    }

    void refreshProviderSnapshot(const QString &providerId)
    {
        auto *provider = m_providerManager->providerById(providerId);
        if (!provider) {
            m_snapshot.providers.erase(
                std::remove_if(m_snapshot.providers.begin(), m_snapshot.providers.end(),
                               [&providerId](const auto &item) { return item.id == providerId; }),
                m_snapshot.providers.end());
            return;
        }
        ProviderSnapshot value;
        value.id = provider->id();
        value.kind = provider->kind();
        value.displayName = provider->displayName();
        value.state = m_providerManager->providerState(providerId);
        value.errorMessage = Kalburator::Sync::redactCredentials(provider->lastError());
        value.warningMessage = Kalburator::Sync::redactCredentials(provider->lastWarning());
        value.backendIds = m_providerManager->backendIdsForProvider(providerId);
        value.collections = provider->collections();
        auto it = std::find_if(m_snapshot.providers.begin(), m_snapshot.providers.end(),
                               [&providerId](const auto &item) { return item.id == providerId; });
        if (it == m_snapshot.providers.end())
            m_snapshot.providers.append(std::move(value));
        else
            *it = std::move(value);
    }

    void setEventSink(std::function<void(const RuntimeEvent &)> sink) override
    {
        QMutexLocker lock(&m_eventSinkState->mutex);
        m_eventSinkState->sink = std::move(sink);
    }

    bool addProvider(const Kalburator::Sync::BackendConfiguration &config,
                     QString &errorMessage) override
    {
        if (!validateProviderMutation(config, errorMessage))
            return false;
        if (m_providerManager->providerById(config.id)) {
            errorMessage = QStringLiteral("provider already exists: ") + config.id;
            return false;
        }
        auto *contribution = m_backendRegistry.contributionFor(config.type);
        if (!contribution) {
            errorMessage = QStringLiteral("no provider contribution for type: ") + config.type;
            return false;
        }
        auto provider = contribution->createProvider();
        if (!provider) {
            errorMessage = QStringLiteral("provider contribution returned null: ") + config.type;
            return false;
        }
        provider->load(config);
        m_providerManager->addProvider(std::move(provider));
        m_definition.providers.append(config);
        m_snapshot.providerIds.append(config.id);
        refreshProviderSnapshot(config.id);
        errorMessage.clear();
        return true;
    }

    bool updatePolicy(const RuntimePolicy &policy, QString &errorMessage) override
    {
        if (!m_usable) {
            errorMessage = QStringLiteral("runtime is unavailable after a store lifecycle failure");
            return false;
        }
        if (m_running || m_topologyApplying) {
            errorMessage = QStringLiteral("cannot change run policy while a sync is active");
            return false;
        }
        if (policy.maxConcurrentMappings < 1) {
            errorMessage = QStringLiteral("maximum mapping concurrency must be at least one");
            return false;
        }
        m_definition.policy = policy;
        if (m_engine) {
            m_engine->setMaxConcurrentMappings(policy.maxConcurrentMappings);
            m_engine->setSkipUnchangedMappings(policy.skipUnchangedMappings);
        }
        errorMessage.clear();
        return true;
    }

    bool updateProvider(const Kalburator::Sync::BackendConfiguration &config,
                        QString &errorMessage) override
    {
        if (m_topologyApplying) {
            errorMessage = QStringLiteral("cannot mutate providers while topology is applying");
            return false;
        }
        if (!validateProviderMutation(config, errorMessage))
            return false;
        auto *provider = m_providerManager->providerById(config.id);
        if (!provider) {
            errorMessage = QStringLiteral("unknown provider: ") + config.id;
            return false;
        }
        if (provider->kind() != config.type) {
            errorMessage = QStringLiteral("provider type cannot change in place: ")
                + config.id;
            return false;
        }
        if (!m_providerManager->updateProvider(config)) {
            errorMessage = QStringLiteral("provider update failed: ") + config.id;
            return false;
        }
        for (auto &existing : m_definition.providers) {
            if (existing.id == config.id) {
                existing = config;
                break;
            }
        }
        refreshProviderSnapshot(config.id);
        invalidateExecutableTopology();
        errorMessage.clear();
        return true;
    }

    bool removeProvider(const QString &providerId,
                        QString &errorMessage) override
    {
        if (!m_usable) {
            errorMessage = QStringLiteral("runtime is unavailable after a store lifecycle failure");
            return false;
        }
        if (m_topologyApplying) {
            errorMessage = QStringLiteral("cannot mutate providers while topology is applying");
            return false;
        }
        if (m_running) {
            errorMessage = QStringLiteral("cannot mutate providers while a sync is active");
            return false;
        }
        if (!m_providerManager->providerById(providerId)) {
            errorMessage = QStringLiteral("unknown provider: ") + providerId;
            return false;
        }
        const QString prefix = providerId + QLatin1Char(':');
        for (const auto &endpointId : m_endpointResourceIds.keys()) {
            if (endpointId.startsWith(prefix)) {
                errorMessage = QStringLiteral("provider is referenced by topology: ")
                    + providerId;
                return false;
            }
        }
        m_providerManager->removeProvider(providerId);
        m_definition.providers.erase(
            std::remove_if(m_definition.providers.begin(), m_definition.providers.end(),
                           [&providerId](const auto &config) { return config.id == providerId; }),
            m_definition.providers.end());
        m_snapshot.providerIds.removeAll(providerId);
        refreshProviderSnapshot(providerId);
        m_snapshot.backendIds.erase(
            std::remove_if(m_snapshot.backendIds.begin(), m_snapshot.backendIds.end(),
                           [&prefix](const auto &id) { return id.startsWith(prefix); }),
            m_snapshot.backendIds.end());
        errorMessage.clear();
        return true;
    }

    QFuture<bool> connectProviders() override
    {
        auto promise = std::make_shared<QPromise<bool>>();
        promise->start();
        if (!m_usable) {
            promise->addResult(false);
            promise->finish();
            return promise->future();
        }
        auto future = m_providerManager->connectAll();
        auto *watcher = new QFutureWatcher<void>();
        QObject::connect(watcher, &QFutureWatcher<void>::finished, watcher,
                         [this, watcher, promise]() {
            const bool ready = std::all_of(
                m_definition.providers.cbegin(), m_definition.providers.cend(),
                [this](const auto &config) {
                    return m_providerManager->providerState(config.id)
                        == Kalburator::Sync::ProviderConnectionState::Connected;
                });
            promise->addResult(ready);
            promise->finish();
            watcher->deleteLater();
        });
        watcher->setFuture(future);
        return promise->future();
    }

    TopologyResult applyTopology(const TopologyDefinition &desired) override
    {
        TopologyResult result;
        if (!m_usable) {
            result.errorMessage = QStringLiteral(
                "runtime is unavailable after a store lifecycle failure");
            return result;
        }
        // Replacing topology tears down endpoint executors below.  A running
        // engine may still be dispatching through them, so admission must be
        // rejected before provider staging, endpoint materialization, or any
        // durable/physical topology effect occurs.
        if (m_running) {
            result.errorMessage = QStringLiteral(
                "cannot change topology while a sync is active");
            return result;
        }
        if (m_topologyApplying) {
            result.errorMessage = QStringLiteral("topology application is already in progress");
            return result;
        }
        struct TopologyApplicationGuard {
            bool &active;
            explicit TopologyApplicationGuard(bool &value) : active(value) { active = true; }
            ~TopologyApplicationGuard() { active = false; }
        } applyingTopology{m_topologyApplying};
        const auto valid = [](const QStringList &ids) {
            QSet<QString> seen;
            return std::all_of(ids.cbegin(), ids.cend(),
                               [&seen](const QString &id) {
                                   return !id.isEmpty() && !seen.contains(id)
                                       && (seen.insert(id), true);
                               });
        };
        const auto previousProviders = m_definition.providers;
        bool providersStaged = false;
        bool topologyCommitted = false;
        auto waitForProviderConnections = [&]() {
            auto future = m_providerManager->connectAll();
            QEventLoop loop;
            QFutureWatcher<void> watcher;
            QObject::connect(&watcher, &QFutureWatcher<void>::finished,
                             &loop, &QEventLoop::quit);
            watcher.setFuture(future);
            loop.exec();
        };
        auto restoreProviders = [&]() {
            if (!providersStaged)
                return;
            const auto current = m_providerManager->providers();
            for (auto *provider : current)
                m_providerManager->removeProvider(provider->id());
            for (const auto &config : previousProviders) {
                auto *contribution = m_backendRegistry.contributionFor(config.type);
                if (!contribution)
                    continue;
                auto provider = contribution->createProvider();
                if (!provider)
                    continue;
                provider->load(config);
                m_providerManager->addProvider(std::move(provider));
            }
            waitForProviderConnections();
            m_definition.providers = previousProviders;
            m_snapshot.providerIds.clear();
            for (const auto &config : previousProviders) {
                m_snapshot.providerIds.append(config.id);
                refreshProviderSnapshot(config.id);
            }
            providersStaged = false;
        };
        struct ProviderRollbackGuard {
            std::function<void()> rollback;
            ~ProviderRollbackGuard() { rollback(); }
        } rollbackGuard{[&]() {
            if (providersStaged && !topologyCommitted)
                restoreProviders();
        }};
        if (desired.replaceProviders) {
            QSet<QString> desiredProviderIds;
            for (const auto &config : desired.providers) {
                if (!config.isValid() || desiredProviderIds.contains(config.id)) {
                    result.errorMessage = QStringLiteral("topology contains an invalid or duplicate provider");
                    return result;
                }
                if (!m_backendRegistry.contributionFor(config.type)) {
                    result.errorMessage = QStringLiteral("no provider contribution for type: ") + config.type;
                    return result;
                }
                desiredProviderIds.insert(config.id);
            }
            providersStaged = true;
            for (const auto &config : previousProviders) {
                if (!desiredProviderIds.contains(config.id))
                    m_providerManager->removeProvider(config.id);
            }
            for (const auto &config : desired.providers) {
                auto *existing = m_providerManager->providerById(config.id);
                if (existing) {
                    if (existing->kind() != config.type
                        || !m_providerManager->updateProvider(config)) {
                        restoreProviders();
                        result.errorMessage = QStringLiteral("provider update failed: ") + config.id;
                        return result;
                    }
                } else {
                    auto *contribution = m_backendRegistry.contributionFor(config.type);
                    auto provider = contribution->createProvider();
                    if (!provider) {
                        restoreProviders();
                        result.errorMessage = QStringLiteral("provider creation failed: ") + config.id;
                        return result;
                    }
                    provider->load(config);
                    m_providerManager->addProvider(std::move(provider));
                }
            }
            waitForProviderConnections();
            for (const auto &config : desired.providers) {
                if (m_providerManager->providerState(config.id)
                    != Kalburator::Sync::ProviderConnectionState::Connected) {
                    restoreProviders();
                    result.errorMessage = QStringLiteral("provider did not connect: ") + config.id;
                    return result;
                }
            }
        }
        QSet<QString> providerIds;
        for (const auto &id : (desired.replaceProviders
                               ? [&desired]() { QStringList ids; for (const auto &p : desired.providers) ids.append(p.id); return ids; }()
                               : m_snapshot.providerIds))
            providerIds.insert(id);
        QSet<QString> resourceIds;
        for (const auto &resource : m_definition.resources) {
            if (resource.id.isEmpty() || !resource.lease) {
                result.errorMessage = QStringLiteral("runtime contains an invalid external resource");
                return result;
            }
            resourceIds.insert(resource.id);
        }
        QSet<QString> endpointIds;
        QHash<QString, Kalburator::Sync::SyncBackendBase *> endpointBackends;
        QHash<QString, QString> endpointResourceIds;
        std::map<QString, std::unique_ptr<Kalburator::Sync::BackendExecutor>> endpointExecutors;
        QStringList committedBackendIds;
        QStringList factoryBackendIds;
        std::vector<std::unique_ptr<BackendEndpoint>> materialized;
        if (!valid(desired.removedEndpointIds)) {
            result.errorMessage = QStringLiteral("topology contains an invalid removal id");
            return result;
        }
        for (const auto &endpoint : desired.endpoints) {
            if (endpoint.id.isEmpty() || endpointIds.contains(endpoint.id)
                || (endpoint.factoryId.isEmpty() && endpoint.providerId.isEmpty())) {
                result.errorMessage = QStringLiteral("topology contains an invalid or duplicate endpoint id");
                return result;
            }
            endpointIds.insert(endpoint.id);
            const auto existingBackend = m_backendRegistry.backendInstance(endpoint.id);
            const bool replacesOwnedEndpoint = m_endpointBackendIds.contains(endpoint.id);
            const bool retainsProviderEndpoint = endpoint.factoryId.isEmpty()
                && !endpoint.providerId.isEmpty()
                && m_providerManager->backendIdsForProvider(endpoint.providerId)
                    .contains(endpoint.id);
            if (existingBackend && !replacesOwnedEndpoint && !retainsProviderEndpoint) {
                result.errorMessage = QStringLiteral("topology endpoint id collides with a live backend: ")
                    + endpoint.id;
                return result;
            }
            endpointResourceIds.insert(endpoint.id, endpoint.externalResourceId);
            if (!endpoint.providerId.isEmpty() && !providerIds.contains(endpoint.providerId)) {
                result.errorMessage = QStringLiteral("topology endpoint references unknown provider: ")
                    + endpoint.providerId;
                return result;
            }
            if (!endpoint.externalResourceId.isEmpty()
                && !resourceIds.contains(endpoint.externalResourceId)) {
                result.errorMessage = QStringLiteral("topology endpoint references unknown resource: ")
                    + endpoint.externalResourceId;
                return result;
            }
            if (!endpoint.providerId.isEmpty()
                && m_providerManager->providerState(endpoint.providerId)
                    != Kalburator::Sync::ProviderConnectionState::Connected) {
                result.errorMessage = QStringLiteral("topology endpoint provider is not connected: ")
                    + endpoint.providerId;
                return result;
            }
            if (endpoint.factoryId.isEmpty()) {
                auto *providerBackend = m_backendRegistry.backendInstance(endpoint.id);
                if (!providerBackend) {
                    result.errorMessage = QStringLiteral(
                        "provider endpoint backend is unavailable: ") + endpoint.id;
                    return result;
                }
                endpointBackends.insert(endpoint.id, providerBackend);
                committedBackendIds.append(endpoint.id);
                continue;
            }
            auto factoryIt = std::find_if(m_definition.backendFactories.cbegin(),
                                          m_definition.backendFactories.cend(),
                [&endpoint](const auto &factory) {
                    return factory && factory->factoryId() == endpoint.factoryId;
                });
            if (factoryIt == m_definition.backendFactories.cend()) {
                result.errorMessage = QStringLiteral("no backend factory for: ") + endpoint.factoryId;
                return result;
            }
            QString factoryError;
            auto created = (*factoryIt)->createEndpoint(
                {endpoint.id, endpoint.factoryId, endpoint.factoryInput,
                 endpoint.providerId, endpoint.externalResourceId, endpoint.collectionIds},
                factoryError);
            if (!created) {
                result.errorMessage = factoryError.isEmpty()
                    ? QStringLiteral("backend factory returned no endpoint: ") + endpoint.id
                    : factoryError;
                return result;
            }
            if (!created->isReady(factoryError)) {
                result.errorMessage = factoryError.isEmpty()
                    ? QStringLiteral("backend endpoint is not ready: ") + endpoint.id
                    : factoryError;
                return result;
            }
            if (auto backend = created->takeBackend()) {
                auto *asSync = dynamic_cast<Kalburator::Sync::SyncBackendBase *>(backend.get());
                if (!asSync) {
                    result.errorMessage = QStringLiteral(
                        "backend endpoint does not provide an executable backend: ")
                        + endpoint.id;
                    return result;
                }
                auto executor = std::make_unique<Kalburator::Sync::BackendExecutor>(
                    std::move(backend));
                if (!executor->start()) {
                    result.errorMessage = QStringLiteral("could not start backend executor: ")
                        + endpoint.id;
                    return result;
                }
                asSync = dynamic_cast<Kalburator::Sync::SyncBackendBase *>(executor->backend());
                endpointBackends.insert(endpoint.id, asSync);
                endpointExecutors.emplace(endpoint.id, std::move(executor));
                factoryBackendIds.append(endpoint.id);
            }
            materialized.push_back(std::move(created));
            committedBackendIds.append(endpoint.id);
        }
        for (const auto &removedId : desired.removedEndpointIds) {
            if (!m_endpointResourceIds.contains(removedId)) {
                result.errorMessage = QStringLiteral("topology removes an unknown endpoint: ")
                    + removedId;
                return result;
            }
            if (endpointIds.contains(removedId)) {
                result.errorMessage = QStringLiteral("topology endpoint is both retained and removed: ")
                    + removedId;
                return result;
            }
        }
        for (const auto &existingId : m_endpointResourceIds.keys()) {
            if (!endpointIds.contains(existingId)
                && !desired.removedEndpointIds.contains(existingId)) {
                result.errorMessage = QStringLiteral("topology omits existing endpoint without removal: ")
                    + existingId;
                return result;
            }
        }
        QSet<QString> seenMappingIds;
        for (const auto &mapping : desired.mappings) {
            if (!mapping.isValid()) {
                result.errorMessage = QStringLiteral("topology contains an invalid mapping");
                return result;
            }
            if (seenMappingIds.contains(mapping.id)) {
                result.errorMessage = QStringLiteral("topology contains duplicate mapping id: ")
                    + mapping.id;
                return result;
            }
            seenMappingIds.insert(mapping.id);
            if (!endpointIds.contains(mapping.sourceBackend)
                || !endpointIds.contains(mapping.targetBackend)) {
                result.errorMessage = QStringLiteral("mapping references an unmaterialized endpoint");
                return result;
            }
            if (!endpointBackends.contains(mapping.sourceBackend)
                || !endpointBackends.contains(mapping.targetBackend)) {
                result.errorMessage = QStringLiteral(
                    "mapping endpoint does not provide an executable backend");
                return result;
            }
        }
        // Validate every mutation before preparing durable state or performing
        // the first physical operation.  Runtime failures from a valid backend
        // operation still need compensation below; malformed input and absent
        // capabilities must not make an earlier mutation observable at all.
        for (const auto &mutation : desired.collectionMutations) {
            auto *backend = endpointBackends.value(mutation.endpointId);
            if (!backend) {
                result.errorMessage = QStringLiteral(
                    "collection mutation references an unknown endpoint: ")
                    + mutation.endpointId;
                return result;
            }
            const QString collectionId = mutation.collectionId.isEmpty()
                ? mutation.collection.id : mutation.collectionId;
            if (collectionId.isEmpty()) {
                result.errorMessage = QStringLiteral("collection mutation has no collection id");
                return result;
            }
            const auto requiresMutator = mutation.kind == CollectionMutationKind::Update
                || mutation.kind == CollectionMutationKind::Rename
                || mutation.kind == CollectionMutationKind::Destroy;
            if (requiresMutator
                && !dynamic_cast<Kalburator::Sync::IBackendCollectionMutator *>(backend)) {
                result.errorMessage = QStringLiteral(
                    "collection mutation is unsupported by endpoint: ") + mutation.endpointId;
                return result;
            }
            if (mutation.kind == CollectionMutationKind::Rename
                && mutation.metadata.value(QStringLiteral("newCollectionId")).toString().isEmpty()) {
                result.errorMessage = QStringLiteral("collection rename has no new collection id");
                return result;
            }
        }
        bool persistencePrepared = false;
        if (m_definition.topologyPersistence) {
            QString persistenceError;
            if (!m_definition.topologyPersistence->prepare(desired, persistenceError)) {
                result.errorMessage = persistenceError.isEmpty()
                    ? QStringLiteral("topology persistence preparation failed")
                    : persistenceError;
                m_definition.topologyPersistence->rollback();
                restoreProviders();
                return result;
            }
            persistencePrepared = true;
        }
        auto rollbackPrepared = [&]() {
            if (persistencePrepared && m_definition.topologyPersistence) {
                m_definition.topologyPersistence->rollback();
                persistencePrepared = false;
            }
        };
        QList<RuntimeEvent> collectionEvents;
        QList<CollectionMutation> appliedMutations;
        auto invokeEndpoint = [&](const QString &endpointId, auto &&operation) {
            const auto executor = endpointExecutors.find(endpointId);
            if (executor != endpointExecutors.end())
                return executor->second->invoke(std::forward<decltype(operation)>(operation));
            return m_providerManager->invokeOwnedBackend(endpointId,
                std::forward<decltype(operation)>(operation));
        };
        auto compensateCollections = [&]() {
            bool complete = true;
            for (auto it = appliedMutations.crbegin(); it != appliedMutations.crend(); ++it) {
                auto *compensationBackend = endpointBackends.value(it->endpointId);
                auto *compensator = dynamic_cast<Kalburator::Sync::IBackendCollectionMutator *>(
                    compensationBackend);
                if (!compensator && it->kind != CollectionMutationKind::Adopt
                    && it->kind != CollectionMutationKind::Untrack)
                    complete = false;
                switch (it->kind) {
                case CollectionMutationKind::Create:
                    {
                    bool reverted = false;
                    if (!compensator || !invokeEndpoint(it->endpointId, [&] {
                            reverted = compensator->deletePhysicalCollection(it->collection.id);
                        }) || !reverted)
                        complete = false;
                    break;
                    }
                case CollectionMutationKind::Rename: {
                    const QString oldId = it->collectionId.isEmpty()
                        ? it->collection.id : it->collectionId;
                    const QString newId = it->metadata.value(QStringLiteral("newCollectionId"))
                        .toString();
                    bool reverted = false;
                    if (!compensator || newId.isEmpty()
                        || !invokeEndpoint(it->endpointId, [&] {
                            reverted = compensator->renamePhysicalCollection(newId, oldId);
                        }) || !reverted)
                        complete = false;
                    break;
                }
                case CollectionMutationKind::Adopt:
                case CollectionMutationKind::Untrack:
                    break;
                case CollectionMutationKind::Update:
                case CollectionMutationKind::Destroy:
                    // Metadata restoration and remote destruction are not
                    // generally reversible without a backend-specific durable
                    // journal. Surface repair instead of claiming rollback.
                    complete = false;
                    break;
                }
            }
            return complete;
        };
        for (const auto &mutation : desired.collectionMutations) {
            auto *backend = endpointBackends.value(mutation.endpointId);
            if (!backend) {
                result.errorMessage = QStringLiteral("collection mutation references an unknown endpoint: ")
                    + mutation.endpointId;
                rollbackPrepared();
                return result;
            }
            const QString collectionId = mutation.collectionId.isEmpty()
                ? mutation.collection.id : mutation.collectionId;
            if (collectionId.isEmpty()) {
                result.errorMessage = QStringLiteral("collection mutation has no collection id");
                rollbackPrepared();
                return result;
            }
            bool success = false;
            CollectionChangeKind changeKind = CollectionChangeKind::Updated;
            auto *mutator = dynamic_cast<Kalburator::Sync::IBackendCollectionMutator *>(backend);
            switch (mutation.kind) {
            case CollectionMutationKind::Create:
                success = invokeEndpoint(mutation.endpointId, [&] {
                    success = !backend->createCollection(mutation.collection).isEmpty();
                }) && success;
                changeKind = CollectionChangeKind::Created;
                break;
            case CollectionMutationKind::Adopt:
                {
                    success = invokeEndpoint(mutation.endpointId, [&] {
                        const auto available = backend->availableCollections();
                        success = std::any_of(available.cbegin(), available.cend(),
                                              [&collectionId](const auto &item) {
                                                  return item.id == collectionId;
                                              });
                    }) && success;
                }
                break;
            case CollectionMutationKind::Untrack:
                // Untracking changes only the desired topology.  It does not
                // mutate the physical backend and therefore publishes no
                // CollectionChanged observation.
                success = true;
                break;
            case CollectionMutationKind::Update: {
                success = mutator && invokeEndpoint(mutation.endpointId, [&] {
                    success = mutator->updateCollectionMetadata(collectionId, mutation.metadata);
                }) && success;
                break;
            }
            case CollectionMutationKind::Rename: {
                const QString newCollectionId = mutation.metadata.value(
                    QStringLiteral("newCollectionId")).toString();
                success = mutator && !newCollectionId.isEmpty()
                    && invokeEndpoint(mutation.endpointId, [&] {
                        success = mutator->renamePhysicalCollection(collectionId, newCollectionId);
                    }) && success;
                break;
            }
            case CollectionMutationKind::Destroy: {
                success = mutator && invokeEndpoint(mutation.endpointId, [&] {
                    success = mutator->deletePhysicalCollection(collectionId);
                }) && success;
                changeKind = CollectionChangeKind::Deleted;
                break;
            }
            }
            if (!success) {
                result.errorMessage = QStringLiteral("collection mutation is unsupported or failed: ")
                    + collectionId;
                result.repairRequired = !compensateCollections()
                    || mutation.kind == CollectionMutationKind::Destroy;
                rollbackPrepared();
                return result;
            }
            if (mutation.kind != CollectionMutationKind::Adopt
                && mutation.kind != CollectionMutationKind::Untrack) {
                RuntimeEvent event;
                event.kind = RuntimeEvent::Kind::CollectionChanged;
                event.backendId = mutation.endpointId;
                event.collectionId = mutation.kind == CollectionMutationKind::Rename
                    ? mutation.metadata.value(QStringLiteral("newCollectionId")).toString()
                    : collectionId;
                event.collectionChangeKind = changeKind;
                event.collection = mutation.collection;
                event.collectionMutationCommitted = true;
                collectionEvents.append(std::move(event));
            }
            appliedMutations.append(mutation);
        }
        if (m_definition.topologyPersistence) {
            QString persistenceError;
            if (!m_definition.topologyPersistence->commit(persistenceError)) {
                result.errorMessage = persistenceError.isEmpty()
                    ? QStringLiteral("topology persistence commit failed")
                    : persistenceError;
                m_definition.topologyPersistence->rollback();
                persistencePrepared = false;
                restoreProviders();
                result.repairRequired = !compensateCollections();
                return result;
            }
            persistencePrepared = false;
        }
        // BackendRegistry is deliberately non-owning. Endpoints retain the
        // QObject lifetime and are replaced only after every new endpoint and
        // mapping has passed validation, so the engine never sees a partially
        // materialized mapping set.
        for (const auto &backendId : m_endpointBackendIds)
            m_backendRegistry.unregisterBackendInstance(backendId);
        m_endpointExecutors.clear();
        for (auto it = endpointBackends.cbegin(); it != endpointBackends.cend(); ++it) {
            const QString externalResourceId = endpointResourceIds.value(it.key());
            it.value()->setResourceId(externalResourceId.isEmpty()
                                          ? it.key() : externalResourceId);
            m_backendRegistry.registerBackendInstance(it.key(), it.value());
        }
        m_snapshot.generation++;
        m_endpointBackendIds = factoryBackendIds;
        m_snapshot.backendIds = committedBackendIds;
        for (const auto &providerId : m_snapshot.providerIds)
            m_snapshot.backendIds.append(m_providerManager->backendIdsForProvider(providerId));
        m_snapshot.backendIds.removeDuplicates();
        m_snapshot.mappingIds.clear();
        m_snapshot.mappings.clear();
        for (const auto &mapping : desired.mappings)
            m_snapshot.mappingIds.append(mapping.id);
        for (const auto &mapping : desired.mappings) {
            MappingSnapshot item;
            item.id = mapping.id;
            item.sourceBackendId = mapping.sourceBackend;
            item.sourceCollectionId = mapping.sourceCalendar;
            item.targetBackendId = mapping.targetBackend;
            item.targetCollectionId = mapping.targetCalendar;
            item.enabled = mapping.enabled;
            item.lastSuccessfulSync = m_baselineStore->lastSyncTime(mapping.id);
            m_snapshot.mappings.append(std::move(item));
        }
        m_endpoints = std::move(materialized);
        m_endpointExecutors = std::move(endpointExecutors);
        m_endpointResourceIds = std::move(endpointResourceIds);
        m_mappings = desired.mappings;
        if (desired.replaceProviders) {
            m_definition.providers = desired.providers;
            m_snapshot.providerIds.clear();
            for (const auto &config : desired.providers)
                m_snapshot.providerIds.append(config.id);
        }
        m_engine->setSyncMappings(desired.mappings);
        result.committed = true;
        topologyCommitted = true;
        result.snapshot = m_snapshot;
        emitEvent({RuntimeEvent::Kind::TopologyCommitted, {}, {}, -1});
        for (const auto &event : collectionEvents)
            emitEvent(event);
        return result;
    }

    QFuture<RunResult> run(const RunRequest &request) override
    {
        auto promise = std::make_shared<QPromise<RunResult>>();
        promise->start();
        auto future = promise->future();
        RunResult result;
        QString error;
        bool sessionStarted = false;
        if (!m_usable) {
            result.errorMessage = QStringLiteral(
                "runtime is unavailable after a store lifecycle failure");
        } else if (!request.validate(error)) {
            result.errorMessage = error;
        } else if (m_topologyApplying) {
            result.errorMessage = QStringLiteral("cannot start a run while topology is applying");
        } else if (m_running) {
            result.errorMessage = QStringLiteral("runtime is already running");
        } else {
            m_running = true;
            m_cancelled = false;
            m_resourcesFinished = false;
            m_lostResourceIds.clear();
            sessionStarted = true;
            QStringList selectedMappingIds;
            switch (request.selection.kind) {
            case RunSelection::Kind::AllEnabled:
                selectedMappingIds = m_snapshot.mappingIds;
                break;
            case RunSelection::Kind::ExactSet:
            case RunSelection::Kind::One:
                selectedMappingIds = request.selection.mappingIds;
                for (const auto &mappingId : selectedMappingIds) {
                    if (!m_snapshot.mappingIds.contains(mappingId)) {
                        result.errorMessage = QStringLiteral("unknown mapping id: ") + mappingId;
                        break;
                    }
                }
                break;
            case RunSelection::Kind::None:
                break;
            }

            // A selection is a set semantically, but retain the caller's first-seen
            // order so result identity remains stable and meaningful to consumers.
            QStringList normalizedMappingIds;
            for (const auto &mappingId : selectedMappingIds) {
                if (!normalizedMappingIds.contains(mappingId))
                    normalizedMappingIds.append(mappingId);
            }
            selectedMappingIds = std::move(normalizedMappingIds);
            m_activeMappingIds = selectedMappingIds;
            m_activeResourceIds.clear();
            for (const auto &mapping : std::as_const(m_mappings)) {
                if (!selectedMappingIds.contains(mapping.id)) continue;
                for (const auto &endpointId : {mapping.sourceBackend,
                                               mapping.targetBackend}) {
                    const QString resourceId = m_endpointResourceIds.value(endpointId);
                    if (!resourceId.isEmpty() && !m_activeResourceIds.contains(resourceId))
                        m_activeResourceIds.append(resourceId);
                }
            }

            if (result.errorMessage.isEmpty() && !m_engine->syncMappings().isEmpty()) {
                for (const auto &mapping : m_engine->syncMappings()) {
                    if (!selectedMappingIds.contains(mapping.id))
                        continue;
                    if (!m_backendRegistry.backendInstance(mapping.sourceBackend)) {
                        result.errorMessage = QStringLiteral("backend unavailable: ")
                            + mapping.sourceBackend;
                        break;
                    }
                    if (!m_backendRegistry.backendInstance(mapping.targetBackend)) {
                        result.errorMessage = QStringLiteral("backend unavailable: ")
                            + mapping.targetBackend;
                        break;
                    }
                }
            }

            if (result.errorMessage.isEmpty() && !selectedMappingIds.isEmpty()) {
                emitEvent({RuntimeEvent::Kind::RunStarted, {}, {}, 0});
                for (const auto &resourceId : std::as_const(m_activeResourceIds)) {
                    const auto lease = leaseFor(resourceId);
                    if (!lease || !lease->prepare(error)
                        || !lease->executePhase(QStringLiteral("sync"), error)) {
                        result.errorMessage = error.isEmpty()
                            ? QStringLiteral("external resource preparation failed") : error;
                        break;
                    }
                }
                if (m_cancelled) {
                    result.cancelled = true;
                    result.errorMessage = QStringLiteral("runtime has been cancelled");
                }
                if (result.errorMessage.isEmpty() && !m_engine->syncMappings().isEmpty()) {
                    Kalburator::Engine::SyncRequest engineRequest;
                    engineRequest.mappingIds = selectedMappingIds;
                    if (request.intent == RunIntent::FullRediff) {
                        for (const auto &mappingId : selectedMappingIds) {
                            if (!m_baselineStore->clearMappingV3(mappingId)) {
                                result.errorMessage = QStringLiteral("could not clear baseline for: ")
                                    + mappingId;
                                break;
                            }
                        }
                        m_engine->setSkipUnchangedMappings(false);
                    } else {
                        m_engine->setSkipUnchangedMappings(
                            m_definition.policy.skipUnchangedMappings);
                    }
                    if (result.errorMessage.isEmpty()
                        && request.intent == RunIntent::DestructiveRebuild) {
                        engineRequest.executionOverride =
                            Kalburator::Sync::ExecutionOverride{.clobber = true};
                    } else if (result.errorMessage.isEmpty()
                               && request.intent == RunIntent::Mirror) {
                        Kalburator::Sync::ExecutionOverride override;
                        override.direction = request.mirrorDirection
                                == MirrorDirection::SourceToTarget
                            ? Kalburator::Sync::ExecutionOverride::Direction::MirrorAToB
                            : Kalburator::Sync::ExecutionOverride::Direction::MirrorBToA;
                        engineRequest.executionOverride = override;
                    }

                    if (!result.errorMessage.isEmpty()) {
                        // The common synchronous completion funnel below owns
                        // lease finish and future completion.
                    } else {
                    engineRequest.behavior = m_definition.policy.interaction
                        == RunInteraction::Monitored
                        ? Kalburator::Engine::SyncEngine::SyncBehavior::Monitored
                        : Kalburator::Engine::SyncEngine::SyncBehavior::Unmonitored;
                    for (const auto &mappingId : selectedMappingIds) {
                        RuntimeEvent event;
                        event.kind = RuntimeEvent::Kind::RunProgress;
                        event.mappingId = mappingId;
                        event.mappingStarted = true;
                        event.progressKind = RunProgressKind::MappingStarted;
                        emitEvent(event);
                    }
                    auto engineFuture = m_engine->runSync(engineRequest);
                    auto *watcher = new QFutureWatcher<QList<Kalburator::Sync::SyncResult>>;
                    m_engineWatcher = watcher;
                    m_runPromise = promise;
                    QObject::connect(watcher,
                        &QFutureWatcher<QList<Kalburator::Sync::SyncResult>>::finished,
                        watcher,
                        [this, watcher, promise, selectedMappingIds]() mutable {
                            const auto engineFuture = watcher->future();
                            RunResult engineResult;
                            QList<Kalburator::Sync::SyncResult> syncResults;
                            if (engineFuture.resultCount() > 0)
                                syncResults = engineFuture.resultAt(0);
                            engineResult.success = !engineFuture.isCanceled();
                            engineResult.cancelled = engineFuture.isCanceled();
                            if (engineResult.cancelled)
                                engineResult.errorMessage = QStringLiteral("runtime has been cancelled");

                            for (const auto &mappingId : selectedMappingIds) {
                                MappingRunResult mappingResult;
                                mappingResult.mappingId = mappingId;
                                const auto it = std::find_if(
                                    syncResults.cbegin(), syncResults.cend(),
                                    [&mappingId](const auto &syncResult) {
                                        return syncResult.mappingId == mappingId;
                                    });
                                if (it == syncResults.cend()) {
                                    mappingResult.success = false;
                                    mappingResult.errorMessage = QStringLiteral(
                                        "engine returned no result for mapping: ") + mappingId;
                                    engineResult.success = false;
                                } else {
                                    mappingResult.success = it->success && !it->cancelled;
                                    mappingResult.cancelled = it->cancelled;
                                    mappingResult.errorMessage = it->errorMessage;
                                    mappingResult.sourceStats = it->sourceStats;
                                    mappingResult.targetStats = it->targetStats;
                                    if (mappingTouchesLostResource(mappingId)) {
                                        mappingResult.success = false;
                                        mappingResult.cancelled = true;
                                        mappingResult.errorMessage =
                                            QStringLiteral("external resource lost");
                                    }
                                    if (!mappingResult.success && !mappingResult.cancelled)
                                        engineResult.success = false;
                                    if (mappingResult.cancelled)
                                        engineResult.cancelled = true;
                                    if (mappingResult.cancelled)
                                        engineResult.success = false;
                                    if (engineResult.errorMessage.isEmpty()
                                        && !mappingResult.errorMessage.isEmpty())
                                        engineResult.errorMessage = mappingResult.errorMessage;
                                }
                                engineResult.mappings.append(mappingResult);
                                RuntimeEvent mappingEvent;
                                mappingEvent.kind = RuntimeEvent::Kind::RunProgress;
                                mappingEvent.mappingId = mappingId;
                                mappingEvent.mappingFinished = true;
                                mappingEvent.mappingSuccess = mappingResult.success;
                                mappingEvent.mappingCancelled = mappingResult.cancelled;
                                mappingEvent.message = mappingResult.errorMessage;
                                mappingEvent.progressKind = RunProgressKind::MappingFinished;
                                if (mappingResult.success) {
                                    mappingEvent.lastSuccessfulSync = QDateTime::currentDateTimeUtc();
                                    for (auto &mapping : m_snapshot.mappings) {
                                        if (mapping.id == mappingId)
                                            mapping.lastSuccessfulSync = mappingEvent.lastSuccessfulSync;
                                    }
                                }
                                emitEvent(mappingEvent);
                            }
                            if (engineResult.cancelled && engineResult.errorMessage.isEmpty())
                                engineResult.errorMessage = QStringLiteral("runtime has been cancelled");
                            if (engineResult.success) {
                                QString flushError;
                                for (const auto &resourceId : std::as_const(m_activeResourceIds)) {
                                    const auto lease = leaseFor(resourceId);
                                    if (lease && !lease->flush(flushError)) {
                                        engineResult.success = false;
                                        engineResult.errorMessage = flushError.isEmpty()
                                            ? QStringLiteral("external resource flush failed")
                                            : flushError;
                                        break;
                                    }
                                }
                            }
                            finishResourcesOnce(engineResult);
                            m_running = false;
                            m_cancelled = false;
                            m_activeMappingIds.clear();
                            m_activeResourceIds.clear();
                            m_lostResourceIds.clear();
                            if (m_engineWatcher == watcher) m_engineWatcher = nullptr;
                            promise->addResult(engineResult);
                            promise->finish();
                            m_runPromise.reset();
                            RuntimeEvent finished;
                            finished.kind = RuntimeEvent::Kind::RunFinished;
                            finished.message = engineResult.errorMessage;
                            finished.progress = engineResult.success ? 100 : -1;
                            emitEvent(finished);
                            watcher->deleteLater();
                        });
                    watcher->setFuture(engineFuture);
                    return future;
                    }
                }
                for (const auto &mappingId : selectedMappingIds) {
                    MappingRunResult mappingResult;
                    mappingResult.mappingId = mappingId;
                    mappingResult.cancelled = result.cancelled;
                    mappingResult.success = result.errorMessage.isEmpty()
                        && !result.cancelled;
                    mappingResult.errorMessage = result.errorMessage;
                    result.mappings.append(mappingResult);
                }
            }
            result.success = result.errorMessage.isEmpty();
        }
        if (sessionStarted) {
            finishResourcesOnce(result);
            m_running = false;
            m_cancelled = false;
            m_activeMappingIds.clear();
            m_activeResourceIds.clear();
            m_lostResourceIds.clear();
        }
        promise->addResult(result);
        promise->finish();
        RuntimeEvent finished;
        finished.kind = RuntimeEvent::Kind::RunFinished;
        finished.message = result.errorMessage;
        finished.progress = result.success ? 100 : -1;
        emitEvent(finished);
        return future;
    }

    bool resolveConflict(const QString &conflictId,
                         Kalburator::Sync::ConflictResolution resolution,
                         const QString &mergedNative) override
    {
        if (!m_conflictManager || conflictId.isEmpty()
            || resolution == Kalburator::Sync::ConflictResolution::AskUser)
            return false;
        if (!m_conflictManager->applyResolution(conflictId, resolution, mergedNative))
            return false;

        RuntimeEvent event;
        event.kind = RuntimeEvent::Kind::ConflictResolved;
        event.objectId = conflictId;
        event.resolution = resolution;
        event.conflict = m_conflictStore->conflict(conflictId);
        emitEvent(event);
        return true;
    }

    QList<Kalburator::Sync::ConflictInfo> unresolvedConflicts() const override
    {
        if (!m_usable || !m_conflictStore)
            return {};
        return m_conflictStore->unresolvedConflicts();
    }

    void cancel() override
    {
        if (!m_running) return;
        m_cancelled = true;
        // Cancel the engine itself as well as the runtime-facing watcher.
        // Cancelling only the outer future does not reliably wake the engine's
        // worker-side nested event loops during collection teardown.
        if (m_engine)
            m_engine->cancelWithReason(
                Kalburator::Engine::CancellationReason::UserRequested);
        if (m_engineWatcher) m_engineWatcher->future().cancel();
        for (const auto &resourceId : std::as_const(m_activeResourceIds)) {
            const auto lease = leaseFor(resourceId);
            if (lease) lease->cancel();
        }
    }

    bool reset(QString &errorMessage) override
    {
        if (m_engine && m_engine->isSyncing()) {
            errorMessage = QStringLiteral("cannot reset while a sync is active");
            return false;
        }
        m_usable = false;
        for (const auto &backendId : m_endpointBackendIds)
            m_backendRegistry.unregisterBackendInstance(backendId);
        m_endpointBackendIds.clear();
        m_endpointResourceIds.clear();
        m_mappings.clear();
        m_endpointExecutors.clear();
        m_endpoints.clear();
        m_snapshot = {};
        m_providerManager->disconnectAll();
        m_engine.reset();
        closeStores();
        if (!removeRuntimeDatabaseArtifacts(m_definition.storagePath, errorMessage)) {
            return false;
        }
        if (!openStores(errorMessage))
            return false;
        configureEngine();
        m_cancelled = false;
        m_usable = true;
        errorMessage.clear();
        return true;
    }

private:
    bool validateProviderMutation(const Kalburator::Sync::BackendConfiguration &config,
                                  QString &errorMessage) const
    {
        if (!m_usable) {
            errorMessage = QStringLiteral("runtime is unavailable after a store lifecycle failure");
            return false;
        }
        if (m_running) {
            errorMessage = QStringLiteral("cannot mutate providers while a sync is active");
            return false;
        }
        if (!config.isValid()) {
            errorMessage = QStringLiteral("provider configuration is invalid");
            return false;
        }
        return true;
    }

    void invalidateExecutableTopology()
    {
        for (const auto &backendId : m_endpointBackendIds)
            m_backendRegistry.unregisterBackendInstance(backendId);
        m_endpointBackendIds.clear();
        m_endpointExecutors.clear();
        m_endpoints.clear();
        m_endpointResourceIds.clear();
        m_mappings.clear();
        m_snapshot.mappingIds.clear();
        m_snapshot.generation++;
        if (m_engine)
            m_engine->setSyncMappings({});
    }

    void configureEngine()
    {
        m_engine = std::make_unique<Kalburator::Engine::SyncEngine>(
            &m_backendRegistry, m_host.get(), m_shape);
        QObject::connect(m_engine.get(), &Kalburator::Engine::SyncEngine::progressUpdated,
                         m_engine.get(), [this](int current, int total,
                                               const QString &message) {
            const int progress = total > 0 ? (current * 100 / total) : 0;
            RuntimeEvent event;
            event.kind = RuntimeEvent::Kind::RunProgress;
            event.message = message;
            event.progress = progress;
            event.progressKind = RunProgressKind::Overall;
            emitEvent(event);
        });
        QObject::connect(m_engine.get(), &Kalburator::Engine::SyncEngine::syncPassStarted,
                         m_engine.get(), [this](int pass, int maxPasses) {
            RuntimeEvent event;
            event.kind = RuntimeEvent::Kind::RunProgress;
            event.pass = pass;
            event.maxPasses = maxPasses;
            event.progressKind = RunProgressKind::ConvergencePass;
            emitEvent(event);
        });
        QObject::connect(m_engine.get(),
                         &Kalburator::Engine::SyncEngine::conflictDetected,
                         m_engine.get(), [this](Kalburator::Sync::ConflictInfo conflict) {
            if (conflict.conflictId.isEmpty() && m_conflictStore) {
                const QString recordedId = m_conflictStore->recordConflict(conflict);
                if (!recordedId.isEmpty())
                    conflict.conflictId = recordedId;
                if (conflict.conflictId.isEmpty()) {
                    const auto unresolved =
                        m_conflictStore->unresolvedConflicts(conflict.mappingId);
                    const auto it = std::find_if(
                        unresolved.cbegin(), unresolved.cend(),
                        [&conflict](const auto &candidate) {
                            return candidate.sourceId == conflict.sourceId;
                        });
                    if (it != unresolved.cend())
                        conflict.conflictId = it->conflictId;
                }
            }
            if (conflict.conflictId.isEmpty())
                return;
            RuntimeEvent event;
            event.kind = RuntimeEvent::Kind::ConflictDetected;
            event.objectId = conflict.conflictId;
            event.mappingId = conflict.mappingId;
            event.recordId = conflict.sourceId;
            event.conflict = conflict;
            emitEvent(event);
        }, Qt::QueuedConnection);
        m_engine->setBaselineStore(m_baselineStore.get());
        m_engine->setSyncConflictStore(m_conflictStore.get());
        if (!m_conflictManager)
            m_conflictManager = std::make_unique<Kalburator::Sync::ConflictManager>();
        m_conflictManager->setSyncConflictStore(m_conflictStore.get());
        // The facade surfaces conflicts as events; it never opens UI dialogs.
        m_conflictManager->setWorkflowMode(
            Kalburator::Sync::ConflictManager::WorkflowMode::Deferred);
        m_engine->setConflictManager(m_conflictManager.get());
        m_engine->setMaxConcurrentMappings(m_definition.policy.maxConcurrentMappings);
        m_engine->setSkipUnchangedMappings(m_definition.policy.skipUnchangedMappings);
        if (m_definition.policy.confirmMassDelete) {
            m_massDeleteGuard = std::make_unique<RuntimeMassDeleteGuard>(
                m_definition.policy.confirmMassDelete);
            m_engine->setMassDeleteGuard(m_massDeleteGuard.get());
        }
    }

    bool openStores(QString &errorMessage)
    {
        auto baseline = std::make_unique<Kalburator::Storage::BaselineStore>(
            m_definition.storagePath);
        if (!baseline->isOpen()) {
            errorMessage = QStringLiteral("baseline store: ") + baseline->lastError();
            return false;
        }
        auto conflicts = std::make_unique<Kalburator::Sync::SyncConflictStore>(
            m_definition.storagePath);
        if (!conflicts->isOpen()) {
            errorMessage = QStringLiteral("conflict store: ") + conflicts->lastError();
            return false;
        }

        // Publish the complete set only after every schema has opened. The
        // engine is attached by the caller after this method returns, so a
        // partially migrated store set can never be observed as usable.
        m_baselineStore = std::move(baseline);
        m_conflictStore = std::move(conflicts);
        errorMessage.clear();
        return true;
    }

    void closeStores()
    {
        m_conflictStore.reset();
        m_baselineStore.reset();
    }

    QSharedPointer<ExternalResourceLease> leaseFor(const QString &resourceId) const
    {
        const auto it = std::find_if(m_definition.resources.cbegin(),
                                     m_definition.resources.cend(),
            [&resourceId](const RuntimeDefinition::Resource &resource) {
                return resource.id == resourceId;
            });
        return it == m_definition.resources.cend() ? QSharedPointer<ExternalResourceLease>{}
                                                    : it->lease;
    }

    void finishResourcesOnce(const RunResult &result)
    {
        if (m_resourcesFinished) return;
        m_resourcesFinished = true;
        for (const auto &resourceId : std::as_const(m_activeResourceIds)) {
            const auto lease = leaseFor(resourceId);
            if (lease) lease->finish(result);
        }
    }

    bool mappingTouchesLostResource(const QString &mappingId) const
    {
        const auto it = std::find_if(m_mappings.cbegin(), m_mappings.cend(),
            [&mappingId](const Kalburator::Sync::SyncMapping &mapping) {
                return mapping.id == mappingId;
            });
        if (it == m_mappings.cend()) return false;
        return m_lostResourceIds.contains(m_endpointResourceIds.value(it->sourceBackend))
            || m_lostResourceIds.contains(m_endpointResourceIds.value(it->targetBackend));
    }

    void emitEvent(const RuntimeEvent &event)
    {
        const auto state = m_eventSinkState;
        const auto deliver = [state, event]() {
            std::function<void(const RuntimeEvent &)> sink;
            {
                QMutexLocker lock(&state->mutex);
                sink = state->sink;
            }
            if (sink) sink(event);
        };
        if (QThread::currentThread() == m_eventContext.thread())
            deliver();
        else
            QMetaObject::invokeMethod(&m_eventContext, deliver, Qt::QueuedConnection);
    }

    RuntimeDefinition m_definition;
    Kalburator::Sync::BackendRegistry m_backendRegistry;
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::PluginManager m_pluginManager;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselineStore;
    std::unique_ptr<Kalburator::Sync::SyncConflictStore> m_conflictStore;
    std::unique_ptr<Kalburator::Sync::ProviderManager> m_providerManager;
    std::unique_ptr<RuntimeSyncHost> m_host;
    std::unique_ptr<Kalburator::Engine::SyncEngine> m_engine;
    std::unique_ptr<Kalburator::Sync::ConflictManager> m_conflictManager;
    std::unique_ptr<RuntimeMassDeleteGuard> m_massDeleteGuard;
    std::vector<std::unique_ptr<BackendEndpoint>> m_endpoints;
    std::map<QString, std::unique_ptr<Kalburator::Sync::BackendExecutor>> m_endpointExecutors;
    QStringList m_endpointBackendIds;
    QHash<QString, QString> m_endpointResourceIds;
    QList<Kalburator::Sync::SyncMapping> m_mappings;
    RuntimeSnapshot m_snapshot;
    QObject m_eventContext;
    std::shared_ptr<EventSinkState> m_eventSinkState = std::make_shared<EventSinkState>();
    QString m_constructionError;
    bool m_cancelled = false;
    bool m_running = false;
    bool m_topologyApplying = false;
    bool m_resourcesFinished = true;
    QStringList m_activeMappingIds;
    QStringList m_activeResourceIds;
    QStringList m_lostResourceIds;
    std::shared_ptr<QPromise<RunResult>> m_runPromise;
    QFutureWatcher<QList<Kalburator::Sync::SyncResult>> *m_engineWatcher = nullptr;
    bool m_usable = true;
};

} // namespace detail

std::unique_ptr<CollectionRuntime>
CollectionRuntime::create(const RuntimeDefinition &definition, QString &errorMessage)
{
    if (definition.storagePath.isEmpty()) {
        errorMessage = QStringLiteral("runtime storage path must not be empty");
        return nullptr;
    }
    if (definition.policy.maxConcurrentMappings < 1) {
        errorMessage = QStringLiteral("maximum mapping concurrency must be at least one");
        return nullptr;
    }

    QList<QSharedPointer<BackendFactory>> factories = definition.backendFactories;
    factories.append(definition.extensions);
    for (const auto &factory : factories) {
        if (!factory) {
            errorMessage = QStringLiteral("runtime contains a null backend factory");
            return nullptr;
        }
        if (!factory->validate(errorMessage)) {
            if (errorMessage.isEmpty())
                errorMessage = QStringLiteral("backend factory validation failed");
            return nullptr;
        }
    }

    auto runtime = std::make_unique<detail::CollectionRuntimeImpl>(definition);
    if (!runtime->constructionError().isEmpty()) {
        errorMessage = runtime->constructionError();
        return nullptr;
    }
    errorMessage.clear();
    return runtime;
}

} // namespace Kalburator::Runtime
