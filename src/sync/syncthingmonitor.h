#ifndef SYNCTHINGMONITOR_H
#define SYNCTHINGMONITOR_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QMap>
#include <QDateTime>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

namespace Kalburator::Sync {


/**
 * @brief Monitors a Syncthing instance for file sync events via REST API.
 *
 * Long-polls two event streams:
 * 1. /rest/events/disk — LocalChangeDetected, RemoteChangeDetected
 * 2. /rest/events?events=ItemFinished,FolderCompletion
 *
 * Filters events by a configurable path prefix (e.g., "DecSync/") and emits
 * signals for consumers. Independent of any backend — connect via signals.
 */
class SyncthingMonitor : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,  ///< Can't reach Syncthing API
        Idle,          ///< Connected, nothing pending
        Syncing,       ///< Files in flight (incoming or outgoing)
        Error          ///< Connected but sync errors detected
    };
    Q_ENUM(State)

    struct DeviceSyncStatus {
        QString deviceId;
        double completion = 0.0;   ///< 0-100
        int needItems = 0;
        QString remoteState;       ///< "valid", "paused", "unknown", "notSharing"
        QDateTime lastSeen;
    };

    explicit SyncthingMonitor(QObject *parent = nullptr);
    ~SyncthingMonitor() override;

    /// Configure the Syncthing connection. Must be called before start().
    void setConnection(const QUrl &url, const QString &apiKey);

    /// Set the folder to watch and the path prefix to filter events.
    /// folderId: Syncthing folder ID (from /rest/config/folders)
    /// pathPrefix: relative prefix within the folder (e.g., "DecSync/")
    void setWatchedFolder(const QString &folderId, const QString &pathPrefix);

    /// Start monitoring. Validates connection, then begins long-polling.
    void start();

    /// Stop monitoring. Cancels in-flight requests.
    void stop();

    /// Whether the monitor is actively polling.
    bool isRunning() const;

    // --- State accessors ---

    State state() const;
    int pendingItems() const;
    double completionPercent() const;
    QDateTime lastSyncTime() const;
    QString lastError() const;

    // --- Per-device status ---

    DeviceSyncStatus deviceSyncStatus(const QString &deviceId) const;
    QMap<QString, DeviceSyncStatus> allDeviceSyncStatuses() const;

signals:
    // File change events (filtered by path prefix)
    void remoteChangeDetected(const QString &folder, const QString &path,
                               const QString &modifiedBy);
    void localChangeDetected(const QString &folder, const QString &path);

    // Debounced trigger for controller
    void remoteChangesReady(const QString &folder);

    // Sync completion events
    void itemSynced(const QString &folder, const QString &path,
                    const QString &error);

    // State changes
    void stateChanged(SyncthingMonitor::State state);
    void syncProgressChanged(int pendingItems, double completionPercent);
    void deviceStatusChanged(const QString &deviceId, double completion,
                              int needItems, const QString &remoteState);

    // Connection health
    void connectionLost();
    void connectionRestored();

private slots:
    void onDiskEventsReply();
    void onSyncEventsReply();
    void onValidationReply();
    void onDebounceTimeout();
    void onRetryTimeout();

private:
    void startDiskEventsPoll();
    void startSyncEventsPoll();
    void validateConnection();
    void handleNetworkError(QNetworkReply *reply, const QString &streamName);
    void setState(State newState);
    void scheduleRetry();
    bool matchesPathPrefix(const QString &path) const;

    // Connection config
    QUrl m_baseUrl;
    QString m_apiKey;
    QString m_folderId;
    QString m_pathPrefix;

    // Network
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_diskEventsReply = nullptr;
    QNetworkReply *m_syncEventsReply = nullptr;

    // Event stream cursors (since parameter)
    qint64 m_diskEventsSince = 0;
    qint64 m_syncEventsSince = 0;

    // State
    State m_state = State::Disconnected;
    bool m_running = false;
    int m_pendingItems = 0;
    double m_completionPercent = 100.0;
    QDateTime m_lastSyncTime;
    QString m_lastError;
    QMap<QString, DeviceSyncStatus> m_deviceStatuses;

    // Debounce timer for remoteChangesReady
    QTimer m_debounceTimer;
    static constexpr int DEBOUNCE_MS = 500;

    // Retry backoff
    QTimer m_retryTimer;
    int m_retryDelayMs = 1000;
    static constexpr int MAX_RETRY_DELAY_MS = 60000;
};

} // namespace Kalburator::Sync

#endif // SYNCTHINGMONITOR_H
