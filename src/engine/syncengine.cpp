#include "syncengine.h"
#include "transcodingregistry.h"
#include "decsyncactivecontroller.h"
#include "calendarbaselinestore.h"
#include "blobbaselinestore.h"
#include "iblobbackend.h"
#include "conflictpolicy.h"
#include "conflictrecord.h"
#include "conflictstore.h"
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
#include "synctransaction.h"
#include "createincidenceitem.h"
#include "updateincidenceitem.h"
#include "deleteincidenceitem.h"

#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QMetaObject>
#include <QTimer>
#include <QMutexLocker>
#include <memory>

namespace Kalburator::Sync {

SyncEngine::SyncEngine(BackendRegistry *registry,
                                   ISyncHost *host,
                                   QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_controller(host)
    , m_transcodingRouter(TranscodingRegistry::instance())
    , m_calendarAdapter(m_transcodingRouter)
{
    // Create worker but don't start thread yet
    m_worker = new SyncEngineWorker(m_transcodingRouter);
    setupWorkerConnections();

    // F2 Task 19: install the cancel oracle on the calendar adapter.
    // The lambda captures `this` (the engine), and reads m_worker at
    // call-time so it remains valid across stopWorkerThread()/restart.
    // The engine outlives the adapter (engine owns the adapter as a
    // value member), so the lambda's captured `this` is always live
    // while the adapter is alive.
    m_calendarAdapter.setCancelOracle([this]() {
        return m_worker && m_worker->isCancelled();
    });
}

SyncEngine::~SyncEngine()
{
    stopWorkerThread();
}

void SyncEngine::setupWorkerConnections()
{
    if (!m_worker) return;

    // Connect worker signals to coordinator slots (Qt::QueuedConnection for cross-thread)
    connect(m_worker, &SyncEngineWorker::syncStarted,
            this, &SyncEngine::onWorkerSyncStarted, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::phaseChanged,
            this, &SyncEngine::onWorkerPhaseChanged, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::fetchProgress,
            this, &SyncEngine::onWorkerFetchProgress, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::itemReady,
            this, &SyncEngine::onWorkerItemReady, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::writeProgress,
            this, &SyncEngine::onWorkerWriteProgress, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::conflictDetected,
            this, &SyncEngine::onWorkerConflictDetected, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::conflictPauseRequested,
            this, &SyncEngine::onWorkerConflictPauseRequested, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::syncCompleted,
            this, &SyncEngine::onWorkerSyncCompleted, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::syncError,
            this, &SyncEngine::onWorkerSyncError, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::transcodingWarning,
            this, &SyncEngine::onWorkerTranscodingWarning, Qt::QueuedConnection);

    // Note: Worker is deleted explicitly in stopWorkerThread() rather than
    // via finished->deleteLater, since the thread's event loop has exited
    // by the time finished is emitted.
}

void SyncEngine::startWorkerThread()
{
    if (m_workerThread.isRunning()) {
        return;
    }

    // Set dependencies before moving to thread
    m_worker->setDependencies(m_controller, m_calendarBaselines, m_collection,
                              m_blobBaselines, &m_calendarAdapter, this);

    // Move worker to thread
    m_worker->moveToThread(&m_workerThread);

    // Start thread
    m_workerThread.start();

    qDebug() << "SyncEngine: Worker thread started";
}

void SyncEngine::stopWorkerThread()
{
    if (m_workerThread.isRunning()) {
        if (m_worker) {
            m_worker->cancel();
        }

        m_workerThread.quit();
        m_workerThread.wait();

        qDebug() << "SyncEngine: Worker thread stopped";
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

void SyncEngine::setCalendarBaselineStore(CalendarBaselineStore *store)
{
    m_calendarBaselines = store;
    m_calendarAdapter.setBaselineStore(store);
}

void SyncEngine::setCollection(ICalendarCollection *collection)
{
    m_collection = collection;
    m_calendarAdapter.setCollection(collection);
}

void SyncEngine::setBlobBaselineStore(BlobBaselineStore *store)
{
    m_blobBaselines = store;
}

void SyncEngine::setSyncConflictStore(SyncConflictStore *store)
{
    m_conflictStore = store;
}

void SyncEngine::loadSyncMappings(ICalendarCollection *collection)
{
    m_syncMappings.clear();
    setCollection(collection);

    if (!collection || !m_controller) {
        qDebug() << "SyncEngine::loadSyncMappings - no collection or controller";
        return;
    }

    // Load mappings from KalbConfigManager
    ISyncConfigStore *configManager = m_controller->configStore();
    if (!configManager) {
        qDebug() << "SyncEngine::loadSyncMappings - no config manager";
        return;
    }

    if (!configManager->hasSyncMappings()) {
        qDebug() << "SyncEngine::loadSyncMappings - no sync mappings configured";
        return;
    }

    m_syncMappings = configManager->syncMappings();
    qDebug() << "SyncEngine::loadSyncMappings - loaded"
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

void SyncEngine::setMappingEnabled(const QString &mappingId, bool enabled)
{
    for (auto &mapping : m_syncMappings) {
        if (mapping.id == mappingId) {
            mapping.enabled = enabled;
            break;
        }
    }
}

void SyncEngine::registerActiveController(const QString &calendarId,
                                                 DecSyncActiveController *controller)
{
    m_activeControllers[calendarId] = controller;
    qDebug() << "SyncEngine: Registered active controller for" << calendarId;
}

void SyncEngine::unregisterActiveController(const QString &calendarId)
{
    m_activeControllers.remove(calendarId);
}

bool SyncEngine::hasSyncWork() const
{
    return !m_syncMappings.isEmpty() || !m_activeControllers.isEmpty();
}

// F2 Task 42: the void runSync(behavior) form is deleted. Its body
// is rebadged into a private helper driveQueue() invoked by
// runSyncFuture(behavior) below — the only remaining caller.
void SyncEngine::driveQueue(SyncBehavior behavior)
{
    if (m_syncMappings.isEmpty() && m_activeControllers.isEmpty()) {
        qDebug() << "SyncEngine::driveQueue - no sync work configured";
        m_lastResult = SyncResult{};
        m_lastResult.success = true;
        // Finish the multi-iface (the QFuture caller is waiting on it).
        if (m_currentMultiIface) {
            m_currentMultiIface->reportResult(m_queueResults);
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        return;
    }

    m_isSyncing = true;
    m_cancelled = false;
    m_currentMappingIndex = -1;
    m_currentSyncBehavior = behavior;
    m_pendingUnmonitoredConflicts.clear();
    m_lastResult = SyncResult{};
    m_lastResult.startTime = QDateTime::currentDateTime();

    // F2 Task 21: tag this run as a queue dispatch so
    // onWorkerSyncCompleted advances to the next mapping rather than
    // finishing a single-mapping future.
    m_dispatchMode = DispatchMode::Queue;
    m_queueResults.clear();

    // Run active controllers first (they're fast, synchronous)
    for (auto it = m_activeControllers.constBegin(); it != m_activeControllers.constEnd(); ++it) {
        if (m_cancelled) break;
        emit progressUpdated(0, m_syncMappings.size() + m_activeControllers.size(),
                             tr("Syncing %1 (DecSync)").arg(it.key()));
        it.value()->runActiveSync();
    }

    // Phase-1 + Phase-2 perf: prime fresh CTags and fingerprints, decide
    // per-mapping skip eligibility. Best-effort; on failure we simply fall
    // back to per-call PROPFIND inside SyncEngineWorker.
    if (!m_cancelled && !m_syncMappings.isEmpty()) {
        prepareSyncFastPath();
    }

    if (m_syncMappings.isEmpty() || m_cancelled) {
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        m_lastResult.success = !m_cancelled;
        m_lastResult.endTime = QDateTime::currentDateTime();
        // Finish the multi-iface with what we have.
        if (m_currentMultiIface) {
            m_currentMultiIface->reportResult(m_queueResults);
            if (m_cancelled) m_currentMultiIface->reportCanceled();
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        m_dispatchMode = DispatchMode::None;
        return;
    }

    // Start worker thread for mapping-based sync
    startWorkerThread();
    processQueue();
}

// F2 Task 21: single-mapping driver. Dispatches exactly one Request to
// the worker; onWorkerSyncCompleted distinguishes via m_dispatchMode and
// finishes immediately rather than advancing a queue. This replaces the
// leaky path documented in FINDINGS where the single-mapping form
// re-entered processNextMapping (which iterated from index 0 and
// double-dispatched the same mapping).
void SyncEngine::processSingleMapping(const QString &mappingId,
                                      SyncBehavior behavior)
{
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

    // F2 Task 23 follow-up: cancel-precheck. If cancellation was
    // observed before the worker dispatches (e.g., the caller
    // invoked QFuture::cancel() immediately after runSyncFuture
    // returned), short-circuit with a cancelled SyncResult.
    // Symmetric to the multi-mapping path's check at the top of
    // advanceQueue(). Task 21's plan body specified this; the
    // landed Task 21 commit (35c1881) did the structural split but
    // did not include the precheck. C1 (commit 4b24a08) exposed
    // the gap.
    if (m_cancelled) {
        SyncResult cancelled;
        cancelled.success = false;
        cancelled.cancelled = true;
        cancelled.skipped = true;
        cancelled.startTime = QDateTime::currentDateTime();
        cancelled.endTime = cancelled.startTime;
        if (m_currentSingleIface) {
            m_currentSingleIface->reportResult(cancelled);
            m_currentSingleIface->reportCanceled();
            m_currentSingleIface->reportFinished();
            delete m_currentSingleIface;
            m_currentSingleIface = nullptr;
        }
        m_dispatchMode = DispatchMode::None;
        m_isSyncing = false;
        return;
    }

    int idx = 0;
    for (const auto &mapping : m_syncMappings) {
        if (mapping.id == mappingId && mapping.enabled) {
            m_isSyncing = true;
            m_cancelled = false;
            m_currentSyncBehavior = behavior;
            m_lastResult = SyncResult{};
            m_lastResult.startTime = QDateTime::currentDateTime();

            // F2 Task 21: tag the run mode and remember which mapping is
            // in flight so onWorkerItemReady can resolve metadata. This
            // is the only field shared with the Queue path.
            m_dispatchMode = DispatchMode::Single;
            m_currentMappingIndex = idx;

            // Start worker thread if not running
            startWorkerThread();

            // Create request and invoke worker
            SyncEngineWorker::Request request;
            request.mapping = mapping;
            request.mode = (behavior == SyncBehavior::Monitored)
                ? SyncEngineWorker::Mode::Monitored
                : SyncEngineWorker::Mode::Unmonitored;
            request.collectionId = m_collection ? m_collection->id() : QString();
            request.useQuickPath = !m_calendarBaselines || !m_calendarBaselines->hasBaselines(mapping.id);

            // Invoke worker in its thread
            QMetaObject::invokeMethod(m_worker, "processSync",
                                      Qt::QueuedConnection,
                                      Q_ARG(SyncEngineWorker::Request, request));
            return;
        }
        ++idx;
    }
    qWarning() << "SyncEngine::runSync - mapping not found:" << mappingId;

    // Mapping-not-found: report failure on the future iface (if any
    // QFuture caller is waiting) and emit the legacy syncCompleted.
    SyncResult err;
    err.success = false;
    err.errorMessage = QStringLiteral("Mapping not found: %1").arg(mappingId);
    err.startTime = QDateTime::currentDateTime();
    err.endTime = err.startTime;
    if (m_currentSingleIface) {
        m_currentSingleIface->reportResult(err);
        m_currentSingleIface->reportFinished();
        delete m_currentSingleIface;
        m_currentSingleIface = nullptr;
    }
    m_dispatchMode = DispatchMode::None;
    // F2 Task 21 follow-up: clear m_isSyncing on the not-found path.
    // runSyncFuture sets m_isSyncing = true before calling
    // processSingleMapping; if we return here without dispatching,
    // nothing else will clear it and subsequent runSync* calls are
    // rejected by the m_isSyncing guard.
    m_isSyncing = false;
}

// F2 Task 21: rewritten to populate m_currentSingleIface directly
// (replacing the fragile signal-shim from Task 15). The engine owns the
// iface; processSingleMapping installs m_dispatchMode = Single, and
// onWorkerSyncCompleted reports the single result and finishes the
// future. No connection-management gymnastics, no double-fire window.
QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    SyncBehavior behavior)
{
    if (m_isSyncing || m_currentSingleIface || m_currentMultiIface) {
        // Reject overlapping runs cleanly with a finished failed future.
        QFutureInterface<SyncResult> rejected;
        rejected.reportStarted();
        SyncResult err;
        err.success = false;
        err.errorMessage = QStringLiteral("Sync already in progress");
        rejected.reportResult(err);
        rejected.reportFinished();
        return rejected.future();
    }

    // Allocate iface; processSingleMapping → onWorkerSyncCompleted will
    // populate / finalise it.
    m_currentSingleIface = new QFutureInterface<SyncResult>;
    m_currentSingleIface->reportStarted();
    // F2 Task 23 follow-up: ensure cancellation-marker SyncResults
    // reach future.results() even after reportCanceled(). Per Qt6
    // QFutureInterface, reportResult is silently dropped once
    // cancellation is reported unless this opt-in is set. The F2
    // contract delivers a cancelled SyncResult on the future for
    // cancel-before/after-start; without this opt-in, consumers
    // see future.results() as empty.
    m_currentSingleIface->setAddResultsIfCanceledEnabled(true);
    QFuture<SyncResult> future = m_currentSingleIface->future();

    // F2 Task 17: install QFutureWatcher to forward QFuture::cancel()
    // to the worker via onCancelObserved slot.
    delete m_singleWatcher;
    m_singleWatcher = new QFutureWatcher<SyncResult>(this);
    m_singleWatcher->setFuture(future);
    connect(m_singleWatcher, &QFutureWatcher<SyncResult>::canceled,
            this, &SyncEngine::onCancelObserved);

    m_isSyncing = true;
    processSingleMapping(mappingId, behavior);
    return future;
}

// F2 Task 21: rewritten to populate m_currentMultiIface directly. The
// per-mapping results accumulate in m_queueResults (filled by
// onWorkerSyncCompleted during a Queue run); when the queue drains,
// processQueue's terminal branch reports them on the iface.
QFuture<QList<SyncResult>> SyncEngine::runSyncFuture(
    SyncBehavior behavior)
{
    if (m_isSyncing || m_currentSingleIface || m_currentMultiIface) {
        QFutureInterface<QList<SyncResult>> rejected;
        rejected.reportStarted();
        rejected.reportResult(QList<SyncResult>{});
        rejected.reportFinished();
        return rejected.future();
    }

    m_currentMultiIface = new QFutureInterface<QList<SyncResult>>;
    m_currentMultiIface->reportStarted();
    // F2 Task 23 follow-up: see runSyncFuture(mappingId) overload —
    // cancellation-marker results must reach future.results() even
    // after reportCanceled().
    m_currentMultiIface->setAddResultsIfCanceledEnabled(true);
    QFuture<QList<SyncResult>> future = m_currentMultiIface->future();

    // F2 Task 17: install QFutureWatcher to forward QFuture::cancel()
    // to the worker via onCancelObserved slot.
    delete m_multiWatcher;
    m_multiWatcher = new QFutureWatcher<QList<SyncResult>>(this);
    m_multiWatcher->setFuture(future);
    connect(m_multiWatcher, &QFutureWatcher<QList<SyncResult>>::canceled,
            this, &SyncEngine::onCancelObserved);

    driveQueue(behavior);
    return future;
}

// F2 Task 17: forwards QFutureWatcher::canceled to the worker thread.
// Runs on the engine thread (where the watcher lives); hops to the
// worker thread via Qt::QueuedConnection. The worker's observeCancel
// (added in Task 16) sets m_cancelled and emits cancellationObserved
// to wake nested QEventLoops in await<> / conflict pause.
void SyncEngine::onCancelObserved()
{
    // F2 Task 23 follow-up: also set the engine-side m_cancelled flag
    // so onWorkerSyncCompleted decorates the final SyncResult with
    // cancelled=true (and reportCanceled fires on the iface). Without
    // this, future.cancel() observed via the watcher only forwards to
    // the worker; the worker's syncCompleted result lands with
    // cancelled=false, and the iface never sees reportCanceled().
    m_cancelled = true;

    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "observeCancel",
                              Qt::QueuedConnection);
}

void SyncEngine::resumeAfterConflictResolution(ConflictResolution resolution,
                                                     const QString &mergedIcal)
{
    if (!m_worker) {
        qWarning() << "SyncEngine::resumeAfterConflictResolution - no worker";
        return;
    }

    qDebug() << "SyncEngine::resumeAfterConflictResolution - resolution:"
             << static_cast<int>(resolution);

    // Invoke on worker thread
    QMetaObject::invokeMethod(m_worker, "resumeAfterConflict",
                              Qt::QueuedConnection,
                              Q_ARG(ConflictResolution, resolution),
                              Q_ARG(QString, mergedIcal));
}

void SyncEngine::setSkipUnchangedMappings(bool enabled)
{
    m_skipUnchangedMappings = enabled;
    qDebug() << "SyncEngine::setSkipUnchangedMappings:" << enabled;
}

void SyncEngine::prepareSyncFastPath()
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
                qInfo() << "SyncEngine: skipping unchanged mapping" << mapping.id;
            } else {
                qInfo() << "SyncEngine: would skip unchanged mapping (flag off)"
                        << mapping.id;
            }
        }
    }

    qDebug() << "SyncEngine::prepareSyncFastPath: of"
             << m_syncMappings.size() << "mappings,"
             << wouldSkipCount << "are unchanged;"
             << actualSkipCount << "actually skipped (flag="
             << m_skipUnchangedMappings << ")";
}

// F2 Task 21: multi-mapping driver — entry point for a queue run.
// Initializes the index and kicks off the first iteration. Subsequent
// iterations are driven by onWorkerSyncCompleted -> advanceQueue while
// m_dispatchMode == Queue. This replaces processNextMapping; the
// single-mapping form no longer participates in queue iteration, fixing
// the FINDINGS leak structurally.
void SyncEngine::processQueue()
{
    advanceQueue();
}

void SyncEngine::advanceQueue()
{
    // Debug log removed - SyncEngineWorker provides detailed timing

    if (m_cancelled) {
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        m_lastResult.success = false;
        m_lastResult.errorMessage = QStringLiteral("Sync cancelled");
        m_lastResult.endTime = QDateTime::currentDateTime();

        // F2 Task 21: finish the multi-iface (if any) with what we have.
        if (m_currentMultiIface) {
            m_currentMultiIface->reportResult(m_queueResults);
            m_currentMultiIface->reportCanceled();
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        m_dispatchMode = DispatchMode::None;
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

        // F2 Task 21: finish the multi-iface (if any) with the per-
        // mapping results. The future resolves to the per-mapping
        // list; the aggregate result is observable via lastSyncResult().
        if (m_currentMultiIface) {
            m_currentMultiIface->reportResult(m_queueResults);
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        m_dispatchMode = DispatchMode::None;
        return;
    }

    const SyncMapping &mapping = m_syncMappings[m_currentMappingIndex];

    // Phase-2 skip: if this mapping's both endpoints are demonstrably
    // unchanged AND the skip flag is on, short-circuit without dispatching
    // to the worker. Append a successful no-op result to the queue so
    // the future caller sees per-mapping completion in resultAt(0).
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
        m_queueResults.append(skippedResult);

        // Advance to the next mapping without touching the worker.
        advanceQueue();
        return;
    }

    emit progressUpdated(m_currentMappingIndex + 1, m_syncMappings.size(),
                         tr("Syncing %1").arg(mapping.id));

    // Create request and invoke worker directly
    SyncEngineWorker::Request request;
    request.mapping = mapping;
    request.mode = (m_currentSyncBehavior == SyncBehavior::Monitored)
        ? SyncEngineWorker::Mode::Monitored : SyncEngineWorker::Mode::Unmonitored;
    request.collectionId = m_collection ? m_collection->id() : QString();
    request.useQuickPath = !m_calendarBaselines || !m_calendarBaselines->hasBaselines(mapping.id);

    QMetaObject::invokeMethod(m_worker, "processSync",
                              Qt::QueuedConnection,
                              Q_ARG(SyncEngineWorker::Request, request));

    // NOTE: Do NOT recurse here!
    // The async operation will call onWorkerSyncCompleted() when done,
    // which will then call advanceQueue() again (Queue mode only).
}

// ============================================================================
// Helper Methods
// ============================================================================

void SyncEngine::updateSyncMetadata(const SyncMapping &mapping, const SyncDiff &diff,
                                          const QList<SyncChange> &resolvedToTarget,
                                          const QList<SyncChange> &resolvedToSource)
{
    if (!m_calendarBaselines) {
        qDebug() << "SyncEngine::updateSyncMetadata - no CalendarBaselineStore, skipping baseline update";
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
            qDebug() << "SyncEngine::updateSyncMetadata - updating baseline for resolved conflict:"
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
            qDebug() << "SyncEngine::updateSyncMetadata - updating baseline for resolved conflict:"
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
        qDebug() << "SyncEngine::updateSyncMetadata - unchanged item has no baseline:" << uid;
    }

    // Update last sync time
    m_calendarBaselines->setLastSyncTime(mapping.id, QDateTime::currentDateTime());
}

// ============================================================================
// Worker Thread Signal Handlers
// ============================================================================

void SyncEngine::onWorkerSyncStarted(const QString &mappingId)
{
    // Debug log removed - SyncEngineWorker shows detailed start info
    emit syncStarted(mappingId);
}

void SyncEngine::onWorkerPhaseChanged(const QString &mappingId, int phase)
{
    Q_UNUSED(mappingId);
    m_currentPhase = static_cast<SyncPhase>(phase);
    emit phaseChanged(m_currentPhase);
}

void SyncEngine::onWorkerFetchProgress(const QString &calendarId, int current, int total)
{
    emit fetchProgress(calendarId, current, total);
}

void SyncEngine::onWorkerItemReady(const QString &calendarId,
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
                    qWarning() << "SyncEngine::onWorkerItemReady: No MemoryCalendar for" << calendarId
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

void SyncEngine::onWorkerWriteProgress(const QString &calendarId, int current, int total)
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

void SyncEngine::onWorkerConflictDetected(const ConflictInfo &conflict)
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

void SyncEngine::onWorkerConflictPauseRequested(const ConflictInfo &conflict)
{
    qDebug() << "SyncEngine::onWorkerConflictPauseRequested - conflict:" << conflict.sourceId;

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

void SyncEngine::onWorkerSyncCompleted(const QString &mappingId, const SyncResult &result)
{
    // Batch-present any unmonitored conflicts collected during this mapping.
    // handleConflicts() (plural) applies hybrid threshold: shows dialogs for
    // small batches, defers large batches to the dock widget.
    if (m_conflictManager && !m_pendingUnmonitoredConflicts.isEmpty()) {
        qDebug() << "SyncEngine: Batch-presenting"
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

    // Reset phase
    m_currentPhase = SyncPhase::Complete;
    emit phaseChanged(m_currentPhase);

    // F2 Task 21: dispatch on mode rather than unconditionally calling
    // processNextMapping (which iterated from index 0 for the single-
    // mapping form — see FINDINGS "SyncEngine::runSync(mappingId) is
    // leaky"). Single-mapping runs finish here; queue runs advance.
    if (m_dispatchMode == DispatchMode::Single) {
        // Finish the single-mapping future and shut the run down.
        // F2 Task 23 follow-up: when cancellation was observed
        // (m_cancelled set by onCancelObserved), decorate
        // the SyncResult with cancelled=true so consumers reading the
        // result via resultAt(0) / resultStoreBase see the marker.
        // The worker sets success=false errorMessage="Cancelled" but
        // doesn't flip the cancelled bit; do it here where we know.
        SyncResult finalResult = result;
        if (m_cancelled || result.cancelled) {
            finalResult.cancelled = true;
            // skipped=true marks "this slot in the run never produced
            // a successful sync"; appropriate when cancelled before
            // any items were applied (C1's case). Mid-fetch / mid-apply
            // cancels with partial work leave skipped=false so consumers
            // can distinguish never-started from cancelled-after-partial.
            if (!finalResult.sourceStats.hasChanges() &&
                !finalResult.targetStats.hasChanges()) {
                finalResult.skipped = true;
            }
            finalResult.success = false;
        }
        if (m_currentSingleIface) {
            m_currentSingleIface->reportResult(finalResult);
            if (m_cancelled || finalResult.cancelled) {
                m_currentSingleIface->reportCanceled();
            }
            m_currentSingleIface->reportFinished();
            delete m_currentSingleIface;
            m_currentSingleIface = nullptr;
        }
        m_dispatchMode = DispatchMode::None;
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        return;
    }

    // Queue mode: append to the per-mapping result list and advance.
    if (m_dispatchMode == DispatchMode::Queue) {
        m_queueResults.append(result);
    }

    // Continue to next mapping (queue mode only).
    advanceQueue();
}

void SyncEngine::onWorkerSyncError(const QString &mappingId, const QString &errorMessage)
{
    qWarning() << "SyncEngine::onWorkerSyncError - mapping:" << mappingId
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

    // F2 Task 21: same dispatch-on-mode pattern as onWorkerSyncCompleted.
    if (m_dispatchMode == DispatchMode::Single) {
        if (m_currentSingleIface) {
            m_currentSingleIface->reportResult(failedResult);
            m_currentSingleIface->reportFinished();
            delete m_currentSingleIface;
            m_currentSingleIface = nullptr;
        }
        m_dispatchMode = DispatchMode::None;
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        return;
    }

    if (m_dispatchMode == DispatchMode::Queue) {
        m_queueResults.append(failedResult);
    }

    // Continue to next mapping (don't fail the whole batch).
    advanceQueue();
}

void SyncEngine::onWorkerTranscodingWarning(const QString &calendarId,
                                                  const QString &uid,
                                                  const QStringList &warnings)
{
    qDebug() << "SyncEngine::onWorkerTranscodingWarning - calendar:" << calendarId
             << "uid:" << uid << "warnings:" << warnings;

    // Forward the transcoding warning signal
    emit transcodingWarning(calendarId, uid, warnings);
}

// ---------------------------------------------------------------------------
// One-shot blob API (F1 Task 6)
// ---------------------------------------------------------------------------
//
// Bodies lifted from the legacy BlobSyncEngine::twoWayWithBaseline / mirror
// (deleted in F1 Task 10). Behavior parity is required for WildPalms's
// migrated SyncRunner. Fetch is routed through
// BlobDomainAdapter::fetchRecordsBlob; the rich ConflictHandlerRegistry /
// ConflictStore / ConflictPolicy logic stays inline because the
// IDomainAdapter::merge contract takes only ConflictResolution today
// (de-singletonisation of the conflict registry is deferred to G).
namespace {

QHash<QString, BackendRecord> indexBlobById(const QList<BackendRecord> &records)
{
    QHash<QString, BackendRecord> out;
    out.reserve(records.size());
    for (const auto &r : records) {
        out.insert(r.id, r);
    }
    return out;
}

} // namespace

BlobSyncResult SyncEngine::runBlobMirror(IBlobBackend *source,
                                          IBlobBackend *target,
                                          const QString &collectionId)
{
    BlobSyncResult result;
    if (!source || !target) {
        result.success = false;
        result.errorMessage = QStringLiteral("runBlobMirror: null backend");
        return result;
    }

    const auto srcRecords = m_blobAdapter.fetchRecordsBlob(source, collectionId);
    const auto tgtRecords = m_blobAdapter.fetchRecordsBlob(target, collectionId);
    const auto tgtById = indexBlobById(tgtRecords);

    // Copy source → target (create or update).
    for (const auto &sr : srcRecords) {
        const auto it = tgtById.constFind(sr.id);
        if (it == tgtById.constEnd()) {
            if (target->createRecord(collectionId, sr).isEmpty()) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.created;
            }
        } else if (it.value().contentHash != sr.contentHash) {
            BackendRecord out = sr;
            out.id = it.value().id;
            if (!target->updateRecord(out)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.updated;
            }
        } else {
            ++result.targetStats.unchanged;
        }
    }

    // Delete target records not in source.
    const auto srcById = indexBlobById(srcRecords);
    for (const auto &tr : tgtRecords) {
        if (!srcById.contains(tr.id)) {
            if (!target->deleteRecord(tr.id)) {
                ++result.targetStats.errors;
            } else {
                ++result.targetStats.deleted;
            }
        }
    }

    result.success = (result.targetStats.errors == 0);
    return result;
}

BlobSyncResult SyncEngine::runBlobTwoWay(
    IBlobBackend *a,
    IBlobBackend *b,
    const QString &collectionId,
    const QString &mappingId,
    BlobBaselineStore *baseline,
    QSyncCore::ConflictHandlerRegistry *handlers,
    QSyncCore::ConflictStore *conflicts,
    const QSyncCore::ConflictPolicy &policy)
{
    BlobSyncResult result;
    if (!a || !b || !baseline) {
        result.success = false;
        result.errorMessage = QStringLiteral(
            "runBlobTwoWay: null backend or baseline store");
        return result;
    }

    const QHash<QString, BackendRecord> byIdA =
        indexBlobById(m_blobAdapter.fetchRecordsBlob(a, collectionId));
    const QHash<QString, BackendRecord> byIdB =
        indexBlobById(m_blobAdapter.fetchRecordsBlob(b, collectionId));

    // Triple-keyed baseline lookup (Phase F1 Task 11): backend identity is
    // taken from the source backend `a`. The `mappingId` parameter is kept
    // for conflict-record identification only.
    const QString backendId = a->backendId();
    QHash<QString, QString> baselineHashes;
    const QStringList baseIds =
        baseline->baselineRecordIds(backendId, collectionId);
    for (const QString &id : baseIds) {
        baselineHashes.insert(id,
                              baseline->baselineHash(backendId, collectionId, id));
    }

    QSet<QString> allIds;
    for (auto it = byIdA.constBegin(); it != byIdA.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = byIdB.constBegin(); it != byIdB.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = baselineHashes.constBegin(); it != baselineHashes.constEnd(); ++it) allIds.insert(it.key());

    QMap<QString, QString> finalHashes;

    for (const QString &id : allIds) {
        const bool hasA = byIdA.contains(id);
        const bool hasB = byIdB.contains(id);
        const bool hasBase = baselineHashes.contains(id);

        if (hasA && hasB && hasBase) {
            const BackendRecord ra = byIdA.value(id);
            const BackendRecord rb = byIdB.value(id);
            const QString bHash = baselineHashes.value(id);
            const bool aChanged = (ra.contentHash != bHash);
            const bool bChanged = (rb.contentHash != bHash);

            if (!aChanged && !bChanged) {
                result.sourceStats.unchanged++;
                result.targetStats.unchanged++;
                finalHashes.insert(id, ra.contentHash);
            } else if (aChanged && !bChanged) {
                if (b->updateRecord(ra)) {
                    result.targetStats.updated++;
                    finalHashes.insert(id, ra.contentHash);
                } else {
                    result.targetStats.errors++;
                    finalHashes.insert(id, bHash);
                }
            } else if (!aChanged && bChanged) {
                if (a->updateRecord(rb)) {
                    result.sourceStats.updated++;
                    finalHashes.insert(id, rb.contentHash);
                } else {
                    result.sourceStats.errors++;
                    finalHashes.insert(id, bHash);
                }
            } else {
                // Both modified → conflict.
                QSyncCore::ConflictRecord cr;
                cr.conflictId = QStringLiteral("%1:%2").arg(mappingId, id);
                cr.conduitId = mappingId;
                cr.type = QSyncCore::ConflictType::BothModified;
                cr.source.id = ra.id;
                cr.source.description = ra.displayName;
                cr.source.content = ra.data;
                cr.source.contentHash = ra.contentHash;
                cr.source.contentType = ra.type;
                cr.source.lastModified = ra.lastModified;
                cr.target.id = rb.id;
                cr.target.description = rb.displayName;
                cr.target.content = rb.data;
                cr.target.contentHash = rb.contentHash;
                cr.target.contentType = rb.type;
                cr.target.lastModified = rb.lastModified;
                cr.detectedAt = QDateTime::currentDateTimeUtc();

                QSyncCore::ConflictHandler *h = handlers
                    ? handlers->handlerFor(a->backendId())
                    : nullptr;

                QSyncCore::ConflictDecision decision = QSyncCore::ConflictDecision::Pending;
                if (h) decision = h->handleConflict(cr, policy);

                if (decision == QSyncCore::ConflictDecision::UseSource) {
                    if (b->updateRecord(ra)) {
                        result.targetStats.updated++;
                        finalHashes.insert(id, ra.contentHash);
                    } else {
                        result.targetStats.errors++;
                        finalHashes.insert(id, bHash);
                    }
                } else if (decision == QSyncCore::ConflictDecision::UseTarget) {
                    if (a->updateRecord(rb)) {
                        result.sourceStats.updated++;
                        finalHashes.insert(id, rb.contentHash);
                    } else {
                        result.sourceStats.errors++;
                        finalHashes.insert(id, bHash);
                    }
                } else {
                    if (conflicts) conflicts->addConflict(cr);
                    result.sourceStats.conflicts++;
                    finalHashes.insert(id, bHash);
                }
            }
        } else if (!hasA && hasB && hasBase) {
            // Deleted on A since baseline → delete on B.
            if (b->deleteRecord(id)) {
                result.targetStats.deleted++;
            } else {
                result.targetStats.errors++;
                finalHashes.insert(id, baselineHashes.value(id));
            }
        } else if (hasA && !hasB && hasBase) {
            // Deleted on B since baseline → delete on A.
            if (a->deleteRecord(id)) {
                result.sourceStats.deleted++;
            } else {
                result.sourceStats.errors++;
                finalHashes.insert(id, baselineHashes.value(id));
            }
        } else if (hasA && !hasB && !hasBase) {
            // New on A → create on B.
            const BackendRecord ra = byIdA.value(id);
            if (!b->createRecord(collectionId, ra).isEmpty()) {
                result.targetStats.created++;
                finalHashes.insert(id, ra.contentHash);
            } else {
                result.targetStats.errors++;
            }
        } else if (!hasA && hasB && !hasBase) {
            // New on B → create on A.
            const BackendRecord rb = byIdB.value(id);
            if (!a->createRecord(collectionId, rb).isEmpty()) {
                result.sourceStats.created++;
                finalHashes.insert(id, rb.contentHash);
            } else {
                result.sourceStats.errors++;
            }
        }
        // Other edge cases (both missing, or impossible combos) fall through.
    }

    if (!finalHashes.isEmpty()) {
        baseline->commitBaselines(backendId, collectionId, finalHashes);
    }

    result.success = (result.sourceStats.errors == 0 && result.targetStats.errors == 0);
    return result;
}

// ============================================================================
// SyncEngineWorker — runs sync operations on m_workerThread.
//
// Phase F1 Task 8 (2026-04-29): formerly a standalone worker class in
// src/calendar/ (deleted in this task). The implementation is moved here
// verbatim; the file boundary went away because the worker is now a private
// implementation detail of SyncEngine.
// ============================================================================

namespace {

// Build a compound sync key from a SyncRecord (uid + recurrenceId).
// Matches the syncRecordKey() format from syncdiff.cpp.
QString syncRecordKey(const SyncRecord &rec)
{
    if (rec.recurrenceId.isValid())
        return rec.uid + QChar(0) + rec.recurrenceId.toString(Qt::ISODate);
    return rec.uid;
}

inline IBlobBackend *asBlob(SyncBackend *b) { return static_cast<IBlobBackend *>(b); }

// Register metatypes for cross-thread signal/slot.
const bool engineWorkerMetatypesRegistered = []() {
    qRegisterMetaType<SyncEngineWorker::Request>("SyncEngineWorker::Request");
    qRegisterMetaType<SyncEngineWorker::Mode>("SyncEngineWorker::Mode");
    qRegisterMetaType<ConflictResolution>("ConflictResolution");
    qRegisterMetaType<ConflictInfo>("ConflictInfo");
    qRegisterMetaType<SyncResult>("SyncResult");
    qRegisterMetaType<KCalendarCore::Incidence::Ptr>("KCalendarCore::Incidence::Ptr");
    return true;
}();

} // namespace

SyncEngineWorker::SyncEngineWorker(const TranscodingRouter &router, QObject *parent)
    : QObject(parent)
    , m_router(router)
{
    // F2 Task 20: when cancellation is observed (via observeCancel()
    // queued from the engine thread), wake any in-progress conflict
    // pause. The conflict pause is a state-machine yield rather than
    // a QEventLoop, so the wiring is a self-connection that performs
    // the cancelled-completion teardown when m_yieldedForConflict is
    // set. DirectConnection is correct: observeCancel() already runs
    // on the worker thread, so cancellationObserved fires here on the
    // worker thread.
    connect(this, &SyncEngineWorker::cancellationObserved,
            this, &SyncEngineWorker::onCancelDuringConflictPause,
            Qt::DirectConnection);
}

SyncEngineWorker::~SyncEngineWorker()
{
    QMutexLocker locker(&m_mutex);
    m_cancelled = true;
}

void SyncEngineWorker::setDependencies(ISyncHost *host,
                                        CalendarBaselineStore *calendarBaselines,
                                        ICalendarCollection *collection,
                                        BlobBaselineStore *blobBaselines,
                                        CalendarDomainAdapter *calendarAdapter,
                                        SyncEngine *engine)
{
    m_controller = host;
    m_calendarBaselines = calendarBaselines;
    m_blobBaselines = blobBaselines;
    m_collection = collection;
    m_calendarAdapter = calendarAdapter;
    m_engine = engine;
}

void SyncEngineWorker::cancel()
{
    QMutexLocker locker(&m_mutex);
    m_cancelled = true;
}

void SyncEngineWorker::observeCancel()
{
    // F2 Task 16: invoked via queued connection from the engine side
    // when QFutureWatcher::canceled fires (Task 17 wires the engine
    // side). No mutex — atomic store is the right primitive here so
    // that forwarding cancellation from the engine thread does not
    // block on the worker's m_mutex.
    m_cancelled.store(true, std::memory_order_release);
    emit cancellationObserved();
}

void SyncEngineWorker::processSync(const SyncEngineWorker::Request &request)
{
    auto syncModeStr = [](SyncMode mode) -> const char* {
        switch (mode) {
            case SyncMode::Disabled: return "Disabled";
            case SyncMode::OneWayUpload: return "OneWayUpload";
            case SyncMode::OneWayDownload: return "OneWayDownload";
            case SyncMode::TwoWay: return "TwoWay";
            default: return "Unknown";
        }
    };

    qDebug().noquote() << QString("SyncEngineWorker: === Starting sync [%1/%2] -> [%3/%4] ===")
        .arg(request.mapping.sourceBackend, request.mapping.sourceCalendar,
             request.mapping.targetBackend, request.mapping.targetCalendar);
    qDebug() << "  mapping:" << request.mapping.id
             << "mode:" << syncModeStr(request.mapping.mode)
             << (request.mode == Mode::Monitored ? "(monitored)" : "(unmonitored)");

    m_totalTimer.start();
    m_propertyFetchMs = 0;
    m_propertyDiffMs = 0;
    m_propertyApplyMs = 0;
    m_sourceFetchMs = 0;
    m_targetFetchMs = 0;
    m_diffMs = 0;

    {
        QMutexLocker locker(&m_mutex);
        m_cancelled = false;
        m_fetchFailed = false;
        m_applyFailed = false;
    }
    m_fetchErrorMessage.clear();
    m_applyErrorMessage.clear();
    m_yieldedForConflict = false;
    m_conflictPhase = ConflictPhase::Done;
    m_conflictIndex = 0;

    m_currentRequest = request;
    m_sourceRecords.clear();
    m_targetRecords.clear();
    m_currentDiff = SyncDiff();
    m_currentResult = SyncResult{};
    m_currentResult.startTime = QDateTime::currentDateTime();
    m_resolvedToTarget.clear();
    m_resolvedToSource.clear();
    m_resolvedToSourceConflictStart = 0;

    m_sourceProperties = CalendarPropertyRecord();
    m_targetProperties = CalendarPropertyRecord();
    m_propertyDiff = CalendarPropertyDiff();

    emit syncStarted(request.mapping.id);

    // First-sync fast path (Phase D Task 21).
    if (request.useQuickPath && request.mapping.mode == SyncMode::OneWayUpload) {
        if (dispatchFirstSync(request))
            return;
    }

    m_phaseTimer.start();
    fetchCalendarProperties();
    m_propertyFetchMs = m_phaseTimer.elapsed();

    m_phaseTimer.restart();
    computePropertyDiff();
    m_propertyDiffMs = m_phaseTimer.elapsed();

    m_phaseTimer.restart();
    applyPropertyChanges();
    m_propertyApplyMs = m_phaseTimer.elapsed();

    m_phaseTimer.start();
    fetchSourceRecords();
    m_sourceFetchMs = m_phaseTimer.elapsed();

    if (m_fetchFailed) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = m_fetchErrorMessage.isEmpty()
            ? QStringLiteral("Source fetch failed") : m_fetchErrorMessage;
        m_currentResult.endTime = QDateTime::currentDateTime();
        qWarning() << "SyncEngineWorker: Source fetch failed, aborting sync:" << m_currentResult.errorMessage;
        emit syncCompleted(request.mapping.id, m_currentResult);
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(request.mapping.id, m_currentResult);
            return;
        }
    }

    m_phaseTimer.restart();
    fetchTargetRecords();
    m_targetFetchMs = m_phaseTimer.elapsed();

    if (m_fetchFailed) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = m_fetchErrorMessage.isEmpty()
            ? QStringLiteral("Target fetch failed") : m_fetchErrorMessage;
        m_currentResult.endTime = QDateTime::currentDateTime();
        qWarning() << "SyncEngineWorker: Target fetch failed, aborting sync:" << m_currentResult.errorMessage;
        emit syncCompleted(request.mapping.id, m_currentResult);
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(request.mapping.id, m_currentResult);
            return;
        }
    }

