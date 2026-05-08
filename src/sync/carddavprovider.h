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
