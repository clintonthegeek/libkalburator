#ifndef KALBURATOR_SYNC_BACKENDREGISTRY_H
#define KALBURATOR_SYNC_BACKENDREGISTRY_H

#include <QObject>
#include <QMap>
#include <QString>
#include <memory>
#include "backendcontribution.h"

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
    void registerBackendInstance(const QString &backendId, SyncBackend *backend);

    /**
     * @brief Unregister a backend instance.
     */
    void unregisterBackendInstance(const QString &backendId);

    /**
     * @brief Get a registered backend instance by ID.
     */
    SyncBackend* backendInstance(const QString &backendId) const;

    /**
     * @brief Get all registered backend instance IDs.
     */
    QStringList registeredInstanceIds() const;

    // ── BackendContribution API (K.7) ─────────────────────────────────
    /**
     * @brief Process-wide singleton instance.
     */
    static BackendRegistry& instance();

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

private:
    QMap<QString, SyncBackend*> m_instances;
    QMap<QString, std::shared_ptr<BackendContribution>> m_contributions;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_BACKENDREGISTRY_H
