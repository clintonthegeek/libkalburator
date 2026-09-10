#ifndef KALBURATOR_RUNTIME_COLLECTIONRUNTIME_H
#define KALBURATOR_RUNTIME_COLLECTIONRUNTIME_H

#include <QByteArray>
#include <QDateTime>
#include <QFuture>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QObject>
#include <memory>
#include <utility>
#include <functional>
#include <QVariantMap>

#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/types/synctypes.h>
#include <kalburator/blob/iblobbackend.h>
#include <kalburator/sync/iprovider.h>
#include <kalburator/plugin/manifest.h>

namespace Kalburator::Sync { class SecretStore; class ProviderManager; class BackendRegistry; }
namespace Kalburator { class Plugin; }

namespace Kalburator::Runtime {

namespace detail { class CollectionRuntimeImpl; }

/// Which user-visible synchronization semantics the coordinator must apply.
enum class RunIntent { Normal, FullRediff, Mirror, DestructiveRebuild };

enum class MirrorDirection { SourceToTarget, TargetToSource };

enum class RunInteraction { Background, Monitored };

struct RuntimePolicy {
    int maxConcurrentMappings = 1;
    bool skipUnchangedMappings = true;
    RunInteraction interaction = RunInteraction::Background;
    std::function<bool(const QString &, const QString &, int, int)> confirmMassDelete;
};

/// Explicit work selection. An empty ExactSet is invalid; None means no work.
struct RunSelection {
    enum class Kind { AllEnabled, ExactSet, One, None };
    Kind kind = Kind::None;
    QStringList mappingIds;

    static RunSelection allEnabled() { return {Kind::AllEnabled, {}}; }
    static RunSelection exactSet(QStringList ids) { return {Kind::ExactSet, std::move(ids)}; }
    static RunSelection one(QString id) { return {Kind::One, {std::move(id)}}; }
    static RunSelection none() { return {Kind::None, {}}; }

    bool validate(QString &errorMessage) const;
};

struct RunRequest {
    RunSelection selection = RunSelection::none();
    RunIntent intent = RunIntent::Normal;
    MirrorDirection mirrorDirection = MirrorDirection::SourceToTarget;

    bool validate(QString &errorMessage) const;
};

struct MappingRunResult {
    QString mappingId;
    bool success = false;
    bool cancelled = false;
    QString errorMessage;
    Kalburator::Sync::SyncStats sourceStats;
    Kalburator::Sync::SyncStats targetStats;
};

struct ProviderSnapshot {
    QString id;
    QString kind;
    QString displayName;
    Kalburator::Sync::ProviderConnectionState state =
        Kalburator::Sync::ProviderConnectionState::Disconnected;
    QString errorMessage;
    QString warningMessage;
    QStringList backendIds;
    QList<Kalburator::Sync::CollectionInfo> collections;
};

struct MappingSnapshot {
    QString id;
    QString sourceBackendId;
    QString sourceCollectionId;
    QString targetBackendId;
    QString targetCollectionId;
    bool enabled = true;
    QDateTime lastSuccessfulSync;
};

struct RunResult {
    bool success = false;
    bool cancelled = false;
    QString errorMessage;
    QList<MappingRunResult> mappings;
};

struct RuntimeSnapshot {
    quint64 generation = 0;
    QStringList providerIds;
    QStringList backendIds;
    QStringList mappingIds;
    QList<ProviderSnapshot> providers;
    QList<MappingSnapshot> mappings;
};

/// Neutral input supplied to a factory when an endpoint is materialized.
struct BackendMaterialization {
    QString endpointId;
    QString factoryId;
    QVariantMap factoryInput;
    QString providerId;
    QString externalResourceId;
    QStringList collectionIds;
};

/// Opaque endpoint owned by the runtime; concrete backend types stay private.
class BackendEndpoint {
public:
    /// The endpoint takes ownership of an ordinary QObject implementation.
    /// Runtime internals accept only SyncBackendBase implementations when an
    /// endpoint is used by a mapping; the public factory contract deliberately
    /// exposes no backend-specific type.
    explicit BackendEndpoint(std::unique_ptr<QObject> backend = {});
    virtual ~BackendEndpoint() = default;
    virtual bool isReady(QString &errorMessage) const
    { errorMessage.clear(); return true; }

