#include "syncengine.h"
#include "syncengine_p.h"
#include "syncrequest.h"
#include "baselinestore.h"
#include "perrecorddiff.h"
#include "propertydiff.h"
#include "domainoperationsregistry.h"
#include "domaindefinition.h"
#include "defaultblobwriter.h"
#include "transformationregistry.h"
#include "domainregistry.h"
#include "shaperegistries.h"
#include "decsyncactivecontroller.h"
#include "canonicalrecord.h"
#include "recordwriter.h"
// Phase K.4: the engine no longer dynamic_casts to a concrete writer;
// writer-specific behaviour is mediated by IRecordWriter::threading()
// and IRecordWriter::prepareForApply().
#include "iblobbackend.h"
#include "syncconflictstore.h"
#include "syncdiff.h"
#include "backendregistry.h"
#include "isynchost.h"
// collection.h removed — using icalendarcollection.h only
#include "isyncconfigstore.h"
#include "icalendarcollection.h"
#include "backendcapabilities.h"
#include "backendconfiguration.h"
#include "syncbackend.h"
#include "changedetection.h"
#include "syncoperation.h"
#include "conflictmanager.h"
#include "imassdeleteguard.h"
#include "canonenvelope.h"
#include "lossprofile.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QMetaObject>
#include <QTimer>
#include <QMutexLocker>
#include <QUuid>
#include <memory>

namespace {

/// Returns the list of property ids that will be materially lost when
/// the given pipeline is applied to `canonData`. Only non-reversible
/// loss kinds (Dropped, Simplified, Degraded) are reported, and only
/// for properties that are actually present (non-empty) in the record.
/// Returns an empty list if the pipeline is lossless or no affected
/// properties are present in this record.
static QStringList materializedLoss(const Kalburator::Shape::Pipeline &pipe,
                                    const QByteArray &canonData)
{
    const Kalburator::Shape::LossProfile loss = pipe.composedLoss();
    if (loss.isLossless()) return {};
    const QJsonObject o = Kalburator::Shape::CanonEnvelope::parse(canonData);
    QStringList lost;
    for (auto it = loss.affected.constBegin(); it != loss.affected.constEnd(); ++it) {
        if (it.value() == Kalburator::Shape::LossKind::Reversible) continue;
        const QString k = it.key().toString();
        const QJsonValue v = o.value(k);
        const bool present = !v.isUndefined() && !v.isNull()
            && !(v.isString() && v.toString().isEmpty())
            && !(v.isArray()  && v.toArray().isEmpty())
            && !(v.isObject() && v.toObject().isEmpty());
        if (!present) continue;
        // Value-dependent loss: skip the warning when this record's value is one
        // the target represents losslessly (e.g. classification "public").
        if (v.isString()) {
            const auto safe = loss.losslessValues.constFind(it.key());
            if (safe != loss.losslessValues.constEnd() && safe->contains(v.toString()))
                continue;
        }
        lost << k;
    }
    return lost;
}

} // anonymous namespace

namespace Kalburator::Engine {

SyncEngine::SyncEngine(BackendRegistry *registry,
                                   ISyncHost *host,
                                   Kalburator::Shape::ShapeRegistries &shape,
                                   QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_controller(host)
    , m_shape(shape)
{
    // Create worker but don't start thread yet
    m_worker = new SyncEngineWorker(m_shape);
    setupWorkerConnections();

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

    // Engine→worker command channel (replaces string-form invokeMethod).
    // SyncEngine emits these *Requested signals on m_worker; Qt's
    // QueuedConnection routes them across the thread boundary to the
    // matching slots. Signals are public in Qt, so external emit is fine.
    connect(m_worker, &SyncEngineWorker::processSyncRequested,
            m_worker, &SyncEngineWorker::processSync, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::observeCancelRequested,
            m_worker, &SyncEngineWorker::observeCancel, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::resumeAfterConflictRequested,
            m_worker, &SyncEngineWorker::resumeAfterConflict, Qt::QueuedConnection);

    // Note: Worker is deleted explicitly in stopWorkerThread() rather than
    // via finished->deleteLater, since the thread's event loop has exited
    // by the time finished is emitted.
}

void SyncEngine::startWorkerThread()
{
    if (m_workerThread.isRunning()) {
        return;
    }

    // Set dependencies before moving to thread. `this` is the thread
    // anchor for BaselineStore access; m_massDeleteGuard is pushed in
    // directly (no back-pointer).
    m_worker->setDependencies(m_controller, m_collection,
                              m_baselineStore,
                              this,
                              m_massDeleteGuard);

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

void SyncEngine::setCollection(ICalendarCollection *collection)
{
    m_collection = collection;
}

void SyncEngine::setBaselineStore(Kalburator::Storage::BaselineStore *store)
{
    m_baselineStore = store;
}

void SyncEngine::setSyncConflictStore(SyncConflictStore *store)
{
    m_conflictStore = store;
}

void SyncEngine::setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard)
{
    m_massDeleteGuard = guard;
    // Propagate to the worker thread via queued invocation so updates
    // take effect on the next sync cycle without unsynchronized state.
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker,
            [w = m_worker, guard]() {
                w->setMassDeleteGuardFromEngine(guard);
            },
            Qt::QueuedConnection);
    }
}

Kalburator::Conflict::IMassDeleteGuard *SyncEngine::massDeleteGuard() const
{
    return m_massDeleteGuard;
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
void SyncEngine::driveQueue(SyncBehavior behavior,
                            std::optional<QSet<QString>> filter)
{
    if (m_syncMappings.isEmpty() && m_activeControllers.isEmpty()) {
        qDebug() << "SyncEngine::driveQueue - no sync work configured";
        m_lastResult = SyncResult{};
        m_lastResult.success = true;
        // Finish the multi-iface (the QFuture caller is waiting on it).
        if (m_currentMultiIface) {
            m_currentMultiIface->reportResult(m_queue.drain());
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        return;
    }

    m_isSyncing = true;
    m_cancelled = false;
    m_currentSyncBehavior = behavior;
    m_pendingUnmonitoredConflicts.clear();
    m_lastResult = SyncResult{};
    m_lastResult.startTime = QDateTime::currentDateTime();

    // P1.T3: prime the queue collaborator (clears lost resources,
    // result accumulator, index, and sets DispatchMode::Queue). The
    // filter (if any) was passed through from the runSyncFuture(ids)
    // overload via the helper parameter.
    m_queue.prime(m_syncMappings, std::move(filter));

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
            m_currentMultiIface->reportResult(m_queue.drain());
            if (m_cancelled) m_currentMultiIface->reportCanceled();
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        m_queue.reset();
        return;
    }

    // Start worker thread for mapping-based sync
    startWorkerThread();
    processQueue();
}

// F2 Task 21: single-mapping driver. Dispatches exactly one Request to
// the worker; onWorkerSyncCompleted distinguishes via
// m_queue.dispatchMode() and finishes immediately rather than advancing
// a queue. This replaces the leaky path documented in FINDINGS where
// the single-mapping form re-entered processNextMapping (which iterated
// from index 0 and double-dispatched the same mapping).
void SyncEngine::processSingleMapping(const QString &mappingId,
                                      SyncBehavior behavior,
                                      ExecutionOverride executionOverride)
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
        m_queue.reset();
        m_isSyncing = false;
        return;
    }

    for (const auto &mapping : m_syncMappings) {
        if (mapping.id == mappingId && mapping.enabled) {
            m_isSyncing = true;
            m_cancelled = false;
            m_currentSyncBehavior = behavior;
            m_lastResult = SyncResult{};
            m_lastResult.startTime = QDateTime::currentDateTime();

            // P1.T3: prime the queue for a Single run (sets
            // DispatchMode::Single; no mapping list / filter needed
            // because single-mapping dispatch does not iterate).
            m_queue.primeSingle();

            // Start worker thread if not running
            startWorkerThread();

            // Create request and invoke worker
            SyncEngineWorker::Request request;
            request.mapping = mapping;
            request.behavior = behavior;
            request.collectionId = m_collection ? m_collection->id() : QString();
            request.useQuickPath = !m_baselineStore || m_baselineStore->baselinesForMappingV3(mapping.id).isEmpty();
            // P1.T5: embed the per-call override passed in by the caller.
            // Default-constructed ExecutionOverride means "no override"
            // (Direction::Default), matching the worker Request::override
            // convention. Previously this came from m_pendingOverride, a
            // class member that the caller had to set before invoking us —
            // an implicit-state-machine residue (INVARIANTS §4) now gone.
            request.override = executionOverride;

            // Command-channel: QueuedConnection routes to worker thread.
            emit m_worker->processSyncRequested(request);
            return;
        }
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
    m_queue.reset();
    // F2 Task 21 follow-up: clear m_isSyncing on the not-found path.
    // runSyncFuture sets m_isSyncing = true before calling
    // processSingleMapping; if we return here without dispatching,
    // nothing else will clear it and subsequent runSync* calls are
    // rejected by the m_isSyncing guard.
    m_isSyncing = false;
}

// Architectural-redress Plan 1 Task 4 (2026-05-29): canonical entry
// point. Absorbs the bodies of the four runSyncFuture overloads:
//
//   - Single mapping  : request.mappingIds.size() == 1
//                       (executionOverride, if set, threads through
//                        dispatchSingleNative → processSingleMapping
//                        as a method parameter — P1.T5).
//   - All enabled     : request.mappingIds.isEmpty()
//   - Subset          : request.mappingIds.size() > 1
//
// All three dispatch shapes return QFuture<QList<SyncResult>>. The
// single-mapping shim wraps this with QFuture::then() to expose the
// single-result type its callers expect.
//
// The overlap guard (m_isSyncing / m_currentSingleIface /
// m_currentMultiIface) and the QFutureWatcher cancellation channel
// are unified here too — previously each overload duplicated them.
QFuture<QList<SyncResult>> SyncEngine::runSync(const SyncRequest &request)
{
    if (m_isSyncing || m_currentSingleIface || m_currentMultiIface) {
        // Reject overlapping runs cleanly with a finished failed future.
        QFutureInterface<QList<SyncResult>> rejected;
        rejected.reportStarted();
        rejected.reportResult(QList<SyncResult>{});
        rejected.reportFinished();
        return rejected.future();
    }

    if (request.isSingleMapping()) {
        // Single-mapping path uses m_currentSingleIface natively (see
        // dispatchSingleNative()), then wraps via .then() to expose the
        // QList<SyncResult> public shape uniformly with the multi-
        // mapping paths.
        //
        // Caveat (FINDINGS "single-shim bypasses canonical runSync"):
        // QFuture::then() does not run its continuation when the source
        // future is canceled in Qt6, so canonical-API single-mapping
        // consumers reading resultAt(0) after a cancel will see an
        // empty result list. The deprecated runSyncFuture(mappingId, …)
        // shims sidestep this by calling dispatchSingleNative directly
        // and returning the single-iface future, which preserves the
        // F2 Task 23 contract (resultCount() == 1 after cancel) natively.
        std::optional<ExecutionOverride> ov;
        if (request.executionOverride.has_value())
            ov = *request.executionOverride;
        QFuture<SyncResult> singleFuture =
            dispatchSingleNative(request.mappingIds.first(),
                                 request.behavior, ov);
        return singleFuture.then([](const SyncResult &r) {
            return QList<SyncResult>{ r };
        });
    }

    // Multi-mapping path (all-enabled or subset).
    m_currentMultiIface = new QFutureInterface<QList<SyncResult>>;
    m_currentMultiIface->reportStarted();
    // F2 Task 23 follow-up: ensure cancellation-marker SyncResults
    // reach future.results() even after reportCanceled().
    m_currentMultiIface->setAddResultsIfCanceledEnabled(true);
    QFuture<QList<SyncResult>> future = m_currentMultiIface->future();

    delete m_multiWatcher;
    m_multiWatcher = new QFutureWatcher<QList<SyncResult>>(this);
    m_multiWatcher->setFuture(future);
    connect(m_multiWatcher, &QFutureWatcher<QList<SyncResult>>::canceled,
            this, &SyncEngine::onCancelObserved);

    if (request.isAllEnabled()) {
        driveQueue(request.behavior);
    } else {
        // Subset path.
        QSet<QString> filter(request.mappingIds.constBegin(),
                             request.mappingIds.constEnd());
        driveQueue(request.behavior,
                   std::optional<QSet<QString>>(std::move(filter)));
    }
    return future;
}

