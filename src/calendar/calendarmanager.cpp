#include "calendarmanager.h"
#include "isynchost.h"
#include "isyncconfigstore.h"
#include "synctypes.h"
#include "syncbackend.h"
#include "remotebackend.h"
#include "localbackend.h"
#include "icalendarcollection.h"
#include "syncengine.h"
#include "transcodingregistry.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimeZone>
#include <QUrl>

namespace Kalburator::Sync {

// Register metatypes for signals
static bool s_metatypesRegistered = []() {
    qRegisterMetaType<DeleteMode>("DeleteMode");
    qRegisterMetaType<CreationResult>("CreationResult");
    qRegisterMetaType<DeletionResult>("DeletionResult");
    qRegisterMetaType<CalendarSnapshot>("CalendarSnapshot");
    qRegisterMetaType<OperationType>("OperationType");
    return true;
}();

CalendarManager::CalendarManager(ISyncHost *host,
                                 ICalendarCollection *collection,
                                 QObject *parent)
    : QObject(parent)
    , m_controller(host)
    , m_configManager(host ? host->configStore() : nullptr)
    , m_collection(collection)
    , m_transcodingRegistry(nullptr)  // Will be implemented in Phase 5
{
    Q_ASSERT(m_controller);
    qDebug() << "CalendarManager: Initialized";
}

CalendarManager::~CalendarManager()
{
    qDebug() << "CalendarManager: Destroyed";
}

// ========== IMMEDIATE Calendar CRUD ==========

CreationResult CalendarManager::createCalendar(const LogicalCalendar &logCal)
{
    CreationResult result;
    result.logicalCalendarId = logCal.id;

    if (!m_configManager || !m_collection) {
        result.errors.append(tr("No collection loaded"));
        emit operationFailed(QStringLiteral("createCalendar"), result.errors.first());
        return result;
    }

    qDebug() << "CalendarManager::createCalendar:" << logCal.displayName
             << "with" << logCal.bindings.size() << "binding(s)";

    // 1. Add to config FIRST (so we have the logical calendar registered)
    m_configManager->addLogicalCalendar(logCal);

    int total = logCal.bindings.size();
    int current = 0;

    // 2. Create on EACH backend IMMEDIATELY
    for (const auto &binding : logCal.bindings) {
        ++current;
        emit operationProgress(QStringLiteral("createCalendar"), current, total);

        if (!binding.enabled) {
            result.backendResults[binding.backendId] = true;  // Skipped but OK
            continue;
        }

        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) {
            QString error = tr("Backend not found: %1").arg(binding.backendId);
            result.errors.append(error);
            result.backendResults[binding.backendId] = false;
            emit operationFailed(QStringLiteral("createCalendar"), error);
            continue;
        }

        bool success = true;

        if (binding.needsCreation) {
            // IMMEDIATE creation - no staging
            QString collectionId = m_collection->id();

            qDebug() << "CalendarManager: Creating calendar on backend"
                     << binding.backendId << ":" << binding.calendarId;

            success = backend->createCalendar(
                collectionId,
                binding.calendarId,
                logCal.displayName,
                logCal.type);

            if (success) {
                // Clear needsCreation flag in config
                LogicalCalendar updatedCal = m_configManager->logicalCalendar(logCal.id);
                for (auto &b : updatedCal.bindings) {
                    if (b.backendId == binding.backendId) {
                        b.needsCreation = false;

                        // For CalDAV, construct and store the davUrl
                        if (backend->backendType() == RemoteBackend::BackendTypeName) {
                            QVariantMap backendConfig = m_configManager->backendConfig(binding.backendId);
                            QString baseUrl = backendConfig.value(QStringLiteral("url")).toString();
                            QString username = backendConfig.value(QStringLiteral("username")).toString();
                            QString password = backendConfig.value(QStringLiteral("password")).toString();

                            QUrl calUrl = QUrl::fromUserInput(baseUrl);
                            QString path = calUrl.path();
                            if ((path.isEmpty() || path == QLatin1String("/")) && !username.isEmpty()) {
                                path = QLatin1Char('/') + username + QLatin1Char('/');
                            }
                            if (!path.endsWith('/')) path += '/';
                            path += binding.calendarId + '/';
                            calUrl.setPath(path);
                            calUrl.setUserName(username);
                            calUrl.setPassword(password);
                            b.setDavUrl(calUrl.toString());
                        }
                        break;
                    }
                }
                m_configManager->updateLogicalCalendar(updatedCal);
            } else {
                QString error = tr("Failed to create on %1: %2")
                    .arg(binding.backendId, backend->backendType());
                result.errors.append(error);
                emit operationFailed(QStringLiteral("createCalendar"), error);
            }
        }

        result.backendResults[binding.backendId] = success;
    }

