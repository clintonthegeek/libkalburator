#ifndef CALDAVCAPABILITYDISCOVERY_H
#define CALDAVCAPABILITYDISCOVERY_H

#include "backendconfiguration.h"
#include <QObject>
#include <QUrl>
#include <QMap>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief Discovers CalDAV server capabilities using RFC 4791 PROPFIND.
 *
 * This class performs PROPFIND requests to discover:
 * - Server product name and version (DAV:resourcetype, DAV:server-version)
 * - Supported component types (supported-calendar-component-set)
 * - Per-calendar restrictions (VEVENT-only, VTODO-only, etc.)
 * - Calendar colors and display names
 *
 * The discovered capabilities are stored in a DiscoveredCapabilities struct
 * that can be persisted to the .kalb file.
 *
 * Usage:
 * @code
 * CalDavCapabilityDiscovery discovery(serverUrl, username, password);
 * connect(&discovery, &CalDavCapabilityDiscovery::finished, this, [&](bool success) {
 *     if (success) {
 *         DiscoveredCapabilities caps = discovery.discoveredCapabilities();
 *         configManager->setBackendCapabilities(backendId, caps);
 *     }
 * });
 * discovery.start();
 * @endcode
 */
class CalDavCapabilityDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit CalDavCapabilityDiscovery(const QUrl &serverUrl,
                                        const QString &username,
                                        const QString &password,
                                        QObject *parent = nullptr);
    ~CalDavCapabilityDiscovery() override;

    /**
     * @brief Start the discovery process.
     *
     * Performs async PROPFIND requests to discover server capabilities.
     * Emits finished() when complete.
     */
    void start();

    /**
     * @brief Get the discovered capabilities.
     *
     * Only valid after finished(true) is emitted.
     */
    DiscoveredCapabilities discoveredCapabilities() const { return m_capabilities; }

    /**
     * @brief Get any error message from the discovery process.
     */
    QString errorMessage() const { return m_errorMessage; }

    /**
     * @brief Check if discovery is currently running.
     */
    bool isRunning() const { return m_running; }

Q_SIGNALS:
    /**
     * @brief Emitted when discovery completes.
     * @param success true if discovery succeeded, false on error
     */
    void finished(bool success);

    /**
     * @brief Emitted when a calendar is discovered with its capabilities.
     * @param calendarId The calendar identifier
     * @param caps The per-calendar capabilities
     */
    void calendarDiscovered(const QString &calendarId, const PerCalendarCapabilities &caps);

    /**
     * @brief Emitted during discovery to update progress.
     * @param message Current status message
     */
    void progressMessage(const QString &message);

private Q_SLOTS:
    void onPrincipalReplyFinished();
    void onCalendarHomeReplyFinished();
    void onCalendarsListReplyFinished();
    void onCalendarPropsReplyFinished();

private:
    void discoverPrincipal();
    void discoverCalendarHome();
    void discoverCalendars();
    void fetchCalendarProperties(const QString &calendarUrl);

    QByteArray buildPropfindRequest(const QStringList &properties, int depth = 0) const;
    QString extractHref(const QByteArray &response, const QString &element) const;
    QStringList extractCalendarUrls(const QByteArray &response) const;
    PerCalendarCapabilities parseCalendarProperties(const QByteArray &response,
                                                     const QString &calendarUrl) const;
    QString parseServerProduct(const QByteArray &response) const;

    void finishWithError(const QString &error);
    void finishWithSuccess();

    QUrl m_serverUrl;
    QString m_username;
    QString m_password;
    QNetworkAccessManager *m_networkManager = nullptr;

    // Discovery state
    bool m_running = false;
    QString m_principalUrl;
    QString m_calendarHomeUrl;
    QStringList m_pendingCalendarUrls;
    int m_pendingReplies = 0;

    // Results
    DiscoveredCapabilities m_capabilities;
    QString m_errorMessage;
};

#endif // CALDAVCAPABILITYDISCOVERY_H
