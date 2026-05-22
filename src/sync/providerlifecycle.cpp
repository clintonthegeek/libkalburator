#include "providerlifecycle.h"

#include <QDebug>

#include <KConfig>
#include <KConfigGroup>

#include "backendregistry.h"
#include "providermanager.h"
#include "iprovider.h"
#include "backendconfiguration.h"

namespace Kalburator::Sync {

ProviderLifecycle::ProviderLifecycle(BackendRegistry *registry,
                                     ProviderManager *manager,
                                     QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_manager(manager)
{
    Q_ASSERT(registry);
    Q_ASSERT(manager);
}

ProviderLifecycle::~ProviderLifecycle() = default;

// ── Profile path ──────────────────────────────────────────────────────────────

void ProviderLifecycle::setProfilePath(const QString &path)
{
    if (m_profilePath.isEmpty())
        m_profilePath = path;
}

// ── Bulk config-source operations ─────────────────────────────────────────────

void ProviderLifecycle::loadFromProfile()
{
    if (!m_manager) return;
    if (m_profilePath.isEmpty()) return;

    KConfig cfg(m_profilePath, KConfig::SimpleConfig);
    KConfigGroup providersGroup(&cfg, QStringLiteral("Providers"));
    m_manager->loadFromProfile(providersGroup);
}

void ProviderLifecycle::saveToProfile() const
{
    if (!m_manager) return;
    if (m_profilePath.isEmpty()) return;

    KConfig cfg(m_profilePath, KConfig::SimpleConfig);
    KConfigGroup providersGroup(&cfg, QStringLiteral("Providers"));
    m_manager->saveToProfile(providersGroup);
    cfg.sync();
}

// ── Per-provider operations ───────────────────────────────────────────────────

QString ProviderLifecycle::provisionProvider(std::unique_ptr<IProvider> provider)
{
    if (!provider) {
        qWarning() << "ProviderLifecycle::provisionProvider: null provider";
        return {};
    }
    if (!m_manager) {
        qWarning() << "ProviderLifecycle::provisionProvider:"
                   << "no provider manager (init failed?)";
        return {};
    }

    // Capture id BEFORE std::move (the unique_ptr is consumed).
    const QString uuid = provider->id();
    m_manager->addProvider(std::move(provider));

    saveToProfile();

    QFuture<void> f = m_manager->connectAll();
    f.then(this, [this]() { emit backendsReady(); });

    emit providerProvisioned(uuid);
    return uuid;
}

bool ProviderLifecycle::updateProvider(const QString &uuid,
                                       const BackendConfiguration &cfg)
{
    if (!m_manager) return false;
    auto *provider = m_manager->providerById(uuid);
    if (!provider) return false;
    provider->applyConfig(cfg);
    saveToProfile();
    emit providerUpdated(uuid);
    return true;
}

} // namespace Kalburator::Sync
