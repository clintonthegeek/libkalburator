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
 * Phase L.5/L.7: createBackend() routes calendar + contacts collections.
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

    // Returns nullptr until Phase L.5/L.7 wire in real backends.
    std::unique_ptr<IBlobBackend>
        createBackend(const QString &collectionId) override;

    // PHASE2-TASK2.3 — v2 contract for Akonadi's "provider == resource"
    // topology. Akonadi surfaces one collection per discovered mimetype
    // collection (Event/Todo/Addressee), each with a stable Collection
    // id that the resource owns — so unlike DAV providers where
    // domainId is one of {cal, contacts}, here domainId IS the
    // collectionId. For the given collectionId (which must be in
    // collections()) returns ONE spec whose:
    //
    //   spec.collectionId = collectionId
    //   spec.kind           = Calendar (mimes include Event or Todo) or
    //                         Contacts (mime includes Addressee)
    //   spec.backendId      = "<providerId>:<collectionId>:<collectionId>"
    //                         (slug = collectionId since Akonadi ids
    //                         are already stable and unique within a
    //                         resource)
    //   spec.displayName    = m_collections[i].name or collectionId
    //   spec.contentTypes   = m_collections[i].contentTypes verbatim
    //                         (Akonadi discovery already returned the
    //                         mime-derived content types)
    //
    // The body lives next to AkonadiProvider::createBackend() in the
    // .cpp because Akonadi::CollectionFetchScope / MIME detection live
    // in the same #ifdef-gated block there. When HAVE_AKONADI is OFF,
    // there is no class at all (this entire file is fenced), so the
    // override's mere presence is only relevant when HAVE_AKONADI is
    // ON — and the implementation there returns a real spec for every
    // owned collection, not the Phase-1 empty stub.
    //
    // Returns {} when: not connected, collectionId is empty, or
    // collectionId isn't in m_collections. Matches the v1
    // createBackend() nullptr contract.
    QList<ProviderBackendSpec>
        createBackends(const QString &collectionId) const override;

private:
    void onCollectionsFetched(KJob *job);

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