    // 3. Create MemoryCalendar using primary binding's calendarId
    CalendarBackendBinding primary = logCal.primaryBinding();
    if (primary.isValid()) {
        ensureMemoryCalendar(primary.calendarId, logCal.displayName);
    }

    // 4. Regenerate sync mappings
    regenerateSyncMappings();

    // 5. Save config
    m_configManager->save();

    // Determine overall success
    result.success = result.errors.isEmpty();

    if (result.success) {
        emit calendarCreated(logCal.id);
        qDebug() << "CalendarManager: Successfully created calendar" << logCal.displayName;
    }

    return result;
}

bool CalendarManager::updateCalendar(const QString &logicalCalendarId,
                                     const QVariantMap &properties)
{
    if (!m_configManager) {
        emit operationFailed(QStringLiteral("updateCalendar"), tr("No config manager"));
        return false;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        emit operationFailed(QStringLiteral("updateCalendar"),
                            tr("Calendar not found: %1").arg(logicalCalendarId));
        return false;
    }

    qDebug() << "CalendarManager::updateCalendar:" << logicalCalendarId
             << "properties:" << properties.keys();

    // Update logical calendar properties
    if (properties.contains(QStringLiteral("displayName"))) {
        logCal.displayName = properties.value(QStringLiteral("displayName")).toString();
    }
    if (properties.contains(QStringLiteral("description"))) {
        logCal.description = properties.value(QStringLiteral("description")).toString();
    }
    if (properties.contains(QStringLiteral("color"))) {
        logCal.color = properties.value(QStringLiteral("color")).value<QColor>();
    }
    if (properties.contains(QStringLiteral("enabled"))) {
        logCal.enabled = properties.value(QStringLiteral("enabled")).toBool();
    }
    if (properties.contains(QStringLiteral("visible"))) {
        logCal.visible = properties.value(QStringLiteral("visible")).toBool();
    }
    if (properties.contains(QStringLiteral("syncEnabled"))) {
        logCal.syncEnabled = properties.value(QStringLiteral("syncEnabled")).toBool();
    }

    // Update backend calendars if supported
    bool allSucceeded = true;
    for (const auto &binding : logCal.enabledBindings()) {
        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) continue;

        // Only update backend if it supports calendar updates
        if (backend->supportsCalendarCreation()) {
            QString collectionId = m_collection->id();
            if (!backend->updateCalendar(collectionId, binding.calendarId, properties)) {
                qWarning() << "CalendarManager: Failed to update calendar on"
                          << binding.backendId;
                allSucceeded = false;
            }
        }
    }

    // Update config
    m_configManager->updateLogicalCalendar(logCal);
    m_configManager->save();

    // Update Collection runtime state
    if (m_collection) {
        CalendarBackendBinding primary = logCal.primaryBinding();
        if (primary.isValid()) {
            if (properties.contains(QStringLiteral("color"))) {
                m_collection->setCalendarColor(
                    primary.calendarId, logCal.color);
            }
            if (properties.contains(QStringLiteral("visible"))) {
                m_collection->setCalendarVisible(
                    primary.calendarId, logCal.visible);
            }
        }
    }

    // Regenerate sync mappings if syncEnabled changed
    if (properties.contains(QStringLiteral("syncEnabled"))) {
        regenerateSyncMappings();
    }

    emit calendarUpdated(logicalCalendarId);
    return allSucceeded;
}

