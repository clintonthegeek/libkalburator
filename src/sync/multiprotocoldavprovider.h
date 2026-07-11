#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H

#include "iprovider.h"
#include "backendconfiguration.h"  // PerCalendarCapabilities (retained for priming)

#include <QFutureWatcher>
#include <QMap>
#include <QPromise>
#include <QUrl>

#include <memory>

namespace Kalburator::Sync {

class CalDavCapabilityDiscovery;
class CardDavCapabilityDiscovery;

/**
 * @brief DAV provider that speaks both CalDAV and CardDAV against
 *        one base URL with one credential set — Nextcloud-style.
 *
 * Owns one CalDavCapabilityDiscovery and one CardDavCapabilityDiscovery,
 * runs them in parallel during connect(), and federates their results
 * into a single collections() list. Collection ids are prefixed
 * "multiproto-dav:<provider-id>:cal:<inner-id>" /
 * "multiproto-dav:<provider-id>:contacts:<inner-id>" so createBackend()
 * can dispatch by prefix.
 *
 * Configuration (BackendConfiguration::connectionParams):
 *   - "url"                       QString — server base URL
 *   - "username"                  QString
 *   - "password"                  QString — plaintext (KWallet later)
 *   - "manualCaldavPrincipal"     QString — optional override URL
 *   - "manualCarddavPrincipal"    QString — optional override URL
 */
class MultiProtocolDavProvider : public IProvider
{
    Q_OBJECT
public:
    explicit MultiProtocolDavProvider(bool calendarsOnly = true, QObject *parent = nullptr);
    ~MultiProtocolDavProvider() override;

    QString id() const override          { return m_id; }
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

    // PHASE2-TASK2.3 — implements the v2 contract for multi-protocol
    // DAV accounts (originally introduced in Task 2.2; doc updated in
    // Task 2.3 to reflect the shared-slug consolidation). The
    // provider still models ONE provider-instance but surfaces TWO
    // logical collections (calendar + contacts) when both legs were
    // discovered. For a given collectionId:
    //
    //   * if it belongs to the CalDAV leg (prefix
    //     "multiproto-dav:<m_id>:cal:"), returns ONE BackendKind::Calendar
    //     spec sourced from m_calDavUrlMap + m_calDavCaps + m_collections,
    //     using the shared Kalburator::Sync::makeDavSlug() (src/sync/
    //     davslug.h) for the stable backendId tail.
    //   * if it belongs to the CardDAV leg (prefix
    //     "multiproto-dav:<m_id>:contacts:"), returns ONE BackendKind::Contacts
    //     spec sourced from m_cardDavUrlMap + m_collections, using the
    //     same shared makeDavSlug() helper for the stable backendId tail.
    //   * otherwise {} (matches the v1 createBackend() nullptr contract).
    //
    // Returns {} when: not connected, collectionId is empty, the leg-
    // specific URL map has no entry for the inner key, or the prefix
    // does not match either leg.
    //
    // The backendId format mirrors CalDavProvider / CardDavProvider
    // (Tasks 2.1 / 2.3):
    //
    //   "<providerId>:<collectionId>:<stableSlug>"
    //
    // For multi-protocol accounts the collectionId includes the
    // "multiproto-dav:<m_id>:<leg>:" prefix (the id this provider advertises
    // in collections()), so backendIds here are longer than CalDav's,
    // but keep the same three-segment "<a>:<b>:<c>" shape so ProviderManager
    // Chapter 2.4+ can parse a uniform spec layout.
    QList<ProviderBackendSpec>
        createBackends(const QString &collectionId) const override;

    QString lastWarning() const override { return m_lastWarning; }
    QString lastError()   const override { return m_lastError; }

private slots:
    void onCalDavFinished(bool success);

private:
    void onCardDavFinished(QFutureWatcher<QList<CollectionInfo>> *w);
    void maybeResolveConnect();

    // Identity / config
    QString m_id;
    QString m_displayName;
    QUrl    m_serverUrl;
    QString m_username;
    QString m_password;
    QString m_manualCalDavPrincipal;
    QString m_manualCardDavPrincipal;

    // Runtime
    bool                  m_calendarsOnly = true;
    bool                  m_connected = false;
    QString               m_lastWarning;
    QString               m_lastError;
    QList<CollectionInfo> m_collections;
    QMap<QString, QString> m_urlByCollectionId;

    // Owned discovery objects
    CalDavCapabilityDiscovery  *m_caldavDiscovery  = nullptr;
    CardDavCapabilityDiscovery *m_carddavDiscovery = nullptr;

    // In-flight state for the parallel connect()
    bool m_calDavDone  = false;
    bool m_cardDavDone = false;
    QList<CollectionInfo> m_calDavResult;
    QList<CollectionInfo> m_cardDavResult;
    QString m_calDavError;
    QString m_cardDavError;
    QMap<QString, QString> m_calDavUrlMap;   // inner calendarId → URL href
    QMap<QString, QString> m_cardDavUrlMap;  // inner collectionId → URL href
    // Per-calendar capabilities retained from CalDAV discovery so createBackend()
    // can prime each RemoteCalendarBackend (keyed by inner calendarId).
    QMap<QString, PerCalendarCapabilities> m_calDavCaps;

    std::shared_ptr<QPromise<bool>> m_connectPromise;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H
