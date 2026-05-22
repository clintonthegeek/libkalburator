#ifndef KALBURATOR_SYNC_PROVIDERLIFECYCLE_H
#define KALBURATOR_SYNC_PROVIDERLIFECYCLE_H

#include <QObject>
#include <QString>
#include <QFuture>
#include <memory>

class KConfigGroup;

namespace Kalburator::Sync {

class BackendRegistry;
class ProviderManager;
class IProvider;
struct BackendConfiguration;

/**
 * @brief Owns the provider provisioning / persistence lifecycle.
 *
 * Extracted from PlanStan::CollectionController per spec
 * 2026-05-22-collectioncontroller-decomp-and-akonadi-api-design.md (step 1).
 *
 * Responsibilities:
 *  - Persist providers to/from a KConfig sidecar file alongside the
 *    host's collection document.
 *  - Add, update, and remove providers through ProviderManager.
 *  - Drive provider connection (ProviderManager::connectAll) after
 *    provisioning, then notify the host via backendsReady() so the
 *    host can mirror provider-supplied backends into its own tables.
 *
 * What this class does NOT own:
 *  - The main backend hash (m_backends in CC) — mirroring provider
 *    backends into the legacy hash is CC's job; this class notifies
 *    via backendsReady().
 *  - Logical-calendar cascade logic on removal — that touches the
 *    host's config store (KalbConfigManager / ISyncConfigStore) and
 *    stays in CC::removeProvider for now.
 *  - Wizard-binding rewrite — stays in CC (needs host config store).
 *
 * Config-source coupling: ProviderLifecycle takes a pre-resolved
 * profile path string. The host resolves the path (e.g. from
 * KalbConfigManager) and passes it in via setProfilePath(). This
 * keeps libkalburator free of libkalcal dependencies.
 */
class ProviderLifecycle : public QObject
{
    Q_OBJECT
public:
    /**
     * @param registry  Borrowed. Must outlive this object.
     * @param manager   Borrowed. Must outlive this object.
     * @param parent    Qt parent.
     */
    explicit ProviderLifecycle(BackendRegistry *registry,
                               ProviderManager *manager,
                               QObject *parent = nullptr);
    ~ProviderLifecycle() override;

    // ── Profile path ──────────────────────────────────────────────────────

    /**
     * @brief Set the path to the KConfig sidecar file holding provider
     * configuration (typically "<kalb-path>.providers").
     *
     * Has no effect if the path is already set (idempotent for the
     * wizard pre-provisioning use-case where the host sets the path
     * before the full kalb load).
     */
    void setProfilePath(const QString &path);
    QString profilePath() const { return m_profilePath; }

    // ── Bulk config-source operations ──────────────────────────────────────

    /**
     * @brief Load providers from the sidecar KConfig file.
     * No-op if ProviderManager is null or profile path is empty.
     */
    void loadFromProfile();

    /**
     * @brief Persist providers to the sidecar KConfig file.
     * No-op if ProviderManager is null or profile path is empty.
     */
    void saveToProfile() const;

    // ── Per-provider operations ────────────────────────────────────────────

    /**
     * @brief Register a new provider with ProviderManager, persist the
     * sidecar, and drive ProviderManager::connectAll().
     *
     * Emits backendsReady() once connectAll() resolves — the host
     * should connect to this signal and call its mirrorProviderBackends()
     * (or equivalent) in response.
     *
     * @param provider  Ownership transferred. Must be non-null.
     * @return The provider's UUID (captured before the move), or empty
     *         string if provider was null or ProviderManager is absent.
     */
    QString provisionProvider(std::unique_ptr<IProvider> provider);

    /**
     * @brief Apply an edited config to a provider.
     * @return true on success; false if uuid not found or no manager.
     */
    bool updateProvider(const QString &uuid,
                        const BackendConfiguration &cfg);

    // ── Accessors ────────────────────────────────────────────────────────

    ProviderManager *providerManager() const { return m_manager; }
    BackendRegistry *backendRegistry() const { return m_registry; }

Q_SIGNALS:
    /**
     * @brief Emitted after ProviderManager::connectAll() resolves
     * following a provisionProvider() call.
     *
     * The host (CollectionController) connects this to its
     * mirrorProviderBackends() slot so provider-supplied backends
     * are wired into m_backends and initial discovery is triggered.
     */
    void backendsReady();

    /**
     * @brief Emitted when a provider is successfully provisioned.
     * @param uuid  The provider's id.
     */
    void providerProvisioned(const QString &uuid);

    /**
     * @brief Emitted when a provider's config is updated.
     * @param uuid  The provider's id.
     */
    void providerUpdated(const QString &uuid);

private:
    BackendRegistry *m_registry;  // borrowed
    ProviderManager *m_manager;   // borrowed
    QString          m_profilePath;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_PROVIDERLIFECYCLE_H
