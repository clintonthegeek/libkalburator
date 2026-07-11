#ifndef KALBURATOR_SYNC_NEUTRALPROVIDER_H
#define KALBURATOR_SYNC_NEUTRALPROVIDER_H

#include <functional>
#include <memory>
#include "iprovider.h"
#include "collectioninfo.h"

namespace Kalburator::Sync {

class IBlobBackend;

/**
 * @brief Account-neutral IProvider adapter.
 *
 * RETAINED: K.7.1 scaffolding — bridges local/file backends into the IProvider
 * shape for consumers that only know about IProvider (e.g. ProviderManager).
 * Review for production consumer or remove next quarter if none materialises.
 *
 * Wraps a single-collection backend factory and a CollectionInfo so that
 * backends with no account concept (local files, raw-file sinks, etc.)
 * fit the IProvider shape without requiring a full CalDAV/CardDAV provider.
 *
 * The provider is always considered "connected" once connect() is called;
 * there is no network auth or capability discovery.
 */
class NeutralProvider : public IProvider {
    Q_OBJECT
public:
    using BackendFactory = std::function<std::unique_ptr<IBlobBackend>()>;

    NeutralProvider(QString kind, CollectionInfo info, BackendFactory factory,
                    QObject *parent = nullptr);
    ~NeutralProvider() override;

    // Non-copyable/non-movable (QObject rule)
    NeutralProvider(const NeutralProvider &) = delete;
    NeutralProvider &operator=(const NeutralProvider &) = delete;

    // ── IProvider ─────────────────────────────────────────────────────
    QString id() const override { return m_id; }
    QString kind() const override { return m_kind; }
    QString displayName() const override { return m_info.name; }

    void load(const BackendConfiguration &config) override;
    BackendConfiguration save() const override;

    QWidget *createConfigWidget(QWidget *parent) override;
    QFuture<bool> connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return { m_info }; }
    std::unique_ptr<IBlobBackend> createBackend(const QString &collectionId) override;

    // PHASE2-TASK2.3 — v2 contract for the account-neutral adapter.
    // NeutralProvider always models exactly one collection (m_info),
    // so the entry point returns at most one spec and only when the
    // caller asks for the matching collectionId.
    //
    // BackendKind is inferred from m_info.type with sane defaults so
    // the local-file use case collapses cleanly:
    //   * "calendar" → BackendKind::Calendar
    //   * "contacts" → BackendKind::Contacts
    //   * "" / "journal" / unknown → BackendKind::Calendar (Phase 2
    //     task spec: "always Calendar for its use case" when no kind
    //     hint is available — NeutralProvider is overwhelmingly used
    //     to bridge local-calendar fixtures into ProviderManager).
    //
    // backendId format mirrors the CalDav / CardDav / multi-protocol
    // DAV providers' "<providerId>:<collectionId>:<stableSlug>" triple,
    // but uses m_info.id itself as the slug (NeutralProvider has no
    // server-derived href; the manager's spec-merge isn't expected to
    // deep-compare slugs across providers, only across collections in
    // the same provider, and a single-collection neutral provider
    // cannot produce a collision in its own domain).
    //
    // Returns {} when: not connected, collectionId is empty, or
    // collectionId doesn't match m_info.id. Matches the v1
    // createBackend() nullptr contract for the same inputs.
    QList<ProviderBackendSpec>
        createBackends(const QString &collectionId) const override;

private:
    QString          m_id;
    QString          m_kind;
    CollectionInfo   m_info;
    BackendFactory   m_factory;
    bool             m_connected = false;
};

} // namespace Kalburator::Sync

#endif