    m_phaseTimer.restart();
    computeDiff();
    handleConflicts();

    if (m_yieldedForConflict) {
        return;
    }

    continueAfterConflicts();
}

void SyncEngineWorker::onCancelDuringConflictPause()
{
    // F2 Task 20: cancellationObserved fired. The conflict pause is a
    // state-machine yield (m_yieldedForConflict + early return in
    // handleConflicts()), not a QEventLoop, so we don't need to quit
    // anything — we just need to tear down the sync via the cancelled
    // path if we're currently paused.
    //
    // If we're not yielded, this is a no-op: cancellation during the
    // active phases is observed by the per-record CancelOracle (Task
    // 19) and the await<> boundary check (Task 16), and processSync
    // will already exit on the next mutex-guarded m_cancelled check.
    if (!m_yieldedForConflict) {
        return;
    }

    qInfo() << "SyncEngineWorker: cancellation observed during"
            << "conflict pause — leaving conflict in store and"
            << "completing sync as cancelled";

    // Clear the yield flag so a late-arriving resumeAfterConflict
    // (already queued before cancel) is ignored by its existing guard
    // (the !m_yieldedForConflict check at the top of
    // resumeAfterConflict).
    m_yieldedForConflict = false;

    // DO NOT modify SyncConflictStore: the conflict that was waiting
    // on user resolution must remain in the persistent store so the
    // next sync run picks it up.

    m_currentResult.success = false;
    m_currentResult.errorMessage = QStringLiteral("Cancelled");
    m_currentResult.endTime = QDateTime::currentDateTime();
    emit syncCompleted(m_currentRequest.mapping.id, m_currentResult);
}

