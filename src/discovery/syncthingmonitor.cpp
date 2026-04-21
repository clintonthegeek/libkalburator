#include "syncthingmonitor.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

namespace Kalburator::Sync {

SyncthingMonitor::SyncthingMonitor(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(DEBOUNCE_MS);
    connect(&m_debounceTimer, &QTimer::timeout, this, &SyncthingMonitor::onDebounceTimeout);

    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &SyncthingMonitor::onRetryTimeout);
}

SyncthingMonitor::~SyncthingMonitor()
{
    stop();
}

void SyncthingMonitor::setConnection(const QUrl &url, const QString &apiKey)
{
    m_baseUrl = url;
    m_apiKey = apiKey;
}

void SyncthingMonitor::setWatchedFolder(const QString &folderId, const QString &pathPrefix)
{
    m_folderId = folderId;
    m_pathPrefix = pathPrefix;
}

bool SyncthingMonitor::isRunning() const { return m_running; }
SyncthingMonitor::State SyncthingMonitor::state() const { return m_state; }
int SyncthingMonitor::pendingItems() const { return m_pendingItems; }
double SyncthingMonitor::completionPercent() const { return m_completionPercent; }
QDateTime SyncthingMonitor::lastSyncTime() const { return m_lastSyncTime; }
QString SyncthingMonitor::lastError() const { return m_lastError; }

SyncthingMonitor::DeviceSyncStatus
SyncthingMonitor::deviceSyncStatus(const QString &deviceId) const
{
    return m_deviceStatuses.value(deviceId);
}

QMap<QString, SyncthingMonitor::DeviceSyncStatus>
SyncthingMonitor::allDeviceSyncStatuses() const
{
    return m_deviceStatuses;
}

void SyncthingMonitor::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(m_state);
    }
}

bool SyncthingMonitor::matchesPathPrefix(const QString &path) const
{
    if (m_pathPrefix.isEmpty()) return true;
    return path.startsWith(m_pathPrefix);
}

void SyncthingMonitor::start()
{
    if (m_running) return;
    m_running = true;
    m_retryDelayMs = 1000;
    validateConnection();
}

void SyncthingMonitor::stop()
{
    m_running = false;
    m_retryTimer.stop();
    m_debounceTimer.stop();

    if (m_diskEventsReply) {
        m_diskEventsReply->abort();
        m_diskEventsReply = nullptr;
    }
    if (m_syncEventsReply) {
        m_syncEventsReply->abort();
        m_syncEventsReply = nullptr;
    }

    setState(State::Disconnected);
}

void SyncthingMonitor::validateConnection()
{
    QUrl url = m_baseUrl;
    url.setPath(QStringLiteral("/rest/config/folders"));

    QNetworkRequest request(url);
    request.setRawHeader("X-API-Key", m_apiKey.toUtf8());

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, &SyncthingMonitor::onValidationReply);
}

void SyncthingMonitor::onValidationReply()
{
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (!m_running) return;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "SyncthingMonitor: Connection validation failed:" << reply->errorString();
        handleNetworkError(reply, QStringLiteral("validation"));
        return;
    }

    // Parse response to verify folder exists
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonArray folders = doc.array();

    bool folderFound = false;
    for (const QJsonValue &val : folders) {
        QJsonObject folder = val.toObject();
        if (folder.value(QStringLiteral("id")).toString() == m_folderId) {
            folderFound = true;
            break;
        }
    }

    if (!folderFound && !m_folderId.isEmpty()) {
        qWarning() << "SyncthingMonitor: Folder" << m_folderId << "not found in Syncthing config";
        // Still proceed — folder might be added later
    }

    qDebug() << "SyncthingMonitor: Connected to Syncthing at" << m_baseUrl.toString();

    if (m_state == State::Disconnected) {
        emit connectionRestored();
    }

    m_retryDelayMs = 1000;  // Reset backoff
    setState(State::Idle);

    // Start both event streams
    startDiskEventsPoll();
    startSyncEventsPoll();
}

