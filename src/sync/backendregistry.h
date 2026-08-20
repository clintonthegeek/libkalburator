#ifndef KALBURATOR_SYNC_BACKENDREGISTRY_H
#define KALBURATOR_SYNC_BACKENDREGISTRY_H

#include <QObject>
#include <QMap>
#include <QMutex>
#include <QString>
#include <memory>
#include "backendcontribution.h"
#include "syncbackendbase.h"

namespace Kalburator::Sync {

class SyncBackend;

/**
 * @brief Registry for sync backends and backend contributions.
 *
 * Manages live backend instances (registered by ProviderManager) and
 * BackendContributions (registered by plugins at startup via
 * BackendContribution::createProvider()).
 */
class BackendRegistry : public QObject
{
    Q_OBJECT
public:
    explicit BackendRegistry(QObject *parent = nullptr);
    ~BackendRegistry() override = default;

    /**
     * @brief Register a live backend instance by ID.
     *
     * Used by CollectionController to make backends accessible
     * to the SyncRouter by their configured ID.
     */
    void registerBackendInstance(const QString &backendId, SyncBackendBase *backend);

    /**
     * @brief Unregister a backend instance.
     */
    void unregisterBackendInstance(const QString &backendId);

    /**
     * @brief Get a registered backend instance by ID.
     */
    SyncBackendBase* backendInstance(const QString &backendId) const;

    /**
     * @brief Get all registered backend instance IDs.
     */
    QStringList registeredInstanceIds() const;

    // ── BackendContribution API (K.7) ─────────────────────────────────
    // (Phase Q.1, 2026-05-21: instance() singleton removed; each
    //  session/test/embedder owns its own BackendRegistry.)

    /**
     * @brief Register a BackendContribution.
     * @return true on success, false if a contribution with the same
     *         backendType() is already registered.
     */
    bool registerContribution(std::shared_ptr<BackendContribution> contribution);

    /**
     * @brief Look up a contribution by backendType.
     * @return Pointer to the contribution, or nullptr if not found.
     *         Ownership remains with the registry.
     */
    BackendContribution* contributionFor(const QString &backendType) const;

    /**
     * @brief All registered contributions (order unspecified).
     */
    QList<BackendContribution*> contributions() const;

    /**
     * @brief Remove a contribution by backendType. No-op if not found.
     */
    void unregisterContribution(const QString &typeName);

    /**
     * @brief Clear all state: instances and contributions.
     *        Intended for test teardown only.
     */
    void clear();

signals:
    /**
     * @brief Emitted when a backend instance is registered.
     */
    void backendInstanceRegistered(const QString &backendId);

    /**
     * @brief Emitted when a backend instance is unregistered.
     */
    void backendInstanceUnregistered(const QString &backendId);

    /// O.1.1: Emitted after a BackendContribution is added. UI pickers
    /// listen to this to keep their kind lists in sync with runtime
    /// plugin registration.
    void contributionRegistered(const QString &backendType);

    /// O.1.1: Emitted after a BackendContribution is removed.
    void contributionUnregistered(const QString &backendType);

private:
    QMap<QString, SyncBackendBase*> m_instances;
    QMap<QString, std::shared_ptr<BackendContribution>> m_contributions;

    /// Parallel-sync Task 12 prerequisite: registerBackendInstance()/
    /// unregisterBackendInstance() mutate m_instances from the GUI thread
    /// (ProviderManager, on provider connect/removal) with no lock, while
    /// backendInstance() is read from every SyncEngineWorker thread during
    /// a run. Pre-existing at concurrency 1 (a narrow sampling window);
    /// N>1 raises the number of concurrent reads in flight and with it the
    /// chance of overlapping a mutation. Same shape as
    /// TransformationRegistry::m_frozenDomainsMutex (Task 16) — guards
    /// only m_instances, not m_contributions (plugin-registration-time
    /// only, never touched mid-sync).
    mutable QMutex m_instancesMutex;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_BACKENDREGISTRY_H