void SyncEngineWorker::resumeAfterConflict(ConflictResolution resolution, const QString &mergedIcal)
{
    qDebug() << "SyncEngineWorker::resumeAfterConflict - resolution:" << static_cast<int>(resolution);

    if (!m_yieldedForConflict) {
        qWarning() << "SyncEngineWorker::resumeAfterConflict called but not yielded — ignoring";
        return;
    }

    const QList<SyncChange> &currentList =
        (m_conflictPhase == ConflictPhase::ToTarget) ? m_currentDiff.toTarget : m_currentDiff.toSource;

    if (m_conflictIndex < currentList.size()) {
        const SyncChange &change = currentList[m_conflictIndex];
        applyMonitoredResolution(change, resolution, mergedIcal);
    }

    m_conflictIndex++;
    m_yieldedForConflict = false;

    handleConflicts();

    if (m_yieldedForConflict) {
        return;
    }

    continueAfterConflicts();
}

// ----------------------------------------------------------------------------
// First-sync dispatch via the engine's own blob mirror (Phase D Task 21;
// originally routed through the standalone BlobSyncEngine, now routed
// through SyncEngine::runBlobMirror per F1 Task 10).
// ----------------------------------------------------------------------------

bool SyncEngineWorker::dispatchFirstSync(const Request &request)
{
    qDebug() << "SyncEngineWorker::dispatchFirstSync - checking if target is empty for"
             << request.mapping.id;

    SyncBackend *srcBackend = m_controller->backendById(request.mapping.sourceBackend);
    SyncBackend *tgtBackend = m_controller->backendById(request.mapping.targetBackend);

    if (!srcBackend || !tgtBackend) {
        SyncResult result;
        result.success = false;
        result.errorMessage = QStringLiteral("dispatchFirstSync: backend not found");
        emit syncCompleted(request.mapping.id, result);
        return true;
    }

    IBlobBackend *tgt = asBlob(tgtBackend);
    const QString colId = request.mapping.sourceCalendar;

    bool targetEmpty = false;
    QMetaObject::invokeMethod(tgtBackend,
        [tgt, colId, &targetEmpty]() {
            targetEmpty = tgt->loadRecords(colId).isEmpty();
        }, Qt::BlockingQueuedConnection);

    if (!targetEmpty) {
        qDebug() << "SyncEngineWorker::dispatchFirstSync - target non-empty, deferring to quick-path for"
                 << request.mapping.id;
        return false;
    }

    qDebug() << "SyncEngineWorker::dispatchFirstSync - target empty, routing via SyncEngine::runBlobMirror for"
             << request.mapping.id;

    IBlobBackend *src = asBlob(srcBackend);

    // F1 Task 10: route via the engine's own one-shot blob facade
    // (replaces the deleted standalone BlobSyncEngine). Marshalled to
    // the source backend's main thread because runBlobMirror walks
    // backends synchronously.
    BlobSyncResult blobResult;
    SyncEngine *engine = m_engine;

    if (!engine) {
        SyncResult result;
        result.success = false;
        result.errorMessage = QStringLiteral(
            "dispatchFirstSync: SyncEngineWorker has no engine pointer");
        emit syncCompleted(request.mapping.id, result);
        return true;
    }

    QMetaObject::invokeMethod(srcBackend,
        [engine, src, tgt, colId, &blobResult]() {
            blobResult = engine->runBlobMirror(src, tgt, colId);
        }, Qt::BlockingQueuedConnection);

    SyncResult result;
    if (!blobResult.success) {
        result.success = false;
        result.errorMessage = blobResult.errorMessage;
        qWarning() << "SyncEngineWorker::dispatchFirstSync - runBlobMirror failed:"
                   << blobResult.errorMessage;
        emit syncCompleted(request.mapping.id, result);
        return true;
    }

    harvestBaselinesAfterFirstSync(request);

    result.success = true;
    result.startTime = m_currentResult.startTime;
    result.endTime = QDateTime::currentDateTime();
    emit syncCompleted(request.mapping.id, result);
    return true;
}

