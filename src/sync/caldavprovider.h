#ifndef KALBURATOR_SYNC_CALDAVPROVIDER_H
#define KALBURATOR_SYNC_CALDAVPROVIDER_H

#include "iprovider.h"

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

private slots:
    void onDiscoveryFinished(bool success);

private:
    QString                              m_id;             // UUID
    QString                              m_displayName;
    QUrl                                 m_serverUrl;
    QString                              m_username;
    QString                              m_password;
    bool                                 m_connected = false;
    QList<CollectionInfo>                m_collections;

    CalDavCapabilityDiscovery           *m_discovery = nullptr;
    QMap<QString, QString>               m_calendarUrls;   // collectionId -> href
    std::unique_ptr<QPromise<bool>>      m_connectPromise;
};

} // namespace Kalburator::Sync

#endif
