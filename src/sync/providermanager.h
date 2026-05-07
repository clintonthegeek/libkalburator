#ifndef KALBURATOR_SYNC_PROVIDERMANAGER_H
#define KALBURATOR_SYNC_PROVIDERMANAGER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QFuture>
#include <memory>
#include <functional>
#include <map>
#include <vector>

class KConfigGroup;

namespace Kalburator::Sync {

class IProvider;
class BackendRegistry;
class IBlobBackend;

/**
 * @brief Per-profile owner of IProvider instances.
 *
 * Instantiates providers from a KConfigGroup, drives their lifecycle
 * (load -> connect -> register backends -> disconnect), and exposes
 * the list to the app's settings UI. Apps own one ProviderManager per
 * loaded profile.
 *
 * Provider construction is delegated to a factory function so test
 * code can inject mock providers without registering them in the
 * production factory map.
 */
class ProviderManager : public QObject
{
    Q_OBJECT
public:
    using Factory = std::function<std::unique_ptr<IProvider>(const QString &kind)>;

    explicit ProviderManager(BackendRegistry *registry,
                             QObject *parent = nullptr);
    ~ProviderManager() override;

    void setFactoryForTest(Factory factory);

    void loadFromProfile(const KConfigGroup &providersGroup);
    void saveToProfile(KConfigGroup &providersGroup) const;

    void addProvider(std::unique_ptr<IProvider> provider);
    void removeProvider(const QString &providerId);

    QFuture<void> connectAll();
    void disconnectAll();

    QList<IProvider*> providers() const;
    IProvider *providerById(const QString &id) const;

signals:
    void providerConnectionStateChanged(QString providerId, bool connected);
    void providersChanged();

private slots:
    void onProviderConnectionStateChanged(bool connected);
    void onProviderCollectionsChanged();

private:
    void registerProviderBackends(IProvider *provider);
    void unregisterProviderBackends(IProvider *provider);
    void wireProviderSignals(IProvider *provider);

    BackendRegistry                              *m_registry;  // borrowed
    Factory                                       m_factory;
    // std::vector instead of QList: QList copies on detach, which fails
    // for move-only unique_ptr.
    std::vector<std::unique_ptr<IProvider>>       m_providers;
    // std::map (not QHash) because std::unique_ptr is move-only and QHash's
    // growth path requires copyable values. std::unordered_map is unavailable
    // because Qt6 doesn't ship a std::hash<QString> specialisation.
    std::map<QString, std::unique_ptr<IBlobBackend>> m_ownedBackends;
};

} // namespace Kalburator::Sync

#endif