void SyncEngineWorker::harvestBaselinesAfterFirstSync(const Request &request)
{
    if (!m_calendarBaselines) {
        qDebug() << "SyncEngineWorker::harvestBaselinesAfterFirstSync - no CalendarBaselineStore, skipping";
        return;
    }

    SyncBackend *srcBackend = m_controller->backendById(request.mapping.sourceBackend);
    if (!srcBackend) {
        qWarning() << "SyncEngineWorker::harvestBaselinesAfterFirstSync - source backend not found";
        return;
    }

    IBlobBackend *src = asBlob(srcBackend);
    const QString colId = request.mapping.sourceCalendar;
    const QString backendId = request.mapping.sourceBackend;

    QList<BackendRecord> records;
    QMetaObject::invokeMethod(srcBackend,
        [src, colId, &records]() {
            records = src->loadRecords(colId);
        }, Qt::BlockingQueuedConnection);

    KCalendarCore::ICalFormat icalFormat;
    QHash<QString, QString> uidToIcal;

    for (const BackendRecord &r : records) {
        const QString ical = QString::fromUtf8(r.data);
        uidToIcal.insert(r.id, ical);

        if (m_blobBaselines) {
            BlobBaselineStore *bbs = m_blobBaselines;
            const QString bId = backendId;
            const QString cId = colId;
            const QString rId = r.id;
            const QString hash = r.contentHash;
            QMetaObject::invokeMethod(m_calendarBaselines,
                [bbs, bId, cId, rId, hash]() {
                    bbs->setBaseline(bId, cId, rId, hash);
                }, Qt::BlockingQueuedConnection);
        }
    }

    const QString mappingId = request.mapping.id;
    const QDateTime now = QDateTime::currentDateTime();
    QMetaObject::invokeMethod(m_calendarBaselines,
        [this, mappingId, uidToIcal, now]() {
            m_calendarBaselines->setBaselines(mappingId, uidToIcal);
            m_calendarBaselines->setLastSyncTime(mappingId, now);
        }, Qt::BlockingQueuedConnection);

    qDebug().noquote() << QString("SyncEngineWorker::harvestBaselinesAfterFirstSync - seeded %1 baselines for %2")
        .arg(uidToIcal.size()).arg(mappingId);
}

