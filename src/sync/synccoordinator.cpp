#include "synccoordinator.h"
#include "decsyncactivecontroller.h"
#include "syncstore.h"
#include "syncdiff.h"
#include "backendregistry.h"
#include "isynchost.h"
// collection.h removed — using icalendarcollection.h only
#include "isyncconfigstore.h"
#include "icalendarcollection.h"
#include "backendconfiguration.h"
#include "syncbackend.h"
#include "syncoperation.h"
#include "conflictmanager.h"
#include "synctesthooks.h"
#include "iincidencesource.h"
#include "iincidenceregistry.h"

#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QSet>
#include <QCoreApplication>

SyncCoordinator::SyncCoordinator(BackendRegistry *registry,
                                   ISyncHost *host,
                                   QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_controller(host)
{
    // Create worker but don't start thread yet
    m_worker = new SyncWorker();
    setupWorkerConnections();
}

SyncCoordinator::~SyncCoordinator()
{
    stopWorkerThread();
}

void SyncCoordinator::setupWorkerConnections()
{
    if (!m_worker) return;

    // Connect worker signals to coordinator slots (Qt::QueuedConnection for cross-thread)
    connect(m_worker, &SyncWorker::syncStarted,
            this, &SyncCoordinator::onWorkerSyncStarted, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::phaseChanged,
            this, &SyncCoordinator::onWorkerPhaseChanged, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::fetchProgress,
            this, &SyncCoordinator::onWorkerFetchProgress, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::itemReady,
            this, &SyncCoordinator::onWorkerItemReady, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::writeProgress,
            this, &SyncCoordinator::onWorkerWriteProgress, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::conflictDetected,
            this, &SyncCoordinator::onWorkerConflictDetected, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::conflictPauseRequested,
            this, &SyncCoordinator::onWorkerConflictPauseRequested, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::syncCompleted,
            this, &SyncCoordinator::onWorkerSyncCompleted, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::syncError,
            this, &SyncCoordinator::onWorkerSyncError, Qt::QueuedConnection);
    connect(m_worker, &SyncWorker::transcodingWarning,
            this, &SyncCoordinator::onWorkerTranscodingWarning, Qt::QueuedConnection);

    // Note: Worker is deleted explicitly in stopWorkerThread() rather than
    // via finished->deleteLater, since the thread's event loop has exited
    // by the time finished is emitted.
}

void SyncCoordinator::startWorkerThread()
{
    if (m_workerThread.isRunning()) {
        return;
    }

    // Set dependencies before moving to thread
    m_worker->setDependencies(m_controller, m_syncStore, m_collection);

    // Move worker to thread
    m_worker->moveToThread(&m_workerThread);

    // Start thread
    m_workerThread.start();

    qDebug() << "SyncCoordinator: Worker thread started";
}

void SyncCoordinator::stopWorkerThread()
{
    if (m_workerThread.isRunning()) {
        if (m_worker) {
            m_worker->cancel();
        }

        m_workerThread.quit();
        m_workerThread.wait();

        qDebug() << "SyncCoordinator: Worker thread stopped";
    }

    // After wait() returns, the thread's event loop has stopped and it's safe
    // to delete the worker directly. Note: moveToThread() cannot be used here
    // because you can only "push" objects to another thread from the thread
    // they're currently on (we're on main thread, worker is on worker thread).
    // Qt allows deletion of QObjects from any thread after their thread has stopped.
    if (m_worker) {
        delete m_worker;
        m_worker = nullptr;
    }
}

void SyncCoordinator::setSyncStore(SyncStore *store)
{
    m_syncStore = store;
}

void SyncCoordinator::loadSyncMappings(ICalendarCollection *collection)
{
    m_syncMappings.clear();
    m_collection = collection;

    if (!collection || !m_controller) {
        qDebug() << "SyncCoordinator::loadSyncMappings - no collection or controller";
        return;
    }

    // Load mappings from KalbConfigManager
    ISyncConfigStore *configManager = m_controller->configStore();
    if (!configManager) {
        qDebug() << "SyncCoordinator::loadSyncMappings - no config manager";
        return;
    }

    if (!configManager->hasSyncMappings()) {
        qDebug() << "SyncCoordinator::loadSyncMappings - no sync mappings configured";
        return;
    }

    m_syncMappings = configManager->syncMappings();
    qDebug() << "SyncCoordinator::loadSyncMappings - loaded"
             << m_syncMappings.size() << "mappings";

    // Log mapping details
    for (const auto &mapping : m_syncMappings) {
        qDebug() << "  Mapping:" << mapping.id
                 << "source:" << mapping.sourceBackend << "/" << mapping.sourceCalendar
                 << "target:" << mapping.targetBackend << "/" << mapping.targetCalendar
                 << "mode:" << syncModeToString(mapping.mode)
                 << "enabled:" << mapping.enabled;
    }
}

void SyncCoordinator::setMappingEnabled(const QString &mappingId, bool enabled)
{
    for (auto &mapping : m_syncMappings) {
        if (mapping.id == mappingId) {
            mapping.enabled = enabled;
            break;
        }
    }
}

void SyncCoordinator::registerActiveController(const QString &calendarId,
                                                 DecSyncActiveController *controller)
{
    m_activeControllers[calendarId] = controller;
    qDebug() << "SyncCoordinator: Registered active controller for" << calendarId;
}

void SyncCoordinator::unregisterActiveController(const QString &calendarId)
{
    m_activeControllers.remove(calendarId);
}

bool SyncCoordinator::hasSyncWork() const
{
    return !m_syncMappings.isEmpty() || !m_activeControllers.isEmpty();
}

void SyncCoordinator::runSync(SyncBehavior behavior)
{
    if (m_isSyncing) {
        qWarning() << "SyncCoordinator::runSync - sync already in progress";
        return;
    }

    if (m_syncMappings.isEmpty() && m_activeControllers.isEmpty()) {
        qDebug() << "SyncCoordinator::runSync - no sync work configured";
        m_lastResult = SyncResult{};
        m_lastResult.success = true;
        emit allSyncsCompleted(m_lastResult);
        return;
    }

    m_isSyncing = true;
    m_cancelled = false;
    m_currentMappingIndex = -1;
    m_currentSyncBehavior = behavior;
    m_pendingUnmonitoredConflicts.clear();
    m_lastResult = SyncResult{};
    m_lastResult.startTime = QDateTime::currentDateTime();

    // Run active controllers first (they're fast, synchronous)
    for (auto it = m_activeControllers.constBegin(); it != m_activeControllers.constEnd(); ++it) {
        if (m_cancelled) break;
        emit progressUpdated(0, m_syncMappings.size() + m_activeControllers.size(),
                             tr("Syncing %1 (DecSync)").arg(it.key()));
        it.value()->runActiveSync();
    }

    if (m_syncMappings.isEmpty() || m_cancelled) {
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        m_lastResult.success = !m_cancelled;
        m_lastResult.endTime = QDateTime::currentDateTime();
        emit allSyncsCompleted(m_lastResult);
        return;
    }

    // Start worker thread for mapping-based sync
    startWorkerThread();
    processNextMapping();
}

void SyncCoordinator::runSync(const QString &mappingId, SyncBehavior behavior)
{
    if (m_isSyncing) {
        qWarning() << "SyncCoordinator::runSync - sync already in progress";
        return;
    }

    for (const auto &mapping : m_syncMappings) {
        if (mapping.id == mappingId && mapping.enabled) {
            m_isSyncing = true;
            m_cancelled = false;
            m_currentSyncBehavior = behavior;
            m_lastResult = SyncResult{};
            m_lastResult.startTime = QDateTime::currentDateTime();

            // Start worker thread if not running
            startWorkerThread();

            // Create request and invoke worker
            SyncWorker::Request request;
            request.mapping = mapping;
            request.mode = (behavior == SyncBehavior::Monitored)
                ? SyncWorker::Mode::Monitored
                : SyncWorker::Mode::Unmonitored;
            request.collectionId = m_collection ? m_collection->id() : QString();
            request.useQuickPath = !m_syncStore || m_syncStore->allBaselines(mapping.id).isEmpty();

            // Invoke worker in its thread
            QMetaObject::invokeMethod(m_worker, "processSync",
                                      Qt::QueuedConnection,
                                      Q_ARG(SyncWorker::Request, request));
            return;
        }
    }
    qWarning() << "SyncCoordinator::runSync - mapping not found:" << mappingId;
}

void SyncCoordinator::resumeAfterConflictResolution(ConflictResolution resolution,
                                                     const QString &mergedIcal)
{
    if (!m_worker) {
        qWarning() << "SyncCoordinator::resumeAfterConflictResolution - no worker";
        return;
    }

    qDebug() << "SyncCoordinator::resumeAfterConflictResolution - resolution:"
             << static_cast<int>(resolution);

    // Invoke on worker thread
    QMetaObject::invokeMethod(m_worker, "resumeAfterConflict",
                              Qt::QueuedConnection,
                              Q_ARG(ConflictResolution, resolution),
                              Q_ARG(QString, mergedIcal));
}

void SyncCoordinator::cancelSync()
{
    if (m_isSyncing) {
        m_cancelled = true;
        if (m_worker) {
            m_worker->cancel();
        }
        qDebug() << "SyncCoordinator::cancelSync - sync cancelled";
    }
}

void SyncCoordinator::processNextMapping()
{
    // Debug log removed - SyncWorker provides detailed timing

    if (m_cancelled) {
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        m_lastResult.success = false;
        m_lastResult.errorMessage = QStringLiteral("Sync cancelled");
        m_lastResult.endTime = QDateTime::currentDateTime();
        emit allSyncsCompleted(m_lastResult);
        return;
    }

    m_currentMappingIndex++;

    // Find next enabled mapping
    while (m_currentMappingIndex < m_syncMappings.size()) {
        if (m_syncMappings[m_currentMappingIndex].enabled) {
            break;
        }
        m_currentMappingIndex++;
    }

    if (m_currentMappingIndex >= m_syncMappings.size()) {
        // All mappings processed
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        // Aggregate success: false if stats report errors/conflicts, or if any
        // per-mapping result already set it to false (e.g. fetch failures that
        // abort before any operations and thus leave stats clean).
        bool statsOk = !m_lastResult.sourceStats.hasErrors() &&
                       !m_lastResult.targetStats.hasErrors() &&
                       !m_lastResult.hasUnresolvedConflicts();
        m_lastResult.success = m_lastResult.success && statsOk;
        m_lastResult.endTime = QDateTime::currentDateTime();
        emit allSyncsCompleted(m_lastResult);
        return;
    }

    emit progressUpdated(m_currentMappingIndex + 1, m_syncMappings.size(),
                         tr("Syncing %1").arg(m_syncMappings[m_currentMappingIndex].id));

    // Create request and invoke worker directly
    const SyncMapping &mapping = m_syncMappings[m_currentMappingIndex];
    SyncWorker::Request request;
    request.mapping = mapping;
    request.mode = (m_currentSyncBehavior == SyncBehavior::Monitored)
        ? SyncWorker::Mode::Monitored : SyncWorker::Mode::Unmonitored;
    request.collectionId = m_collection ? m_collection->id() : QString();
    request.useQuickPath = !m_syncStore || m_syncStore->allBaselines(mapping.id).isEmpty();

    QMetaObject::invokeMethod(m_worker, "processSync",
                              Qt::QueuedConnection,
                              Q_ARG(SyncWorker::Request, request));

    // NOTE: Do NOT recurse here!
    // The async operation will call onWorkerSyncCompleted() when done,
    // which will then call processNextMapping() again.
}

// ============================================================================
// Helper Methods
// ============================================================================

void SyncCoordinator::updateSyncMetadata(const SyncMapping &mapping, const SyncDiff &diff,
                                          const QList<SyncChange> &resolvedToTarget,
                                          const QList<SyncChange> &resolvedToSource)
{
    if (!m_syncStore) {
        qDebug() << "SyncCoordinator::updateSyncMetadata - no SyncStore, skipping baseline update";
        return;
    }

    // Update baselines for all synced items
    // After sync, the baseline becomes the current state

    // For items that were synced to target (source is authoritative)
    for (const auto &change : diff.toTarget) {
        if (change.isConflict) {
            continue;  // Don't update baseline for unresolved conflicts (handled below)
        }

        if (change.type == SyncChangeType::Deleted) {
            // Remove baseline for deleted items
            m_syncStore->removeBaseline(mapping.id, change.uid);
        } else if (change.sourceRecord.isValid()) {
            // Update baseline to current source state
            m_syncStore->setBaseline(mapping.id, change.uid, change.sourceRecord.icalData);
        }
    }

    // For items that were synced to source (target is authoritative)
    for (const auto &change : diff.toSource) {
        if (change.isConflict) {
            continue;
        }

        if (change.type == SyncChangeType::Deleted) {
            m_syncStore->removeBaseline(mapping.id, change.uid);
        } else if (change.targetRecord.isValid()) {
            m_syncStore->setBaseline(mapping.id, change.uid, change.targetRecord.icalData);
        }
    }

    // IMPORTANT: Update baselines for RESOLVED conflicts
    // These were originally conflicts but the user resolved them, so we need to
    // update the baseline to prevent the same conflict from appearing again.

    // Resolved conflicts that went to target (source record was chosen or merged)
    for (const auto &change : resolvedToTarget) {
        // Skip newly created items from Duplicate resolution - they'll be picked up on next sync
        if (change.type == SyncChangeType::Created && change.baselineRecord.uid.isEmpty()) {
            continue;
        }

        if (change.sourceRecord.isValid()) {
            qDebug() << "SyncCoordinator::updateSyncMetadata - updating baseline for resolved conflict:"
                     << change.uid << "(source wins/merged)";
            m_syncStore->setBaseline(mapping.id, change.uid, change.sourceRecord.icalData);
        }
    }

    // Resolved conflicts that went to source (target record was chosen)
    for (const auto &change : resolvedToSource) {
        if (change.type == SyncChangeType::Created && change.baselineRecord.uid.isEmpty()) {
            continue;
        }

        if (change.targetRecord.isValid()) {
            qDebug() << "SyncCoordinator::updateSyncMetadata - updating baseline for resolved conflict:"
                     << change.uid << "(target wins)";
            m_syncStore->setBaseline(mapping.id, change.uid, change.targetRecord.icalData);
        }
    }

    // For unchanged items, ensure baseline exists
    for (const QString &uid : diff.unchangedUids) {
        if (!m_syncStore->baseline(mapping.id, uid).isEmpty()) {
            continue;  // Already has baseline
        }

        // This shouldn't happen in normal operation, but handle gracefully
        qDebug() << "SyncCoordinator::updateSyncMetadata - unchanged item has no baseline:" << uid;
    }

    // Update last sync time
    m_syncStore->setLastSyncTime(mapping.id, QDateTime::currentDateTime());
}

// ============================================================================
// Worker Thread Signal Handlers
// ============================================================================

void SyncCoordinator::onWorkerSyncStarted(const QString &mappingId)
{
    // Debug log removed - SyncWorker shows detailed start info
    emit syncStarted(mappingId);
}

void SyncCoordinator::onWorkerPhaseChanged(const QString &mappingId, int phase)
{
    Q_UNUSED(mappingId);
    m_currentPhase = static_cast<SyncPhase>(phase);
    emit phaseChanged(m_currentPhase);
}

void SyncCoordinator::onWorkerFetchProgress(const QString &calendarId, int current, int total)
{
    emit fetchProgress(calendarId, current, total);
}

void SyncCoordinator::onWorkerItemReady(const QString &calendarId,
                                         const KCalendarCore::Incidence::Ptr &incidence,
                                         int changeType)
{
    // Forward to itemFetched signal for backward compatibility
    emit itemFetched(calendarId, incidence);

    // Update incidence model on main thread
    if (m_controller && incidence && m_currentMappingIndex >= 0 &&
        m_currentMappingIndex < m_syncMappings.size()) {
        IIncidenceSource *source = m_controller->incidenceSource();
        IIncidenceRegistry *registry = m_controller->incidenceRegistry();
        const SyncMapping &currentMapping = m_syncMappings[m_currentMappingIndex];
        SyncBackend *backend = m_controller->backendById(currentMapping.targetBackend);

        if (source && registry && backend && m_collection) {
            KCalendarCore::MemoryCalendar *cal = m_collection->calendar(calendarId);
            QString backendType = backend->backendType();

            // Debug: verify we have the calendar
            if (!cal) {
                static QSet<QString> warnedCalendars;
                if (!warnedCalendars.contains(calendarId)) {
                    warnedCalendars.insert(calendarId);
                    qWarning() << "SyncCoordinator::onWorkerItemReady: No MemoryCalendar for" << calendarId
                               << "- items will not appear in CategoryManager!";
                    QStringList availableIds;
                    for (auto *c : m_collection->calendars()) {
                        availableIds << c->id();
                    }
                    qDebug() << "  Available calendars:" << availableIds;
                }
            }

            SyncChangeType type = static_cast<SyncChangeType>(changeType);
            switch (type) {
                case SyncChangeType::Created: {
                    auto existing = source->lookupEntry(incidence->uid(), calendarId,
                        incidence->hasRecurrenceId() ? incidence->recurrenceId() : QDateTime());
                    if (!existing.incidence) {
                        // Add to MemoryCalendar (for CategoryManager, saving, etc.)
                        if (cal && !cal->incidence(incidence->uid(), incidence->recurrenceId())) {
                            cal->addIncidence(incidence);
                        }
                        // Add to incidence registry (for UI display)
                        registry->addIncidence(incidence, calendarId, backendType, cal,
                                               backend->dataDomain());
                    }
                    break;
                }

                case SyncChangeType::Modified:
                    // Update in MemoryCalendar
                    if (cal) {
                        auto existing = cal->incidence(incidence->uid(), incidence->recurrenceId());
                        if (existing) {
                            cal->deleteIncidence(existing);
                        }
                        cal->addIncidence(incidence);
                    }
                    registry->updateIncidenceForCalendar(incidence, calendarId);
                    break;

                case SyncChangeType::Deleted:
                    // Remove from MemoryCalendar
                    if (cal) {
                        auto existing = cal->incidence(incidence->uid(), incidence->recurrenceId());
                        if (existing) {
                            cal->deleteIncidence(existing);
                        }
                    }
                    registry->removeIncidence(incidence->uid(), calendarId,
                        incidence->hasRecurrenceId() ? incidence->recurrenceId() : QDateTime());
                    break;

                case SyncChangeType::Unchanged:
                    break;
            }
        }
    }
}

void SyncCoordinator::onWorkerWriteProgress(const QString &calendarId, int current, int total)
{
    emit writeProgress(calendarId, current, total);
}

static void resolveConflictDisplayNames(ConflictInfo &conflict, BackendRegistry *registry)
{
    auto resolveOne = [&](const QString &backendId) -> QString {
        if (backendId.isEmpty()) return {};
        SyncBackend *backend = registry ? registry->backendInstance(backendId) : nullptr;
        if (backend) {
            return BackendConfiguration::friendlyTypeName(backend->backendType());
        }
        return BackendConfiguration::friendlyTypeName(backendId);
    };

    if (conflict.sourceBackendDisplayName.isEmpty())
        conflict.sourceBackendDisplayName = resolveOne(conflict.sourceBackendId);
    if (conflict.targetBackendDisplayName.isEmpty())
        conflict.targetBackendDisplayName = resolveOne(conflict.targetBackendId);
}

void SyncCoordinator::onWorkerConflictDetected(const ConflictInfo &conflict)
{
    // Resolve human-readable backend names for UI
    ConflictInfo enriched = conflict;
    resolveConflictDisplayNames(enriched, m_registry);

    // Emit for UI notifications (status bar, conflict count badge)
    emit conflictDetected(enriched);

    // Record in sync store if available
    if (m_syncStore) {
        m_syncStore->recordConflict(enriched);
    }

    // Collect for batch presentation after sync mapping completes.
    // Previously this called handleConflict() immediately, which created
    // a modal dialog per conflict via nested exec() event loops —
    // causing N stacked dialogs for N conflicts.
    m_pendingUnmonitoredConflicts.append(enriched);
}

void SyncCoordinator::onWorkerConflictPauseRequested(const ConflictInfo &conflict)
{
    qDebug() << "SyncCoordinator::onWorkerConflictPauseRequested - conflict:" << conflict.sourceId;

    // Resolve human-readable backend names for UI
    ConflictInfo enriched = conflict;
    resolveConflictDisplayNames(enriched, m_registry);

    // Store conflict for dialog
    m_pendingConflict = enriched;

    // Emit signal for backward compatibility
    emit conflictDetected(enriched);

    // In monitored mode, show dialog via ConflictManager
    if (m_conflictManager) {
        ConflictResolution resolution = m_conflictManager->handleConflict(enriched);
        QString mergedIcal;

        if (resolution == ConflictResolution::CustomMerge) {
            mergedIcal = m_conflictManager->lastMergedIcalData();
        }

        // Resume worker with resolution
        resumeAfterConflictResolution(resolution, mergedIcal);
    } else {
        // No conflict manager - skip conflict
        resumeAfterConflictResolution(ConflictResolution::Skip);
    }
}

void SyncCoordinator::onWorkerSyncCompleted(const QString &mappingId, const SyncResult &result)
{
    // Batch-present any unmonitored conflicts collected during this mapping.
    // handleConflicts() (plural) applies hybrid threshold: shows dialogs for
    // small batches, defers large batches to the dock widget.
    if (m_conflictManager && !m_pendingUnmonitoredConflicts.isEmpty()) {
        qDebug() << "SyncCoordinator: Batch-presenting"
                 << m_pendingUnmonitoredConflicts.size()
                 << "conflicts for mapping" << mappingId;
        m_conflictManager->handleConflicts(m_pendingUnmonitoredConflicts);
        m_pendingUnmonitoredConflicts.clear();
    }

    // Update current mapping result
    m_currentMappingResult = result;

    // Aggregate stats into last result
    m_lastResult.sourceStats += result.sourceStats;
    m_lastResult.targetStats += result.targetStats;
    m_lastResult.unresolvedConflicts.append(result.unresolvedConflicts);

    // Propagate per-mapping failure to aggregate result
    if (!result.success) {
        m_lastResult.success = false;
        if (!result.errorMessage.isEmpty())
            m_lastResult.errorMessage = result.errorMessage;
    }

    // Test hook: sync mapping end
    SYNC_HOOK_CALL(onSyncMappingEnd, mappingId, result.success);

    emit syncCompleted(mappingId, result);

    // Reset phase
    m_currentPhase = SyncPhase::Complete;
    emit phaseChanged(m_currentPhase);

    // Continue to next mapping
    processNextMapping();
}

void SyncCoordinator::onWorkerSyncError(const QString &mappingId, const QString &errorMessage)
{
    qWarning() << "SyncCoordinator::onWorkerSyncError - mapping:" << mappingId
               << "error:" << errorMessage;

    // Create failed result
    SyncResult failedResult;
    failedResult.success = false;
    failedResult.errorMessage = errorMessage;
    failedResult.endTime = QDateTime::currentDateTime();

    // Propagate failure to aggregate result
    m_lastResult.success = false;
    if (!errorMessage.isEmpty())
        m_lastResult.errorMessage = errorMessage;

    // Emit completion with error
    emit syncCompleted(mappingId, failedResult);

    // Continue to next mapping (don't fail the whole batch)
    processNextMapping();
}

void SyncCoordinator::onWorkerTranscodingWarning(const QString &calendarId,
                                                  const QString &uid,
                                                  const QStringList &warnings)
{
    qDebug() << "SyncCoordinator::onWorkerTranscodingWarning - calendar:" << calendarId
             << "uid:" << uid << "warnings:" << warnings;

    // Forward the transcoding warning signal
    emit transcodingWarning(calendarId, uid, warnings);
}