// Architectural-redress Plan 1 Task 4 (2026-05-29): native single-
// mapping dispatcher. Sets up m_currentSingleIface + watcher and calls
// processSingleMapping. Returns the single-iface future directly,
// preserving the F2 Task 23 cancellation contract
// (setAddResultsIfCanceledEnabled + reportResult-before-reportCanceled,
// read via resultCount() + resultAt(0)). Used by:
//
//  - The deprecated runSyncFuture(mappingId, …) shims, which return
//    this future verbatim — single-shape contract preserved natively.
//
//  - The canonical runSync(SyncRequest) single-mapping branch, which
//    wraps the returned future via .then() to expose the uniform
//    QFuture<QList<SyncResult>> shape. The .then() wrapper loses the
//    cancellation-result preservation (Qt6 semantics — see FINDINGS
//    "single-shim bypasses canonical runSync(SyncRequest)"), so
//    canonical-API consumers must add their own onCanceled handler if
//    they need cancellation-result access on the single-mapping path.
QFuture<SyncResult> SyncEngine::dispatchSingleNative(
    const QString &mappingId,
    SyncBehavior behavior,
    const std::optional<ExecutionOverride> &executionOverride)
{
    if (m_isSyncing || m_currentSingleIface || m_currentMultiIface) {
        // Reject overlapping runs cleanly with a finished failed future.
        QFutureInterface<SyncResult> rejected;
        rejected.reportStarted();
        rejected.reportResult(SyncResult{});
        rejected.reportFinished();
        return rejected.future();
    }

    m_currentSingleIface = new QFutureInterface<SyncResult>;
    m_currentSingleIface->reportStarted();
    // F2 Task 23: ensure cancellation-marker SyncResult reaches
    // resultAt(0) even after reportCanceled().
    m_currentSingleIface->setAddResultsIfCanceledEnabled(true);
    QFuture<SyncResult> singleFuture = m_currentSingleIface->future();

    // F2 Task 17 cancellation channel — watcher fires onCancelObserved
    // when the caller invokes future.cancel().
    delete m_singleWatcher;
    m_singleWatcher = new QFutureWatcher<SyncResult>(this);
    m_singleWatcher->setFuture(singleFuture);
    connect(m_singleWatcher, &QFutureWatcher<SyncResult>::canceled,
            this, &SyncEngine::onCancelObserved);

    m_isSyncing = true;
    // P1.T5: thread the override through as a method parameter (was
    // m_pendingOverride class-member residue before).
    processSingleMapping(mappingId, behavior,
                         executionOverride.value_or(ExecutionOverride{}));
    return singleFuture;
}

// Deprecated shim — bypasses runSync(SyncRequest) to return the native
// single-iface future, preserving the F2 Task 23 cancellation contract
// that the .then() wrapper used by runSync(SyncRequest) loses
// (Qt6 QFuture::then doesn't propagate the source result on cancel).
// See dispatchSingleNative() for the rationale.
QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    SyncBehavior behavior)
{
    return dispatchSingleNative(mappingId, behavior, std::nullopt);
}

QFuture<SyncResult> SyncEngine::runSyncFuture(
    const QString &mappingId,
    const ExecutionOverride &executionOverride,
    SyncBehavior behavior)
{
    return dispatchSingleNative(mappingId, behavior, executionOverride);
}

QFuture<QList<SyncResult>> SyncEngine::runSyncFuture(
    SyncBehavior behavior)
{
    SyncRequest req;
    req.behavior = behavior;
    return runSync(req);
}

// G.6 Task 43: subset dispatch — run only the specified mapping IDs.
//
// Empty-list semantics: the historical subset overload treated an
// empty `ids` as "empty subset → zero mappings dispatched" (distinct
// from runSyncFuture() with no args, which means "all enabled"). The
// canonical SyncRequest collapses both into mappingIds.isEmpty() and
// runs all enabled; this shim preserves the historical distinction by
// short-circuiting on empty input.
QFuture<QList<SyncResult>> SyncEngine::runSyncFuture(
    const QList<QString> &ids,
    SyncBehavior behavior)
{
    if (ids.isEmpty()) {
        QFutureInterface<QList<SyncResult>> empty;
        empty.reportStarted();
        empty.reportResult(QList<SyncResult>{});
        empty.reportFinished();
        return empty.future();
    }
    SyncRequest req;
    req.mappingIds = ids;
    req.behavior = behavior;
    return runSync(req);
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
    emit m_worker->observeCancelRequested();
}

// G.6 Task 46: resourceId-aware cancellation.
// For ResourceLost + non-empty resourceId: records the resource as lost on
// the MappingQueue so advanceQueue skips pending mappings whose backends
// touch that resource with a cancelled SyncResult. The in-flight mapping
// is NOT forcibly cancelled here —
// it runs to completion, then advanceQueue skips the next resource-matching
// mapping. Mappings not touching the resource continue normally.
//
// For all other reasons: stop the entire queue (same as QFuture::cancel()).
void SyncEngine::cancelWithReason(CancellationReason reason,
                                  const QString &resourceId)
{
    if (reason == CancellationReason::ResourceLost && !resourceId.isEmpty()) {
        m_queue.markResourceLost(resourceId);
        // advanceQueue queries m_queue.isResourceLost() when picking the
        // next mapping. No m_cancelled=true here — we want the queue to
        // continue with mappings that don't use the lost resource.
    } else {
        // All other reasons: stop the entire queue.
        m_cancelled = true;
        if (m_worker)
            emit m_worker->observeCancelRequested();
    }
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

    emit m_worker->resumeAfterConflictRequested(resolution, mergedIcal);
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

    // Collect collection IDs per backend that implements Backend::ChangeDetection.
    QMap<QString, QStringList> colIdsByBackend;
    auto collectChangeDetection = [&](const QString &backendId, const QString &colId) {
        SyncBackend *base = m_registry->backendInstance(backendId);
        if (dynamic_cast<Backend::ChangeDetection*>(base))
            colIdsByBackend[backendId].append(colId);
    };
    for (const auto &mapping : m_syncMappings) {
        if (!mapping.enabled) continue;
        collectChangeDetection(mapping.sourceBackend, mapping.sourceCalendar);
        collectChangeDetection(mapping.targetBackend, mapping.targetCalendar);
    }

    // Fetch fresh revisions per backend (batched where the backend supports it).
    QMap<QPair<QString, QString>, QString> freshRevisions; // (backendId, colId) -> revision
    for (auto it = colIdsByBackend.constBegin(); it != colIdsByBackend.constEnd(); ++it) {
        SyncBackend *base = m_registry->backendInstance(it.key());
        auto *cd = dynamic_cast<Backend::ChangeDetection*>(base);
        if (!cd) continue;
        QStringList ids = it.value();
        ids.removeDuplicates();
        const QMap<QString, QString> revs = cd->collectionRevisions(ids);
        for (auto rit = revs.constBegin(); rit != revs.constEnd(); ++rit)
            freshRevisions[qMakePair(it.key(), rit.key())] = rit.value();
    }

    if (!m_baselineStore) return;

    int wouldSkipCount = 0;
    int actualSkipCount = 0;
    for (const auto &mapping : m_syncMappings) {
        if (!mapping.enabled) continue;

        FreshSyncState fresh;
        bool sourceCovered = false;
        bool targetCovered = false;
        bool sourceUnchanged = false;
        bool targetUnchanged = false;

        auto checkSide = [&](const QString &backendId, const QString &colId,
                              QString &outRevision, bool &covered, bool &unchanged) {
            SyncBackend *base = m_registry->backendInstance(backendId);
            auto *cd = dynamic_cast<Backend::ChangeDetection*>(base);
            if (!cd) return;
            covered = true;
            outRevision = freshRevisions.value(qMakePair(backendId, colId));
            const QString stored = cd->cachedCollectionRevision(colId);
            unchanged = !outRevision.isEmpty() && !stored.isEmpty()
                        && outRevision == stored;
        };

        checkSide(mapping.sourceBackend, mapping.sourceCalendar,
                  fresh.sourceRevision, sourceCovered, sourceUnchanged);
        checkSide(mapping.targetBackend, mapping.targetCalendar,
                  fresh.targetRevision, targetCovered, targetUnchanged);

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
// m_queue.dispatchMode() == Queue. This replaces processNextMapping;
// the single-mapping form no longer participates in queue iteration,
// fixing the FINDINGS leak structurally.
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
            m_currentMultiIface->reportResult(m_queue.drain());
            m_currentMultiIface->reportCanceled();
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        m_queue.reset();
        return;
    }

    // P1.T3: ask the queue for the next enabled+in-filter mapping.
    // Returns nullopt past the end (queue exhausted).
    std::optional<SyncMapping> nextMapping = m_queue.next();

    if (!nextMapping.has_value()) {
        // All mappings processed (or filtered out).
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
            m_currentMultiIface->reportResult(m_queue.drain());
            m_currentMultiIface->reportFinished();
            delete m_currentMultiIface;
            m_currentMultiIface = nullptr;
        }
        m_queue.reset();
        return;
    }

    const SyncMapping &mapping = *nextMapping;

    // G.6 Task 46: ResourceLost skip — if any of this mapping's backends
    // uses a resource that became unavailable, add a cancelled SyncResult
    // and advance without dispatching to the worker.
    if (m_queue.hasLostResources() && m_controller) {
        auto *src = m_controller->backendById(mapping.sourceBackend);
        auto *tgt = m_controller->backendById(mapping.targetBackend);
        const bool srcLost = src && m_queue.isResourceLost(src->resourceId());
        const bool tgtLost = tgt && m_queue.isResourceLost(tgt->resourceId());
        if (srcLost || tgtLost) {
            SyncResult cancelled;
            cancelled.success   = false;
            cancelled.cancelled = true;
            cancelled.errorMessage = QStringLiteral("Resource lost");
            cancelled.startTime    = QDateTime::currentDateTime();
            cancelled.endTime      = cancelled.startTime;
            m_queue.recordResult(cancelled);
            advanceQueue();
            return;
        }
    }

    // Phase-2 skip: if this mapping's both endpoints are demonstrably
    // unchanged AND the skip flag is on, short-circuit without dispatching
    // to the worker. Append a successful no-op result to the queue so
    // the future caller sees per-mapping completion in resultAt(0).
    if (m_skippedMappingIds.contains(mapping.id)) {
        emit progressUpdated(m_queue.currentIndex() + 1, m_queue.totalSize(),
                             tr("Skipping unchanged %1").arg(mapping.id));

        SyncResult skippedResult;
        skippedResult.success = true;
        skippedResult.startTime = QDateTime::currentDateTime();
        skippedResult.endTime = skippedResult.startTime;

        // Aggregate into last result (no stats to add; success stays true unless
        // already false from a prior mapping failure).
        m_queue.recordResult(skippedResult);

        // Advance to the next mapping without touching the worker.
        advanceQueue();
        return;
    }

    emit progressUpdated(m_queue.currentIndex() + 1, m_queue.totalSize(),
                         tr("Syncing %1").arg(mapping.id));

    // Create request and invoke worker directly
    SyncEngineWorker::Request request;
    request.mapping = mapping;
    request.behavior = m_currentSyncBehavior;
    request.collectionId = m_collection ? m_collection->id() : QString();
    request.useQuickPath = !m_baselineStore || m_baselineStore->baselinesForMappingV3(mapping.id).isEmpty();

    emit m_worker->processSyncRequested(request);

    // NOTE: Do NOT recurse here!
    // The async operation will call onWorkerSyncCompleted() when done,
    // which will then call advanceQueue() again (Queue mode only).
}

