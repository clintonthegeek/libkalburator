#ifndef KALBURATOR_SYNC_AKONADIPROVIDER_H
#define KALBURATOR_SYNC_AKONADIPROVIDER_H

#ifdef HAVE_AKONADI

#include "iprovider.h"

#include <Akonadi/Session>

#include <QPromise>

#include <memory>

class KJob;

namespace Kalburator::Sync {

/**
 * @brief Akonadi-backed IProvider.
 *
 * Wraps a local Akonadi session (KPim6::AkonadiCore) behind the IProvider
 * interface. A single AkonadiProvider represents "the local Akonadi store"
 * on this machine — the user configures it once and all Akonadi
 * collections become available as sub-resources.
 *
 * Phase L.1: skeleton only. connect() always resolves false.
 * Phase L.4: real Akonadi session open.
 * Phase L.5/L.7: createBackends() routes calendar + contacts collections.
 * Phase L.8: createConfigWidget() provides account-setup UI.
 *
 * Configuration (BackendConfiguration::connectionParams):
 *   (none for Phase L.1 — Akonadi is local, no auth required)
 */
class AkonadiProvider : public IProvider
{
    Q_OBJECT
public:
    explicit AkonadiProvider(bool calendarsOnly = true, QObject *parent = nullptr);
    ~AkonadiProvider() override;

    QString id() const override { return m_id; }
    QString kind() const override;
    QString displayName() const override { return m_displayName; }

    void load(const BackendConfiguration &config) override;
    BackendConfiguration save() const override;

    // Returns nullptr until Phase L.8 fills in a config widget.
    QWidget *createConfigWidget(QWidget *parent) override;

    QFuture<bool> connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }

    QList<CollectionInfo> collections() const override
    { return m_collections; }

    std::vector<ProviderBackendSpec> createBackends() override;

private:
    void onCollectionsFetched(KJob *job);

    // Behavior-preserving per-collection construction, unchanged body from
    // the old public createBackend(collectionId). createBackends() wraps
    // this in a loop over m_collections (one spec per collection) —
    // Akonadi stays this shape permanently (no Task 2 collapse).
    std::unique_ptr<IBlobBackend>
        createBackendForCollection(const QString &collectionId);

    QString               m_id;           // stable UUID
    QString               m_displayName;
    bool                  m_calendarsOnly = true;
    bool                  m_connected = false;
    QList<CollectionInfo> m_collections;

    std::shared_ptr<QPromise<bool>> m_connectPromise;
    Akonadi::Session               *m_session = nullptr;
};

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
#endif // KALBURATOR_SYNC_AKONADIPROVIDER_H