// ----------------------------------------------------------------------------
// Blob-view fetch helper (Phase D Task 19)
// ----------------------------------------------------------------------------

void SyncEngineWorker::fetchRecordsViaBlob(const QString &backendId,
                                            const QString &calendarId,
                                            QList<SyncRecord> &out)
{
    SyncBackend *backend = m_controller->backendById(backendId);
    if (!backend) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Backend not found: %1").arg(backendId);
        return;
    }

    QList<BackendRecord> records;
    IBlobBackend *blob = asBlob(backend);
    QMetaObject::invokeMethod(backend, [blob, calendarId, &records]() {
        records = blob->loadRecords(calendarId);
    }, Qt::BlockingQueuedConnection);

    KCalendarCore::ICalFormat icalFormat;
    for (const BackendRecord &r : records) {
        const QString ical = QString::fromUtf8(r.data);
        KCalendarCore::Incidence::Ptr inc = icalFormat.fromString(ical);
        if (inc) {
            out.append(SyncRecord::fromIncidence(inc, calendarId, backendId));
        }
    }
}

void SyncEngineWorker::fetchSourceRecords()
{
    emit phaseChanged(m_currentRequest.mapping.id, 1);

    if (!m_controller) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("No controller");
        return;
    }

    if (!m_currentRequest.useQuickPath) {
        fetchRecordsViaBlob(m_currentRequest.mapping.sourceBackend,
                            m_currentRequest.mapping.sourceCalendar,
                            m_sourceRecords);
        return;
    }

    SyncBackend *backend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
    if (!backend) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Source backend not found: %1").arg(m_currentRequest.mapping.sourceBackend);
        return;
    }

    FetchOperation *fetchOpRaw = nullptr;
    QMetaObject::invokeMethod(backend, [backend, this, &fetchOpRaw]() {
        fetchOpRaw = backend->fetchItems(m_currentRequest.mapping.sourceCalendar);
    }, Qt::BlockingQueuedConnection);

    QPointer<FetchOperation> fetchOp = fetchOpRaw;
    if (!fetchOp) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Source fetchItems returned null");
        return;
    }

    QEventLoop loop;
    connect(fetchOp.data(), &SyncOperation::finished, &loop, &QEventLoop::quit, Qt::QueuedConnection);

    connect(backend, &SyncBackend::fetchProgressChanged, this,
            [this](const QString &calendarId, int current, int total) {
                emit fetchProgress(calendarId, current, total);
            }, Qt::QueuedConnection);

    if (fetchOp && (fetchOp->state() == SyncOperation::Succeeded ||
                    fetchOp->state() == SyncOperation::Failed ||
                    fetchOp->state() == SyncOperation::Cancelled)) {
        // Already done
    } else {
        loop.exec();
    }

    backend->disconnect(this);

    if (!fetchOp || fetchOp->state() == SyncOperation::Failed) {
        m_fetchFailed = true;
        m_fetchErrorMessage = fetchOp ? fetchOp->errorString() : QStringLiteral("Source fetch operation deleted");
        return;
    }

    QList<KCalendarCore::Incidence::Ptr> incidences = fetchOp->fetchedItems();
    for (const auto &inc : incidences) {
        if (inc) {
            m_sourceRecords.append(SyncRecord::fromIncidence(inc,
                                                              m_currentRequest.mapping.sourceCalendar,
                                                              m_currentRequest.mapping.sourceBackend));
        }
    }
}

