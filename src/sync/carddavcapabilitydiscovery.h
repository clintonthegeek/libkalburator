#ifndef KALBURATOR_SYNC_CARDDAVCAPABILITYDISCOVERY_H
#define KALBURATOR_SYNC_CARDDAVCAPABILITYDISCOVERY_H

#include "collectioninfo.h"

#include <QFuture>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace Kalburator::Sync {

/**
 * @brief Discovers CardDAV server addressbooks via RFC 6352 PROPFIND walk.
 *
 * Performs a three-step PROPFIND chain:
 *   1. PROPFIND server root → current-user-principal href
 *   2. PROPFIND principal href → addressbook-home-set href
 *   3. PROPFIND home href (Depth:1) → list of addressbook resources
 *
 * Usage:
 * @code
 * auto *discovery = new CardDavCapabilityDiscovery(this);
 * discovery->setCredentials(serverUrl, username, password);
 * connect(discovery, &CardDavCapabilityDiscovery::error, this, &MyClass::onError);
 * QFuture<QList<CollectionInfo>> fut = discovery->discover();
 * @endcode
 *
 * The returned QFuture resolves to a (possibly empty) list of discovered
 * addressbooks on success, or an empty list if any PROPFIND fails (the
 * error() signal carries the human-readable reason).
 *
 * Mirror of CalDavCapabilityDiscovery with CardDAV namespace differences:
 *   CalDAV:  urn:ietf:params:xml:ns:caldav,  calendar-home-set,  <CALDAV:calendar>
 *   CardDAV: urn:ietf:params:xml:ns:carddav, addressbook-home-set, <CARDDAV:addressbook>
 */
class CardDavCapabilityDiscovery : public QObject
{
    Q_OBJECT
public:
    explicit CardDavCapabilityDiscovery(QObject *parent = nullptr);
    ~CardDavCapabilityDiscovery() override;

    void setCredentials(const QUrl &serverRoot,
                        const QString &username,
                        const QString &password);

    /**
     * @brief Override the current-user-principal href, skipping auto-discovery.
     *
     * When set (non-empty), discover() bypasses the /.well-known bootstrap and
     * the principal PROPFIND and walks straight from this principal to the
     * addressbook-home-set. Backs the "Advanced → CardDAV principal" manual
     * override.
     */
    void setPrincipalHrefOverride(const QString &principalHref)
    { m_principalOverride = principalHref; }

    /**
     * @brief Async: walks current-user-principal → addressbook-home-set
     *        → addressbook collections.
     *
     * Returns a QFuture that resolves to the discovered addressbook list, or
     * an empty list on failure (error() is emitted with the reason). Calling
     * discover() while a previous call is still in flight starts a fresh
     * discovery attempt; the old future is abandoned.
     */
    QFuture<QList<CollectionInfo>> discover();

    /**
     * @brief After successful discover(): absolute URL of each addressbook
     *        keyed by CollectionInfo::id.
     */
    QMap<QString, QString> addressbookUrls() const { return m_addressbookUrls; }

signals:
    /// Emitted when any step of discovery fails. The future resolves to
    /// an empty list immediately after this signal.
    void error(const QString &message);

private slots:
    void onContextPathReplyFinished();
    void onPrincipalReplyFinished();
    void onHomeSetReplyFinished();
    void onAddressbooksReplyFinished();

private:
    void stepResolveContextPath();
    void stepDiscoverPrincipal();
    void stepDiscoverHomeSet();
    void stepDiscoverAddressbooks();

    QByteArray buildPropfindXml(const QStringList &davProps,
                                const QStringList &carddavProps) const;
    QString extractHref(const QByteArray &response,
                        const QString &elementLocalName,
                        const QString &elementNamespaceUri) const;

    void resolveWithError(const QString &msg);
    void resolveWithSuccess(const QList<CollectionInfo> &books);

    QUrl    m_serverRoot;
    QUrl    m_baseUrl;  // effective DAV base: m_serverRoot, or the RFC 6764
                        // well-known context path once resolved
    QString m_principalOverride;  // manual "Advanced" principal; skips probe
    QString m_username;
    QString m_password;

    QNetworkAccessManager *m_nam = nullptr;

    // In-flight discovery state — valid between discover() and resolution.
    QString m_principalHref;
    QString m_homeHref;

    // Promise held for the duration of an in-flight discover() call.
    // Reset (to null) once resolved.
    std::unique_ptr<QPromise<QList<CollectionInfo>>> m_promise;

    // Results accumulated across a successful discovery pass.
    QMap<QString, QString> m_addressbookUrls;  // id → absolute href
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_CARDDAVCAPABILITYDISCOVERY_H