DeletionResult CalendarManager::deleteCalendar(const QString &logicalCalendarId,
                                               DeleteMode mode)
{
    DeletionResult result;
    result.logicalCalendarId = logicalCalendarId;

    if (!m_configManager) {
        result.errors.append(tr("No config manager"));
        emit operationFailed(QStringLiteral("deleteCalendar"), result.errors.first());
        return result;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        result.errors.append(tr("Calendar not found: %1").arg(logicalCalendarId));
        emit operationFailed(QStringLiteral("deleteCalendar"), result.errors.first());
        return result;
    }

    qDebug() << "CalendarManager::deleteCalendar:" << logCal.displayName
             << "mode:" << static_cast<int>(mode);

    CalendarBackendBinding primary = logCal.primaryBinding();
    QString primaryCalendarId = primary.isValid() ? primary.calendarId : QString();

    switch (mode) {
    case DeleteMode::Hide:
        // Just hide from UI (set visible=false)
        logCal.visible = false;
        m_configManager->updateLogicalCalendar(logCal);
        if (m_collection && !primaryCalendarId.isEmpty()) {
            m_collection->setCalendarVisible(primaryCalendarId, false);
        }
        result.success = true;
        break;

    case DeleteMode::Disable:
        // Unload from memory, keep in config (set enabled=false)
        logCal.enabled = false;
        m_configManager->updateLogicalCalendar(logCal);
        if (!primaryCalendarId.isEmpty()) {
            emit calendarUnloadRequested(primaryCalendarId);
        }
        result.success = true;
        break;

    case DeleteMode::DisconnectSync:
        // Remove all non-primary bindings
        for (const auto &binding : logCal.bindings) {
            if (binding.role != BackendRole::Primary) {
                removeBinding(logicalCalendarId, binding.backendId, false);
            }
        }
        logCal.syncEnabled = false;
        m_configManager->updateLogicalCalendar(logCal);
        regenerateSyncMappings();
        result.success = true;
        break;

    case DeleteMode::Forget:
        // Remove from config only, keep backend data
        if (!primaryCalendarId.isEmpty()) {
            emit calendarUnloadRequested(primaryCalendarId);
        }
        m_configManager->removeLogicalCalendar(logicalCalendarId);
        result.success = true;
        break;

    case DeleteMode::DeleteFromAll:
        // Delete from ALL backends IMMEDIATELY
        for (const auto &binding : logCal.bindings) {
            SyncBackend *backend = m_controller->backendById(binding.backendId);
            if (!backend) {
                result.backendResults[binding.backendId] = false;
                result.errors.append(tr("Backend not found: %1").arg(binding.backendId));
                continue;
            }

            // IMMEDIATE deletion - no staging
            QString collectionId = m_collection ?
                                   m_collection->id() : QString();

            qDebug() << "CalendarManager: Deleting calendar from backend"
                     << binding.backendId << ":" << binding.calendarId;

            bool success = backend->deleteCalendar(collectionId, binding.calendarId);
            result.backendResults[binding.backendId] = success;

            if (!success) {
                QString error = tr("Failed to delete from %1").arg(binding.backendId);
                result.errors.append(error);
                emit operationFailed(QStringLiteral("deleteCalendar"), error);
            }
        }

        // Unload and remove from config
        if (!primaryCalendarId.isEmpty()) {
            emit calendarUnloadRequested(primaryCalendarId);
        }
        m_configManager->removeLogicalCalendar(logicalCalendarId);

        result.success = result.errors.isEmpty();
        break;
    }

    // CRITICAL: Regenerate sync mappings after deletion
    regenerateSyncMappings();

    // Save config
    m_configManager->save();

    if (result.success) {
        emit calendarDeleted(logicalCalendarId);
        qDebug() << "CalendarManager: Deleted calendar" << logCal.displayName;
    }

    return result;
}

// ========== IMMEDIATE Binding CRUD ==========