void SyncEngineWorker::fetchTargetRecords()
{
    emit phaseChanged(m_currentRequest.mapping.id, 2);

    if (!m_currentRequest.useQuickPath) {
        fetchRecordsViaBlob(m_currentRequest.mapping.targetBackend,
                            m_currentRequest.mapping.targetCalendar,
                            m_targetRecords);
        return;
    }

    SyncBackend *backend = m_controller->backendById(m_currentRequest.mapping.targetBackend);
    if (!backend) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Target backend not found: %1").arg(m_currentRequest.mapping.targetBackend);
        return;
    }

    FetchOperation *fetchOpRaw = nullptr;
    QMetaObject::invokeMethod(backend, [backend, this, &fetchOpRaw]() {
        fetchOpRaw = backend->fetchItems(m_currentRequest.mapping.targetCalendar);
    }, Qt::BlockingQueuedConnection);

    QPointer<FetchOperation> fetchOp = fetchOpRaw;
    if (!fetchOp) {
        m_fetchFailed = true;
        m_fetchErrorMessage = QStringLiteral("Target fetchItems returned null");
        return;
    }

    QEventLoop loop;
    connect(fetchOp.data(), &SyncOperation::finished, &loop, &QEventLoop::quit, Qt::QueuedConnection);

    connect(backend, &SyncBackend::fetchProgressChanged, this,
            [this](const QString &calendarId, int current, int total) {
                emit fetchProgress(calendarId, current, total);
            }, Qt::QueuedConnection);

    if (fetchOp && (fetchOp->state() == SyncOperation::Succeeded ||
                    fetchOp->state() == SyncOperation::Failed ||
                    fetchOp->state() == SyncOperation::Cancelled)) {
        // Already done
    } else {
        loop.exec();
    }

    backend->disconnect(this);

    if (!fetchOp || fetchOp->state() == SyncOperation::Failed) {
        m_fetchFailed = true;
        m_fetchErrorMessage = fetchOp ? fetchOp->errorString() : QStringLiteral("Target fetch operation deleted");
        return;
    }

    QList<KCalendarCore::Incidence::Ptr> incidences = fetchOp->fetchedItems();
    for (const auto &inc : incidences) {
        if (inc) {
            m_targetRecords.append(SyncRecord::fromIncidence(inc,
                                                              m_currentRequest.mapping.targetCalendar,
                                                              m_currentRequest.mapping.targetBackend));
        }
    }
}

void SyncEngineWorker::computeDiff()
{
    emit phaseChanged(m_currentRequest.mapping.id, 3);

    QMap<QString, QString> baselines;
    if (!m_currentRequest.useQuickPath && m_calendarBaselines) {
        const QString mappingId = m_currentRequest.mapping.id;
        QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, &baselines]() {
            const QHash<QString, QString> hash = m_calendarBaselines->allBaselines(mappingId);
            for (auto it = hash.constBegin(); it != hash.constEnd(); ++it)
                baselines.insert(it.key(), it.value());
        }, Qt::BlockingQueuedConnection);
    }

    if (m_calendarAdapter) {
        m_currentDiff = m_calendarAdapter->diffCalendarRecords(
            m_sourceRecords, m_targetRecords, baselines,
            m_currentRequest.mapping.mode, m_currentRequest.useQuickPath);
    } else if (m_currentRequest.useQuickPath) {
        m_currentDiff = computeQuickDiff(m_sourceRecords, m_targetRecords,
                                          m_currentRequest.mapping.mode);
    } else {
        m_currentDiff = computeSyncDiff(m_sourceRecords, m_targetRecords, baselines,
                                         m_currentRequest.mapping.mode);
    }

    if (!m_currentDiff.toTarget.isEmpty() || !m_currentDiff.toSource.isEmpty() ||
        !m_currentDiff.conflicts.isEmpty()) {
        qDebug().noquote() << QString("  Diff: toTarget=%1 toSource=%2 conflicts=%3")
            .arg(m_currentDiff.toTarget.size())
            .arg(m_currentDiff.toSource.size())
            .arg(m_currentDiff.conflicts.size());
    }
}

void SyncEngineWorker::handleConflicts()
{
    if (m_conflictPhase == ConflictPhase::Done && !m_yieldedForConflict) {
        m_resolvedToSourceConflictStart = m_resolvedToSource.size();
        m_conflictPhase = ConflictPhase::ToTarget;
        m_conflictIndex = 0;
    }

    ConflictResolution effectivePolicy = m_currentRequest.mapping.conflictPolicy;
    if (m_currentRequest.useQuickPath && effectivePolicy == ConflictResolution::AskUser) {
        effectivePolicy = ConflictResolution::SourceWins;
        if (m_conflictPhase == ConflictPhase::ToTarget && m_conflictIndex == 0) {
            qDebug() << "SyncEngineWorker: First sync (no baselines) — auto-resolving"
                     << "BothCreated conflicts as SourceWins";
        }
    }

    if (m_conflictPhase == ConflictPhase::ToTarget) {
        for (int i = m_conflictIndex; i < m_currentDiff.toTarget.size(); ++i) {
            {
                QMutexLocker locker(&m_mutex);
                if (m_cancelled) return;
            }

            const SyncChange &change = m_currentDiff.toTarget[i];

            if (change.isConflict) {
                SYNC_HOOK_CALL(onConflictDetected, change.conflictInfo);
#ifdef PLANSTAN_TESTING
                SyncTestHooks::instance().conflictCount++;
#endif

                if (effectivePolicy == ConflictResolution::AskUser) {
                    if (m_currentRequest.mode == Mode::Monitored) {
                        m_conflictPhase = ConflictPhase::ToTarget;
                        m_conflictIndex = i;
                        m_yieldedForConflict = true;
                        qDebug() << "SyncEngineWorker::handleConflicts - yielding for monitored conflict:" << change.uid;
                        emit conflictPauseRequested(change.conflictInfo);
                        return;
                    } else {
                        handleConflictUnmonitored(change);
                    }
                } else {
                    resolveConflictAutomatically(change, effectivePolicy);
                }
            } else {
                m_resolvedToTarget.append(change);
            }
        }

        m_conflictPhase = ConflictPhase::ToSource;
        m_conflictIndex = 0;
    }

    if (m_conflictPhase == ConflictPhase::ToSource) {
        for (int i = m_conflictIndex; i < m_currentDiff.toSource.size(); ++i) {
            {
                QMutexLocker locker(&m_mutex);
                if (m_cancelled) return;
            }

            const SyncChange &change = m_currentDiff.toSource[i];

            if (change.isConflict) {
                SYNC_HOOK_CALL(onConflictDetected, change.conflictInfo);
#ifdef PLANSTAN_TESTING
                SyncTestHooks::instance().conflictCount++;
#endif

                if (effectivePolicy == ConflictResolution::AskUser) {
                    if (m_currentRequest.mode == Mode::Monitored) {
                        m_conflictPhase = ConflictPhase::ToSource;
                        m_conflictIndex = i;
                        m_yieldedForConflict = true;
                        qDebug() << "SyncEngineWorker::handleConflicts - yielding for monitored conflict:" << change.uid;
                        emit conflictPauseRequested(change.conflictInfo);
                        return;
                    } else {
                        handleConflictUnmonitored(change);
                    }
                } else {
                    resolveConflictAutomatically(change, effectivePolicy);
                }
            } else {
                m_resolvedToSource.append(change);
            }
        }

        m_conflictPhase = ConflictPhase::Done;
    }
}

void SyncEngineWorker::continueAfterConflicts()
{
    m_diffMs = m_phaseTimer.elapsed();

    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(m_currentRequest.mapping.id, m_currentResult);
            return;
        }
    }

    QElapsedTimer applyTimer;
    applyTimer.start();
    applyChanges();
    qint64 applyMs = applyTimer.elapsed();

    QElapsedTimer baselinesTimer;
    baselinesTimer.start();
    updateBaselines();
    updatePropertyBaselines();
    qint64 baselinesMs = baselinesTimer.elapsed();

    m_currentResult.success = !m_applyFailed &&
                              !m_currentResult.hasUnresolvedConflicts() &&
                              !m_currentResult.sourceStats.hasErrors() &&
                              !m_currentResult.targetStats.hasErrors();
    if (m_applyFailed && m_currentResult.errorMessage.isEmpty()) {
        m_currentResult.errorMessage = m_applyErrorMessage;
    }
    m_currentResult.endTime = QDateTime::currentDateTime();

    qint64 totalMs = m_totalTimer.elapsed();
    qDebug().noquote() << QString("SyncEngineWorker: === Completed [%1] %2 in %3ms (%4 items) ===")
        .arg(m_currentRequest.mapping.sourceCalendar,
             m_currentResult.success ? "OK" : "FAILED",
             QString::number(totalMs),
             QString::number(m_sourceRecords.size()));
    qDebug().noquote() << QString("  Timing: props=%1+%2+%3ms fetch=%4+%5ms diff=%6ms apply=%7ms baselines=%8ms")
        .arg(m_propertyFetchMs).arg(m_propertyDiffMs).arg(m_propertyApplyMs)
        .arg(m_sourceFetchMs).arg(m_targetFetchMs).arg(m_diffMs).arg(applyMs).arg(baselinesMs);

    emit syncCompleted(m_currentRequest.mapping.id, m_currentResult);
}

void SyncEngineWorker::handleConflictUnmonitored(const SyncChange &change)
{
    qDebug() << "SyncEngineWorker::handleConflictUnmonitored - queuing conflict:" << change.uid;
    emit conflictDetected(change.conflictInfo);
    m_currentResult.unresolvedConflicts.append(change.conflictInfo);
}

void SyncEngineWorker::applyMonitoredResolution(const SyncChange &change,
                                                 ConflictResolution resolution,
                                                 const QString &mergedIcal)
{
    qDebug() << "SyncEngineWorker::applyMonitoredResolution - resolved with:" << static_cast<int>(resolution);

    if (resolution == ConflictResolution::Skip || resolution == ConflictResolution::AskUser) {
        m_currentResult.unresolvedConflicts.append(change.conflictInfo);
        return;
    }

    if (resolution == ConflictResolution::CustomMerge) {
        if (!mergedIcal.isEmpty()) {
            KCalendarCore::ICalFormat icalFormat;
            auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
                new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
            );
            if (icalFormat.fromString(tempCal, mergedIcal)) {
                auto incidences = tempCal->incidences();
                if (!incidences.isEmpty()) {
                    KCalendarCore::Incidence::Ptr mergedIncidence = incidences.first();

                    SyncChange resolved = change;
                    resolved.isConflict = false;
                    resolved.type = SyncChangeType::Modified;
                    resolved.sourceRecord.incidence = mergedIncidence;
                    resolved.sourceRecord.icalData = mergedIcal;
                    resolved.targetRecord.incidence = mergedIncidence;
                    resolved.targetRecord.icalData = mergedIcal;

                    m_resolvedToTarget.append(resolved);
                    m_resolvedToSource.append(resolved);
                    return;
                }
            }
        }
        m_currentResult.unresolvedConflicts.append(change.conflictInfo);
        return;
    }

    resolveConflictAutomatically(change, resolution);
}

