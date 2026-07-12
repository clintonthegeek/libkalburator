#ifndef KALBURATOR_SYNC_CALDAVPROVIDER_H
#define KALBURATOR_SYNC_CALDAVPROVIDER_H

#include "iprovider.h"
#include "backendconfiguration.h"  // PerCalendarCapabilities (retained for priming)

#include <QHash>
#include <QMap>
#include <QPromise>
#include <QUrl>

#include <memory>

namespace Kalburator::Sync {

class CalDavCapabilityDiscovery;

/**
 * @brief CalDAV-speaking provider. Wraps CalDavCapabilityDiscovery
 *        (capability + collection enumeration) and RemoteCalendarBackend
 *        (per-collection sync) behind the IProvider interface.
 *
 * Phase H supports calendar collections only; CardDAV is Phase I.
 *
 * Configuration (BackendConfiguration::connectionParams):
 *   - "url"      QString — server base URL
 *   - "username" QString
 *   - "password" QString — plaintext (Phase H baseline; KWallet later)
 */
class CalDavProvider : public IProvider
{
    Q_OBJECT
public:
    explicit CalDavProvider(QObject *parent = nullptr);
    ~CalDavProvider() override;

    QString id() const override { return m_id; }
    QString kind() const override;
    QString displayName() const override { return m_displayName; }

    void load(const BackendConfiguration &config) override;
    BackendConfiguration save() const override;

    QWidget *createConfigWidget(QWidget *parent) override;

    QFuture<bool> connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }

    QList<CollectionInfo> collections() const override
    { return m_collections; }
    std::vector<ProviderBackendSpec> createBackends() override;

    QString lastError() const override { return m_lastError; }

private slots:
    void onDiscoveryFinished(bool success);

private:
    QString                              m_id;             // UUID
    QString                              m_displayName;
    QUrl                                 m_serverUrl;
    QString                              m_username;
    QString                              m_password;
    bool                                 m_connected = false;
    QString                              m_lastError;
    QList<CollectionInfo>                m_collections;

    CalDavCapabilityDiscovery           *m_discovery = nullptr;
    // Task 2.1: re-keyed by slug (davSlugFromUrl(href)), NOT the discovery's
    // display-name-ish key — discovery's raw QMaps are consumed as locals in
    // onDiscoveryFinished() and never retained. Slugs are stable per-account
    // calendar ids (survive renames, unique within a calendar home), unlike
    // display names. Populated at connect() time; consumed by createBackends()
    // to register + prime the single "cal" backend's calendars.
    QHash<QString, QString>              m_urlBySlug;   // slug -> href
    QHash<QString, PerCalendarCapabilities> m_capsBySlug;  // slug -> caps
    std::unique_ptr<QPromise<bool>>      m_connectPromise;
};

} // namespace Kalburator::Sync

#endif