// ============================================================================
// Helper Methods
// ============================================================================

namespace {

// Build a CanonicalRecord for calendar iCal text (calendar/ical shape).
// Used by updateSyncMetadata and harvestBaselinesAfterFirstSync when routing
// through Storage::BaselineStore::setBaselineV3.
Kalburator::Shape::CanonicalRecord makeCalendarRec(const QString &uid,
                                                    const QString &icalText)
{
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")}};
    rec.data     = icalText.toUtf8();
    return rec;
}

} // namespace

void SyncEngine::updateSyncMetadata(const SyncMapping &mapping, const SyncDiff &diff,
                                          const QList<SyncChange> &resolvedToTarget,
                                          const QList<SyncChange> &resolvedToSource)
{
    if (!m_baselineStore) {
        qDebug() << "SyncEngine::updateSyncMetadata - no BaselineStore, skipping baseline update";
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
            m_baselineStore->removeBaselineV3(mapping.id, change.uid);
        } else if (change.sourceRecord.isValid()) {
            // Update baseline to current source state
            m_baselineStore->setBaselineV3(mapping.id, makeCalendarRec(change.uid, change.sourceRecord.icalData));
        }
    }

    // For items that were synced to source (target is authoritative)
    for (const auto &change : diff.toSource) {
        if (change.isConflict) {
            continue;
        }

        if (change.type == SyncChangeType::Deleted) {
            m_baselineStore->removeBaselineV3(mapping.id, change.uid);
        } else if (change.targetRecord.isValid()) {
            m_baselineStore->setBaselineV3(mapping.id, makeCalendarRec(change.uid, change.targetRecord.icalData));
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
            m_baselineStore->setBaselineV3(mapping.id, makeCalendarRec(change.uid, change.sourceRecord.icalData));
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
            m_baselineStore->setBaselineV3(mapping.id, makeCalendarRec(change.uid, change.targetRecord.icalData));
        }
    }

    // For unchanged items, ensure baseline exists
    for (const QString &uid : diff.unchangedUids) {
        if (!m_baselineStore->baselineV3(mapping.id, uid).has_value()) {
            // This shouldn't happen in normal operation, but handle gracefully
            qDebug() << "SyncEngine::updateSyncMetadata - unchanged item has no baseline:" << uid;
        }
    }

    // Update last sync time
    m_baselineStore->setLastSyncTime(mapping.id, QDateTime::currentDateTime());
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
            return Sync::BackendConfiguration::friendlyTypeName(backend->backendType());
        }
        return Sync::BackendConfiguration::friendlyTypeName(backendId);
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

    // P1.T3: the former m_currentMappingResult write was dead (never
    // read by any slot or accessor) and was removed.

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

    // Persist fresh revisions so the next sync's pre-pass has up-to-date baselines.
    if (result.success && m_baselineStore) {
        auto stateIt = m_freshState.constFind(mappingId);
        if (stateIt != m_freshState.constEnd()) {
            const FreshSyncState &fresh = stateIt.value();
            const SyncMapping *mapping = nullptr;
            for (const auto &m : m_syncMappings) {
                if (m.id == mappingId) { mapping = &m; break; }
            }
            if (mapping) {
                auto persistRevision = [&](const QString &backendId,
                                           const QString &colId,
                                           const QString &revision) {
                    if (revision.isEmpty()) return;
                    SyncBackend *base = m_registry->backendInstance(backendId);
                    if (auto *cd = dynamic_cast<Backend::ChangeDetection*>(base))
                        cd->primeRevisionCache({{colId, revision}});
                };
                persistRevision(mapping->sourceBackend, mapping->sourceCalendar,
                                fresh.sourceRevision);
                persistRevision(mapping->targetBackend, mapping->targetCalendar,
                                fresh.targetRevision);
            }
        }
    }

    // Reset phase
    m_currentPhase = SyncPhase::Complete;
    emit phaseChanged(m_currentPhase);

    // F2 Task 21: dispatch on mode rather than unconditionally calling
    // processNextMapping (which iterated from index 0 for the single-
    // mapping form — see FINDINGS "SyncEngine::runSync(mappingId) is
    // leaky"). Single-mapping runs finish here; queue runs advance.
    if (m_queue.dispatchMode() == MappingQueue::DispatchMode::Single) {
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
        m_queue.reset();
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        return;
    }

    // Queue mode: record the per-mapping result and advance.
    // recordResult is a no-op outside Queue mode (defensive).
    m_queue.recordResult(result);

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
    if (m_queue.dispatchMode() == MappingQueue::DispatchMode::Single) {
        if (m_currentSingleIface) {
            m_currentSingleIface->reportResult(failedResult);
            m_currentSingleIface->reportFinished();
            delete m_currentSingleIface;
            m_currentSingleIface = nullptr;
        }
        m_queue.reset();
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        return;
    }

    // recordResult is a no-op outside Queue mode (defensive).
    m_queue.recordResult(failedResult);

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
// Blob sync helpers
// ---------------------------------------------------------------------------
//
// Shared utilities used by dispatchSync's two-way merge logic.
// indexBlobById is called when building source/target lookup maps for
// the diff step inside the blob sync worker path.
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

// Phase Ia.5 Task 11: classification helper for the writer-based apply
// path. Mirrors the inline classification the old direct-IBlobBackend
// apply loop did — load the destination's existing record ids, then
// route each post-merge record into creates / updates / deletes.
// Must run on the backend's thread (loadRecords calls into the backend).
struct WriterBatch {
    QList<BackendRecord> creates;
    QList<BackendRecord> updates;
    QStringList          deletes;
};

WriterBatch classifyForWriter(
    const QList<BackendRecord> &toWrite,
    IBlobBackend *backend,
    const QString &collectionId,
    QString *errOut = nullptr)
{
    WriterBatch batch;
    QList<BackendRecord> destRecords;
    QString classifyErr;
    if (!backend->loadRecordsOrError(collectionId, destRecords, classifyErr)) {
        if (errOut) *errOut = classifyErr;
        return batch;   // empty batch; caller must inspect errOut
    }
    QHash<QString, bool> existing;
    for (const auto &r : destRecords)
        existing.insert(r.id, true);
    for (const auto &rec : toWrite) {
        if (rec.isDeleted) {
            batch.deletes.append(rec.id);
        } else if (existing.contains(rec.id)) {
            batch.updates.append(rec);
        } else {
            batch.creates.append(rec);
        }
    }
    return batch;
}

// Register metatypes for cross-thread signal/slot.
const bool engineWorkerMetatypesRegistered = []() {
    qRegisterMetaType<SyncEngineWorker::Request>("SyncEngineWorker::Request");
    qRegisterMetaType<ConflictResolution>("ConflictResolution");
    qRegisterMetaType<ConflictInfo>("ConflictInfo");
    qRegisterMetaType<SyncResult>("SyncResult");
    return true;
}();

} // namespace

SyncEngineWorker::SyncEngineWorker(const Kalburator::Shape::ShapeRegistries &shape,
                                   QObject *parent)
    : QObject(parent)
    , m_shape(shape)
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
                                        ICalendarCollection *collection,
                                        Kalburator::Storage::BaselineStore *baselineStore,
                                        QObject *baselineStoreAnchor,
                                        Kalburator::Conflict::IMassDeleteGuard *massDeleteGuard)
{
    m_controller = host;
    m_baselineStore = baselineStore;
    m_collection = collection;
    m_baselineStoreAnchor = baselineStoreAnchor;
    m_massDeleteGuard = massDeleteGuard;
}

