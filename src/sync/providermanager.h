#ifndef KALBURATOR_SYNC_PROVIDERMANAGER_H
#define KALBURATOR_SYNC_PROVIDERMANAGER_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QFuture>
#include <memory>
#include <map>
#include <vector>

#include "iprovider.h"  // ProviderConnectionState (Task 4: relocated here)

class KConfigGroup;

namespace Kalburator::Sync {

class BackendRegistry;
class IBlobBackend;

// O.1.2: Per-provider connection state, returned by
// ProviderManager::providerState(id) and reported on providerStateChanged.
// Task 4 (sync-graph redesign Phase 1) relocated the enum itself to
// iprovider.h — see Kalburator::Sync::ProviderConnectionState there for the
// definition and emission contract. Connecting/Error are now actually
// emitted by the DAV-family providers' own connectionStateChanged
// overload; ProviderManager's own m_providerStates mirror below still only
// derives from the legacy bool overload (Connected/Disconnected) — wiring
// Connecting/Error through the manager is left to a later task.

/**
 * @brief Per-profile owner of IProvider instances.
 *
 * Instantiates providers from a KConfigGroup, drives their lifecycle
 * (load -> connect -> register backends -> disconnect), and exposes
 * the list to the app's settings UI. Apps own one ProviderManager per
 * loaded profile.
 *
 * Provider construction is delegated to BackendContribution objects
 * registered in the BackendRegistry. Register contributions before
 * calling loadFromProfile().
 */
class ProviderManager : public QObject
{
    Q_OBJECT
public:
    explicit ProviderManager(BackendRegistry *registry,
                             QObject *parent = nullptr);
    ~ProviderManager() override;

    void loadFromProfile(const KConfigGroup &providersGroup);
    void saveToProfile(KConfigGroup &providersGroup) const;

    void addProvider(std::unique_ptr<IProvider> provider);
    void removeProvider(const QString &providerId);

    QFuture<void> connectAll();
    void disconnectAll();

    QList<IProvider*> providers() const;
    IProvider *providerById(const QString &id) const;
    BackendRegistry *backendRegistry() const { return m_registry; }

    /// O.1.2: Current connection state for a provider, or Disconnected
    /// if the id is unknown (safe default).
    ProviderConnectionState providerState(const QString &id) const;

    /// Registered backend ids ("<providerId>:<domainId>") currently owned
    /// by this provider. Empty if the provider is unknown or has no
    /// registered backends.
    QStringList backendIdsForProvider(const QString &providerId) const;

signals:
    void providersChanged();
    /// O.1.2: Per-provider connection state change.
    void providerStateChanged(QString providerId,
                              ProviderConnectionState state);

private slots:
    void onProviderConnectionStateChanged(bool connected);
    void onProviderCollectionsChanged();

private:
    void registerProviderBackends(IProvider *provider);
    void unregisterProviderBackends(IProvider *provider);
    void wireProviderSignals(IProvider *provider);

    BackendRegistry                              *m_registry;  // borrowed
    // std::vector instead of QList: QList copies on detach, which fails
    // for move-only unique_ptr.
    std::vector<std::unique_ptr<IProvider>>       m_providers;
    // std::map (not QHash) because std::unique_ptr is move-only and QHash's
    // growth path requires copyable values. std::unordered_map is unavailable
    // because Qt6 doesn't ship a std::hash<QString> specialisation.
    std::map<QString, std::unique_ptr<IBlobBackend>> m_ownedBackends;
    // O.1.2: per-provider state mirror. Updated from
    // onProviderConnectionStateChanged. Keyed by IProvider::id().
    QHash<QString, ProviderConnectionState> m_providerStates;
};

} // namespace Kalburator::Sync

// Q_DECLARE_METATYPE(Kalburator::Sync::ProviderConnectionState) now lives in
// iprovider.h (Task 4) — declaring it here too would be a duplicate
// QMetaType specialization in any TU that includes both headers.

#endif