bool CalendarManager::addBinding(const QString &logicalCalendarId,
                                 const CalendarBackendBinding &binding)
{
    if (!m_configManager) {
        emit operationFailed(QStringLiteral("addBinding"), tr("No config manager"));
        return false;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        emit operationFailed(QStringLiteral("addBinding"),
                            tr("Calendar not found: %1").arg(logicalCalendarId));
        return false;
    }

    qDebug() << "CalendarManager::addBinding:" << logicalCalendarId
             << "backend:" << binding.backendId << "calendar:" << binding.calendarId;

    // Check for duplicate binding
    for (const auto &existing : logCal.bindings) {
        if (existing.backendId == binding.backendId) {
            emit operationFailed(QStringLiteral("addBinding"),
                                tr("Binding already exists for backend: %1").arg(binding.backendId));
            return false;
        }
    }

    CalendarBackendBinding newBinding = binding;
    bool success = true;

    // If needsCreation, create on backend IMMEDIATELY
    if (newBinding.needsCreation) {
        SyncBackend *backend = m_controller->backendById(newBinding.backendId);
        if (!backend) {
            emit operationFailed(QStringLiteral("addBinding"),
                                tr("Backend not found: %1").arg(newBinding.backendId));
            return false;
        }

        QString collectionId = m_collection ?
                               m_collection->id() : QString();

        success = backend->createCalendar(
            collectionId,
            newBinding.calendarId,
            logCal.displayName,
            logCal.type);

        if (success) {
            newBinding.needsCreation = false;

            // For CalDAV, construct and store davUrl
            if (backend->backendType() == RemoteBackend::BackendTypeName) {
                QVariantMap backendConfig = m_configManager->backendConfig(newBinding.backendId);
                QString baseUrl = backendConfig.value(QStringLiteral("url")).toString();
                QString username = backendConfig.value(QStringLiteral("username")).toString();
                QString password = backendConfig.value(QStringLiteral("password")).toString();

                QUrl calUrl = QUrl::fromUserInput(baseUrl);
                QString path = calUrl.path();
                if ((path.isEmpty() || path == QLatin1String("/")) && !username.isEmpty()) {
                    path = QLatin1Char('/') + username + QLatin1Char('/');
                }
                if (!path.endsWith('/')) path += '/';
                path += newBinding.calendarId + '/';
                calUrl.setPath(path);
                calUrl.setUserName(username);
                calUrl.setPassword(password);
                newBinding.setDavUrl(calUrl.toString());
            }
        } else {
            emit operationFailed(QStringLiteral("addBinding"),
                                tr("Failed to create calendar on %1").arg(newBinding.backendId));
            return false;
        }
    }

    // Add binding to logical calendar
    logCal.bindings.append(newBinding);

    // If adding a sync binding, enable sync
    if (isSyncRole(newBinding.role)) {
        logCal.syncEnabled = true;
    }

    // Update config
    m_configManager->updateLogicalCalendar(logCal);
    m_configManager->save();

    // Regenerate sync mappings
    regenerateSyncMappings();

    emit bindingAdded(logicalCalendarId, binding.backendId);
    qDebug() << "CalendarManager: Added binding" << binding.backendId
             << "to" << logCal.displayName;

    return success;
}

bool CalendarManager::removeBinding(const QString &logicalCalendarId,
                                    const QString &backendId,
                                    bool deleteFromBackend)
{
    if (!m_configManager) {
        emit operationFailed(QStringLiteral("removeBinding"), tr("No config manager"));
        return false;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        emit operationFailed(QStringLiteral("removeBinding"),
                            tr("Calendar not found: %1").arg(logicalCalendarId));
        return false;
    }

    qDebug() << "CalendarManager::removeBinding:" << logicalCalendarId
             << "backend:" << backendId << "deleteFromBackend:" << deleteFromBackend;

    // Find the binding
    CalendarBackendBinding targetBinding;
    bool found = false;
    for (const auto &binding : logCal.bindings) {
        if (binding.backendId == backendId) {
            targetBinding = binding;
            found = true;
            break;
        }
    }

    if (!found) {
        emit operationFailed(QStringLiteral("removeBinding"),
                            tr("Binding not found for backend: %1").arg(backendId));
        return false;
    }

    // Prevent removing primary binding
    if (targetBinding.role == BackendRole::Primary) {
        emit operationFailed(QStringLiteral("removeBinding"),
                            tr("Cannot remove primary binding"));
        return false;
    }

    // Delete from backend if requested
    if (deleteFromBackend) {
        SyncBackend *backend = m_controller->backendById(backendId);
        if (backend) {
            QString collectionId = m_collection ?
                                   m_collection->id() : QString();
            if (!backend->deleteCalendar(collectionId, targetBinding.calendarId)) {
                qWarning() << "CalendarManager: Failed to delete from backend"
                          << backendId << "but continuing with binding removal";
            }
        }
    }

    // Remove binding from logical calendar
    for (auto it = logCal.bindings.begin(); it != logCal.bindings.end(); ++it) {
        if (it->backendId == backendId) {
            logCal.bindings.erase(it);
            break;
        }
    }

    // Update syncEnabled based on remaining bindings
    logCal.syncEnabled = !logCal.syncBindings().isEmpty();

    // Update config
    m_configManager->updateLogicalCalendar(logCal);
    m_configManager->save();

    // Regenerate sync mappings
    regenerateSyncMappings();

    emit bindingRemoved(logicalCalendarId, backendId);
    qDebug() << "CalendarManager: Removed binding" << backendId
             << "from" << logCal.displayName;

    return true;
}