void SyncthingMonitor::startDiskEventsPoll()
{
    if (!m_running) return;

    QUrl url = m_baseUrl;
    url.setPath(QStringLiteral("/rest/events/disk"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("since"), QString::number(m_diskEventsSince));
    query.addQueryItem(QStringLiteral("timeout"), QStringLiteral("60"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("X-API-Key", m_apiKey.toUtf8());
    // Long-poll: set a generous timeout (90s > Syncthing's 60s server timeout)
    request.setTransferTimeout(90000);

    m_diskEventsReply = m_nam->get(request);
    connect(m_diskEventsReply, &QNetworkReply::finished,
            this, &SyncthingMonitor::onDiskEventsReply);
}

void SyncthingMonitor::onDiskEventsReply()
{
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    m_diskEventsReply = nullptr;

    if (!m_running) return;

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, QStringLiteral("disk-events"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonArray events = doc.array();

    bool hasRemoteChange = false;

    for (const QJsonValue &val : events) {
        QJsonObject event = val.toObject();
        qint64 eventId = event.value(QStringLiteral("id")).toInteger();
        if (eventId > m_diskEventsSince) {
            m_diskEventsSince = eventId;
        }

        QString type = event.value(QStringLiteral("type")).toString();
        QJsonObject data = event.value(QStringLiteral("data")).toObject();
        QString folder = data.value(QStringLiteral("folder")).toString();
        QString path = data.value(QStringLiteral("path")).toString();

        // Filter by folder and path prefix
        if (!m_folderId.isEmpty() && folder != m_folderId) continue;
        if (!matchesPathPrefix(path)) continue;

        if (type == QStringLiteral("RemoteChangeDetected")) {
            QString modifiedBy = data.value(QStringLiteral("modifiedBy")).toString();
            emit remoteChangeDetected(folder, path, modifiedBy);
            hasRemoteChange = true;
            setState(State::Syncing);
        } else if (type == QStringLiteral("LocalChangeDetected")) {
            emit localChangeDetected(folder, path);
        }
    }

    // Debounce remote changes
    if (hasRemoteChange) {
        m_debounceTimer.start();
    }

    // Re-poll immediately
    startDiskEventsPoll();
}

void SyncthingMonitor::startSyncEventsPoll()
{
    if (!m_running) return;

    QUrl url = m_baseUrl;
    url.setPath(QStringLiteral("/rest/events"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("since"), QString::number(m_syncEventsSince));
    query.addQueryItem(QStringLiteral("timeout"), QStringLiteral("60"));
    query.addQueryItem(QStringLiteral("events"),
                       QStringLiteral("ItemFinished,FolderCompletion"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("X-API-Key", m_apiKey.toUtf8());
    request.setTransferTimeout(90000);

    m_syncEventsReply = m_nam->get(request);
    connect(m_syncEventsReply, &QNetworkReply::finished,
            this, &SyncthingMonitor::onSyncEventsReply);
}

void SyncthingMonitor::onSyncEventsReply()
{
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    m_syncEventsReply = nullptr;

    if (!m_running) return;

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, QStringLiteral("sync-events"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonArray events = doc.array();

    for (const QJsonValue &val : events) {
        QJsonObject event = val.toObject();
        qint64 eventId = event.value(QStringLiteral("id")).toInteger();
        if (eventId > m_syncEventsSince) {
            m_syncEventsSince = eventId;
        }

        QString type = event.value(QStringLiteral("type")).toString();
        QJsonObject data = event.value(QStringLiteral("data")).toObject();

        if (type == QStringLiteral("ItemFinished")) {
            QString folder = data.value(QStringLiteral("folder")).toString();
            QString item = data.value(QStringLiteral("item")).toString();

            if (!m_folderId.isEmpty() && folder != m_folderId) continue;
            if (!matchesPathPrefix(item)) continue;

            QString error;
            if (!data.value(QStringLiteral("error")).isNull()) {
                error = data.value(QStringLiteral("error")).toString();
                m_lastError = error;
                setState(State::Error);
            } else {
                m_lastSyncTime = QDateTime::currentDateTime();
                if (m_state == State::Error) {
                    m_lastError.clear();
                }
            }

            emit itemSynced(folder, item, error);

        } else if (type == QStringLiteral("FolderCompletion")) {
            QString folder = data.value(QStringLiteral("folder")).toString();
            if (!m_folderId.isEmpty() && folder != m_folderId) continue;

            QString deviceId = data.value(QStringLiteral("device")).toString();
            double completion = data.value(QStringLiteral("completion")).toDouble();
            int needItems = data.value(QStringLiteral("needItems")).toInt();
            QString remoteState = data.value(QStringLiteral("remoteState")).toString();

            DeviceSyncStatus status;
            status.deviceId = deviceId;
            status.completion = completion;
            status.needItems = needItems;
            status.remoteState = remoteState;
            status.lastSeen = QDateTime::currentDateTime();
            m_deviceStatuses[deviceId] = status;

            emit deviceStatusChanged(deviceId, completion, needItems, remoteState);

            // Recalculate aggregate: worst-case across devices
            int worstNeedItems = 0;
            double worstCompletion = 100.0;
            for (auto it = m_deviceStatuses.constBegin();
                 it != m_deviceStatuses.constEnd(); ++it) {
                if (it.value().needItems > worstNeedItems)
                    worstNeedItems = it.value().needItems;
                if (it.value().completion < worstCompletion)
                    worstCompletion = it.value().completion;
            }

            bool changed = (worstNeedItems != m_pendingItems ||
                            worstCompletion != m_completionPercent);
            m_pendingItems = worstNeedItems;
            m_completionPercent = worstCompletion;

            if (changed) {
                emit syncProgressChanged(m_pendingItems, m_completionPercent);
            }

            // Update state based on pending items
            if (m_pendingItems == 0 && m_state == State::Syncing) {
                setState(State::Idle);
            } else if (m_pendingItems > 0 && m_state == State::Idle) {
                setState(State::Syncing);
            }
        }
    }

    // Re-poll immediately
    startSyncEventsPoll();
}

void SyncthingMonitor::onDebounceTimeout()
{
    emit remoteChangesReady(m_folderId);
}

void SyncthingMonitor::handleNetworkError(QNetworkReply *reply,
                                           const QString &streamName)
{
    qWarning() << "SyncthingMonitor:" << streamName << "error:"
               << reply->errorString();

    // OperationCanceledError happens during stop() — not a real error
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        return;
    }

    if (m_state != State::Disconnected) {
        setState(State::Disconnected);
        emit connectionLost();
    }

    scheduleRetry();
}

void SyncthingMonitor::scheduleRetry()
{
    if (!m_running) return;

    qDebug() << "SyncthingMonitor: Retrying in" << m_retryDelayMs << "ms";
    m_retryTimer.start(m_retryDelayMs);

    // Exponential backoff, capped
    m_retryDelayMs = qMin(m_retryDelayMs * 2, MAX_RETRY_DELAY_MS);
}

void SyncthingMonitor::onRetryTimeout()
{
    if (!m_running) return;
    validateConnection();
}


} // namespace Kalburator::Sync