    QObject *backendObject() const;
    std::unique_ptr<Kalburator::Sync::IBlobBackend> takeBackend();

private:
    std::unique_ptr<QObject> m_backend;
};

struct TopologyEndpoint {
    QString id;
    QString factoryId;
    QVariantMap factoryInput;
    QString providerId;
    QString externalResourceId;
    QStringList collectionIds;
};

enum class CollectionMutationKind { Create, Update, Adopt, Rename, Untrack, Destroy };

// Consumer-neutral policy for removing a provider while a desired topology is
// being submitted.
enum class ProviderRemovalPolicy { Strict, DropBindings, DropBindingsAndOrphans };

struct CollectionMutation {
    CollectionMutationKind kind = CollectionMutationKind::Update;
    QString endpointId;
    QString collectionId;
    Kalburator::Sync::CollectionInfo collection;
    QVariantMap metadata;
};

struct TopologyDefinition {
    QList<TopologyEndpoint> endpoints;
    QList<Kalburator::Sync::SyncMapping> mappings;
    QStringList removedEndpointIds;
    // When true, providers is the complete desired provider set.  This makes
    // account add/edit/remove part of the same transaction as endpoint and
    // mapping publication; an empty list is therefore an explicit removal of
    // all providers, not an omitted edit.
    bool replaceProviders = false;
    QList<Kalburator::Sync::BackendConfiguration> providers;
    QHash<QString, ProviderRemovalPolicy> providerRemovalPolicies;
    QList<CollectionMutation> collectionMutations;
};

struct TopologyResult {
    bool committed = false;
    bool repairRequired = false;
    QString errorMessage;
    RuntimeSnapshot snapshot;
};

/// Consumer-owned durable desired-state participant. The runtime calls
/// prepare before changing live topology and commit before publication;
/// commit failure leaves the previous runtime topology untouched.
class TopologyPersistenceParticipant {
public:
    virtual ~TopologyPersistenceParticipant() = default;
    virtual bool prepare(const TopologyDefinition &, QString &) = 0;
    virtual bool commit(QString &) = 0;
    virtual void rollback() = 0;
};

enum class RecordChangeKind { Created, Updated, Deleted };

/// The typed meaning of a RunProgress observation.  The numeric percentage
/// and human-readable message are supplementary display data only.
enum class RunProgressKind { Overall, MappingStarted, MappingFinished,
                             ConvergencePass };

/// Physical collection mutations are reported only after their topology
/// transaction commits.  A failed prepare, rollback, or compensation emits
/// no CollectionChanged event; an irreversible failure is reported by the
/// command result as repairRequired instead.
enum class CollectionChangeKind { Created, Updated, Deleted };


/// Opaque extension point for consumer/backend factories. Concrete backend
/// types stay out of the runtime facade and are registered explicitly.
class BackendFactory {
public:
    virtual ~BackendFactory() = default;
    virtual QString factoryId() const = 0;
    virtual bool validate(QString &errorMessage) const = 0;
    virtual std::unique_ptr<BackendEndpoint> createEndpoint(
        const BackendMaterialization &request, QString &errorMessage) const = 0;
};

struct RuntimeEvent {
    enum class Kind { ProviderStateChanged, TopologyCommitted, RunStarted,
                      RunProgress, RunFinished, CollectionChanged, RecordChanged,
                      ConflictDetected, ConflictResolved };
    Kind kind = Kind::RunProgress;
    QString objectId;
    QString message;
    int progress = -1;
    QString mappingId;
    QString backendId;
    QString collectionId;
    QString recordId;
    RecordChangeKind changeKind = RecordChangeKind::Updated;
    QString canonicalDomain;
    QString canonicalEncoding;
    QByteArray canonicalPayload;
    int canonicalSchemaVersion = 1;
    Kalburator::Sync::ConflictInfo conflict;
    Kalburator::Sync::ConflictResolution resolution =
        Kalburator::Sync::ConflictResolution::AskUser;
    Kalburator::Sync::ProviderConnectionState providerState =
        Kalburator::Sync::ProviderConnectionState::Disconnected;
    QString providerError;
    QString providerWarning;
    QList<Kalburator::Sync::CollectionInfo> providerCollections;
    bool mappingStarted = false;
    bool mappingFinished = false;
    bool mappingSuccess = false;
    bool mappingCancelled = false;
    int pass = 0;
    int maxPasses = 0;
    QDateTime lastSuccessfulSync;
    RunProgressKind progressKind = RunProgressKind::Overall;
    CollectionChangeKind collectionChangeKind = CollectionChangeKind::Updated;
    Kalburator::Sync::CollectionInfo collection;
    QString providerId;
    bool collectionMutationCommitted = false;
};

/// Host-owned scarce-resource boundary. Implementations must be generic: no
/// Palm, network-provider, or application model types cross this interface.
class ExternalResourceLease {
public:
    virtual ~ExternalResourceLease() = default;
    /// Install the runtime's link/resource-loss callback. The lease invokes
    /// it when an acquired resource becomes unavailable during a run.
    virtual void setLossHandler(std::function<void()> handler)
    { Q_UNUSED(handler) }
    virtual bool prepare(QString &errorMessage) = 0;
    virtual bool executePhase(const QString &phase, QString &errorMessage) = 0;
    virtual bool flush(QString &errorMessage) = 0;
    virtual void cancel() = 0;
    virtual void finish(const RunResult &result) = 0;
};

struct RuntimeDefinition {
    QString storagePath;
    QList<Kalburator::Sync::BackendConfiguration> providers;
    QList<QSharedPointer<BackendFactory>> backendFactories;
    // Consumer extensions and backend factories share the same neutral
    // validation boundary during the construction phase.
    QList<QSharedPointer<BackendFactory>> extensions;
    // Plugin objects remain owned by the consumer; the runtime owns their
    // registration and keeps the pointers valid for its lifetime.
    QList<QPair<Kalburator::Plugin *, Kalburator::PluginManifest>> pluginExtensions;
    struct Resource {
        QString id;
        QSharedPointer<ExternalResourceLease> lease;
    };
    QList<Resource> resources;
    QSharedPointer<Kalburator::Sync::SecretStore> secrets;
    RuntimePolicy policy;
    QSharedPointer<TopologyPersistenceParticipant> topologyPersistence;
};

/// Public ownership boundary for one application collection/profile.
/// Construction and topology/run methods are the only routine entry points;
/// registries, stores, engines, threads, and mutable mappings stay private.
class CollectionRuntime {
public:
    virtual ~CollectionRuntime() = default;