void SyncEngineWorker::resolveConflictAutomatically(const SyncChange &change,
                                                     ConflictResolution policy)
{
    SyncChange resolved = change;
    resolved.isConflict = false;

    switch (policy) {
        case ConflictResolution::SourceWins: {
            if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                if (!change.sourceRecord.incidence) {
                    resolved.type = SyncChangeType::Deleted;
                } else if (!change.targetRecord.incidence) {
                    resolved.type = SyncChangeType::Created;
                    resolved.targetRecord.incidence = change.sourceRecord.incidence;
                    resolved.targetRecord.icalData = change.sourceRecord.icalData;
                }
            }
            m_resolvedToTarget.append(resolved);
            break;
        }

        case ConflictResolution::TargetWins: {
            if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                if (!change.targetRecord.incidence) {
                    resolved.type = SyncChangeType::Deleted;
                } else if (!change.sourceRecord.incidence) {
                    resolved.type = SyncChangeType::Created;
                    resolved.sourceRecord.incidence = change.targetRecord.incidence;
                    resolved.sourceRecord.icalData = change.targetRecord.icalData;
                }
            }
            m_resolvedToSource.append(resolved);
            break;
        }

        case ConflictResolution::LastWriteWins: {
            bool sourceNewer = change.sourceRecord.lastModified > change.targetRecord.lastModified;
            if (sourceNewer) {
                if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                    if (!change.sourceRecord.incidence) {
                        resolved.type = SyncChangeType::Deleted;
                    } else if (!change.targetRecord.incidence) {
                        resolved.type = SyncChangeType::Created;
                        resolved.targetRecord.incidence = change.sourceRecord.incidence;
                        resolved.targetRecord.icalData = change.sourceRecord.icalData;
                    }
                }
                m_resolvedToTarget.append(resolved);
            } else {
                if (change.conflictInfo.type == ConflictType::ModifyDelete) {
                    if (!change.targetRecord.incidence) {
                        resolved.type = SyncChangeType::Deleted;
                    } else if (!change.sourceRecord.incidence) {
                        resolved.type = SyncChangeType::Created;
                        resolved.sourceRecord.incidence = change.targetRecord.incidence;
                        resolved.sourceRecord.icalData = change.targetRecord.icalData;
                    }
                }
                m_resolvedToSource.append(resolved);
            }
            break;
        }

        case ConflictResolution::Duplicate: {
            if (!change.sourceRecord.incidence && !change.targetRecord.incidence) {
                break;
            }

            if (!change.sourceRecord.incidence || !change.targetRecord.incidence) {
                if (change.sourceRecord.incidence) {
                    SyncChange recreate = resolved;
                    recreate.type = SyncChangeType::Created;
                    recreate.targetRecord.incidence = change.sourceRecord.incidence;
                    recreate.targetRecord.icalData = change.sourceRecord.icalData;
                    m_resolvedToTarget.append(recreate);
                } else {
                    SyncChange recreate = resolved;
                    recreate.type = SyncChangeType::Created;
                    recreate.sourceRecord.incidence = change.targetRecord.incidence;
                    recreate.sourceRecord.icalData = change.targetRecord.icalData;
                    m_resolvedToSource.append(recreate);
                }
                break;
            }

            m_resolvedToTarget.append(resolved);

            KCalendarCore::Incidence::Ptr targetClone =
                KCalendarCore::Incidence::Ptr(change.targetRecord.incidence->clone());
            QString newUid = KCalendarCore::CalFormat::createUniqueId();
            targetClone->setUid(newUid);
            targetClone->setSummary(targetClone->summary() + QStringLiteral(" (conflict copy)"));
            targetClone->setLastModified(QDateTime::currentDateTimeUtc());

            SyncChange cloneToSource;
            cloneToSource.type = SyncChangeType::Created;
            cloneToSource.uid = newUid;
            cloneToSource.isConflict = false;
            cloneToSource.sourceRecord.uid = newUid;
            cloneToSource.sourceRecord.incidence = targetClone;
            cloneToSource.targetRecord.uid = newUid;
            cloneToSource.targetRecord.incidence = targetClone;
            m_resolvedToSource.append(cloneToSource);

            SyncChange cloneToTarget;
            cloneToTarget.type = SyncChangeType::Created;
            cloneToTarget.uid = newUid;
            cloneToTarget.isConflict = false;
            cloneToTarget.sourceRecord.uid = newUid;
            cloneToTarget.sourceRecord.incidence = targetClone;
            cloneToTarget.targetRecord.uid = newUid;
            cloneToTarget.targetRecord.incidence = targetClone;
            m_resolvedToTarget.append(cloneToTarget);
            break;
        }

        case ConflictResolution::Skip:
            m_currentResult.unresolvedConflicts.append(change.conflictInfo);
            break;

        case ConflictResolution::AskUser:
            m_currentResult.unresolvedConflicts.append(change.conflictInfo);
            break;

        case ConflictResolution::CustomMerge:
            m_resolvedToTarget.append(resolved);
            break;
    }
}

void SyncEngineWorker::applyChanges()
{
    emit phaseChanged(m_currentRequest.mapping.id, 3);

    for (const auto &change : m_resolvedToTarget) {
        KCalendarCore::Incidence::Ptr inc = change.sourceRecord.incidence;
        if (inc) {
            emit itemReady(m_currentRequest.mapping.targetCalendar, inc,
                          static_cast<int>(change.type));
        }
    }

    if (!m_resolvedToTarget.isEmpty()) {
        qDebug().noquote() << QString("  Applying %1 changes to target")
            .arg(m_resolvedToTarget.size());
        SYNC_HOOK_CALL(onBackendPush, m_currentRequest.mapping.targetBackend,
                       m_currentRequest.mapping.targetCalendar, m_resolvedToTarget.size());

        applyChangesToBackend(m_currentRequest.mapping.targetBackend,
                              m_currentRequest.mapping.targetCalendar,
                              m_resolvedToTarget);
        m_currentResult.targetStats = m_currentDiff.targetStats();
    }

    QList<SyncChange> sourceChangesToApply;
    if (m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        sourceChangesToApply = m_resolvedToSource;
    } else {
        for (int i = m_resolvedToSourceConflictStart; i < m_resolvedToSource.size(); ++i) {
            sourceChangesToApply.append(m_resolvedToSource[i]);
        }
    }

    if (!sourceChangesToApply.isEmpty()) {
        for (const auto &change : sourceChangesToApply) {
            KCalendarCore::Incidence::Ptr inc = change.targetRecord.incidence;
            if (inc) {
                emit itemReady(m_currentRequest.mapping.sourceCalendar, inc,
                              static_cast<int>(change.type));
            }
        }

        qDebug().noquote() << QString("  Applying %1 changes to source")
            .arg(sourceChangesToApply.size());
        SYNC_HOOK_CALL(onBackendPush, m_currentRequest.mapping.sourceBackend,
                       m_currentRequest.mapping.sourceCalendar, sourceChangesToApply.size());

        applyChangesToBackend(m_currentRequest.mapping.sourceBackend,
                              m_currentRequest.mapping.sourceCalendar,
                              sourceChangesToApply, true);
        m_currentResult.sourceStats = m_currentDiff.sourceStats();
    }
}

void SyncEngineWorker::applyChangesToBackend(const QString &backendId,
                                              const QString &calendarId,
                                              const QList<SyncChange> &changes,
                                              bool useTargetRecord)
{
    if (!m_controller || !m_collection) {
        qWarning() << "SyncEngineWorker::applyChangesToBackend - no controller or collection";
        return;
    }

    SyncBackend *backend = m_controller->backendById(backendId);
    if (!backend) {
        qWarning() << "SyncEngineWorker::applyChangesToBackend - backend not found:" << backendId;
        return;
    }

    if (!m_calendarAdapter) {
        qWarning() << "SyncEngineWorker::applyChangesToBackend - no CalendarDomainAdapter; "
                      "construct via SyncEngine, not directly";
        m_applyFailed = true;
        m_applyErrorMessage = QStringLiteral(
            "SyncEngineWorker has no CalendarDomainAdapter wired");
        return;
    }

    const QString targetType = backend->backendType();
    QString sourceType;
    if (useTargetRecord) {
        SyncBackend *sourceBackend =
            m_controller->backendById(m_currentRequest.mapping.targetBackend);
        sourceType = sourceBackend ? sourceBackend->backendType() : QString();
    } else {
        SyncBackend *sourceBackend =
            m_controller->backendById(m_currentRequest.mapping.sourceBackend);
        sourceType = sourceBackend ? sourceBackend->backendType() : QString();
    }
    const TranscodingPlan plan = m_router.plan(sourceType, targetType);

    QMetaObject::Connection transcodingConn = connect(
            backend, &SyncBackend::transcodingWarning,
            this, &SyncEngineWorker::transcodingWarning,
            Qt::DirectConnection);

    int itemCount = 0;
    for (const auto &change : changes) {
        if (change.type != SyncChangeType::Unchanged) ++itemCount;
    }
    for (int i = 0; i < itemCount; ++i) {
        emit writeProgress(calendarId, i + 1, itemCount);
    }

    QString errorMessage;
    const bool ok = m_calendarAdapter->applyChangesToBackend(
        backend, calendarId, changes, useTargetRecord,
        m_currentRequest.mapping.id, plan, &errorMessage);

    QObject::disconnect(transcodingConn);

    if (!ok) {
        m_applyFailed = true;
        m_applyErrorMessage = errorMessage;
    }
}

void SyncEngineWorker::updateBaselines()
{
    if (!m_calendarBaselines) {
        qDebug() << "SyncEngineWorker::updateBaselines - no CalendarBaselineStore, skipping";
        return;
    }

    if (m_applyFailed) {
        qWarning() << "SyncEngineWorker::updateBaselines - SKIPPING baseline update because "
                      "apply phase failed:" << m_applyErrorMessage;
        return;
    }

    const QString mappingId = m_currentRequest.mapping.id;

    QHash<QString, QString> baselinesToSet;
    QStringList baselinesToRemove;

    for (const auto &change : m_currentDiff.toTarget) {
        if (change.isConflict) continue;

        QString key = change.sourceRecord.isValid()
            ? syncRecordKey(change.sourceRecord)
            : (change.targetRecord.isValid() ? syncRecordKey(change.targetRecord) : change.uid);

        if (change.type == SyncChangeType::Deleted) {
            baselinesToRemove.append(key);
        } else if (change.sourceRecord.isValid()) {
            baselinesToSet[key] = change.sourceRecord.icalData;
        }
    }

    for (const auto &change : m_currentDiff.toSource) {
        if (change.isConflict) continue;

        QString key = change.targetRecord.isValid()
            ? syncRecordKey(change.targetRecord)
            : (change.sourceRecord.isValid() ? syncRecordKey(change.sourceRecord) : change.uid);

        if (change.type == SyncChangeType::Deleted) {
            if (!baselinesToRemove.contains(key)) {
                baselinesToRemove.append(key);
            }
        } else if (change.targetRecord.isValid()) {
            baselinesToSet[key] = change.targetRecord.icalData;
        }
    }

    for (const auto &change : m_resolvedToTarget) {
        if (change.type == SyncChangeType::Created && change.baselineRecord.uid.isEmpty()) {
            continue;
        }
        if (change.sourceRecord.isValid()) {
            baselinesToSet[syncRecordKey(change.sourceRecord)] = change.sourceRecord.icalData;
        }
    }

    for (const auto &change : m_resolvedToSource) {
        if (change.type == SyncChangeType::Created && change.baselineRecord.uid.isEmpty()) {
            continue;
        }
        if (change.targetRecord.isValid()) {
            baselinesToSet[syncRecordKey(change.targetRecord)] = change.targetRecord.icalData;
        }
    }

    QHash<QString, QString> existingBaselines;
    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, &existingBaselines]() {
        existingBaselines = m_calendarBaselines->allBaselines(mappingId);
    }, Qt::BlockingQueuedConnection);

    for (const auto &rec : m_sourceRecords) {
        if (!rec.isValid()) continue;
        if (!m_currentDiff.unchangedUids.contains(rec.uid)) continue;
        QString key = syncRecordKey(rec);
        if (!existingBaselines.contains(key)) {
            baselinesToSet[key] = rec.icalData;
        }
    }

    if (!baselinesToRemove.isEmpty()) {
        QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, baselinesToRemove]() {
            for (const QString &uid : baselinesToRemove)
                m_calendarBaselines->removeBaseline(mappingId, uid);
        }, Qt::BlockingQueuedConnection);
    }

    if (!baselinesToSet.isEmpty()) {
        QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, baselinesToSet]() {
            m_calendarBaselines->setBaselines(mappingId, baselinesToSet);
        }, Qt::BlockingQueuedConnection);
    }

    if (!baselinesToSet.isEmpty() || !baselinesToRemove.isEmpty()) {
        qDebug().noquote() << QString("  Baselines: +%1 -%2")
            .arg(baselinesToSet.size())
            .arg(baselinesToRemove.size());
    }

    QDateTime now = QDateTime::currentDateTime();
    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, now]() {
        m_calendarBaselines->setLastSyncTime(mappingId, now);
    }, Qt::BlockingQueuedConnection);
}