bool CalendarManager::updateBinding(const QString &logicalCalendarId,
                                    const QString &backendId,
                                    const CalendarBackendBinding &newBinding)
{
    if (!m_configManager) {
        emit operationFailed(QStringLiteral("updateBinding"), tr("No config manager"));
        return false;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        emit operationFailed(QStringLiteral("updateBinding"),
                            tr("Calendar not found: %1").arg(logicalCalendarId));
        return false;
    }

    qDebug() << "CalendarManager::updateBinding:" << logicalCalendarId
             << "backend:" << backendId;

    // Find and update the binding
    bool found = false;
    for (auto &binding : logCal.bindings) {
        if (binding.backendId == backendId) {
            binding = newBinding;
            found = true;
            break;
        }
    }

    if (!found) {
        emit operationFailed(QStringLiteral("updateBinding"),
                            tr("Binding not found for backend: %1").arg(backendId));
        return false;
    }

    // Update config
    m_configManager->updateLogicalCalendar(logCal);
    m_configManager->save();

    // Regenerate sync mappings if roles changed
    regenerateSyncMappings();

    emit bindingUpdated(logicalCalendarId, backendId);
    return true;
}

// ========== IMMEDIATE Incidence CRUD ==========

bool CalendarManager::createIncidence(const QString &logicalCalendarId,
                                      const KCalendarCore::Incidence::Ptr &incidence)
{
    if (!m_configManager || !incidence) {
        emit operationFailed(QStringLiteral("createIncidence"), tr("Invalid parameters"));
        return false;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        emit operationFailed(QStringLiteral("createIncidence"),
                            tr("Calendar not found: %1").arg(logicalCalendarId));
        return false;
    }

    qDebug() << "CalendarManager::createIncidence:" << incidence->uid()
             << "to" << logCal.displayName;

    QString sourceType = getBackendType(logCal.primaryBinding().backendId);
    bool allSucceeded = true;

    for (const auto &binding : logCal.enabledBindings()) {
        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) continue;

        QString targetType = backend->backendType();

        // Clone and transcode for this backend
        auto transcodedIncidence = KCalendarCore::Incidence::Ptr(incidence->clone());
        QStringList warnings = TranscodingRegistry::instance()
            .transcodeIncidence(sourceType, targetType, transcodedIncidence);

        if (!warnings.isEmpty()) {
            emit dataLossWarning(logicalCalendarId, warnings);
        }

        // Push to backend
        auto *pushOp = backend->pushItems(binding.calendarId, {transcodedIncidence});
        if (pushOp) {
            // For now, synchronous wait - in future could be async
            // The operation should complete relatively quickly
            QEventLoop loop;
            connect(pushOp, &SyncOperation::finished, &loop, &QEventLoop::quit);
            loop.exec();

            if (pushOp->state() != SyncOperation::Succeeded) {
                allSucceeded = false;
                emit operationFailed(QStringLiteral("createIncidence"),
                                    tr("Failed on %1: %2").arg(binding.backendId, pushOp->errorString()));
            }
            pushOp->deleteLater();
        }
    }

    if (allSucceeded) {
        emit incidenceCreated(logicalCalendarId, incidence->uid());
    }

    return allSucceeded;
}

bool CalendarManager::updateIncidence(const QString &logicalCalendarId,
                                      const KCalendarCore::Incidence::Ptr &incidence)
{
    if (!m_configManager || !incidence) {
        emit operationFailed(QStringLiteral("updateIncidence"), tr("Invalid parameters"));
        return false;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        emit operationFailed(QStringLiteral("updateIncidence"),
                            tr("Calendar not found: %1").arg(logicalCalendarId));
        return false;
    }

    qDebug() << "CalendarManager::updateIncidence:" << incidence->uid()
             << "in" << logCal.displayName;

    QString sourceType = getBackendType(logCal.primaryBinding().backendId);
    bool allSucceeded = true;

    for (const auto &binding : logCal.enabledBindings()) {
        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) continue;

        QString targetType = backend->backendType();

        // Clone and transcode for this backend
        auto transcodedIncidence = KCalendarCore::Incidence::Ptr(incidence->clone());
        QStringList warnings = TranscodingRegistry::instance()
            .transcodeIncidence(sourceType, targetType, transcodedIncidence);

        if (!warnings.isEmpty()) {
            emit dataLossWarning(logicalCalendarId, warnings);
        }

        // Push update to backend (same as create in push model)
        auto *pushOp = backend->pushItems(binding.calendarId, {transcodedIncidence});
        if (pushOp) {
            QEventLoop loop;
            connect(pushOp, &SyncOperation::finished, &loop, &QEventLoop::quit);
            loop.exec();

            if (pushOp->state() != SyncOperation::Succeeded) {
                allSucceeded = false;
                emit operationFailed(QStringLiteral("updateIncidence"),
                                    tr("Failed on %1: %2").arg(binding.backendId, pushOp->errorString()));
            }
            pushOp->deleteLater();
        }
    }

    if (allSucceeded) {
        emit incidenceUpdated(logicalCalendarId, incidence->uid());
    }

    return allSucceeded;
}

