#ifndef KALBURATOR_SYNC_CARDDAVPROVIDER_H
#define KALBURATOR_SYNC_CARDDAVPROVIDER_H

#include "iprovider.h"

#include <QMap>
#include <QPromise>
#include <QUrl>

#include <memory>

namespace Kalburator::Sync {

class CardDavCapabilityDiscovery;

/**
 * @brief CardDAV-speaking provider. Wraps CardDavCapabilityDiscovery
 *        (capability + addressbook enumeration) and RemoteContactsBackend
 *        (per-collection sync) behind the IProvider interface.
 *
 * Phase Ib implements addressbook collections; CalDAV is Phase H.
 *
 * Configuration (BackendConfiguration::connectionParams):
 *   - "url"      QString — server base URL
 *   - "username" QString
 *   - "password" QString — plaintext (Phase Ib baseline; KWallet later)
 */
class CardDavProvider : public IProvider
{
    Q_OBJECT
public:
    explicit CardDavProvider(QObject *parent = nullptr);
    ~CardDavProvider() override;

    QString id() const override { return m_id; }
    QString kind() const override { return QStringLiteral("carddav"); }
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

    // PHASE2-TASK2.3 — implements the v2 contract for CardDAV
    // addressbooks. For the given collectionId (which must be in
    // collections()) returns ONE BackendKind::Contacts
    // ProviderBackendSpec sourced from m_addressbookUrls / m_collections:
    //
    //   spec.collectionId = collectionId
    //   spec.kind           = BackendKind::Contacts
    //   spec.backendId      = "<providerId>:<collectionId>:<stableSlug>"
    //   spec.displayName    = m_collections[i].name (serverDisplayName,
    //                         falling back to collectionId) — never empty.
    //   spec.color          = "" (CardDAV discovery does not currently
    //                         surface a server-supplied color via the
    //                         libkcal-derived walks; keep contract open
    //                         for a future phase to fill in).
    //   spec.contentTypes   = {"VCARD"} per RFC 6352.
    //
    // stableSlug is the last non-empty path segment of the server-given
    // href, sanitised via Kalburator::Sync::makeDavSlug() in src/sync/
    // davslug.h (the same helper CalDavProvider and
    // MultiProtocolDavProvider use, so Phase 2.4+ can count on a
    // uniform "<a>:<b>:<c>" backendId shape across every DAV provider).
    //
    // Returns {} when: not connected, collectionId is empty, or
    // collectionId isn't in m_addressbookUrls. This matches the v1
    // createBackend()'s nullptr contract for the same inputs.
    //
    // The Phase 1 stub was a "return {}" inline body; Task 2.3 fills
    // in the spec-producer body. The v1 createBackend() above is
    // intentionally UNTOUCHED — additive migration only.
    QList<ProviderBackendSpec>
        createBackends(const QString &collectionId) const override;

private:
    void onDiscoveryFinished(const QList<CollectionInfo> &books, bool hadError);
    QString                              m_id;             // UUID
    QString                              m_displayName;
    QUrl                                 m_serverUrl;
    QString                              m_username;
    QString                              m_password;
    bool                                 m_connected = false;
    QList<CollectionInfo>                m_collections;

    CardDavCapabilityDiscovery           *m_discovery = nullptr;
    QMap<QString, QString>               m_addressbookUrls;  // collectionId -> href
    std::unique_ptr<QPromise<bool>>      m_connectPromise;
};

} // namespace Kalburator::Sync

#endif
