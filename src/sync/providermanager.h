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

class KConfigGroup;

namespace Kalburator::Sync {

class IProvider;
class BackendRegistry;
class IBlobBackend;

/// O.1.2: Per-provider connection state. Returned by
/// ProviderManager::providerState(id) and reported on
/// providerStateChanged signal. Supersedes the boolean overload
/// providerConnectionStateChanged(id, bool) (preserved for one
/// release; removed in Phase O.4).
enum class ProviderConnectionState {
    Disconnected,
    Connecting,   ///< Reserved. Not yet emitted — requires IProvider::connectionStateChanged
                  ///< to become enum-typed (planned for Phase O.3).
    Connected,
    Error         ///< Reserved. Not yet emitted — requires richer IProvider error signal (O.3).
};

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

signals:
    void providerConnectionStateChanged(QString providerId, bool connected);
    void providersChanged();
    /// O.1.2: Per-provider connection state change. Supersedes
    /// providerConnectionStateChanged(QString, bool) which is preserved
    /// for one release (removed in Phase O.4).
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

Q_DECLARE_METATYPE(Kalburator::Sync::ProviderConnectionState)

#endif