bool CalendarManager::deleteIncidence(const QString &logicalCalendarId,
                                      const QString &uid)
{
    if (!m_configManager || uid.isEmpty()) {
        emit operationFailed(QStringLiteral("deleteIncidence"), tr("Invalid parameters"));
        return false;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        emit operationFailed(QStringLiteral("deleteIncidence"),
                            tr("Calendar not found: %1").arg(logicalCalendarId));
        return false;
    }

    qDebug() << "CalendarManager::deleteIncidence:" << uid
             << "from" << logCal.displayName;

    bool allSucceeded = true;

    for (const auto &binding : logCal.enabledBindings()) {
        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) continue;

        // Delete from backend
        auto *deleteOp = backend->deleteItems(binding.calendarId, {uid});
        if (deleteOp) {
            QEventLoop loop;
            connect(deleteOp, &SyncOperation::finished, &loop, &QEventLoop::quit);
            loop.exec();

            if (deleteOp->state() != SyncOperation::Succeeded) {
                allSucceeded = false;
                emit operationFailed(QStringLiteral("deleteIncidence"),
                                    tr("Failed on %1: %2").arg(binding.backendId, deleteOp->errorString()));
            }
            deleteOp->deleteLater();
        }
    }

    if (allSucceeded) {
        emit incidenceDeleted(logicalCalendarId, uid);
    }

    return allSucceeded;
}

// ========== Transcoding Integration ==========

QStringList CalendarManager::validateOperation(const QString &logicalCalendarId,
                                               const KCalendarCore::Incidence::Ptr &incidence,
                                               OperationType op)
{
    Q_UNUSED(op);
    QStringList warnings;

    if (!m_configManager || !incidence) {
        return warnings;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        return warnings;
    }

    // Check each target backend for capability limitations
    for (const auto &binding : logCal.enabledBindings()) {
        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) continue;

        // Check recurrence capabilities
        RecurrenceLossInfo lossInfo = backend->analyzeRecurrenceLoss(incidence);
        if (lossInfo.hasLoss) {
            warnings.append(tr("%1: %2").arg(binding.backendId, lossInfo.summary()));
        }
    }

    return warnings;
}

// ========== Snapshot for Undo ==========

CalendarSnapshot CalendarManager::captureSnapshot(const QString &logicalCalendarId) const
{
    CalendarSnapshot snapshot;

    if (!m_configManager) {
        return snapshot;
    }

    LogicalCalendar logCal = m_configManager->logicalCalendar(logicalCalendarId);
    if (!logCal.isValid()) {
        return snapshot;
    }

    snapshot.logicalCalendar = logCal;
    snapshot.capturedAt = QDateTime::currentDateTime();

    // Capture all incidences from the primary calendar
    CalendarBackendBinding primary = logCal.primaryBinding();
    if (primary.isValid() && m_collection) {
        KCalendarCore::MemoryCalendar *cal =
            m_collection->calendar(primary.calendarId);
        if (cal) {
            const auto incidences = cal->incidences();
            for (const auto &inc : incidences) {
                snapshot.incidences.append(KCalendarCore::Incidence::Ptr(inc->clone()));
            }
        }
    }

    qDebug() << "CalendarManager: Captured snapshot of" << logCal.displayName
             << "with" << snapshot.incidences.size() << "incidences";

    return snapshot;
}

bool CalendarManager::restoreFromSnapshot(const CalendarSnapshot &snapshot)
{
    if (!snapshot.isValid()) {
        emit operationFailed(QStringLiteral("restoreFromSnapshot"), tr("Invalid snapshot"));
        return false;
    }

    qDebug() << "CalendarManager: Restoring from snapshot of"
             << snapshot.logicalCalendar.displayName;

    // For now, this is a placeholder - full implementation would:
    // 1. Delete current incidences
    // 2. Restore snapshot incidences
    // 3. Push to all backends

    // TODO: Implement full restore logic
    return false;
}