// ----------------------------------------------------------------------------
// Property sync
// ----------------------------------------------------------------------------

void SyncEngineWorker::fetchCalendarProperties()
{
    qDebug() << "SyncEngineWorker::fetchCalendarProperties: Fetching calendar properties";

    if (!m_controller || !m_collection) {
        qWarning() << "SyncEngineWorker::fetchCalendarProperties: Missing dependencies";
        return;
    }

    SyncBackend *sourceBackend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
    SyncBackend *targetBackend = m_controller->backendById(m_currentRequest.mapping.targetBackend);

    if (!sourceBackend || !targetBackend) {
        qWarning() << "SyncEngineWorker::fetchCalendarProperties: Backends not available";
        return;
    }

    m_sourceProperties.backendId = m_currentRequest.mapping.sourceBackend;
    m_sourceProperties.calendarId = m_currentRequest.mapping.sourceCalendar;
    m_sourceProperties.color = sourceBackend->calendarColor(m_currentRequest.mapping.sourceCalendar);
    m_sourceProperties.description = sourceBackend->calendarDescription(m_currentRequest.mapping.sourceCalendar);
    m_sourceProperties.versionHash = CalendarPropertyRecord::computeHash(m_sourceProperties);

    m_targetProperties.backendId = m_currentRequest.mapping.targetBackend;
    m_targetProperties.calendarId = m_currentRequest.mapping.targetCalendar;
    m_targetProperties.color = targetBackend->calendarColor(m_currentRequest.mapping.targetCalendar);
    m_targetProperties.description = targetBackend->calendarDescription(m_currentRequest.mapping.targetCalendar);
    m_targetProperties.versionHash = CalendarPropertyRecord::computeHash(m_targetProperties);

    qDebug().noquote() << QString("  Source props: color=%1 desc=%2")
        .arg(m_sourceProperties.color.isValid() ? m_sourceProperties.color.name() : "(none)")
        .arg(m_sourceProperties.description.isEmpty() ? "(none)" : m_sourceProperties.description.left(30));
    qDebug().noquote() << QString("  Target props: color=%1 desc=%2")
        .arg(m_targetProperties.color.isValid() ? m_targetProperties.color.name() : "(none)")
        .arg(m_targetProperties.description.isEmpty() ? "(none)" : m_targetProperties.description.left(30));
}

void SyncEngineWorker::computePropertyDiff()
{
    qDebug() << "SyncEngineWorker::computePropertyDiff: Computing property changes";

    if (!m_calendarBaselines) {
        qWarning() << "SyncEngineWorker::computePropertyDiff: No CalendarBaselineStore available";
        return;
    }

    QString baselineJson;
    QString mappingId = m_currentRequest.mapping.id;
    QString calendarId = m_currentRequest.mapping.sourceCalendar;

    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, calendarId, &baselineJson]() {
        baselineJson = m_calendarBaselines->propertyBaseline(mappingId, calendarId);
    }, Qt::BlockingQueuedConnection);

    qDebug().noquote() << QString("  Baseline check: isEmpty=%1 length=%2")
        .arg(baselineJson.isEmpty() ? "true" : "false")
        .arg(baselineJson.length());

    if (baselineJson.isEmpty()) {
        qDebug() << "  First property sync - storing baseline, no changes to apply";
        m_propertyDiff = CalendarPropertyDiff();
        return;
    }

    CalendarPropertyRecord baseline = CalendarPropertyRecord::fromJson(
        baselineJson,
        m_sourceProperties.backendId,
        m_sourceProperties.calendarId
    );

    bool sourceColorChanged = (m_sourceProperties.color != baseline.color);
    bool targetColorChanged = (m_targetProperties.color != baseline.color);

    if (sourceColorChanged && !targetColorChanged) {
        m_propertyDiff.colorChanged = true;
        m_propertyDiff.newColor = m_sourceProperties.color;
        qDebug().noquote() << QString("  Color change detected: %1 -> %2 (propagate to target)")
            .arg(baseline.color.name())
            .arg(m_sourceProperties.color.name());
    } else if (targetColorChanged && !sourceColorChanged &&
               m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        m_propertyDiff.colorChanged = true;
        m_propertyDiff.newColor = m_targetProperties.color;
        qDebug().noquote() << QString("  Color change detected: %1 -> %2 (propagate to source)")
            .arg(baseline.color.name())
            .arg(m_targetProperties.color.name());
    } else if (sourceColorChanged && targetColorChanged) {
        qWarning() << "  Property conflict: both sides changed color";
        if (m_currentRequest.mapping.conflictPolicy == ConflictResolution::SourceWins) {
            m_propertyDiff.colorChanged = true;
            m_propertyDiff.newColor = m_sourceProperties.color;
            qDebug() << "    Resolved: source wins";
        } else {
            m_propertyDiff.colorChanged = true;
            m_propertyDiff.newColor = m_targetProperties.color;
            qDebug() << "    Resolved: target wins";
        }
    }

    bool sourceDescChanged = (m_sourceProperties.description != baseline.description);
    bool targetDescChanged = (m_targetProperties.description != baseline.description);

    if (sourceDescChanged && !targetDescChanged) {
        m_propertyDiff.descriptionChanged = true;
        m_propertyDiff.newDescription = m_sourceProperties.description;
        qDebug() << "  Description change detected (propagate to target)";
    } else if (targetDescChanged && !sourceDescChanged &&
               m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        m_propertyDiff.descriptionChanged = true;
        m_propertyDiff.newDescription = m_targetProperties.description;
        qDebug() << "  Description change detected (propagate to source)";
    } else if (sourceDescChanged && targetDescChanged) {
        qWarning() << "  Property conflict: both sides changed description";
        if (m_currentRequest.mapping.conflictPolicy == ConflictResolution::SourceWins) {
            m_propertyDiff.descriptionChanged = true;
            m_propertyDiff.newDescription = m_sourceProperties.description;
        } else {
            m_propertyDiff.descriptionChanged = true;
            m_propertyDiff.newDescription = m_targetProperties.description;
        }
    }

    if (!m_propertyDiff.hasChanges()) {
        qDebug() << "  No property changes detected";
    }
}

void SyncEngineWorker::applyPropertyChanges()
{
    if (!m_propertyDiff.hasChanges()) {
        return;
    }

    qDebug() << "SyncEngineWorker::applyPropertyChanges: Applying property changes";

    if (!m_controller) {
        qWarning() << "SyncEngineWorker::applyPropertyChanges: Missing controller";
        return;
    }

    SyncBackend *sourceBackend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
    SyncBackend *targetBackend = m_controller->backendById(m_currentRequest.mapping.targetBackend);

    if (!sourceBackend || !targetBackend) {
        qWarning() << "SyncEngineWorker::applyPropertyChanges: Backends not available";
        return;
    }

    QVariantMap properties;
    if (m_propertyDiff.colorChanged) {
        properties[QStringLiteral("color")] = m_propertyDiff.newColor;
    }
    if (m_propertyDiff.descriptionChanged) {
        properties[QStringLiteral("description")] = m_propertyDiff.newDescription;
    }

    bool updateSource = false;
    bool updateTarget = false;

    if (m_currentRequest.mapping.mode == SyncMode::OneWayUpload) {
        updateTarget = true;
    } else if (m_currentRequest.mapping.mode == SyncMode::OneWayDownload) {
        updateSource = true;
    } else if (m_currentRequest.mapping.mode == SyncMode::TwoWay) {
        updateSource = true;
        updateTarget = true;
    }

    bool success = true;

    if (updateTarget) {
        qDebug() << "  Updating target calendar properties";
        if (!targetBackend->updateCalendar(
                m_currentRequest.collectionId,
                m_currentRequest.mapping.targetCalendar,
                properties)) {
            qWarning() << "  Failed to update target calendar properties";
            success = false;
        } else {
            if (m_propertyDiff.colorChanged) {
                m_targetProperties.color = m_propertyDiff.newColor;
            }
            if (m_propertyDiff.descriptionChanged) {
                m_targetProperties.description = m_propertyDiff.newDescription;
            }
            m_targetProperties.versionHash = CalendarPropertyRecord::computeHash(m_targetProperties);
        }
    }

    if (updateSource) {
        qDebug() << "  Updating source calendar properties";
        if (!sourceBackend->updateCalendar(
                m_currentRequest.collectionId,
                m_currentRequest.mapping.sourceCalendar,
                properties)) {
            qWarning() << "  Failed to update source calendar properties";
            success = false;
        } else {
            if (m_propertyDiff.colorChanged) {
                m_sourceProperties.color = m_propertyDiff.newColor;
            }
            if (m_propertyDiff.descriptionChanged) {
                m_sourceProperties.description = m_propertyDiff.newDescription;
            }
            m_sourceProperties.versionHash = CalendarPropertyRecord::computeHash(m_sourceProperties);
        }
    }

    if (success) {
        qDebug() << "  Property changes applied successfully";
    }
}

void SyncEngineWorker::updatePropertyBaselines()
{
    qDebug() << "SyncEngineWorker::updatePropertyBaselines: Updating property baselines";

    if (!m_calendarBaselines) {
        qWarning() << "SyncEngineWorker::updatePropertyBaselines: No CalendarBaselineStore available";
        return;
    }

    QString propertiesJson = m_sourceProperties.toJson();
    QString mappingId = m_currentRequest.mapping.id;
    QString calendarId = m_currentRequest.mapping.sourceCalendar;

    QMetaObject::invokeMethod(m_calendarBaselines, [this, mappingId, calendarId, propertiesJson]() {
        m_calendarBaselines->setPropertyBaseline(mappingId, calendarId, propertiesJson);
    }, Qt::BlockingQueuedConnection);

    qDebug() << "  Property baseline updated";
}

} // namespace Kalburator::Sync
