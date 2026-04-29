#include "synccoordinator.h"
#include "decsyncactivecontroller.h"
#include "calendarbaselinestore.h"
#include "blobbaselinestore.h"
#include "syncconflictstore.h"
#include "syncdiff.h"
#include "backendregistry.h"
#include "isynchost.h"
// collection.h removed — using icalendarcollection.h only
#include "isyncconfigstore.h"
#include "icalendarcollection.h"
#include "backendconfiguration.h"
#include "syncbackend.h"
#include "remotebackend.h"
#include "localbackend.h"
#include "syncoperation.h"
#include "conflictmanager.h"
#include "synctesthooks.h"
#include "iincidencesource.h"
#include "iincidenceregistry.h"

#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QSet>
#include <QCoreApplication>

namespace Kalburator::Sync {

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
    m_worker->setDependencies(m_controller, m_calendarBaselines, m_collection,
                              m_blobBaselines);

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

void SyncCoordinator::setCalendarBaselineStore(CalendarBaselineStore *store)
{
    m_calendarBaselines = store;
}

void SyncCoordinator::setBlobBaselineStore(BlobBaselineStore *store)
{
    m_blobBaselines = store;
}

void SyncCoordinator::setSyncConflictStore(SyncConflictStore *store)
{
    m_conflictStore = store;
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

    // Phase-1 + Phase-2 perf: prime fresh CTags and fingerprints, decide
    // per-mapping skip eligibility. Best-effort; on failure we simply fall
    // back to per-call PROPFIND inside SyncWorker.
    if (!m_cancelled && !m_syncMappings.isEmpty()) {
        prepareSyncFastPath();
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

    // Phase-2: clear any leftover state from a previous multi-mapping
    // runSync. The single-mapping path does not run prepareSyncFastPath,
    // so without this, onWorkerSyncCompleted could persist stale fresh
    // state captured during an earlier multi-mapping sync.
    m_freshState.clear();
    m_skippedMappingIds.clear();
    // Note: this also means that single-mapping runSync does NOT update
    // Phase-2 ctag/fingerprint baselines on success. That's correct —
    // baseline updates happen as part of the multi-mapping pre-pass
    // (prepareSyncFastPath), and a stale single-mapping baseline would
    // be more dangerous than no baseline update.

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
            request.useQuickPath = !m_calendarBaselines || !m_calendarBaselines->hasBaselines(mapping.id);

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

void SyncCoordinator::setSkipUnchangedMappings(bool enabled)
{
    m_skipUnchangedMappings = enabled;
    qDebug() << "SyncCoordinator::setSkipUnchangedMappings:" << enabled;
}

void SyncCoordinator::prepareSyncFastPath()
{
    m_skippedMappingIds.clear();
    m_freshState.clear();

    if (!m_registry) return;

    // ----- Phase 1: gather fresh CTags from each RemoteBackend (one batched
    //                PROPFIND per parent URL) -----
    QMap<RemoteBackend*, QStringList> remoteCalIdsByBackend;
    QMap<RemoteBackend*, QString> remoteBackendIds;  // RemoteBackend* -> backendId string
    auto collectRemote = [&](const QString &backendId, const QString &calId) {
        SyncBackend *base = m_registry->backendInstance(backendId);
        if (auto *r = qobject_cast<RemoteBackend*>(base)) {
            remoteCalIdsByBackend[r].append(calId);
            remoteBackendIds[r] = backendId;
        }
    };
    for (const auto &mapping : m_syncMappings) {
        if (!mapping.enabled) continue;
        collectRemote(mapping.sourceBackend, mapping.sourceCalendar);
        collectRemote(mapping.targetBackend, mapping.targetCalendar);
    }

    QMap<QPair<QString, QString>, QString> freshRemoteCtags;  // (backendId, calId) -> ctag
    for (auto it = remoteCalIdsByBackend.constBegin(); it != remoteCalIdsByBackend.constEnd(); ++it) {
        QStringList ids = it.value();
        ids.removeDuplicates();
        const QMap<QString, QString> ctags = it.key()->fetchAllCtags(ids);
        if (!ctags.isEmpty()) {
            it.key()->primeCtagCache(ctags);
            const QString backendId = remoteBackendIds.value(it.key());
            for (auto cit = ctags.constBegin(); cit != ctags.constEnd(); ++cit) {
                freshRemoteCtags[qMakePair(backendId, cit.key())] = cit.value();
            }
        }
    }

    // ----- Phase 2: gather fresh fingerprints from each LocalBackend -----
    QMap<QPair<QString, QString>, QString> freshLocalFingerprints;
    auto collectLocal = [&](const QString &backendId, const QString &calId) {
        SyncBackend *base = m_registry->backendInstance(backendId);
        if (auto *l = qobject_cast<LocalBackend*>(base)) {
            const QString fp = l->calendarFingerprint(calId);
            if (!fp.isEmpty()) {
                freshLocalFingerprints[qMakePair(backendId, calId)] = fp;
            }
        }
    };
    for (const auto &mapping : m_syncMappings) {
        if (!mapping.enabled) continue;
        collectLocal(mapping.sourceBackend, mapping.sourceCalendar);
        collectLocal(mapping.targetBackend, mapping.targetCalendar);
    }

    // ----- Decide skip per mapping. -----
    if (!m_calendarBaselines) return;  // can't compare without baselines

    int wouldSkipCount = 0;
    int actualSkipCount = 0;
    for (const auto &mapping : m_syncMappings) {
        if (!mapping.enabled) continue;

        FreshSyncState fresh;
        bool sourceCovered = false;
        bool targetCovered = false;
        bool sourceUnchanged = false;
        bool targetUnchanged = false;

        // Resolve source side.
        SyncBackend *srcBase = m_registry->backendInstance(mapping.sourceBackend);
        if (auto *srcRemote = qobject_cast<RemoteBackend*>(srcBase)) {
            sourceCovered = true;
            fresh.sourceCtag = freshRemoteCtags.value(
                qMakePair(mapping.sourceBackend, mapping.sourceCalendar));
            const QString stored = srcRemote->ctag(mapping.sourceCalendar);
            sourceUnchanged = !fresh.sourceCtag.isEmpty()
                              && !stored.isEmpty()
                              && fresh.sourceCtag == stored;
        } else if (auto *srcLocal = qobject_cast<LocalBackend*>(srcBase)) {
            sourceCovered = true;
            fresh.sourceFingerprint = freshLocalFingerprints.value(
                qMakePair(mapping.sourceBackend, mapping.sourceCalendar));
            const QString stored = srcLocal->cachedFingerprint(mapping.sourceCalendar);
            sourceUnchanged = !fresh.sourceFingerprint.isEmpty()
                              && !stored.isEmpty()
                              && fresh.sourceFingerprint == stored;
        }

        // Resolve target side (mirror logic).
        SyncBackend *tgtBase = m_registry->backendInstance(mapping.targetBackend);
        if (auto *tgtRemote = qobject_cast<RemoteBackend*>(tgtBase)) {
            targetCovered = true;
            fresh.targetCtag = freshRemoteCtags.value(
                qMakePair(mapping.targetBackend, mapping.targetCalendar));
            const QString stored = tgtRemote->ctag(mapping.targetCalendar);
            targetUnchanged = !fresh.targetCtag.isEmpty()
                              && !stored.isEmpty()
                              && fresh.targetCtag == stored;
        } else if (auto *tgtLocal = qobject_cast<LocalBackend*>(tgtBase)) {
            targetCovered = true;
            fresh.targetFingerprint = freshLocalFingerprints.value(
                qMakePair(mapping.targetBackend, mapping.targetCalendar));
            const QString stored = tgtLocal->cachedFingerprint(mapping.targetCalendar);
            targetUnchanged = !fresh.targetFingerprint.isEmpty()
                              && !stored.isEmpty()
                              && fresh.targetFingerprint == stored;
        }

        m_freshState[mapping.id] = fresh;

        const bool eligibleToSkip = sourceCovered && targetCovered
                                     && sourceUnchanged && targetUnchanged;
        if (eligibleToSkip) {
            ++wouldSkipCount;
            if (m_skipUnchangedMappings) {
                m_skippedMappingIds.insert(mapping.id);
                ++actualSkipCount;
                qInfo() << "SyncCoordinator: skipping unchanged mapping" << mapping.id;
            } else {
                qInfo() << "SyncCoordinator: would skip unchanged mapping (flag off)"
                        << mapping.id;
            }
        }
    }

    qDebug() << "SyncCoordinator::prepareSyncFastPath: of"
             << m_syncMappings.size() << "mappings,"
             << wouldSkipCount << "are unchanged;"
             << actualSkipCount << "actually skipped (flag="
             << m_skipUnchangedMappings << ")";
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

    const SyncMapping &mapping = m_syncMappings[m_currentMappingIndex];

    // Phase-2 skip: if this mapping's both endpoints are demonstrably
    // unchanged AND the skip flag is on, short-circuit without dispatching
    // to the worker. Emit syncCompleted with a successful no-op result so
    // subscribers (UI progress, etc.) don't get stuck waiting.
    if (m_skippedMappingIds.contains(mapping.id)) {
        emit progressUpdated(m_currentMappingIndex + 1, m_syncMappings.size(),
                             tr("Skipping unchanged %1").arg(mapping.id));

        SyncResult skippedResult;
        skippedResult.success = true;
        skippedResult.startTime = QDateTime::currentDateTime();
        skippedResult.endTime = skippedResult.startTime;

        // Aggregate into last result (no stats to add; success stays true unless
        // already false from a prior mapping failure).
        SYNC_HOOK_CALL(onSyncMappingEnd, mapping.id, true);
        emit syncCompleted(mapping.id, skippedResult);

        // Advance to the next mapping without touching the worker.
        processNextMapping();
        return;
    }

    emit progressUpdated(m_currentMappingIndex + 1, m_syncMappings.size(),
                         tr("Syncing %1").arg(mapping.id));

    // Create request and invoke worker directly
    SyncWorker::Request request;
    request.mapping = mapping;
    request.mode = (m_currentSyncBehavior == SyncBehavior::Monitored)
        ? SyncWorker::Mode::Monitored : SyncWorker::Mode::Unmonitored;
    request.collectionId = m_collection ? m_collection->id() : QString();
    request.useQuickPath = !m_calendarBaselines || !m_calendarBaselines->hasBaselines(mapping.id);

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
    if (!m_calendarBaselines) {
        qDebug() << "SyncCoordinator::updateSyncMetadata - no CalendarBaselineStore, skipping baseline update";
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
            m_calendarBaselines->removeBaseline(mapping.id, change.uid);
        } else if (change.sourceRecord.isValid()) {
            // Update baseline to current source state
            m_calendarBaselines->setBaseline(mapping.id, change.uid, change.sourceRecord.icalData);
        }
    }

    // For items that were synced to source (target is authoritative)
    for (const auto &change : diff.toSource) {
        if (change.isConflict) {
            continue;
        }

        if (change.type == SyncChangeType::Deleted) {
            m_calendarBaselines->removeBaseline(mapping.id, change.uid);
        } else if (change.targetRecord.isValid()) {
            m_calendarBaselines->setBaseline(mapping.id, change.uid, change.targetRecord.icalData);
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
            m_calendarBaselines->setBaseline(mapping.id, change.uid, change.sourceRecord.icalData);
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
            m_calendarBaselines->setBaseline(mapping.id, change.uid, change.targetRecord.icalData);
        }
    }

    // For unchanged items, ensure baseline exists
    for (const QString &uid : diff.unchangedUids) {
        if (!m_calendarBaselines->baseline(mapping.id, uid).isEmpty()) {
            continue;  // Already has baseline
        }

        // This shouldn't happen in normal operation, but handle gracefully
        qDebug() << "SyncCoordinator::updateSyncMetadata - unchanged item has no baseline:" << uid;
    }

    // Update last sync time
    m_calendarBaselines->setLastSyncTime(mapping.id, QDateTime::currentDateTime());
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

    // Record in conflict store if available
    if (m_conflictStore) {
        m_conflictStore->recordConflict(enriched);
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

    // Phase-2: persist fresh CTags / fingerprints so the next sync's
    // pre-pass has up-to-date baselines.
    if (result.success && m_calendarBaselines) {
        auto stateIt = m_freshState.constFind(mappingId);
        if (stateIt != m_freshState.constEnd()) {
            const FreshSyncState &fresh = stateIt.value();
            const SyncMapping *mapping = nullptr;
            for (const auto &m : m_syncMappings) {
                if (m.id == mappingId) { mapping = &m; break; }
            }
            if (mapping) {
                if (!fresh.sourceCtag.isEmpty()) {
                    if (auto *r = qobject_cast<RemoteBackend*>(
                            m_registry->backendInstance(mapping->sourceBackend)))
                        r->setCtag(mapping->sourceCalendar, fresh.sourceCtag);
                }
                if (!fresh.targetCtag.isEmpty()) {
                    if (auto *r = qobject_cast<RemoteBackend*>(
                            m_registry->backendInstance(mapping->targetBackend)))
                        r->setCtag(mapping->targetCalendar, fresh.targetCtag);
                }
                if (!fresh.sourceFingerprint.isEmpty()) {
                    if (auto *l = qobject_cast<LocalBackend*>(
                            m_registry->backendInstance(mapping->sourceBackend)))
                        l->setCachedFingerprint(mapping->sourceCalendar,
                                                fresh.sourceFingerprint);
                }
                if (!fresh.targetFingerprint.isEmpty()) {
                    if (auto *l = qobject_cast<LocalBackend*>(
                            m_registry->backendInstance(mapping->targetBackend)))
                        l->setCachedFingerprint(mapping->targetCalendar,
                                                fresh.targetFingerprint);
                }
            }
        }
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


} // namespace Kalburator::Sync