// Queued-connection setter for the mass-delete guard. Pushed from
// SyncEngine::setMassDeleteGuard so the worker thread sees a consistent
// snapshot per sync cycle (no shared mutable state).
void SyncEngineWorker::setMassDeleteGuardFromEngine(
    Kalburator::Conflict::IMassDeleteGuard *guard)
{
    m_massDeleteGuard = guard;
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
             << (request.behavior == SyncEngine::SyncBehavior::Monitored
                     ? "(monitored)" : "(unmonitored)");

    m_totalTimer.start();

    {
        QMutexLocker locker(&m_mutex);
        m_cancelled = false;
    }
    m_yieldedForConflict = false;
    m_unifiedConflictIdx = 0;
    m_unifiedDiff = EngineDiff{};
    m_unifiedMerge = EngineMerge{};

    m_currentRequest = request;
    m_currentResult = SyncResult{};
    m_currentResult.startTime = QDateTime::currentDateTime();

    emit syncStarted(request.mapping.id);

    // Task 82: compute composed loss profile for the mapping and notify host.
    // K.9: resolve shapes per-collection (universal sinks like
    // RawFilesBackend hold multiple collections of different shapes;
    // nativeShapes().first() would pick arbitrarily).
    if (m_controller) {
        Kalburator::Shape::LossProfile loss;
        SyncBackend *src = m_controller->backendById(request.mapping.sourceBackend);
        SyncBackend *tgt = m_controller->backendById(request.mapping.targetBackend);
        if (src && tgt) {
            const auto srcShape = src->shapeFor(request.mapping.sourceCalendar);
            const auto tgtShape = tgt->shapeFor(request.mapping.targetCalendar);
            if (!srcShape.isAny() && !tgtShape.isAny()) {
                loss = m_shape.transformation.inspect(
                    srcShape, tgtShape);
            }
        }
        m_controller->syncStarted(request.mapping.id, loss);
    }

    dispatchSync(request);
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

    if (m_unifiedConflictIdx < m_unifiedDiff.toTarget.size()) {
        const EngineDiffOp &op = m_unifiedDiff.toTarget[m_unifiedConflictIdx];
        switch (resolution) {
            case ConflictResolution::SourceWins:
                m_unifiedMerge.finalTarget.append(op.record);
                m_unifiedMerge.updatedBaselines.append(op.record);
                ++m_unifiedMerge.conflictsResolved;
                break;
            case ConflictResolution::TargetWins:
                m_unifiedMerge.finalSource.append(op.targetRecord);
                m_unifiedMerge.updatedBaselines.append(op.targetRecord);
                ++m_unifiedMerge.conflictsResolved;
                break;
            case ConflictResolution::LastWriteWins: {
                const bool srcWins =
                    op.record.lastModified >= op.targetRecord.lastModified;
                if (srcWins) {
                    m_unifiedMerge.finalTarget.append(op.record);
                    m_unifiedMerge.updatedBaselines.append(op.record);
                } else {
                    m_unifiedMerge.finalSource.append(op.targetRecord);
                    m_unifiedMerge.updatedBaselines.append(op.targetRecord);
                }
                ++m_unifiedMerge.conflictsResolved;
                break;
            }
            case ConflictResolution::Duplicate: {
                const bool srcDeleted = op.record.id.isEmpty();
                const bool tgtDeleted = op.targetRecord.id.isEmpty();
                if (srcDeleted) {
                    m_unifiedMerge.finalSource.append(op.targetRecord);
                    m_unifiedMerge.updatedBaselines.append(op.targetRecord);
                } else if (tgtDeleted) {
                    m_unifiedMerge.finalTarget.append(op.record);
                    m_unifiedMerge.updatedBaselines.append(op.record);
                } else {
                    BackendRecord clone = op.targetRecord;
                    clone.id = op.targetRecord.id
                               + QStringLiteral("-dup-")
                               + QUuid::createUuid().toString(QUuid::WithoutBraces);
                    if (!op.targetRecord.id.isEmpty() && !clone.data.isEmpty()) {
                        clone.data.replace(
                            QByteArrayLiteral("UID:") + op.targetRecord.id.toUtf8(),
                            QByteArrayLiteral("UID:") + clone.id.toUtf8());
                    }
                    m_unifiedMerge.finalTarget.append(op.record);
                    m_unifiedMerge.finalTarget.append(clone);
                    m_unifiedMerge.finalSource.append(clone);
                    m_unifiedMerge.updatedBaselines.append(op.record);
                    m_unifiedMerge.updatedBaselines.append(clone);
                }
                ++m_unifiedMerge.conflictsResolved;
                break;
            }
            case ConflictResolution::CustomMerge: {
                if (!m_unifiedMerger) {
                    ConflictInfo info;
                    info.mappingId       = m_currentRequest.mapping.id;
                    info.sourceId        = op.record.id;
                    info.targetId        = op.targetRecord.id.isEmpty()
                                               ? op.record.id
                                               : op.targetRecord.id;
                    info.calendarId      = m_currentRequest.mapping.sourceCalendar;
                    info.sourceBackendId = m_currentRequest.mapping.sourceBackend;
                    info.targetBackendId = m_currentRequest.mapping.targetBackend;
                    info.type            = ConflictType::BothModified;
                    m_currentResult.unresolvedConflicts.append(info);
                    ++m_unifiedMerge.conflictsDeferred;
                    break;
                }
                Kalburator::Shape::CanonicalRecord srcRec{
                    m_unifiedCanonical, op.record.data,         op.record.id};
                Kalburator::Shape::CanonicalRecord tgtRec{
                    m_unifiedCanonical, op.targetRecord.data,   op.record.id};
                Kalburator::Shape::CanonicalRecord baseRec{
                    m_unifiedCanonical, op.baselineRecord.data, op.record.id};
                const auto merged = m_unifiedMerger->merge(
                    srcRec, tgtRec, baseRec,
                    Kalburator::Conflict::ConflictPolicy::deferAll());
                BackendRecord mergedRecord = op.record;
                mergedRecord.data = merged.data;
                m_unifiedMerge.finalTarget.append(mergedRecord);
                m_unifiedMerge.finalSource.append(mergedRecord);
                m_unifiedMerge.updatedBaselines.append(mergedRecord);
                ++m_unifiedMerge.conflictsResolved;
                break;
            }
            default: {
                // Skip / AskUser / unsupported → defer.
                ConflictInfo info;
                info.mappingId       = m_currentRequest.mapping.id;
                info.sourceId        = op.record.id;
                info.targetId        = op.targetRecord.id.isEmpty()
                                           ? op.record.id
                                           : op.targetRecord.id;
                info.calendarId      = m_currentRequest.mapping.sourceCalendar;
                info.sourceBackendId = m_currentRequest.mapping.sourceBackend;
                info.targetBackendId = m_currentRequest.mapping.targetBackend;
                info.type            = ConflictType::BothModified;
                m_currentResult.unresolvedConflicts.append(info);
                ++m_unifiedMerge.conflictsDeferred;
                break;
            }
        }
    }
    m_unifiedConflictIdx++;
    m_yieldedForConflict = false;
    unifiedHandleConflicts();
}