    static std::unique_ptr<CollectionRuntime> create(const RuntimeDefinition &definition,
                                                     QString &errorMessage);

    virtual RuntimeSnapshot snapshot() const = 0;
    /// Borrowed runtime-owned provider manager for presentation adapters.
    /// Provider and backend lifetime remains exclusively with the runtime.
    virtual Kalburator::Sync::ProviderManager *providerManager() const = 0;

    /// Borrowed runtime-owned backend registry for presentation adapters.
    /// Backend lifetime remains exclusively with the runtime.
    virtual Kalburator::Sync::BackendRegistry *backendRegistry() const = 0;
    /// Return the QObject wrapper for a materialized endpoint, or nullptr if
    /// the id is not known.  The pointer remains valid only for the runtime's
    /// lifetime; consumers must not store it or delete it.  This is the narrow
    /// bridge that lets the host reach a runtime-owned backend for discovery
    /// and loading without taking a second ownership share.
    virtual QObject *backendObject(const QString &endpointId) const = 0;
    virtual void setEventSink(std::function<void(const RuntimeEvent &)> sink) = 0;
    virtual bool addProvider(const Kalburator::Sync::BackendConfiguration &config,
                             QString &errorMessage) = 0;
    virtual bool updateProvider(const Kalburator::Sync::BackendConfiguration &config,
                                QString &errorMessage) = 0;
    virtual bool removeProvider(const QString &providerId,
                                QString &errorMessage) = 0;
    virtual bool updatePolicy(const RuntimePolicy &policy,
                              QString &errorMessage) = 0;
    virtual QFuture<bool> connectProviders() = 0;
    virtual TopologyResult applyTopology(const TopologyDefinition &desired) = 0;
    virtual QFuture<RunResult> run(const RunRequest &request) = 0;
    /// Persist and queue a user decision for replay on the next run.
    /// CustomMerge requires the merged native payload used by the backend.
    virtual bool resolveConflict(const QString &conflictId,
                                 Kalburator::Sync::ConflictResolution resolution,
                                 const QString &mergedNative = QString()) = 0;
    /// Return the unresolved persisted conflict backlog without exposing the
    /// conflict store or manager. Every returned item has AskUser resolution.
    virtual QList<Kalburator::Sync::ConflictInfo> unresolvedConflicts() const = 0;
    virtual void cancel() = 0;
    /// Close stores, remove this runtime's database, and reopen cleanly.
    /// The operation is explicit and scoped to this definition's path.
    virtual bool reset(QString &errorMessage) = 0;
};

} // namespace Kalburator::Runtime

#endif
