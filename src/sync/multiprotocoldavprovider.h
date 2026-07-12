#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H

#include "iprovider.h"
#include "backendconfiguration.h"  // PerCalendarCapabilities (retained for priming)

#include <QFutureWatcher>
#include <QHash>
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
 * into a single collections() list. Collection ids are bare DAV URL slugs
 * (davSlugFromUrl(href)) — no per-domain prefix; CollectionInfo::type
 * ("calendar"/"contacts") distinguishes the two domains. createBackends()
 * emits up to two ProviderBackendSpecs: one "cal" spec (a single
 * RemoteCalendarBackend hosting every discovered calendar) and — when
 * !m_calendarsOnly and addressbooks were discovered — one "contacts" spec
 * (a single RemoteContactsBackend hosting every discovered addressbook).
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
    std::vector<ProviderBackendSpec> createBackends() override;

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

    // Task 2.2: re-keyed by slug (davSlugFromUrl(href) for CalDAV; CardDAV
    // discovery already keys by last-path-segment id), NOT the discoveries'
    // raw QMaps — those are consumed as locals in maybeResolveConnect() and
    // never retained. Consumed by createBackends() to register + prime the
    // single "cal" backend's calendars and register the single "contacts"
    // backend's addressbooks.
    QHash<QString, QString> m_calUrlBySlug;    // cal slug -> href
    QHash<QString, PerCalendarCapabilities> m_calCapsBySlug;  // cal slug -> caps
    QHash<QString, QString> m_contactsUrlBySlug;  // contacts slug -> href

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
    // Per-calendar capabilities retained from CalDAV discovery so
    // createBackends() can prime each RemoteCalendarBackend
    // (keyed by inner calendarId).
    QMap<QString, PerCalendarCapabilities> m_calDavCaps;

    std::shared_ptr<QPromise<bool>> m_connectPromise;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_MULTIPROTOCOLDAVPROVIDER_H