// ----------------------------------------------------------------------------
// First-sync dispatch via an inline blob mirror (Phase D Task 21;
// originally routed through the standalone BlobSyncEngine, then through
// SyncEngine::runBlobMirror (F1 Task 10); the self-call was inlined in
// Task 10 ahead of runBlobMirror's deletion in Task 13).
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
    QString targetReadErr;
    QMetaObject::invokeMethod(tgtBackend,
        [tgt, colId, &targetEmpty, &targetReadErr]() {
            QList<BackendRecord> records;
            if (!tgt->loadRecordsOrError(colId, records, targetReadErr)) {
                targetEmpty = false;   // unknown — must NOT be treated as empty
                return;
            }
            targetEmpty = records.isEmpty();
        }, Qt::BlockingQueuedConnection);

    if (!targetReadErr.isEmpty()) {
        SyncResult result;
        result.success      = false;
        result.errorMessage = targetReadErr;
        result.startTime    = m_currentResult.startTime;
        result.endTime      = QDateTime::currentDateTime();
        emit syncCompleted(request.mapping.id, result);
        return true;
    }

    if (!targetEmpty) {
        qDebug() << "SyncEngineWorker::dispatchFirstSync - target non-empty, deferring to quick-path for"
                 << request.mapping.id;
        return false;
    }

    // Even if target is empty, blob baselines from a prior sync mean this
    // is NOT a true first sync — the target may be empty due to a deletion
    // or conflict resolution. Run the normal diff path instead of mirroring.
    if (m_baselineStore && m_baselineStoreAnchor) {
        bool hasExistingBaselines = false;
        Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
        const QString mappingId = request.mapping.id;
        QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, &hasExistingBaselines]() {
            hasExistingBaselines = !bbs->baselinesForMappingV3(mappingId).isEmpty();
        }, Qt::BlockingQueuedConnection);
        if (hasExistingBaselines) {
            qDebug() << "SyncEngineWorker::dispatchFirstSync - blob baselines exist, skipping fast path for"
                     << request.mapping.id;
            return false;
        }
    }

    qDebug() << "SyncEngineWorker::dispatchFirstSync - target empty, running inline blob mirror for"
             << request.mapping.id;

    IBlobBackend *src = asBlob(srcBackend);

    // Authority: never write to a target that reports read-only for this
    // collection. Read-only targets are also excluded upstream (generator +
    // builder ReadOnly seed); this is the engine-level backstop for the
    // first-sync mirror, uniform with the steady-state gate in
    // unifiedContinueAfterConflicts. Skip is a no-op, not an error.
    const bool tgtWritable = tgtBackend->discoveredWritable(colId);
    if (!tgtWritable) {
        qWarning() << "SyncEngine: target backend" << request.mapping.targetBackend
                   << "reports read-only for collection" << colId
                   << "- skipping first-sync writes";
    }

    // Task 14: replaced BlobSyncResult/BlobSyncStats (now deleted) with a
    // plain error counter — the struct's errorMessage was never populated
    // so the failure branch logged an empty string regardless.
    int mirrorErrors = 0;
    QString mirrorReadErr;

    // Task 10: inline the runBlobMirror body directly so Task 13 can
    // delete the F1 facade without leaving a dangling internal caller.
    // Marshalled to the source backend's thread because we walk both
    // backends synchronously (same threading requirement as the old call).
    QMetaObject::invokeMethod(srcBackend,
        [src, tgt, colId, tgtWritable, &mirrorErrors, &mirrorReadErr]() {
            QList<BackendRecord> srcRecords;
            QList<BackendRecord> tgtRecords;
            if (!src->loadRecordsOrError(colId, srcRecords, mirrorReadErr)) {
                return;
            }
            if (!tgt->loadRecordsOrError(colId, tgtRecords, mirrorReadErr)) {
                return;
            }
            const auto tgtById = indexBlobById(tgtRecords);

            // Copy source → target (create or update). Short-circuit &&: the
            // write call is never invoked when the target is read-only, so
            // mirrorErrors stays 0 and the success-completion path runs.
            for (const auto &sr : srcRecords) {
                const auto it = tgtById.constFind(sr.id);
                if (it == tgtById.constEnd()) {
                    if (tgtWritable && tgt->createRecord(colId, sr).isEmpty())
                        ++mirrorErrors;
                } else if (it.value().contentHash != sr.contentHash) {
                    BackendRecord out = sr;
                    out.id = it.value().id;
                    if (tgtWritable && !tgt->updateRecord(out))
                        ++mirrorErrors;
                }
            }

            // Delete target records not in source.
            const auto srcById = indexBlobById(srcRecords);
            for (const auto &tr : tgtRecords) {
                if (!srcById.contains(tr.id)) {
                    if (tgtWritable && !tgt->deleteRecord(tr.id))
                        ++mirrorErrors;
                }
            }
        }, Qt::BlockingQueuedConnection);

    if (!mirrorReadErr.isEmpty()) {
        SyncResult result;
        result.success      = false;
        result.errorMessage = mirrorReadErr;
        result.startTime    = m_currentResult.startTime;
        result.endTime      = QDateTime::currentDateTime();
        emit syncCompleted(request.mapping.id, result);
        return true;
    }

    SyncResult result;
    if (mirrorErrors > 0) {
        result.success = false;
        qWarning() << "SyncEngineWorker::dispatchFirstSync - blob mirror failed:"
                   << mirrorErrors << "error(s)";
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
    if (!m_baselineStore) {
        qDebug() << "SyncEngineWorker::harvestBaselinesAfterFirstSync - no BaselineStore, skipping";
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
    QString harvestReadErr;
    QMetaObject::invokeMethod(srcBackend,
        [src, colId, &records, &harvestReadErr]() {
            (void)src->loadRecordsOrError(colId, records, harvestReadErr);
        }, Qt::BlockingQueuedConnection);
    if (!harvestReadErr.isEmpty()) {
        qWarning() << "SyncEngineWorker::harvestBaselinesAfterFirstSync - read failed:"
                   << harvestReadErr << "(no baselines harvested)";
        return;
    }

    QHash<QString, QString> uidToIcal;
    const QString mappingId = request.mapping.id;

    for (const BackendRecord &r : records) {
        const QString ical = QString::fromUtf8(r.data);
        uidToIcal.insert(r.id, ical);
    }
    const QDateTime now = QDateTime::currentDateTime();
    if (m_baselineStore && m_baselineStoreAnchor) {
        Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
        // Marshal to engine thread — BaselineStore (SQLite) is not thread-safe.
        QMetaObject::invokeMethod(m_baselineStoreAnchor,
            [bbs, mappingId, uidToIcal, now]() {
                for (auto it = uidToIcal.constBegin(); it != uidToIcal.constEnd(); ++it) {
                    bbs->setBaselineV3(mappingId, makeCalendarRec(it.key(), it.value()));
                }
                bbs->setLastSyncTime(mappingId, now);
            }, Qt::BlockingQueuedConnection);
    }

    qDebug().noquote() << QString("SyncEngineWorker::harvestBaselinesAfterFirstSync - seeded %1 baselines for %2")
        .arg(uidToIcal.size()).arg(mappingId);
}

// ----------------------------------------------------------------------------
// Unified domain dispatch (Phase Ia.5 Task 8: renamed from dispatchBlobSync;
// originally G.6 Task 41). Compiles per-mapping shape pipelines so that
// non-blob domains can be promoted to canonical for diff/merge and demoted
// back to native shape for push.
// ----------------------------------------------------------------------------

bool SyncEngineWorker::dispatchSync(const SyncEngineWorker::Request &request)
{
    if (!m_controller) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral("dispatchSync: no controller");
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(request.mapping.id, m_currentResult);
        return true;
    }

    SyncBackend *srcBackend = m_controller->backendById(request.mapping.sourceBackend);
    SyncBackend *tgtBackend = m_controller->backendById(request.mapping.targetBackend);
    if (!srcBackend || !tgtBackend) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral("dispatchSync: backend not found");
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(request.mapping.id, m_currentResult);
        return true;
    }

    IBlobBackend *srcBlob = asBlob(srcBackend);
    IBlobBackend *tgtBlob = asBlob(tgtBackend);
    const QString srcColId  = request.mapping.sourceCalendar;
    const QString tgtColId  = request.mapping.targetCalendar;
    const QString mappingId = request.mapping.id;

    // K.9: resolve per-collection shapes via shapeFor(). Universal sinks
    // (RawFilesBackend, GenericSqliteBackend) may host collections of
    // different shapes in one backend; nativeShapes().first() would pick
    // arbitrarily across them and miscompute pipelines / cross-domain
    // checks for any mapping whose target isn't the first one.
    const Kalburator::Shape::Shape srcShape = srcBackend->shapeFor(srcColId);
    const Kalburator::Shape::Shape tgtShape = tgtBackend->shapeFor(tgtColId);

    if (srcShape.isAny() || tgtShape.isAny()) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral(
            "dispatchSync: backend declares no shape for collection "
            "(srcBackend=%1 srcCol=%2 tgtBackend=%3 tgtCol=%4)")
                .arg(request.mapping.sourceBackend, srcColId,
                     request.mapping.targetBackend, tgtColId);
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(mappingId, m_currentResult);
        return true;
    }

    // Phase Ib.5 Task 4: first-sync fast path (unified for all domains).
    // Only fires when source and target have the same native shape — when shapes
    // differ the fast path would copy bytes verbatim without the pipeline
    // transform (e.g. contacts/vcard3 → contacts/vcard4 would land untransformed).
    // dispatchFirstSync returns false if the target is not empty, in which case
    // we fall through to the full diff path below.
    if (request.useQuickPath && request.mapping.mode == SyncMode::OneWayUpload
            && srcShape == tgtShape) {
        if (dispatchFirstSync(request))
            return true;
    }

    // --- Phase Ia.5 Task 8: compile pipelines for shape promotion ---
    // srcShape -> canonical: promote source records to canonical for diff.
    // tgtShape -> canonical: promote target records to canonical for diff.
    // canonical -> tgtShape: demote outgoing records to target's native shape.
    // canonical -> srcShape: demote outgoing records to source's native shape
    //   (TwoWay; the plan called for 3 pipelines but the existing
    //    finalSource push needs the 4th to avoid breaking TwoWay).
    // For blob domain (src=tgt=canonical), all four pipelines are identity.

    if (srcShape.domain != tgtShape.domain) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral(
            "dispatchSync: cross-domain mappings not supported (src=%1 tgt=%2)")
                .arg(srcShape.domain.toString(), tgtShape.domain.toString());
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(mappingId, m_currentResult);
        return true;
    }

    // Phase Ia.5 Task 9: lift plugin lookup out of the homogeneous /
    // heterogeneous branches. Plugin registrars are pulled into the
    // test binaries via $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
    // (Task 9 precursor) so DomainRegistry has them available at sync
    // time. Compile(X, X) returns identity, so the same code path
    // handles homogeneous and heterogeneous mappings uniformly.
    auto *dd = m_shape.domain
                   .definitionFor(srcShape.domain);
    if (!dd) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral(
            "dispatchSync: no definition for domain '%1'")
                .arg(srcShape.domain.toString());
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(mappingId, m_currentResult);
        return true;
    }

    const Kalburator::Shape::Shape canonical = dd->canonicalShape();
    auto *ops = m_shape.operations
                    .operationsFor(srcShape.domain);

    // Phase Ib.5 Task 7: if-calendar guard removed. Calendar now routes
    // through the same unified dispatchSync path as all other domains.
    // Parity was established by Tasks 3–6: AskUser pause/resume, first-sync
    // fast-path, property-phase deferral, CustomMerge/Duplicate deferral.
    // A WorkerThread writer's apply() is called on the worker thread (not
    // wrapped in the outer BlockingQueuedConnection) by
    // unifiedContinueAfterConflicts' applyBatch helper, which dispatches via
    // IRecordWriter::threading().

    const auto &reg = m_shape.transformation;
    std::optional<Kalburator::Shape::Pipeline> srcToCanon = reg.compile(srcShape, canonical);
    std::optional<Kalburator::Shape::Pipeline> tgtToCanon = reg.compile(tgtShape, canonical);
    std::optional<Kalburator::Shape::Pipeline> canonToTgt = reg.compile(canonical, tgtShape);
    std::optional<Kalburator::Shape::Pipeline> canonToSrc = reg.compile(canonical, srcShape);

    if (!srcToCanon || !tgtToCanon || !canonToTgt || !canonToSrc) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral(
            "dispatchSync: no edge path between shape and canonical "
            "(srcShape=%1/%2, tgtShape=%3/%4, canonical=%5/%6)")
                .arg(srcShape.domain.toString(), srcShape.encoding.toString(),
                     tgtShape.domain.toString(), tgtShape.encoding.toString(),
                     canonical.domain.toString(), canonical.encoding.toString());
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(mappingId, m_currentResult);
        return true;
    }

    // Phase Ia.5 Task 12: run the generic property phase via the plugin's
    // collectionProperties / applyCollectionProperties. For non-calendar
    // plugins (blob, contacts, memo, todo), collectionProperties returns an
    // empty map by default, so the early-return in runPropertyPhase fires and
    // this is a no-op. For calendar, the plugin's collection-property hooks
    // handle color and description sync.
    //
    // Baseline is passed as empty for v1: Task 7 deferred persistence wiring
    // because the old store used CalendarPropertyRecord JSON rather than a
    // generic QVariantMap. For first-sync runs the baseline is empty anyway.
    // Subsequent syncs now persist property-baseline snapshots via T9
    // (unifiedContinueAfterConflicts after successful writes).
    runPropertyPhase(ops, srcBackend, tgtBackend,
                     srcColId, tgtColId,
                     /*baseline=*/QVariantMap{},
                     request.mapping);

    emit phaseChanged(mappingId, 1);

    // --- Fetch source records (cross-thread) ---
    // Phase Ib.5 Task 7: use fetchItems() as a cancellable gating step before
    // loadRecordsOrError(). For backends that support setFetchBlocking (e.g.
    // MockBackend in cancellation tests), fetchItems() returns immediately but
    // starts a background thread that blocks until the test releases the
    // blocker. The worker awaits the FetchOperation in a QEventLoop so that
    // cancellation signals (observeCancel → cancellationObserved) can arrive
    // and abort the sync. Backends that don't override fetchItems() return a
    // immediately-failed op; we skip the QEventLoop for those and proceed
    // directly to loadRecordsOrError().
    {
        SyncOperation *fetchOpRaw = nullptr;
        QMetaObject::invokeMethod(srcBackend, [srcBackend, srcColId, &fetchOpRaw]() {
            fetchOpRaw = srcBackend->fetchItems(srcColId);
        }, Qt::BlockingQueuedConnection);
        QPointer<SyncOperation> fetchOp = fetchOpRaw;
        if (fetchOp && fetchOp->state() == SyncOperation::Running) {
            QEventLoop loop;
            // Connect BEFORE re-checking state: op may complete between the
            // BlockingQueuedConnection above and the loop.exec() call below.
            // Making the connection first guarantees we see the finished()
            // signal even if it fires in that window (the queued event stays
            // in the worker thread's queue until loop.exec() drains it).
            connect(fetchOp.data(), &SyncOperation::finished,
                    &loop, &QEventLoop::quit, Qt::QueuedConnection);
            connect(this, &SyncEngineWorker::cancellationObserved,
                    &loop, &QEventLoop::quit, Qt::DirectConnection);
            // Re-check: if already completed between the BlockingQueuedConnection
            // call and the connect() above, skip loop.exec() to avoid hanging.
            if (fetchOp->state() == SyncOperation::Running)
                loop.exec();
        }
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
    }
    QList<BackendRecord> sourceRecords;
    {
        QString fetchErr;
        QMetaObject::invokeMethod(srcBackend, [srcBlob, srcColId, &sourceRecords, &fetchErr]() {
            srcBlob->loadRecordsOrError(srcColId, sourceRecords, fetchErr);
        }, Qt::BlockingQueuedConnection);
        if (!fetchErr.isEmpty()) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = fetchErr;
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
    }

    // Phase Ia.5 Task 8: promote source records to canonical shape.
    // For blob domain (srcShape == canonical) this is identity and skipped.
    // Note: r.contentHash may be stale after a non-identity apply.
    // blobBatchDiff (still in use until Phase Ib.5) recomputes equality
    // from contentHash, so blob is unaffected. Non-blob domains see
    // intentional transient byte-equality misbehavior that the per-record
    // IRecordDiffer migration fixes.
    if (!srcToCanon->isIdentity()) {
        for (auto &r : sourceRecords) {
            r.data = srcToCanon->apply(r.data);
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
    }

    emit phaseChanged(mappingId, 2);

    // --- Fetch target records (cross-thread) ---
    // Same cancellable gating pattern as source fetch above.
    {
        SyncOperation *fetchOpRaw = nullptr;
        QMetaObject::invokeMethod(tgtBackend, [tgtBackend, tgtColId, &fetchOpRaw]() {
            fetchOpRaw = tgtBackend->fetchItems(tgtColId);
        }, Qt::BlockingQueuedConnection);
        QPointer<SyncOperation> fetchOp = fetchOpRaw;
        if (fetchOp && fetchOp->state() == SyncOperation::Running) {
            QEventLoop loop;
            // Same race-fix as source fetch: connect before re-check so a
            // completed op in this window still wakes the loop via the
            // already-queued finished() event.
            connect(fetchOp.data(), &SyncOperation::finished,
                    &loop, &QEventLoop::quit, Qt::QueuedConnection);
            connect(this, &SyncEngineWorker::cancellationObserved,
                    &loop, &QEventLoop::quit, Qt::DirectConnection);
            if (fetchOp->state() == SyncOperation::Running)
                loop.exec();
        }
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
    }
    QList<BackendRecord> targetRecords;
    {
        QString fetchErr;
        QMetaObject::invokeMethod(tgtBackend, [tgtBlob, tgtColId, &targetRecords, &fetchErr]() {
            tgtBlob->loadRecordsOrError(tgtColId, targetRecords, fetchErr);
        }, Qt::BlockingQueuedConnection);
        if (!fetchErr.isEmpty()) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = fetchErr;
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
    }

    // Phase Ia.5 Task 8: promote target records to canonical shape (same
    // caveat about contentHash staleness as for sourceRecords above).
    if (!tgtToCanon->isIdentity()) {
        for (auto &r : targetRecords) {
            r.data = tgtToCanon->apply(r.data);
        }
    }

    emit phaseChanged(mappingId, 3);

    // --- Load baselines (run on engine thread — BaselineStore is not thread-safe) ---
    QList<BackendRecord> baselineRecords;
    if (m_baselineStore && m_baselineStoreAnchor) {
        Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
        QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, &baselineRecords]() {
            for (const auto &canonical : bbs->baselinesForMappingV3(mappingId)) {
                // The unified engine persists EVERY baseline as blob/raw with
                // data = contentHash bytes (see the two setBaselineV3 sites
                // below and in unifiedContinueAfterConflicts), regardless of the
                // record's real domain. So this filter does not drop modern
                // baselines — it skips only *legacy* calendar baselines
                // (domain="calendar", encoding="ical") left by the pre-unified
                // path, which stored iCal text, not hashes. Loading those would
                // set contentHash = <ical text> and break perRecordDiff's
                // hash-equality comparison. Baseline-driven deletion detection
                // for calendar/contacts works via the blob+hash form (proven by
                // tst_calendar_subsequent_sync_uses_blob_view
                // ::subsequentSync_deletedSourceRecordPropagatesDeletion).
                if (canonical.shape.domain.toString() != QLatin1String("blob")) {
                    continue;
                }
                BackendRecord rec;
                rec.id          = canonical.recordId;
                rec.contentHash = QString::fromUtf8(canonical.data);
                baselineRecords.append(rec);
            }
        }, Qt::BlockingQueuedConnection);
    }

    // --- Diff + merge (pure computation, worker thread) ---
    // Phase N.1: per-record diff via the domain plugin's canonical
    // RecordDiffer. Replaces the Phase Ia.5 batch helper blobBatchDiff.
    m_unifiedDiffer = dd->createCanonicalDiffer();
    m_unifiedMerger = dd->createCanonicalMerger();
    const EngineDiff engineDiff = perRecordDiff(
        sourceRecords, targetRecords, baselineRecords,
        canonical, *m_unifiedDiffer);

    // Seed baselines for records that are already in sync (same ID, same hash
    // on both sides, no existing baseline). Without this, a subsequent sync
    // cannot distinguish "source deleted this record" from "target has a
    // new record the source never knew about". The legacy calendar path saved
    // every known record after each successful sync; we replicate that
    // guarantee here for BaselineStore-backed paths.
    if (m_baselineStore && m_baselineStoreAnchor) {
        QHash<QString, BackendRecord> srcById;
        for (const auto &r : sourceRecords) srcById.insert(r.id, r);
        QHash<QString, BackendRecord> baselineById;
        for (const auto &r : baselineRecords) baselineById.insert(r.id, r);
        QList<Kalburator::Shape::CanonicalRecord> implicitBaselines;
        const Kalburator::Shape::Shape blobShape_{
            Kalburator::Shape::DomainId{QStringLiteral("blob")},
            Kalburator::Shape::EncodingId{QStringLiteral("raw")}};
        for (const auto &tgtRec : targetRecords) {
            if (!srcById.contains(tgtRec.id)) continue;
            if (baselineById.contains(tgtRec.id)) continue; // already tracked
            const BackendRecord &srcRec = srcById.value(tgtRec.id);
            if (srcRec.contentHash != tgtRec.contentHash) continue; // conflict/update — handled by diff
            Kalburator::Shape::CanonicalRecord c;
            c.recordId = tgtRec.id;
            c.shape    = blobShape_;
            c.data     = tgtRec.contentHash.toUtf8();
            implicitBaselines.append(c);
        }
        if (!implicitBaselines.isEmpty()) {
            Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
            QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, implicitBaselines]() {
                for (const auto &c : implicitBaselines)
                    bbs->setBaselineV3(mappingId, c);
            }, Qt::BlockingQueuedConnection);
        }
    }

    // Phase Ib.5 Task 3: unified-path AskUser pause/resume.
    // Store state needed by unifiedHandleConflicts and
    // unifiedContinueAfterConflicts (which re-derives backends/pipelines
    // from m_currentRequest to avoid storing them as members).
    m_unifiedDiff     = std::move(engineDiff);
    m_unifiedMerge    = EngineMerge{};
    m_unifiedConflictIdx = 0;
    m_unifiedPolicy   = request.mapping.conflictPolicy;
    m_unifiedOverride = request.override;
    m_unifiedCanonical = canonical;

    // Mirror override: compute the full merge immediately via the
    // mirror-aware helper and skip the conflict-walk entirely.
    using Direction = ExecutionOverride::Direction;
    const Direction dir = request.override.direction;
    if (dir == Direction::MirrorAToB) {
        m_unifiedMerge = mergeMirrorAToB(m_unifiedDiff);
        unifiedContinueAfterConflicts();
        return true;
    }
    if (dir == Direction::MirrorBToA) {
        m_unifiedMerge = mergeMirrorBToA(m_unifiedDiff);
        unifiedContinueAfterConflicts();
        return true;
    }

    // Process all toSource ops upfront — blobBatchDiff never puts Conflict
    // ops in toSource, so these are always non-conflict ops.
    // OneWayUpload: source is authoritative; target-only changes must NOT
    // flow back to source (blobBatchDiff is mode-agnostic, so we filter here).
    if (request.mapping.mode != SyncMode::OneWayUpload) {
        for (const auto &op : m_unifiedDiff.toSource) {
            using Kind = EngineDiffOp::Kind;
            if (op.kind == Kind::Conflict) continue; // guard
            BackendRecord rec = op.record;
            if (op.kind == Kind::Delete) rec.isDeleted = true;
            m_unifiedMerge.finalSource.append(rec);
            if (op.kind != Kind::Delete)
                m_unifiedMerge.updatedBaselines.append(rec);
        }
    }

    // Walk toTarget ops — may yield on AskUser conflicts.
    unifiedHandleConflicts();

    // If yielded: the future stays in-flight; resumeAfterConflict will
    // re-enter unifiedHandleConflicts and eventually call
    // unifiedContinueAfterConflicts. Do NOT fall through to syncCompleted.
    if (m_yieldedForConflict)
        return true;

    // unifiedHandleConflicts called unifiedContinueAfterConflicts on
    // completion — syncCompleted was already emitted.
    return true;
}