// ========== Internal Helpers ==========

bool CalendarManager::executeOnBackend(const QString &backendId,
                                       const QString &calendarId,
                                       std::function<bool(SyncBackend*)> operation)
{
    SyncBackend *backend = m_controller->backendById(backendId);
    if (!backend) {
        emit operationFailed(QStringLiteral("executeOnBackend"),
                            tr("Backend not found: %1").arg(backendId));
        return false;
    }

    return operation(backend);
}

bool CalendarManager::executeOnAllBindings(
    const LogicalCalendar &logCal,
    std::function<bool(SyncBackend*, const CalendarBackendBinding&)> operation)
{
    bool allSucceeded = true;

    for (const auto &binding : logCal.enabledBindings()) {
        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) {
            emit operationFailed(QStringLiteral("executeOnAllBindings"),
                                tr("Backend not found: %1").arg(binding.backendId));
            allSucceeded = false;
            continue;
        }

        if (!operation(backend, binding)) {
            allSucceeded = false;
        }
    }

    return allSucceeded;
}

KCalendarCore::Incidence::Ptr CalendarManager::transcodeForBackend(
    const QString &sourceBackendType,
    const QString &targetBackendType,
    const KCalendarCore::Incidence::Ptr &incidence)
{
    auto clone = KCalendarCore::Incidence::Ptr(incidence->clone());

    if (sourceBackendType != targetBackendType) {
        QStringList warnings = TranscodingRegistry::instance()
            .transcodeIncidence(sourceBackendType, targetBackendType, clone);
        if (!warnings.isEmpty()) {
            qDebug() << "CalendarManager: transcoding" << sourceBackendType << "->"
                     << targetBackendType << "warnings:" << warnings;
        }
    }

    return clone;
}

QString CalendarManager::getBackendType(const QString &backendId) const
{
    SyncBackend *backend = m_controller->backendById(backendId);
    return backend ? backend->backendType() : QString();
}

void CalendarManager::ensureMemoryCalendar(const QString &calendarId,
                                           const QString &displayName)
{
    if (!m_collection) {
        return;
    }

    // Check if calendar already exists
    if (m_collection->calendar(calendarId)) {
        return;
    }

    // Create working MemoryCalendar. addCalendar() re-parents to the
    // collection for ownership; no explicit setParent needed.
    auto *workingCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    workingCal->setId(calendarId);
    m_collection->addCalendar(workingCal);

    qDebug() << "CalendarManager: Created MemoryCalendar" << calendarId
             << "(" << displayName << ")";
}

void CalendarManager::regenerateSyncMappings()
{
    if (m_batchDepth > 0) {
        m_regenPending = true;
        return;
    }
    // Unconditionally fire regeneration. Hosts without sync configured
    // implement generateSyncMappingsFromLogicalCalendars() as a no-op.
    emit syncMappingRegenerationRequested();
}

void CalendarManager::beginBatch()
{
    ++m_batchDepth;
}

void CalendarManager::endBatch()
{
    Q_ASSERT(m_batchDepth > 0);
    if (--m_batchDepth == 0 && m_regenPending) {
        m_regenPending = false;
        regenerateSyncMappings();
    }
}

bool CalendarManager::processNeedsCreation(const LogicalCalendar &logCal)
{
    bool allSucceeded = true;

    for (const auto &binding : logCal.bindings) {
        if (!binding.needsCreation) {
            continue;
        }

        SyncBackend *backend = m_controller->backendById(binding.backendId);
        if (!backend) {
            qWarning() << "CalendarManager: Backend not found:" << binding.backendId;
            allSucceeded = false;
            continue;
        }

        QString collectionId = m_collection ?
                               m_collection->id() : QString();

        qDebug() << "CalendarManager: Processing deferred creation for"
                 << binding.calendarId << "on" << binding.backendId;

        bool success = backend->createCalendar(
            collectionId,
            binding.calendarId,
            logCal.displayName,
            logCal.type);

        if (!success) {
            allSucceeded = false;
            emit operationFailed(QStringLiteral("processNeedsCreation"),
                                tr("Failed to create on %1").arg(binding.backendId));
        }
    }

    return allSucceeded;
}


} // namespace Kalburator::Sync
