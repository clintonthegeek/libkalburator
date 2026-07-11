#ifndef KALBURATOR_SYNC_CALDAVPROVIDER_H
#define KALBURATOR_SYNC_CALDAVPROVIDER_H

#include "iprovider.h"
#include "backendconfiguration.h"  // PerCalendarCapabilities (retained for priming)

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
    std::unique_ptr<IBlobBackend>
        createBackend(const QString &collectionId) override;

    // PHASE2-TASK2.1 (slug consolidated in Task 2.3) — implements the v2
    // contract for CalDAV calendars. For the given collectionId (which
    // must be in collections()) returns ONE Calendar-kind
    // ProviderBackendSpec sourced from the connect-time
    // m_calendarUrls / m_perCalendarCaps caches:
    //
    //   spec.collectionId = collectionId
    //   spec.kind           = BackendKind::Calendar
    //   spec.backendId      = "<providerId>:<collectionId>:<stableSlug>"
    //   spec.displayName    = m_collections[i].name (i.e. caps serverDisplayName,
    //                         falling back to collectionId) — never empty.
    //   spec.color          = caps.serverColor.name() when valid, else ""
    //   spec.contentTypes   = {"VEVENT", "VTODO"} filtered by caps flags
    //
    // stableSlug is computed by Kalburator::Sync::makeDavSlug() (see
    // src/sync/davslug.h) — the shared helper that Tasks 2.1 / 2.2 /
    // 2.3 all delegate to so the three DAV providers emit identical
    // backendId shapes (last non-empty path segment of the href,
    // sanitised; rawName fallback when the href yields no usable
    // characters). Renames don't invalidate storage ids AND two
    // collections on the same account with similar display names
    // can't collide.
    //
    // Returns {} when: not connected, collectionId is empty, or
    // collectionId isn't in m_calendarUrls. This matches the v1
    // createBackend()'s nullptr contract for the same inputs.
    QList<ProviderBackendSpec>
        createBackends(const QString &collectionId) const override;

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
    QMap<QString, QString>               m_calendarUrls;   // collectionId -> href
    // Per-calendar capabilities copied out of the discovery before it is
    // deleteLater()'d, so createBackend() can prime each backend (color +
    // content types) without keeping the discovery object alive.
    QMap<QString, PerCalendarCapabilities> m_perCalendarCaps;  // collectionId -> caps
    std::unique_ptr<QPromise<bool>>      m_connectPromise;
};

} // namespace Kalburator::Sync

#endif