// ----------------------------------------------------------------------------
// Phase Ib.5 Task 3: unified-path conflict handlers.
// Mirror the legacy handleConflicts / continueAfterConflicts protocol,
// operating on EngineDiffOp / EngineMerge instead of SyncChange / SyncDiff.
// ----------------------------------------------------------------------------

void SyncEngineWorker::unifiedHandleConflicts()
{
    // NOTE: do NOT apply the legacy useQuickPath→SourceWins downgrade here.
    // The unified path uses blobBatchDiff whose baseline check already handles
    // first-sync (BothCreated) vs subsequent-sync (BothModified) conflicts.
    // useQuickPath was a CalendarBaselineStore sentinel that was always true
    // for non-calendar domains (blob/contacts/memo/todo); the whole concept
    // is gone now that CalendarBaselineStore is deleted.
    const ConflictResolution effectivePolicy = m_unifiedPolicy;

    const bool filterNonConflictToTarget =
        (m_currentRequest.mapping.mode == SyncMode::OneWayDownload);

    const auto &toTarget = m_unifiedDiff.toTarget;
    for (int i = m_unifiedConflictIdx; i < toTarget.size(); ++i) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_cancelled) return;
        }

        const EngineDiffOp &op = toTarget[i];
        using Kind = EngineDiffOp::Kind;

        if (op.kind != Kind::Conflict) {
            // Non-conflict: skip in OneWayDownload (source changes must not
            // flow to target — blobBatchDiff is mode-agnostic, so filter here).
            if (!filterNonConflictToTarget) {
                BackendRecord rec = op.record;
                if (op.kind == Kind::Delete)
                    rec.isDeleted = true;
                m_unifiedMerge.finalTarget.append(rec);
                if (op.kind != Kind::Delete)
                    m_unifiedMerge.updatedBaselines.append(rec);
            }
            continue;
        }

        // Conflict op.
        if (effectivePolicy == ConflictResolution::AskUser) {
            if (m_currentRequest.behavior == SyncEngine::SyncBehavior::Monitored) {
                // Yield: store position, set flag, emit signal, return.
                // resumeAfterConflict will re-enter this method from index i.
                m_unifiedConflictIdx = i;
                m_yieldedForConflict = true;

                ConflictInfo info;
                info.mappingId       = m_currentRequest.mapping.id;
                info.sourceId        = op.record.id;
                info.targetId        = op.targetRecord.id.isEmpty()
                                           ? op.record.id
                                           : op.targetRecord.id;
                info.calendarId      = m_currentRequest.mapping.sourceCalendar;
                info.sourceBackendId = m_currentRequest.mapping.sourceBackend;
                info.targetBackendId = m_currentRequest.mapping.targetBackend;
                info.type            = (op.record.id.isEmpty() || op.targetRecord.id.isEmpty())
                                           ? ConflictType::ModifyDelete
                                           : ConflictType::BothModified;
                info.detectedAt      = QDateTime::currentDateTimeUtc();
                info.sourceModified  = op.record.lastModified;
                info.targetModified  = op.targetRecord.lastModified;
                info.sourceIcalData  = QString::fromUtf8(op.record.data);
                info.targetIcalData  = QString::fromUtf8(op.targetRecord.data);

                qDebug() << "SyncEngineWorker::unifiedHandleConflicts - yielding for:"
                         << op.record.id;
                emit conflictPauseRequested(info);
                return;
            } else {
                // Unmonitored AskUser: defer to next sync.
                ConflictInfo info;
                info.mappingId       = m_currentRequest.mapping.id;
                info.sourceId        = op.record.id;
                info.targetId        = op.targetRecord.id.isEmpty()
                                           ? op.record.id
                                           : op.targetRecord.id;
                info.calendarId      = m_currentRequest.mapping.sourceCalendar;
                info.sourceBackendId = m_currentRequest.mapping.sourceBackend;
                info.targetBackendId = m_currentRequest.mapping.targetBackend;
                info.type            = (op.record.id.isEmpty() || op.targetRecord.id.isEmpty())
                                           ? ConflictType::ModifyDelete
                                           : ConflictType::BothModified;
                info.detectedAt      = QDateTime::currentDateTimeUtc();
                emit conflictDetected(info);
                m_currentResult.unresolvedConflicts.append(info);
                ++m_unifiedMerge.conflictsDeferred;
            }
            continue;
        }

        // Auto-resolvable policy.
        bool sourceWins = false;
        bool resolved = false;
        switch (effectivePolicy) {
            case ConflictResolution::SourceWins:
                sourceWins = true;  resolved = true; break;
            case ConflictResolution::TargetWins:
                sourceWins = false; resolved = true; break;
            case ConflictResolution::LastWriteWins:
                // For modify-delete, the deleted side has a null/invalid
                // lastModified, so the modifier always wins via >= comparison.
                sourceWins = op.record.lastModified >= op.targetRecord.lastModified;
                resolved = true; break;
            case ConflictResolution::Duplicate: {
                const bool srcDeleted = op.record.id.isEmpty();
                const bool tgtDeleted = op.targetRecord.id.isEmpty();
                if (srcDeleted) {
                    // Source deleted, target modified: keep modified version.
                    m_unifiedMerge.finalSource.append(op.targetRecord);
                    m_unifiedMerge.updatedBaselines.append(op.targetRecord);
                } else if (tgtDeleted) {
                    // Target deleted, source modified: keep modified version.
                    m_unifiedMerge.finalTarget.append(op.record);
                    m_unifiedMerge.updatedBaselines.append(op.record);
                } else {
                    // Both modified: clone target record under a new UUID;
                    // push source version to target (original UID) and clone
                    // to both sides.
                    BackendRecord clone = op.targetRecord;
                    clone.id = op.targetRecord.id
                               + QStringLiteral("-dup-")
                               + QUuid::createUuid().toString(QUuid::WithoutBraces);
                    // For calendar backends, BackendRecord.id == iCal UID.
                    // Rewrite the UID line in the iCal data so the writer
                    // writes the clone to the correct file (new UID, not the
                    // original — which would overwrite the source record).
                    if (!op.targetRecord.id.isEmpty() && !clone.data.isEmpty()) {
                        clone.data.replace(
                            QByteArrayLiteral("UID:") + op.targetRecord.id.toUtf8(),
                            QByteArrayLiteral("UID:") + clone.id.toUtf8());
                    }
                    m_unifiedMerge.finalTarget.append(op.record);
                    m_unifiedMerge.finalTarget.append(clone);
                    m_unifiedMerge.finalSource.append(clone);
                    m_unifiedMerge.updatedBaselines.append(op.record);
                    m_unifiedMerge.updatedBaselines.append(clone);
                }
                ++m_unifiedMerge.conflictsResolved;
                break;
            }
            case ConflictResolution::CustomMerge: {
                if (!m_unifiedMerger) {
                    ++m_unifiedMerge.conflictsDeferred;
                    break;
                }
                Kalburator::Shape::CanonicalRecord srcRec{
                    m_unifiedCanonical, op.record.data,         op.record.id};
                Kalburator::Shape::CanonicalRecord tgtRec{
                    m_unifiedCanonical, op.targetRecord.data,   op.record.id};
                Kalburator::Shape::CanonicalRecord baseRec{
                    m_unifiedCanonical, op.baselineRecord.data, op.record.id};
                const auto merged = m_unifiedMerger->merge(
                    srcRec, tgtRec, baseRec,
                    Kalburator::Conflict::ConflictPolicy::deferAll());
                BackendRecord mergedRecord = op.record;
                mergedRecord.data = merged.data;
                m_unifiedMerge.finalTarget.append(mergedRecord);
                m_unifiedMerge.finalSource.append(mergedRecord);
                m_unifiedMerge.updatedBaselines.append(mergedRecord);
                ++m_unifiedMerge.conflictsResolved;
                break;
            }
            case ConflictResolution::Skip:
                ++m_unifiedMerge.conflictsDeferred; break;
            default:
                ++m_unifiedMerge.conflictsDeferred; break;
        }
        if (resolved) {
            if (sourceWins) {
                if (op.record.id.isEmpty()) {
                    // Source was deleted — propagate deletion to target.
                    BackendRecord doomed = op.baselineRecord;
                    doomed.isDeleted = true;
                    m_unifiedMerge.finalTarget.append(doomed);
                } else {
                    m_unifiedMerge.finalTarget.append(op.record);
                    m_unifiedMerge.updatedBaselines.append(op.record);
                }
            } else {
                if (op.targetRecord.id.isEmpty()) {
                    // Target was deleted — propagate deletion to source.
                    BackendRecord doomed = op.baselineRecord;
                    doomed.isDeleted = true;
                    m_unifiedMerge.finalSource.append(doomed);
                } else {
                    m_unifiedMerge.finalSource.append(op.targetRecord);
                    m_unifiedMerge.updatedBaselines.append(op.targetRecord);
                }
            }
            ++m_unifiedMerge.conflictsResolved;
        }
    }

    // All toTarget ops consumed — proceed to apply.
    unifiedContinueAfterConflicts();
}

void SyncEngineWorker::unifiedContinueAfterConflicts()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            m_unifiedDiffer.reset();
            m_unifiedMerger.reset();
            emit syncCompleted(m_currentRequest.mapping.id, m_currentResult);
            return;
        }
    }

    const QString mappingId = m_currentRequest.mapping.id;
    const QString srcColId  = m_currentRequest.mapping.sourceCalendar;
    const QString tgtColId  = m_currentRequest.mapping.targetCalendar;

    SyncBackend *srcBackend = m_controller->backendById(m_currentRequest.mapping.sourceBackend);
    SyncBackend *tgtBackend = m_controller->backendById(m_currentRequest.mapping.targetBackend);
    if (!srcBackend || !tgtBackend) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral(
            "unifiedContinueAfterConflicts: backend not found");
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(mappingId, m_currentResult);
        return;
    }

    IBlobBackend *srcBlob = asBlob(srcBackend);
    IBlobBackend *tgtBlob = asBlob(tgtBackend);

    // Re-derive pipelines from the stored canonical shape.
    // K.9: per-collection shape resolution (see dispatchSync above).
    const auto &treg = m_shape.transformation;
    const Kalburator::Shape::Shape srcShape = srcBackend->shapeFor(srcColId);
    const Kalburator::Shape::Shape tgtShape = tgtBackend->shapeFor(tgtColId);
    const auto canonToTgt = treg.compile(m_unifiedCanonical, tgtShape);
    const auto canonToSrc = treg.compile(m_unifiedCanonical, srcShape);

    auto *opsUCC = m_shape.operations
                       .operationsFor(srcShape.domain);
    if (!canonToTgt || !canonToSrc) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral(
            "unifiedContinueAfterConflicts: pipeline unavailable");
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(mappingId, m_currentResult);
        return;
    }

    bool writeFailed = false;
    QString writeError;

    // Helper: apply a batch to a backend.
    //
    // Phase K.4: writer dispatch is driven by `IRecordWriter::threading()`
    // and per-call setup happens via `prepareForApply(ctx)`. The previous
    // `dynamic_cast` to a concrete calendar writer is gone — the engine
    // no longer special-cases the calendar writer.
    //
    // Threading values:
    //   - BackendThread (default): classify + apply both run on the
    //     backend's own thread, wrapped in a single
    //     BlockingQueuedConnection.
    //   - WorkerThread: classify runs on the backend thread; apply
    //     runs on the worker thread (a writer that uses
    //     BlockingQueuedConnection internally must not be called from
    //     the backend thread).
    auto applyBatch = [this, &writeFailed, &writeError](
        Kalburator::Shape::RecordWriter *writer,
        SyncBackend *backend,
        IBlobBackend *blobBackend,
        const QString &colId,
        const QList<BackendRecord> &toWrite,
        const QString &backendRegistryId)
    {
        // Authority: never write to a backend that reports read-only for this
        // collection (e.g. an ACL change at runtime). Skip is a no-op, NOT a
        // failure: return before the ok-checked failure tail so writeFailed
        // stays false and the success path runs. Read-only targets are also
        // excluded upstream (generator + builder ReadOnly seed); this is the
        // engine-level backstop, uniform with the first-sync gate (dispatchFirstSync).
        if (!backend->discoveredWritable(colId)) {
            qWarning() << "SyncEngine: backend" << backendRegistryId
                       << "reports read-only for collection" << colId
                       << "- skipping" << toWrite.size() << "steady-state writes";
            return;
        }

        bool ok = false;

        // The converged writers (DefaultBlobWriter) ignore the host
        // MemoryCalendar; do not source it. prepareForApply remains a no-op hook
        // on the RecordWriter interface. (m_collection / setCollection stay for
        // CalendarManager's separate use.)
        Kalburator::Shape::RecordWriter::ApplyContext ctx;
        ctx.collectionId = colId;
        writer->prepareForApply(ctx);

        // Mass-delete guard: consult the registered guard before allowing
        // a batch of deletes that exceeds either threshold:
        //   - absolute: more than 10 deletes in this batch; OR
        //   - relative: more than 25% of the mapping's current baseline.
        // If no guard is registered, deletes proceed unconditionally
        // (backward compatible). If the guard returns false, the delete
        // list is cleared and creates/updates proceed normally.
        auto applyWithGuard = [this, writer, &colId, &backendRegistryId]
            (WriterBatch &batch) -> bool
        {
            if (!batch.deletes.isEmpty() && m_massDeleteGuard) {
                const int proposed = static_cast<int>(batch.deletes.size());
                const QString mappingId = m_currentRequest.mapping.id;
                int baselineCount = 0;
                if (m_baselineStore && m_baselineStoreAnchor) {
                    Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
                    QMetaObject::invokeMethod(m_baselineStoreAnchor,
                        [bbs, mappingId, &baselineCount]() {
                            baselineCount = static_cast<int>(
                                bbs->baselinesForMappingV3(mappingId).size());
                        },
                        Qt::BlockingQueuedConnection);
                }
                const bool overAbs = proposed > 10;
                const bool overRel = baselineCount > 0
                    && (proposed * 100 / baselineCount) > 25;
                if (overAbs || overRel) {
                    const bool allow = m_massDeleteGuard
                        ->confirmMassDelete(mappingId, backendRegistryId,
                                            proposed, baselineCount);
                    if (!allow) {
                        qDebug() << "SyncEngineWorker: mass-delete gate denied"
                                 << proposed << "deletes for mapping" << mappingId;
                        batch.deletes.clear();
                    }
                }
            }
            return writer->apply(colId, batch.creates, batch.updates, batch.deletes);
        };

        if (writer->threading() ==
            Kalburator::Shape::RecordWriter::Threading::WorkerThread) {
            // Writer manages its own backend-thread marshalling
            // (a WorkerThread writer uses BlockingQueuedConnection
            // internally inside apply()).
            WriterBatch batch;
            QString classifyErr1;
            QMetaObject::invokeMethod(backend, [blobBackend, colId, &batch, &classifyErr1, toWrite]() {
                batch = classifyForWriter(toWrite, blobBackend, colId, &classifyErr1);
            }, Qt::BlockingQueuedConnection);
            if (!classifyErr1.isEmpty()) {
                ok = false;
                writeError = classifyErr1;
            } else {
                ok = applyWithGuard(batch);
            }
        } else {
            // BackendThread: classify on backend thread, then guard check +
            // apply on worker thread to avoid re-entering the backend thread
            // while invoking m_baselineStoreAnchor (which lives on the
            // engine thread — see setDependencies docs in syncengine_p.h).
            WriterBatch batch;
            QString classifyErr2;
            QMetaObject::invokeMethod(backend, [blobBackend, colId, &batch, &classifyErr2, toWrite]() {
                batch = classifyForWriter(toWrite, blobBackend, colId, &classifyErr2);
            }, Qt::BlockingQueuedConnection);
            if (!classifyErr2.isEmpty()) {
                ok = false;
                writeError = classifyErr2;
            } else {
                // Apply (and guard check) run on the worker thread. For
                // BackendThread writers the apply() implementation marshals
                // back to the backend thread internally if needed.
                ok = applyWithGuard(batch);
            }
        }
        if (!ok && !writeFailed) {
            writeFailed = true;
            writeError = QStringLiteral("Write to %1 failed").arg(colId);
        }
    };

    // Apply to target.
    if (!m_unifiedMerge.finalTarget.isEmpty()) {
        QList<BackendRecord> toWrite = m_unifiedMerge.finalTarget;
        if (!canonToTgt->isIdentity()) {
            for (auto &rec : toWrite) {
                if (!rec.isDeleted) {
                    // Warn on materialized non-reversible loss BEFORE apply
                    // overwrites rec.data.
                    const QStringList lost = materializedLoss(*canonToTgt, rec.data);
                    if (!lost.isEmpty())
                        emit transcodingWarning(tgtColId, rec.id, { lost.join(QStringLiteral(", ")) });
                    rec.data = canonToTgt->apply(rec.data);
                }
            }
        }
        auto tgtWriter = opsUCC ? opsUCC->createWriter(tgtBackend) : nullptr;
        if (!tgtWriter)
            tgtWriter = std::make_unique<Kalburator::Shape::DefaultBlobWriter>(tgtBackend);
        applyBatch(tgtWriter.get(), tgtBackend, tgtBlob, tgtColId, toWrite,
                   m_currentRequest.mapping.targetBackend);
    }

    // Apply to source.
    if (!m_unifiedMerge.finalSource.isEmpty()) {
        QList<BackendRecord> toWrite = m_unifiedMerge.finalSource;
        if (!canonToSrc->isIdentity()) {
            for (auto &rec : toWrite) {
                if (!rec.isDeleted) {
                    // Warn on materialized non-reversible loss BEFORE apply
                    // overwrites rec.data.
                    const QStringList lost = materializedLoss(*canonToSrc, rec.data);
                    if (!lost.isEmpty())
                        emit transcodingWarning(srcColId, rec.id, { lost.join(QStringLiteral(", ")) });
                    rec.data = canonToSrc->apply(rec.data);
                }
            }
        }
        auto srcWriter = opsUCC ? opsUCC->createWriter(srcBackend) : nullptr;
        if (!srcWriter)
            srcWriter = std::make_unique<Kalburator::Shape::DefaultBlobWriter>(srcBackend);
        applyBatch(srcWriter.get(), srcBackend, srcBlob, srcColId, toWrite,
                   m_currentRequest.mapping.sourceBackend);
    }

    if (writeFailed) {
        m_currentResult.success = false;
        if (m_currentResult.errorMessage.isEmpty())
            m_currentResult.errorMessage = writeError;
    } else {
        // Only save baselines on successful writes. Saving baselines after a
        // partial write failure would cause "phantom deletions" on retry: the
        // next sync sees the baseline + no target record → wrongly tells source
        // to delete the record the target never actually committed.
        if (m_baselineStore && m_baselineStoreAnchor && !m_unifiedMerge.updatedBaselines.isEmpty()) {
            Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
            const Kalburator::Shape::Shape blobShape{
                Kalburator::Shape::DomainId{QStringLiteral("blob")},
                Kalburator::Shape::EncodingId{QStringLiteral("raw")}};
            const QList<BackendRecord> updated = m_unifiedMerge.updatedBaselines;
            QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, blobShape, updated]() {
                for (const auto &rec : updated) {
                    if (rec.id.isEmpty() || rec.isDeleted)
                        continue;
                    Kalburator::Shape::CanonicalRecord canonical;
                    canonical.recordId = rec.id;
                    canonical.shape    = blobShape;
                    canonical.data     = rec.contentHash.toUtf8();
                    bbs->setBaselineV3(mappingId, canonical);
                }
            }, Qt::BlockingQueuedConnection);
        }
        // T9: persist property-baseline snapshot after successful write.
        if (m_baselineStore && m_baselineStoreAnchor && opsUCC) {
            // baselineProperties is on DomainDefinition, not DomainOperations.
            // Re-look up via DomainRegistry using the same domain the ops cover.
            auto *ddUCC = m_shape.domain
                              .definitionFor(opsUCC->targetDomain());
            const QStringList keys = ddUCC ? ddUCC->baselineProperties() : QStringList{};
            if (!keys.isEmpty()) {
                const QVariantMap collProps =
                    opsUCC->collectionProperties(srcBackend, srcColId);
                QVariantMap snapshot;
                for (const auto &k : keys) {
                    if (collProps.contains(k))
                        snapshot.insert(k, collProps.value(k));
                }
                if (!snapshot.isEmpty()) {
                    Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
                    QMetaObject::invokeMethod(m_baselineStoreAnchor,
                        [bbs, mappingId, srcColId, snapshot]() {
                            bbs->setCollectionBaseline(mappingId, srcColId, snapshot);
                        }, Qt::BlockingQueuedConnection);
                }
            }
        }
        m_currentResult.success = !m_currentResult.hasUnresolvedConflicts();
    }
    m_currentResult.endTime = QDateTime::currentDateTime();
    qDebug() << "SyncEngineWorker::unifiedContinueAfterConflicts completed for" << mappingId;
    m_unifiedDiffer.reset();
    m_unifiedMerger.reset();
    emit syncCompleted(mappingId, m_currentResult);
}

// ----------------------------------------------------------------------------
// Generic property-phase (Phase Ia.5 Task 7).
// ----------------------------------------------------------------------------

void SyncEngineWorker::runPropertyPhase(Kalburator::Shape::DomainOperations *ops,
                                        SyncBackend *src,
                                        SyncBackend *tgt,
                                        const QString &srcCollectionId,
                                        const QString &tgtCollectionId,
                                        const QVariantMap &baseline,
                                        const SyncMapping &mapping)
{
    if (!ops || !src || !tgt) {
        return;
    }

    const QVariantMap srcProps = ops->collectionProperties(src, srcCollectionId);
    const QVariantMap tgtProps = ops->collectionProperties(tgt, tgtCollectionId);

    if (srcProps.isEmpty() && tgtProps.isEmpty() && baseline.isEmpty()) {
        return;  // nothing to do
    }

    const MapPropertyDiff diff = computeMapDiff(srcProps, tgtProps, baseline);

    if (!diff.toApplyToTarget.isEmpty()) {
        ops->applyCollectionProperties(tgt, tgtCollectionId, diff.toApplyToTarget);
    }

    if (mapping.mode == SyncMode::TwoWay && !diff.toApplyToSource.isEmpty()) {
        ops->applyCollectionProperties(src, srcCollectionId, diff.toApplyToSource);
    }

    // Conflict handling (v1, Task 7): resolve all conflicts as SourceWins.
    // This matches the existing computePropertyDiff() default. Task 10 will
    // honor mapping.conflictPolicy and may surface AskUser conflicts via the
    // proper pause/resume mechanism.
    if (!diff.conflicts.isEmpty()) {
        QVariantMap fromSrc;
        for (const QString &k : diff.conflicts) {
            if (srcProps.contains(k)) {
                fromSrc.insert(k, srcProps.value(k));
            }
        }
        if (!fromSrc.isEmpty()) {
            ops->applyCollectionProperties(tgt, tgtCollectionId, fromSrc);
        }
    }
}

} // namespace Kalburator::Engine
