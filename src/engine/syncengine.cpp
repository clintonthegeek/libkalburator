#include "syncengine.h"
#include "syncengine_p.h"
#include "syncrequest.h"
#include "workerteardown.h"
#include "lastwritewins.h"
#include "baselinestore.h"
#include "baselineentry.h"
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
// writer-specific behaviour is mediated by IRecordWriter::prepareForApply()
// (E5.3 CP-A amendment A3 deleted IRecordWriter::threading() — the engine's
// write path no longer calls RecordWriter::apply() at all; see
// SyncEngineWorker::applyBatch and SyncBackendBase::applyRecords()).
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
#include "changedetection.h"
#include "../sync/syncoperation.h"
#include "conflictmanager.h"
#include "imassdeleteguard.h"
#include "canonenvelope.h"
#include "transcodeguard.h"
#include "lossprofile.h"

#include <QDebug>
#include <QJsonArray>
#include <QThread>
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

// D1: marshal a synchronous call onto a backend's own thread. Unlike the
// engine's ~19 QMetaObject::invokeMethod(..., BlockingQueuedConnection)
// sites reached from SyncEngineWorker's dedicated internal thread (which is,
// by construction, never the same thread as any backend), the two call
// sites this guards (prepareSyncFastPath / onWorkerSyncCompleted's
// persistRevision) run on SyncEngine's OWN thread — pre-D1 that is always
// the same thread as the backend (direct calls worked fine), and only
// D1's backend relocation makes it a genuinely different thread sometimes.
// Unconditional BlockingQueuedConnection deadlocks in the same-thread case
// (posting an event to your own queue, then blocking waiting for yourself
// to process it) — found by tst_sync_convergence / tst_engine_skip_unchanged
// timing out after this fix was first written without the guard.
template <typename Func>
void runOnBackendThread(QObject *backend, Func &&fn)
{
    if (QThread::currentThread() == backend->thread()) {
        fn();
    } else {
        QMetaObject::invokeMethod(backend, std::forward<Func>(fn), Qt::BlockingQueuedConnection);
    }
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
    // Architectural-redress Plan 4: the in-flight QFutureInterfaces are owned by
    // unique_ptr members and are freed automatically after this body, fixing the
    // mid-sync memory leak (AUDIT MAJOR "raw QFutureInterface* without lifecycle
    // management"). We deliberately do NOT reportFinished() here: the watchers
    // (m_currentWatcher, parented to this) is torn down by ~QObject
    // immediately after, so emitting finished() now would re-enter the completion
    // slots during teardown. Unblocking a caller that still holds a future while its
    // engine is destroyed mid-sync is a misuse out of Plan 4's scope (see FINDINGS).
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
    // H4 (O16): fast-path pre-pass result, reported back from the worker
    // thread to the engine thread.
    connect(m_worker, &SyncEngineWorker::fastPathReady,
            this, &SyncEngine::onFastPathReady, Qt::QueuedConnection);

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
    // H4 (O16): dispatches the fast-path pre-pass onto the worker thread.
    connect(m_worker, &SyncEngineWorker::fastPathRequested,
            m_worker, &SyncEngineWorker::prepareFastPath, Qt::QueuedConnection);
    // E3 (O33b): dispatches the DecSync active-controller loop onto the
    // worker thread; its completion resumes drive-queue setup.
    connect(m_worker, &SyncEngineWorker::activeControllersRequested,
            m_worker, &SyncEngineWorker::runActiveControllers, Qt::QueuedConnection);
    connect(m_worker, &SyncEngineWorker::activeControllersReady,
            this, &SyncEngine::onActiveControllersReady, Qt::QueuedConnection);

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
                              m_massDeleteGuard,
                              m_registry);

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
            // Synchronous flag set (immediate visibility, unchanged from
            // before E5.3) — but cancel() alone never wakes a nested
            // QEventLoop already running on the worker thread (a fetch or
            // E5.3 write-await gate): only cancellationObserved() does that,
            // and only observeCancel() emits it. E5.3 (O22 residue): also
            // queue observeCancel() onto the worker thread so a genuinely
            // in-flight gate wakes promptly on engine teardown, not just on
            // future.cancel() (which already routed through observeCancel()
            // via onCancelObserved). Queued, not direct: observeCancel()
            // must run ON the worker thread for its cancellationObserved
            // DirectConnection-to-loop.quit() wiring to be same-thread-safe;
            // a nested QEventLoop::exec() still pumps its thread's queued
            // events, so this reaches the gate even while it's parked in
            // loop.exec(). This is what "structurally dissolves E3's
            // stopWorkerThread interim" (E5.3 design note) actually means in
            // practice — waitForWorkerWithDiagnostic's bounded wait below
            // should no longer be the thing that ends an I/O-length wait.
            m_worker->cancel();
            QMetaObject::invokeMethod(m_worker, &SyncEngineWorker::observeCancel,
                                      Qt::QueuedConnection);
        }

        m_workerThread.quit();
        // E3 (O22 residue): bounded wait with a loud diagnostic on
        // expiry, then an unbounded wait — see waitForWorkerWithDiagnostic's
        // doc comment. Post-E5.3 this is a belt-and-braces backstop; the
        // queued observeCancel() above should make an in-flight write/fetch
        // gate settle well before this bound.
        waitForWorkerWithDiagnostic(&m_workerThread);

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
// runSync()'s multi-mapping branch below — the only remaining caller.
void SyncEngine::driveQueue(SyncBehavior behavior,
                            std::optional<QSet<QString>> filter,
                            ExecutionOverride queueOverride)
{
    // v0.65 clobber: stamp the (sanitized) multi-mapping override for this
    // run. Assigned unconditionally so a previous run's clobber can never
    // leak into a later plain sync.
    m_queueOverride = queueOverride;

    if (m_syncMappings.isEmpty() && m_activeControllers.isEmpty()) {
        qDebug() << "SyncEngine::driveQueue - no sync work configured";
        m_lastResult = SyncResult{};
        m_lastResult.success = true;
        // Finish the iface (the QFuture caller is waiting on it).
        if (m_currentIface) {
            m_currentIface->reportResult(m_queue.drain());
            m_currentIface->reportFinished();
            m_currentIface.reset();
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
    // result accumulator, index, and sets DispatchMode::Queue). Not moved
    // from `filter` (H4: the fast-path branch below reuses it below to
    // scope which mappings get a stored-token lookup / dispatch).
    m_queue.prime(m_syncMappings, filter);

    // E3 (O33a): the worker's own cancellation flag is reset exactly
    // once per run, here at the legitimate new-run entry point — never
    // inside processSync() (see SyncEngineWorker::processSync's comment
    // for the erasure race this replaces). startWorkerThread() is
    // idempotent, so this is safe whether or not the loop below or the
    // fast-path branch needs the thread too.
    startWorkerThread();
    QMetaObject::invokeMethod(m_worker, &SyncEngineWorker::resetCancellationFlag,
                              Qt::QueuedConnection);

    if (!m_activeControllers.isEmpty()) {
        // E3 (O33b): DecSyncActiveController::runActiveSync() touches
        // backend-owned state, so it belongs on the worker thread (audit
        // §1's role rules) — not inline here on driveQueue()'s caller
        // thread. Dispatch the whole loop via the same command-channel
        // pattern as fastPathRequested/prepareFastPath;
        // onActiveControllersReady() resumes setup once every controller
        // has run. m_pendingQueueFilter carries `filter` across the
        // async gap the same way the fast-path branch below carries it
        // synchronously.
        emit progressUpdated(0, m_syncMappings.size() + m_activeControllers.size(),
                             tr("Syncing DecSync collections"));
        m_pendingQueueFilter = filter;
        emit m_worker->activeControllersRequested(m_activeControllers.values());
        return; // continuation: onActiveControllersReady() -> continueDriveQueueSetup()
    }

    continueDriveQueueSetup(filter);
}

void SyncEngine::onActiveControllersReady()
{
    continueDriveQueueSetup(m_pendingQueueFilter);
}

void SyncEngine::continueDriveQueueSetup(const std::optional<QSet<QString>> &filter)
{
    // Phase-1 + Phase-2 perf: prime fresh CTags and fingerprints, decide
    // per-mapping skip eligibility. Best-effort; on failure we simply fall
    // back to per-call PROPFIND inside SyncEngineWorker.
    //
    // Clobber runs must NOT skip "unchanged" mappings — the user asked for
    // a forced wipe+repush, and "unchanged" is judged against state the
    // clobber deliberately discards.
    if (!m_cancelled && !m_syncMappings.isEmpty() && !m_queueOverride.clobber) {
        // H4 (O16): the fast-path pre-pass used to run synchronously right
        // here, on driveQueue()'s caller thread — the last engine-side
        // GUI-thread-blocking I/O. It now dispatches to the worker thread
        // (started early, for this purpose) via the same command-channel
        // pattern as processSyncRequested/processSync; the continuation
        // lives in onFastPathReady(). Reading stored tokens stays here:
        // BaselineStore is fast local SQLite and engine-thread-affine.
        startWorkerThread();

        QList<SyncMapping> candidates;
        QHash<QString, QPair<QString, QString>> storedTokens;
        for (const auto &mapping : m_syncMappings) {
            if (!mapping.enabled) continue;
            if (filter && !filter->contains(mapping.id)) continue;
            candidates.append(mapping);
            if (m_baselineStore) {
                storedTokens[mapping.id] = qMakePair(
                    m_baselineStore->syncToken(mapping.id, QStringLiteral("source")),
                    m_baselineStore->syncToken(mapping.id, QStringLiteral("target")));
            }
        }
        emit m_worker->fastPathRequested(candidates, storedTokens, m_skipUnchangedMappings);
        return; // continuation: onFastPathReady() -> finishDriveQueueSetup()
    }

    m_skippedMappingIds.clear();
    // H3: a clobber deliberately discards prior state, so any stored
    // sync-progress token must go with it — otherwise a clobbered
    // mapping could skip next cycle against a token that no longer
    // corresponds to anything real. Safe to over-clear (all enabled
    // mappings, not just the filtered subset): worst case is one
    // extra redundant re-diff, never a masked change. Also clear
    // m_freshState: without the fast-path pre-pass run (skipped on a
    // clobber), a stale entry from a PRIOR non-clobber run would
    // otherwise survive and onWorkerSyncCompleted would immediately
    // re-persist it after this clearSyncTokens call — silently
    // undoing the clear.
    if (m_queueOverride.clobber && m_baselineStore) {
        for (const auto &mapping : m_syncMappings) {
            if (!mapping.enabled) continue;
            m_baselineStore->clearSyncTokens(mapping.id);
        }
    }
    m_freshState.clear();

    finishDriveQueueSetup();
}

void SyncEngine::onFastPathReady(const QSet<QString> &skipped,
                                 const QMap<QString, FreshSyncState> &fresh)
{
    m_skippedMappingIds = skipped;
    m_freshState = fresh;
    finishDriveQueueSetup();
}

void SyncEngine::finishDriveQueueSetup()
{
    if (m_syncMappings.isEmpty() || m_cancelled) {
        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        m_lastResult.success = !m_cancelled;
        m_lastResult.endTime = QDateTime::currentDateTime();
        // Finish the iface with what we have.
        if (m_currentIface) {
            m_currentIface->reportResult(m_queue.drain());
            if (m_cancelled) m_currentIface->reportCanceled();
            m_currentIface->reportFinished();
            m_currentIface.reset();
        }
        m_queue.reset();
        return;
    }

    // Start worker thread for mapping-based sync (idempotent — already
    // running when this is reached via the fast-path branch above).
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
    // this mapping's sync-progress tokens (H3) on success. That's correct —
    // token updates happen as part of the multi-mapping pre-pass
    // (prepareSyncFastPath / onWorkerSyncCompleted's m_freshState lookup),
    // and a stale single-mapping token would be more dangerous than no
    // token update.

    // F2 Task 23 follow-up: cancel-precheck. If cancellation was
    // observed before the worker dispatches (e.g., the caller
    // invoked QFuture::cancel() immediately after runSync
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
        if (m_currentIface) {
            m_currentIface->reportResult(QList<SyncResult>{ cancelled });
            m_currentIface->reportCanceled();
            m_currentIface->reportFinished();
            m_currentIface.reset();
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
            // E3 (O33a): legitimate new-run reset of the worker's own
            // cancellation flag — see driveQueue()'s matching comment.
            QMetaObject::invokeMethod(m_worker, &SyncEngineWorker::resetCancellationFlag,
                                      Qt::QueuedConnection);

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
    if (m_currentIface) {
        m_currentIface->reportResult(QList<SyncResult>{ err });
        m_currentIface->reportFinished();
        m_currentIface.reset();
    }
    m_queue.reset();
    // F2 Task 21 follow-up: clear m_isSyncing on the not-found path.
    // runSync sets m_isSyncing = true before calling
    // processSingleMapping; if we return here without dispatching,
    // nothing else will clear it and subsequent runSync* calls are
    // rejected by the m_isSyncing guard.
    m_isSyncing = false;
}

// Architectural-redress Plan 1 Task 4 (2026-05-29): canonical entry
// point — the sole sync entry since Plan 8 step 3 (2026-06-10) deleted
// the four runSyncFuture overloads. Three dispatch shapes:
//
//   - Single mapping  : request.mappingIds.size() == 1
//                       (executionOverride, if set, threads through
//                        processSingleMapping as a method parameter — P1.T5).
//   - All enabled     : request.mappingIds.isEmpty()
//   - Subset          : request.mappingIds.size() > 1
//
// All three return QFuture<QList<SyncResult>> via the single per-run
// interface set up by beginRun(); the single-mapping branch reports a
// one-element list natively (no .then() wrap, so cancellation results
// survive — see beginRun()).
//
// The overlap guard (m_isSyncing / m_currentIface) and the
// QFutureWatcher cancellation channel are unified here.
QFuture<QList<SyncResult>> SyncEngine::runSync(const SyncRequest &request)
{
    if (m_isSyncing || m_currentIface) {
        // Reject overlapping runs cleanly with a finished failed future.
        // H1.3/O22: an empty result list is indistinguishable from a
        // successful no-op run — report an explicit failed SyncResult
        // instead, so callers (and PlanStan's SyncRunCoordinator) can tell
        // "rejected: already running" apart from "nothing to do".
        qWarning() << "SyncEngine::runSync: rejected — a sync is already running";
        SyncResult rejectedResult;
        rejectedResult.success = false;
        rejectedResult.errorMessage =
            QStringLiteral("rejected: a sync is already running");
        rejectedResult.endTime = QDateTime::currentDateTime();
        QFutureInterface<QList<SyncResult>> rejected;
        rejected.reportStarted();
        rejected.reportResult(QList<SyncResult>{rejectedResult});
        rejected.reportFinished();
        return rejected.future();
    }

    if (request.isSingleMapping()) {
        // Architectural-redress Plan 8 step 3: native single-mapping
        // dispatch. The single-mapping path reports directly into the sole
        // QList iface (no .then() wrap), so the F2 Task 23 cancellation
        // contract — resultCount() == 1 with resultAt(0) == { cancelled
        // SyncResult } — is preserved on the canonical path. (Before the
        // dual future-interface collapse this branch .then()-wrapped a
        // QFuture<SyncResult>, which Qt6 drops on cancel; that wart, and
        // its WildPalms resultCount()>0 workaround, are now gone — see
        // FINDINGS "From Plan 1".)
        QFuture<QList<SyncResult>> future = beginRun();
        m_isSyncing = true;
        std::optional<ExecutionOverride> ov;
        if (request.executionOverride.has_value())
            ov = *request.executionOverride;
        // processSingleMapping primes the queue for a Single run and
        // dispatches exactly the named mapping (no queue iteration).
        processSingleMapping(request.mappingIds.first(), request.behavior,
                             ov.value_or(ExecutionOverride{}));
        return future;
    }

    // Multi-mapping path (all-enabled or subset).
    QFuture<QList<SyncResult>> future = beginRun();

    // v0.65: thread the multi-mapping-applicable part of the override to
    // the queue. Only `clobber` broadens to multi dispatch; `direction`
    // stays a single-mapping concept (SyncRequest doc), so it is sanitized
    // to Default here regardless of what the caller set.
    ExecutionOverride queueOverride;
    if (request.executionOverride.has_value())
        queueOverride.clobber = request.executionOverride->clobber;

    if (request.isAllEnabled()) {
        driveQueue(request.behavior, std::nullopt, queueOverride);
    } else {
        // Subset path.
        QSet<QString> filter(request.mappingIds.constBegin(),
                             request.mappingIds.constEnd());
        driveQueue(request.behavior,
                   std::optional<QSet<QString>>(std::move(filter)),
                   queueOverride);
    }
    return future;
}

// Architectural-redress Plan 8 step 3 (2026-06-10): shared run setup.
// Creates the sole per-run QFutureInterface + its cancellation watcher
// and returns the future callers observe. Both runSync() branches use
// it — the single-mapping branch then dispatches via processSingleMapping,
// the multi-mapping branch via driveQueue. Replaces the former dual
// single/multi interface pair (FINDINGS "From Plan 1"): now that every
// public entry returns QFuture<QList<SyncResult>>, one iface suffices and
// the single-mapping path is native (no .then() wrap, so its cancellation
// result survives — Qt6 drops .then() continuations on cancel).
QFuture<QList<SyncResult>> SyncEngine::beginRun()
{
    m_currentIface = std::make_unique<QFutureInterface<QList<SyncResult>>>();
    m_currentIface->reportStarted();
    // F2 Task 23: cancellation-marker results must survive reportCanceled().
    m_currentIface->setAddResultsIfCanceledEnabled(true);
    QFuture<QList<SyncResult>> future = m_currentIface->future();

    // F2 Task 17 cancellation channel — watcher fires onCancelObserved
    // when the caller invokes future.cancel().
    delete m_currentWatcher;
    m_currentWatcher = new QFutureWatcher<QList<SyncResult>>(this);
    m_currentWatcher->setFuture(future);
    connect(m_currentWatcher, &QFutureWatcher<QList<SyncResult>>::canceled,
            this, &SyncEngine::onCancelObserved);
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

        // F2 Task 21: finish the iface (if any) with what we have.
        if (m_currentIface) {
            m_currentIface->reportResult(m_queue.drain());
            m_currentIface->reportCanceled();
            m_currentIface->reportFinished();
            m_currentIface.reset();
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

        // F2 Task 21: finish the iface (if any) with the per-
        // mapping results. The future resolves to the per-mapping
        // list; the aggregate result is observable via lastSyncResult().
        if (m_currentIface) {
            m_currentIface->reportResult(m_queue.drain());
            m_currentIface->reportFinished();
            m_currentIface.reset();
        }
        m_queue.reset();
        return;
    }

    const SyncMapping &mapping = *nextMapping;

    // G.6 Task 46: ResourceLost skip — if any of this mapping's backends
    // uses a resource that became unavailable, add a cancelled SyncResult
    // and advance without dispatching to the worker.
    if (m_queue.hasLostResources() && m_registry) {
        // v0.66: fetch via the registry (neutral SyncBackendBase*) — the
        // host's calendar-typed backendById() cannot represent base-only
        // backends post-Plan-3 (WildPalms dispatchSync RFC).
        auto *src = m_registry->backendInstance(mapping.sourceBackend);
        auto *tgt = m_registry->backendInstance(mapping.targetBackend);
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
    // v0.65: per-run multi-mapping override (clobber only; direction was
    // sanitized to Default by runSync before reaching the queue).
    request.override = m_queueOverride;

    emit m_worker->processSyncRequested(request);

    // NOTE: Do NOT recurse here!
    // The async operation will call onWorkerSyncCompleted() when done,
    // which will then call advanceQueue() again (Queue mode only).
}

// ============================================================================
// Helper Methods
// ============================================================================

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
        SyncBackendBase *backend = registry ? registry->backendInstance(backendId) : nullptr;
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

    // H3: persist this mapping's sync-progress tokens so the next sync's
    // pre-pass can judge skip-eligibility. Only on success (a failed apply
    // must not advance the token — O17: a stranded change must be retried,
    // never masked as "unchanged"), and only using the pre-fetch snapshot
    // prepareSyncFastPath captured BEFORE this run's fetch — never a live
    // post-write re-query (that re-query was O18/O19's masking surface:
    // a foreign edit landing between the fetch and this callback would be
    // erased from the next diff by a post-write token). A pre-fetch token
    // is never newer than the data actually synced, so a stale token costs
    // at most one redundant re-diff cycle — accepted per CP-A.
    //
    // E9.2 (sync-excellence campaign, O34): a backend that computed an
    // incremental post-write expected fingerprint (WriteOperation::
    // resultRevision(), captured by the worker's applyBatch calls into
    // m_lastAppliedTargetRevision/m_lastAppliedSourceRevision) removes the
    // one-cycle re-diff lag for that side — it is a fresher, still-honest
    // value (computed from the pre-fetch snapshot plus exactly the files
    // THIS run wrote, never a full re-scan) than the pre-fetch snapshot
    // alone. Overrides the corresponding FreshSyncState field ONLY when
    // non-empty; a backend that didn't compute one (e.g. RemoteCalendarBackend
    // — no server-side CTag guessing, per design) leaves the pre-fetch value
    // untouched, unchanged from pre-E9.2 behavior. This does not change
    // WHERE tokens are persisted or WHO owns them — still exactly this
    // block, still engine-owned — only the VALUE fed into it.
    if (result.success && m_baselineStore) {
        auto stateIt = m_freshState.constFind(mappingId);
        if (stateIt != m_freshState.constEnd()) {
            FreshSyncState fresh = stateIt.value();
            if (m_worker) {
                const QString incrementalTarget = m_worker->lastAppliedTargetRevision();
                const QString incrementalSource = m_worker->lastAppliedSourceRevision();
                if (!incrementalTarget.isEmpty()) fresh.targetRevision = incrementalTarget;
                if (!incrementalSource.isEmpty()) fresh.sourceRevision = incrementalSource;
            }
            if (!fresh.sourceRevision.isEmpty()) {
                m_baselineStore->setSyncToken(mappingId, QStringLiteral("source"),
                                              fresh.sourceRevision);
            }
            if (!fresh.targetRevision.isEmpty()) {
                m_baselineStore->setSyncToken(mappingId, QStringLiteral("target"),
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
        if (m_currentIface) {
            m_currentIface->reportResult(QList<SyncResult>{ finalResult });
            if (m_cancelled || finalResult.cancelled) {
                m_currentIface->reportCanceled();
            }
            m_currentIface->reportFinished();
            m_currentIface.reset();
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
        if (m_currentIface) {
            m_currentIface->reportResult(QList<SyncResult>{ failedResult });
            m_currentIface->reportFinished();
            m_currentIface.reset();
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

// v0.66: takes the neutral base — every backend the engine dispatches is a
// SyncBackendBase, which implements IBlobBackend (calendar-typed SyncBackend
// still converts implicitly via upcast).
inline IBlobBackend *asBlob(SyncBackendBase *b) { return static_cast<IBlobBackend *>(b); }

// Phase Ia.5 Task 11: classification helper for the writer-based apply
// path. Mirrors the inline classification the old direct-IBlobBackend
// apply loop did — load the destination's existing record ids, then
// route each post-merge record into creates / updates / deletes.
// Must run on the backend's thread (loadRecords calls into the backend).
//
// E5.3: WriterBatch itself moved to sync/writerbatch.h (SyncBackendBase::
// applyRecords() needs to name it too, and sync/ must not depend on
// engine/) — this is now just the classification function.
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
    // H4 (O16): fastPathRequested/fastPathReady queued-signal parameter types.
    qRegisterMetaType<QList<SyncMapping>>("QList<SyncMapping>");
    qRegisterMetaType<QHash<QString, QPair<QString, QString>>>(
        "QHash<QString,QPair<QString,QString>>");
    qRegisterMetaType<QSet<QString>>("QSet<QString>");
    qRegisterMetaType<QMap<QString, SyncEngine::FreshSyncState>>(
        "QMap<QString,SyncEngine::FreshSyncState>");
    // E3 (O33b): activeControllersRequested's queued-signal parameter type.
    qRegisterMetaType<QList<DecSyncActiveController*>>("QList<DecSyncActiveController*>");
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
                                        Kalburator::Conflict::IMassDeleteGuard *massDeleteGuard,
                                        Kalburator::Sync::BackendRegistry *registry)
{
    m_controller = host;
    m_registry = registry;
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

void SyncEngineWorker::resetCancellationFlag()
{
    // E3 (O33a): the sole legitimate reset point — invoked once per run
    // from SyncEngine's run entry points (driveQueue() /
    // processSingleMapping()), queued so it is guaranteed to execute on
    // the worker thread before that run's first processSyncRequested.
    // See processSync()'s comment for the erasure race this replaces.
    QMutexLocker locker(&m_mutex);
    m_cancelled = false;
}

void SyncEngineWorker::runActiveControllers(const QList<DecSyncActiveController*> &controllers)
{
    // E3 (O33b): runs on the worker thread now (see this method's
    // declaration comment in syncengine_p.h) — runActiveSync() touches
    // backend-owned state, which belongs here per audit §1's role rules,
    // not on driveQueue()'s caller thread.
    for (auto *controller : controllers) {
        if (!controller) continue;
        if (isCancelled()) break;
        controller->runActiveSync();
    }
    emit activeControllersReady();
}

// H4 (O16): moved here from SyncEngine::prepareSyncFastPath, whose logic
// this reproduces exactly (batched fresh-revision query per backend, per-
// mapping skip decision) with one difference: the stored per-mapping
// sync-progress tokens are now a parameter (read by the engine from
// BaselineStore before dispatch) rather than a live BaselineStore lookup,
// since the worker has no baseline-store thread affinity for this purpose.
// The runOnBackendThread marshal below now blocks the WORKER thread
// instead of the engine/caller thread — the whole point of this phase.
void SyncEngineWorker::prepareFastPath(const QList<SyncMapping> &mappings,
                                        const QHash<QString, QPair<QString, QString>> &storedTokens,
                                        bool skipEnabled)
{
    QSet<QString> skipped;
    QMap<QString, SyncEngine::FreshSyncState> freshState;

    if (!m_registry) {
        emit fastPathReady(skipped, freshState);
        return;
    }

    // Collect collection IDs per backend that implements Sync::ChangeDetection.
    QMap<QString, QStringList> colIdsByBackend;
    auto collectChangeDetection = [&](const QString &backendId, const QString &colId) {
        SyncBackendBase *base = m_registry->backendInstance(backendId);
        if (dynamic_cast<Sync::ChangeDetection*>(base))
            colIdsByBackend[backendId].append(colId);
    };
    for (const auto &mapping : mappings) {
        collectChangeDetection(mapping.sourceBackend, mapping.sourceCalendar);
        collectChangeDetection(mapping.targetBackend, mapping.targetCalendar);
    }

    // Fetch fresh revisions per backend (batched where the backend supports
    // it). This marshal blocks the worker thread — a dedicated thread
    // distinct from both the engine/caller thread and any backend I/O
    // thread — so it no longer stalls anything user-visible (O16).
    QMap<QPair<QString, QString>, QString> freshRevisions; // (backendId, colId) -> revision
    for (auto it = colIdsByBackend.constBegin(); it != colIdsByBackend.constEnd(); ++it) {
        SyncBackendBase *base = m_registry->backendInstance(it.key());
        auto *cd = dynamic_cast<Sync::ChangeDetection*>(base);
        if (!cd) continue;
        QStringList ids = it.value();
        ids.removeDuplicates();
        // E5.2 / audit B7 (amendment A6): use the ASYNC revision query and block
        // the WORKER (this) thread on the answer, never a backend thread. The
        // async chain (e.g. CalDAV's fetchAllCtagsAsync) runs on the backend
        // thread without a nested QEventLoop, so an app-side call marshaled onto
        // the backend thread mid-query cannot re-enter it (the surviving
        // B7-family loop the concrete-class fetchItems conversion left open,
        // reachable via a filtered-CalDAV leg). The continuation marshals the
        // result + loop-quit back here, so there is no cross-thread race on
        // `revs` and no same-thread-deadlock hazard (all QueuedConnection).
        //
        // O43 (2026-07-09): the rendezvous must be HEAP-owned and co-owned by
        // the lambda posted to the backend thread. QThread::quit() during
        // engine teardown exits this nested loop too (quitNow), unwinding this
        // frame while the query may still be PENDING on the backend thread; a
        // raw `&loop`/`&revs` capture then dangles and the late continuation
        // SEGVs (deterministic in PlanStan's close-mid-sync / destroy-after-
        // auto-sync-on-load window; pinned by tst_fastpath_teardown_race).
        // `loop` is nulled under the mutex before the frame dies, so a late
        // continuation drops the result instead. The post-to-loop hop stays
        // safe without further guarding: it is posted under the same mutex
        // (so the QEventLoop is alive at post time) and ~QObject removes any
        // still-undelivered metacall targeted at the loop.
        QMap<QString, QString> revs;
        {
            struct Rendezvous {
                QMutex mutex;
                QEventLoop *loop = nullptr;         // guarded by mutex
                QMap<QString, QString> *revs = nullptr; // guarded by mutex
            };
            auto rv = std::make_shared<Rendezvous>();
            QEventLoop loop;
            rv->loop = &loop;
            rv->revs = &revs;
            QMetaObject::invokeMethod(base, [cd, ids, rv]() {
                cd->collectionRevisionsAsync(ids,
                    [rv](QMap<QString, QString> result) {
                        // On the backend thread; hand off to the worker thread
                        // (loop's thread), where `revs` is written and the loop
                        // quits — after loop.exec() has begun. If the worker
                        // frame already unwound (teardown), drop the result.
                        QMutexLocker lock(&rv->mutex);
                        if (!rv->loop)
                            return;
                        QMetaObject::invokeMethod(rv->loop,
                            [rv, result = std::move(result)]() {
                                if (rv->revs)
                                    *rv->revs = result;
                                if (rv->loop)
                                    rv->loop->quit();
                            }, Qt::QueuedConnection);
                    });
            }, Qt::QueuedConnection);
            loop.exec();
            // Invalidate BEFORE ~loop so a pending backend-thread continuation
            // either posts while the loop is provably alive or drops cleanly.
            QMutexLocker lock(&rv->mutex);
            rv->loop = nullptr;
            rv->revs = nullptr;
        }
        for (auto rit = revs.constBegin(); rit != revs.constEnd(); ++rit)
            freshRevisions[qMakePair(it.key(), rit.key())] = rit.value();
    }

    int wouldSkipCount = 0;
    int actualSkipCount = 0;
    for (const auto &mapping : mappings) {
        SyncEngine::FreshSyncState fresh;
        bool sourceCovered = false;
        bool targetCovered = false;
        bool sourceUnchanged = false;
        bool targetUnchanged = false;

        const QPair<QString, QString> stored = storedTokens.value(mapping.id);

        auto checkSide = [&](const QString &backendId, const QString &colId,
                              const QString &storedToken,
                              QString &outRevision, bool &covered, bool &unchanged) {
            SyncBackendBase *base = m_registry->backendInstance(backendId);
            auto *cd = dynamic_cast<Sync::ChangeDetection*>(base);
            if (!cd) return;
            covered = true;
            outRevision = freshRevisions.value(qMakePair(backendId, colId));
            unchanged = !outRevision.isEmpty() && !storedToken.isEmpty()
                        && outRevision == storedToken;
        };

        checkSide(mapping.sourceBackend, mapping.sourceCalendar, stored.first,
                  fresh.sourceRevision, sourceCovered, sourceUnchanged);
        checkSide(mapping.targetBackend, mapping.targetCalendar, stored.second,
                  fresh.targetRevision, targetCovered, targetUnchanged);

        freshState[mapping.id] = fresh;

        const bool eligibleToSkip = sourceCovered && targetCovered
                                     && sourceUnchanged && targetUnchanged;
        if (eligibleToSkip) {
            ++wouldSkipCount;
            if (skipEnabled) {
                skipped.insert(mapping.id);
                ++actualSkipCount;
                qInfo() << "SyncEngineWorker: skipping unchanged mapping" << mapping.id;
            } else {
                qInfo() << "SyncEngineWorker: would skip unchanged mapping (flag off)"
                        << mapping.id;
            }
        }
    }

    qDebug() << "SyncEngineWorker::prepareFastPath: of"
             << mappings.size() << "mappings,"
             << wouldSkipCount << "are unchanged;"
             << actualSkipCount << "actually skipped (flag="
             << skipEnabled << ")";

    emit fastPathReady(skipped, freshState);
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

    // E3 (O33a / audit C4): this used to unconditionally clear
    // m_cancelled here, which could erase a cancel that legitimately
    // landed after this mapping was queued but before this call started
    // (e.g. SyncEngine::stopWorkerThread()'s direct, non-queued
    // m_worker->cancel() racing an already-posted processSyncRequested)
    // — the queue would then run one full extra mapping despite the
    // cancel. The reset now happens exactly once per run, from
    // SyncEngine's run entry points (see resetCancellationFlag()); here
    // we only check and, if already cancelled, short-circuit without
    // ever starting dispatchSync.
    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            locker.unlock();
            SyncResult cancelledResult;
            cancelledResult.success = false;
            cancelledResult.cancelled = true;
            cancelledResult.skipped = true;
            cancelledResult.startTime = QDateTime::currentDateTime();
            cancelledResult.endTime = cancelledResult.startTime;
            emit syncCompleted(request.mapping.id, cancelledResult);
            return;
        }
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
        SyncBackendBase *src = m_registry
            ? m_registry->backendInstance(request.mapping.sourceBackend) : nullptr;
        SyncBackendBase *tgt = m_registry
            ? m_registry->backendInstance(request.mapping.targetBackend) : nullptr;
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
                    Kalburator::Shape::AutoResolveStrategy::None);
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

    SyncBackendBase *srcBackend = m_registry
        ? m_registry->backendInstance(request.mapping.sourceBackend) : nullptr;
    SyncBackendBase *tgtBackend = m_registry
        ? m_registry->backendInstance(request.mapping.targetBackend) : nullptr;

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

    // H2.2 (O21): each side's calls now marshal onto that side's OWN thread
    // — previously the whole mirror (including the target's writes) ran
    // inside a single lambda marshaled onto the source's thread, so the
    // target backend's calls silently executed on the wrong thread whenever
    // source and target are relocated to different I/O threads. The
    // create/update/delete list computation between the two backend calls
    // is pure (no backend I/O) and runs here on the worker thread.
    QList<BackendRecord> srcRecords;
    QMetaObject::invokeMethod(srcBackend,
        [src, colId, &srcRecords, &mirrorReadErr]() {
            src->loadRecordsOrError(colId, srcRecords, mirrorReadErr);
        }, Qt::BlockingQueuedConnection);

    QList<BackendRecord> tgtRecords;
    if (mirrorReadErr.isEmpty()) {
        QMetaObject::invokeMethod(tgtBackend,
            [tgt, colId, &tgtRecords, &mirrorReadErr]() {
                tgt->loadRecordsOrError(colId, tgtRecords, mirrorReadErr);
            }, Qt::BlockingQueuedConnection);
    }

    if (mirrorReadErr.isEmpty()) {
        const auto tgtById = indexBlobById(tgtRecords);

        // Copy source → target (create or update). Short-circuit &&: the
        // write call is never invoked when the target is read-only, so
        // mirrorErrors stays 0 and the success-completion path runs.
        QList<BackendRecord> toCreate;
        QList<BackendRecord> toUpdate;
        for (const auto &sr : srcRecords) {
            const auto it = tgtById.constFind(sr.id);
            if (it == tgtById.constEnd()) {
                toCreate << sr;
            } else if (it.value().contentHash != sr.contentHash) {
                // Phase B4 (N2): this is a genuine cross-side native-bytes
                // hash compare, the same shape as the N2 bug elsewhere in
                // this file — but it does NOT need the per-side-baseline
                // treatment. dispatchFirstSync only reaches this mirror
                // body when the target collection was independently
                // confirmed empty just above (targetEmpty), so tgtById is
                // empty and this branch is unreached in the true
                // first-sync case. It only fires if the target gained
                // records in the narrow window between the emptiness
                // check and this walk (a benign existing-item update,
                // not the steady-state convergence path B4 fixes).
                BackendRecord out = sr;
                out.id = it.value().id;
                toUpdate << out;
            }
        }

        // Delete target records not in source.
        const auto srcById = indexBlobById(srcRecords);
        QStringList toDelete;
        for (const auto &tr : tgtRecords) {
            if (!srcById.contains(tr.id)) {
                toDelete << tr.id;
            }
        }

        QMetaObject::invokeMethod(tgtBackend,
            [tgt, colId, tgtWritable, toCreate, toUpdate, toDelete, &mirrorErrors]() {
                for (const auto &sr : toCreate) {
                    if (tgtWritable && tgt->createRecord(colId, sr).isEmpty())
                        ++mirrorErrors;
                }
                for (const auto &out : toUpdate) {
                    if (tgtWritable && !tgt->updateRecord(out))
                        ++mirrorErrors;
                }
                for (const auto &id : toDelete) {
                    if (tgtWritable && !tgt->deleteRecord(id))
                        ++mirrorErrors;
                }
            }, Qt::BlockingQueuedConnection);
    }

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

    SyncBackendBase *srcBackend = m_registry
        ? m_registry->backendInstance(request.mapping.sourceBackend) : nullptr;
    SyncBackendBase *tgtBackend = m_registry
        ? m_registry->backendInstance(request.mapping.targetBackend) : nullptr;
    if (!srcBackend || !tgtBackend) {
        qWarning() << "SyncEngineWorker::harvestBaselinesAfterFirstSync - backend not found";
        return;
    }

    IBlobBackend *src = asBlob(srcBackend);
    IBlobBackend *tgt = asBlob(tgtBackend);
    const QString srcColId = request.mapping.sourceCalendar;
    const QString tgtColId = request.mapping.targetCalendar;

    // Phase B4 (N2 fix): the mirror just wrote source's content into target
    // using target's own native serialization — its bytes (and hence its
    // hash) generally differ from source's, even for identical logical
    // content (PRODID, property order, folding, server normalization). Read
    // BOTH sides back post-mirror so each gets its own baseline hash,
    // instead of stamping target with source's hash (which is exactly the
    // single-shared-hash bug this phase fixes, just reached via the
    // first-sync path instead of the steady-state one).
    QList<BackendRecord> srcRecords;
    QList<BackendRecord> tgtRecords;
    QString harvestReadErr;
    QMetaObject::invokeMethod(srcBackend,
        [src, srcColId, &srcRecords, &harvestReadErr]() {
            (void)src->loadRecordsOrError(srcColId, srcRecords, harvestReadErr);
        }, Qt::BlockingQueuedConnection);
    if (!harvestReadErr.isEmpty()) {
        qWarning() << "SyncEngineWorker::harvestBaselinesAfterFirstSync - source read failed:"
                   << harvestReadErr << "(no baselines harvested)";
        return;
    }
    QMetaObject::invokeMethod(tgtBackend,
        [tgt, tgtColId, &tgtRecords, &harvestReadErr]() {
            (void)tgt->loadRecordsOrError(tgtColId, tgtRecords, harvestReadErr);
        }, Qt::BlockingQueuedConnection);
    if (!harvestReadErr.isEmpty()) {
        qWarning() << "SyncEngineWorker::harvestBaselinesAfterFirstSync - target read failed:"
                   << harvestReadErr << "(no baselines harvested)";
        return;
    }

    QHash<QString, QString> tgtHashById;
    tgtHashById.reserve(tgtRecords.size());
    for (const BackendRecord &r : tgtRecords) {
        tgtHashById.insert(r.id, r.contentHash);
    }

    const QString mappingId = request.mapping.id;
    QList<Kalburator::Engine::BaselineEntry> entries;
    entries.reserve(srcRecords.size());
    for (const BackendRecord &r : srcRecords) {
        Kalburator::Engine::BaselineEntry e;
        e.id = r.id;
        e.sourceHash = r.contentHash;
        // Fallback to the source hash only if target somehow doesn't have
        // the record yet (shouldn't happen — the mirror just wrote it —
        // but never silently leave a hash empty; see INVARIANTS "fail
        // loud, never silently-empty").
        e.targetHash = tgtHashById.contains(r.id) ? tgtHashById.value(r.id) : r.contentHash;
        entries.append(e);
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (m_baselineStore && m_baselineStoreAnchor) {
        Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
        // Marshal to engine thread — BaselineStore (SQLite) is not thread-safe.
        QMetaObject::invokeMethod(m_baselineStoreAnchor,
            [bbs, mappingId, entries, now]() {
                for (const auto &e : entries) {
                    bbs->setBaselineHashesV4(mappingId, e.id, e.sourceHash, e.targetHash);
                }
                bbs->setLastSyncTime(mappingId, now);
            }, Qt::BlockingQueuedConnection);
    }

    qDebug().noquote() << QString("SyncEngineWorker::harvestBaselinesAfterFirstSync - seeded %1 baselines for %2")
        .arg(entries.size()).arg(mappingId);
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

    SyncBackendBase *srcBackend = m_registry
        ? m_registry->backendInstance(request.mapping.sourceBackend) : nullptr;
    SyncBackendBase *tgtBackend = m_registry
        ? m_registry->backendInstance(request.mapping.targetBackend) : nullptr;
    if (!srcBackend || !tgtBackend) {
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral("dispatchSync: backend not found");
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(request.mapping.id, m_currentResult);
        return true;
    }

    // Per-item progress relay (scoped to this dispatch). Backends emit
    // fetchProgressChanged/writeProgressChanged on the backend-I/O thread; relay
    // them to this worker's fetchProgress/writeProgress signals (already forwarded
    // to SyncEngine at ctor time). Auto-disconnected at every dispatchSync exit via
    // the guard's destructor, so concurrent mappings never cross-talk and no
    // connection leaks across runs.
    struct ProgressRelayGuard {
        QList<QMetaObject::Connection> conns;
        ~ProgressRelayGuard() { for (const auto &c : conns) QObject::disconnect(c); }
    } progressRelay;
    auto installRelay = [&](SyncBackendBase *b) {
        if (!b) return;
        progressRelay.conns << QObject::connect(
            b, &SyncBackendBase::fetchProgressChanged, this,
            [this](const QString &cal, int cur, int tot) { emit fetchProgress(cal, cur, tot); });
        progressRelay.conns << QObject::connect(
            b, &SyncBackendBase::writeProgressChanged, this,
            [this](const QString &cal, int cur, int tot) { emit writeProgress(cal, cur, tot); });
    };
    installRelay(srcBackend);
    if (tgtBackend != srcBackend) installRelay(tgtBackend);

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
    // Clobber bypasses the first-sync fast path so the wipe + repush run as
    // ONE deterministic flow through the unified diff path below (wipe after
    // source fetch, empty baseline, all-creates push).
    if (!request.override.clobber
            && request.useQuickPath && request.mapping.mode == SyncMode::OneWayUpload
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
    // E5.3: unifiedContinueAfterConflicts' applyBatch helper calls
    // SyncBackendBase::applyRecords() directly (no thread-affinity dispatch
    // decision needed — applyRecords() never blocks; see applyBatch's own
    // header comment).

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
    bool srcFetchSucceeded = false;
    {
        SyncOperation *fetchOpRaw = nullptr;
        QMetaObject::invokeMethod(srcBackend, [srcBackend, srcColId, &fetchOpRaw]() {
            fetchOpRaw = srcBackend->fetchItems(srcColId);
        }, Qt::BlockingQueuedConnection);
        QPointer<SyncOperation> fetchOp = fetchOpRaw;
        if (fetchOp && !fetchOp->isFinished()) {
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
            // isFinished() (not state()==Running) catches ops that start life
            // Pending and only flip to Running inside their own deferred
            // callback (e.g. LocalBackend::fetchItems via QTimer::singleShot) —
            // state()==Running alone missed those entirely (H1.1/O24).
            if (!fetchOp->isFinished())
                loop.exec();
        }
        if (m_cancelled.load(std::memory_order_acquire) && fetchOp && !fetchOp->isFinished()) {
            // Mirror the dead await<Op>'s teardown shape (H1.4 deletes it):
            // request the op's own cancel(), then re-enter briefly waiting
            // for it to actually settle (ops aren't pre-emptible mid-record).
            fetchOp->cancel();
            if (!fetchOp->isFinished()) {
                QEventLoop teardownLoop;
                connect(fetchOp.data(), &SyncOperation::finished,
                        &teardownLoop, &QEventLoop::quit, Qt::QueuedConnection);
                teardownLoop.exec();
            }
        }
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            if (fetchOp) fetchOp->deleteLater();
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
        // Fix B: a backend that IMPLEMENTS fetchItems and genuinely failed
        // (state Failed — not the NotSupported "not implemented" default that
        // loadRecords-only backends return) must fail the mapping. Otherwise we
        // read its empty/stale cache via loadRecordsOrError (whose default
        // reports no error) and declare a false success. This gate fires BEFORE
        // the clobber wipe below, so the target is never destroyed when the
        // source can't be read.
        if (fetchOp && fetchOp->state() == SyncOperation::Failed) {
            fetchOp->deleteLater();
            m_currentResult.success = false;
            m_currentResult.errorMessage = fetchOp->errorString();
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
        srcFetchSucceeded = fetchOp && fetchOp->state() == SyncOperation::Succeeded;
        if (fetchOp) fetchOp->deleteLater();
    }
    QList<BackendRecord> sourceRecords;
    {
        QString fetchErr;
        QMetaObject::invokeMethod(srcBackend, [srcBackend, srcBlob, srcColId, srcFetchSucceeded, &sourceRecords, &fetchErr]() {
            // H5/O23: the gate's own fetchItems() already read this collection
            // moments ago — serve its memo instead of a second, fully
            // redundant read. NotSupported/null-op backends (no fetch cache)
            // fall through to loadRecordsOrError() exactly as before.
            if (srcFetchSucceeded) {
                srcBackend->recordsFromLastFetch(srcColId, sourceRecords, fetchErr);
            } else {
                srcBlob->loadRecordsOrError(srcColId, sourceRecords, fetchErr);
            }
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
            const QByteArray before = r.data;
            r.data = srcToCanon->apply(r.data);
            if (Kalburator::Sync::transcodeEmptiedRecord(before, r.data)) {
                m_currentResult.success = false;
                m_currentResult.errorMessage = QStringLiteral(
                    "transcode produced empty bytes for record '%1' "
                    "promoting %2/%3 -> canonical (unhandled component kind?)")
                        .arg(r.id, srcShape.domain.toString(), srcShape.encoding.toString());
                m_currentResult.endTime = QDateTime::currentDateTime();
                emit syncCompleted(mappingId, m_currentResult);
                return true;
            }
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

    // --- Clobber: wipe the target collection (v0.65) ---
    // Deliberately placed AFTER the source fetch succeeded (a target is
    // never destroyed when the source can't be read) and BEFORE the target
    // fetch (which then observes the emptied collection). On wipe failure
    // the mapping fails in isolation — other mappings in the request run
    // normally, same per-mapping isolation as every other failure here.
    if (request.override.clobber) {
        bool wipeOk = false;
        QMetaObject::invokeMethod(tgtBackend, [tgtBlob, tgtColId, &wipeOk]() {
            wipeOk = tgtBlob->wipeCollection(tgtColId);
        }, Qt::BlockingQueuedConnection);
        if (!wipeOk) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral(
                "clobber: wipeCollection failed for target collection %1 "
                "(collection is in an indeterminate state)").arg(tgtColId);
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
        qDebug() << "SyncEngineWorker: clobber wiped target collection"
                 << tgtColId << "for mapping" << mappingId;
    }

    // --- Fetch target records (cross-thread) ---
    // Same cancellable gating pattern as source fetch above.
    bool tgtFetchSucceeded = false;
    {
        SyncOperation *fetchOpRaw = nullptr;
        QMetaObject::invokeMethod(tgtBackend, [tgtBackend, tgtColId, &fetchOpRaw]() {
            fetchOpRaw = tgtBackend->fetchItems(tgtColId);
        }, Qt::BlockingQueuedConnection);
        QPointer<SyncOperation> fetchOp = fetchOpRaw;
        if (fetchOp && !fetchOp->isFinished()) {
            QEventLoop loop;
            // Same race-fix as source fetch: connect before re-check so a
            // completed op in this window still wakes the loop via the
            // already-queued finished() event. isFinished() (not
            // state()==Running) catches ops that start life Pending — see
            // the source fetch gate above (H1.1/O24).
            connect(fetchOp.data(), &SyncOperation::finished,
                    &loop, &QEventLoop::quit, Qt::QueuedConnection);
            connect(this, &SyncEngineWorker::cancellationObserved,
                    &loop, &QEventLoop::quit, Qt::DirectConnection);
            if (!fetchOp->isFinished())
                loop.exec();
        }
        if (m_cancelled.load(std::memory_order_acquire) && fetchOp && !fetchOp->isFinished()) {
            // Mirror the dead await<Op>'s teardown shape (H1.4 deletes it).
            fetchOp->cancel();
            if (!fetchOp->isFinished()) {
                QEventLoop teardownLoop;
                connect(fetchOp.data(), &SyncOperation::finished,
                        &teardownLoop, &QEventLoop::quit, Qt::QueuedConnection);
                teardownLoop.exec();
            }
        }
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            if (fetchOp) fetchOp->deleteLater();
            m_currentResult.success = false;
            m_currentResult.errorMessage = QStringLiteral("Cancelled");
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
        // Fix B: a genuine target fetch failure (state Failed, distinct from the
        // NotSupported "not implemented" default) must fail the mapping rather
        // than reading an empty/stale cache and reporting success. Under clobber
        // the target was already wiped above; converting this to a reported
        // failure is the minimum guarantee for a fetch that fails post-wipe.
        if (fetchOp && fetchOp->state() == SyncOperation::Failed) {
            fetchOp->deleteLater();
            m_currentResult.success = false;
            m_currentResult.errorMessage = fetchOp->errorString();
            m_currentResult.endTime = QDateTime::currentDateTime();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
        tgtFetchSucceeded = fetchOp && fetchOp->state() == SyncOperation::Succeeded;
        if (fetchOp) fetchOp->deleteLater();
    }
    QList<BackendRecord> targetRecords;
    {
        QString fetchErr;
        QMetaObject::invokeMethod(tgtBackend, [tgtBackend, tgtBlob, tgtColId, tgtFetchSucceeded, &targetRecords, &fetchErr]() {
            // H5/O23: same single-fetch-pipeline reasoning as the source
            // block above.
            if (tgtFetchSucceeded) {
                tgtBackend->recordsFromLastFetch(tgtColId, targetRecords, fetchErr);
            } else {
                tgtBlob->loadRecordsOrError(tgtColId, targetRecords, fetchErr);
            }
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
    // Clobber skips the load entirely: the diff must see a first sync (all
    // source records become Creates against the wiped target). A fresh
    // baseline is still written at end-of-sync as normal.
    //
    // Phase B4 (N2 fix): baselines are now per-side hash pairs
    // (Engine::BaselineEntry), not a single hash compared against both
    // sides' native bytes — see baselineentry.h and perrecorddiff.h.
    // baselineHashesForMappingV4() already filters out legacy non-hash rows
    // (e.g. the pre-unified calendar/ical baseline shape written by the
    // engine's old per-mapping baseline-update helper — deleted E1.2/O31,
    // sync-excellence campaign) and transparently upgrades legacy
    // single-hash rows to "both sides equal" (see its doc comment in
    // baselinestore.cpp).
    QList<Kalburator::Engine::BaselineEntry> baselineEntries;
    if (!request.override.clobber && m_baselineStore && m_baselineStoreAnchor) {
        Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
        QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, &baselineEntries]() {
            for (const auto &h : bbs->baselineHashesForMappingV4(mappingId)) {
                Kalburator::Engine::BaselineEntry e;
                e.id         = h.recordId;
                e.sourceHash = h.sourceHash;
                e.targetHash = h.targetHash;
                baselineEntries.append(e);
            }
        }, Qt::BlockingQueuedConnection);
    }

    // --- Diff + merge (pure computation, worker thread) ---
    // Phase N.1: per-record diff via the domain plugin's canonical
    // RecordDiffer. Replaces the Phase Ia.5 batch helper blobBatchDiff.
    m_unifiedDiffer = dd->createCanonicalDiffer();
    m_unifiedMerger = dd->createCanonicalMerger();
    const EngineDiff engineDiff = perRecordDiff(
        sourceRecords, targetRecords, baselineEntries,
        canonical, *m_unifiedDiffer);

    // Seed baselines for records that are already in sync (same ID, no
    // existing baseline) so a subsequent sync can distinguish "source
    // deleted this record" from "target has a new record the source never
    // knew about". The legacy calendar path saved every known record after
    // each successful sync; we replicate that guarantee here for
    // BaselineStore-backed paths.
    //
    // Phase B4 (N2 fix): "already in sync" must be judged by SEMANTIC
    // (canonical) equality, not by comparing the two sides' native-bytes
    // hashes to each other — those never match across backends (that
    // comparison IS the bug), so the old raw-hash check here silently
    // disabled deletion-detection bootstrap for every already-converged
    // record synced across two different backend types. The per-side
    // hashes captured on each successful seed are each side's own current
    // native hash — never each other's.
    if (m_baselineStore && m_baselineStoreAnchor) {
        QHash<QString, BackendRecord> srcById;
        for (const auto &r : sourceRecords) srcById.insert(r.id, r);
        QHash<QString, Kalburator::Engine::BaselineEntry> baselineById;
        for (const auto &e : baselineEntries) baselineById.insert(e.id, e);
        QList<Kalburator::Engine::BaselineEntry> implicitBaselines;
        for (const auto &tgtRec : targetRecords) {
            if (!srcById.contains(tgtRec.id)) continue;
            if (baselineById.contains(tgtRec.id)) continue; // already tracked
            const BackendRecord &srcRec = srcById.value(tgtRec.id);
            const Kalburator::Shape::CanonicalRecord srcCanon{
                canonical, srcRec.data, srcRec.id};
            const Kalburator::Shape::CanonicalRecord tgtCanon{
                canonical, tgtRec.data, tgtRec.id};
            if (!m_unifiedDiffer->equal(srcCanon, tgtCanon))
                continue; // genuinely differ — conflict/update, handled by diff
            Kalburator::Engine::BaselineEntry e;
            e.id         = tgtRec.id;
            e.sourceHash = srcRec.contentHash;
            e.targetHash = tgtRec.contentHash;
            implicitBaselines.append(e);

            // E8 (FINDINGS O28): this is the silent-adoption path for a
            // same-id/no-baseline pair whose byte-different native
            // serializations are canonically equal — the exact shape a
            // partial-push-then-crash repair cycle leaves behind (N
            // records that landed on the target before the crash now have
            // no baseline and never will byte-match the source's native
            // bytes). Before this branch existed the diff had already
            // (correctly, per perRecordDiff's own semanticallyEqual check)
            // emitted NO Conflict op for these ids, so nothing here
            // overrides an existing decision — this only WRITES the
            // baseline that lets subsequent cycles skip the semantic
            // recheck. One info line per adopted id: a crash-repair cycle
            // that silently adopts N records should be visible in logs
            // even though it produces no conflict and no create/update op
            // (SyncStats correctly counts neither, since these ids never
            // enter engineDiff at all).
            qInfo() << "SyncEngineWorker: adopted baseline for" << tgtRec.id
                    << "in mapping" << mappingId
                    << "(no prior baseline, canonically equal to source — "
                       "not a conflict)";
        }
        if (!implicitBaselines.isEmpty()) {
            Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
            QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, implicitBaselines]() {
                for (const auto &e : implicitBaselines)
                    bbs->setBaselineHashesV4(mappingId, e.id, e.sourceHash, e.targetHash);
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
    // `direction` is silently ignored on a clobber (the wipe already
    // fixed the effective direction to source → target; honoring
    // MirrorBToA here would mirror the freshly-emptied target back
    // over the source — i.e. destroy the source).
    using Direction = ExecutionOverride::Direction;
    const Direction dir = request.override.clobber
        ? Direction::Default : request.override.direction;
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
                // Modify-delete: the deleted side has an invalid lastModified, so
                // the modifier wins (valid > invalid). A true modify-modify tie
                // resolves to the target (not source) — see lastWriteWins.h. One
                // shared comparator with ConflictManager so the two sites agree.
                sourceWins = Kalburator::Sync::lastWriteWinsPrefersSource(
                                 op.record.lastModified, op.targetRecord.lastModified);
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
                    Kalburator::Shape::AutoResolveStrategy::None);
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
    // E9.2 (sync-excellence campaign, O34): reset at the top of every run
    // so a stale value from a PRIOR cycle can never leak into this one's
    // syncCompleted (e.g. a cancelled/errored run that never reaches the
    // applyBatch calls below).
    m_lastAppliedTargetRevision.clear();
    m_lastAppliedSourceRevision.clear();

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

    SyncBackendBase *srcBackend = m_registry
        ? m_registry->backendInstance(m_currentRequest.mapping.sourceBackend) : nullptr;
    SyncBackendBase *tgtBackend = m_registry
        ? m_registry->backendInstance(m_currentRequest.mapping.targetBackend) : nullptr;
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
    // E5.3 (sync-excellence campaign, audit B7 / CP-A, 2026-07-08): the
    // H8.5 three-marshal `writer->threading()` dispatch is GONE (the
    // `Threading` enum and `threading()` are deleted from recordwriter.h —
    // CP-A amendment A3). The engine no longer calls `RecordWriter::apply()`
    // at all in the live write path: it calls `SyncBackendBase::
    // applyRecords()` directly, which returns immediately with a
    // `WriteOperation` this worker awaits the SAME cancellable, watchdogged
    // way it awaits a fetch gate (dispatchSync's `srcFetchSucceeded`/
    // `tgtFetchSucceeded` blocks) — no thread-affinity decision needed
    // because nothing blocks. This structurally dissolves E3's
    // `stopWorkerThread` interim and O22's last parked teardown note: the
    // worker never again parks in a `BlockingQueuedConnection` for
    // I/O-length work.
    //
    // CP-A amendment A2: the mass-delete guard resolves BEFORE the write op
    // is enqueued — the op that reaches applyRecords() always carries the
    // already-filtered delete list; the guard itself still runs on THIS
    // (worker) thread, since it reaches the engine-thread baseline anchor
    // via BlockingQueuedConnection.
    //
    // E1.1 (O30): applyBatch populates the caller-supplied SyncStats from
    // the settled WriteOperation's succeeded/failedUids — per-record
    // granularity the old single-bool `writer->apply()` return never had
    // (a batch with 49 successes and 1 failure used to report all 50 as
    // errors). This is what makes advanceQueue's statsOk check and the
    // single-mapping skipped-vs-partial-cancel decision
    // (onWorkerSyncCompleted) honest.
    auto applyBatch = [this, &writeFailed, &writeError, &mappingId](
        Kalburator::Shape::RecordWriter *writer,
        SyncBackendBase *backend,
        IBlobBackend *blobBackend,
        const QString &colId,
        const QList<BackendRecord> &toWrite,
        const QString &backendRegistryId,
        bool notifyHost,
        SyncStats &stats,
        // E9.2 (sync-excellence campaign, O34): out-param — when non-null,
        // filled with the settled WriteOperation's resultRevision() (may
        // stay empty; the backend didn't compute one, or nothing was
        // written). Callers store it into m_lastAppliedTargetRevision /
        // m_lastAppliedSourceRevision so onWorkerSyncCompleted (engine
        // thread) can override the pre-fetch FreshSyncState value with a
        // fresher one for the side that actually wrote.
        QString *outRevision = nullptr)
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
        //
        // H8.5/O27 (still true post-E5.3): the guard decision MUST run on
        // the worker thread — it reaches the engine-thread
        // `m_baselineStoreAnchor` via a BlockingQueuedConnection, which must
        // not be entered from a backend thread. CP-A amendment A2: this
        // MUST resolve before applyRecords() is invoked — the op it
        // produces carries the already-filtered delete list; the guard
        // never runs inside the backend-side op body.
        auto resolveMassDeleteGuard = [this, &backendRegistryId]
            (WriterBatch &batch)
        {
            // Clobber never consults the guard: no deletes are computed (the
            // wipe replaced the diff) and the clobber call IS the user's
            // authorization. Belt-and-braces — the empty-baseline/empty-target
            // diff cannot produce deletes anyway.
            const bool guardApplies = !m_unifiedOverride.clobber;
            if (guardApplies && !batch.deletes.isEmpty() && m_massDeleteGuard) {
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
        };

        WriterBatch batch;
        QString classifyErr;
        QMetaObject::invokeMethod(backend, [blobBackend, colId, &batch, &classifyErr, toWrite]() {
            batch = classifyForWriter(toWrite, blobBackend, colId, &classifyErr);
        }, Qt::BlockingQueuedConnection);

        WriteOperation *writeOp = nullptr;
        if (!classifyErr.isEmpty()) {
            writeError = classifyErr;
        } else {
            resolveMassDeleteGuard(batch);

            // Kick applyRecords() on the backend thread. This returns
            // immediately (E5.1's queue contract: it only creates+enqueues
            // the op) — the actual I/O happens later, asynchronously, off
            // this invoke.
            WriteOperation *writeOpRaw = nullptr;
            QMetaObject::invokeMethod(backend, [backend, colId, &batch, &writeOpRaw]() {
                writeOpRaw = backend->applyRecords(colId, batch);
            }, Qt::BlockingQueuedConnection);
            QPointer<SyncOperation> pendingOp = writeOpRaw;

            // Await exactly like the existing fetch gates (dispatchSync):
            // isFinished() check, QEventLoop + finished-signal QueuedConnection
            // + cancellationObserved DirectConnection, cancel-then-settle
            // teardown.
            if (pendingOp && !pendingOp->isFinished()) {
                QEventLoop loop;
                connect(pendingOp.data(), &SyncOperation::finished,
                        &loop, &QEventLoop::quit, Qt::QueuedConnection);
                connect(this, &SyncEngineWorker::cancellationObserved,
                        &loop, &QEventLoop::quit, Qt::DirectConnection);
                if (!pendingOp->isFinished())
                    loop.exec();
            }
            if (m_cancelled.load(std::memory_order_acquire)
                && pendingOp && !pendingOp->isFinished()) {
                pendingOp->cancel();
                if (!pendingOp->isFinished()) {
                    QEventLoop teardownLoop;
                    connect(pendingOp.data(), &SyncOperation::finished,
                            &teardownLoop, &QEventLoop::quit, Qt::QueuedConnection);
                    teardownLoop.exec();
                }
            }
            writeOp = qobject_cast<WriteOperation *>(pendingOp.data());
        }

        // Preserves the pre-E5.3 semantics exactly: ANY per-record failure
        // fails the whole batch (writeFailed=true), just like
        // DefaultBlobWriter::apply()'s old "ok=false on the first failing
        // record" loop — E5.3 changes WHERE/HOW the write runs, not what it
        // computes (a partial write still blocks baseline persistence below,
        // avoiding the N2-class phantom-delete risk the comment at this
        // function's call sites documents). Requiring state()==Succeeded
        // (not just failedUids().isEmpty()) also catches Cancelled: a write
        // op settled by cancellation before any per-record callback ran has
        // empty failedUids too, but must never be treated as "ok" — doing so
        // would fall through to the baseline-save block below, which talks
        // back to the engine thread via BlockingQueuedConnection and can
        // deadlock against a concurrent ~SyncEngine() teardown (the same
        // thread pair stopWorkerThread() is trying to unwind).
        ok = classifyErr.isEmpty() && writeOp != nullptr
            && writeOp->state() == SyncOperation::Succeeded
            && writeOp->failedUids().isEmpty();

        if (!ok && !writeFailed) {
            writeFailed = true;
            writeError = writeError.isEmpty()
                ? QStringLiteral("Write to %1 failed").arg(colId)
                : writeError;
        }

        // E1.1 (O30): record what this batch actually did, per record — the
        // settled WriteOperation's succeeded/failedUids replace the old
        // batch-wide boolean. Every record in the batch is counted exactly
        // once (created/updated/deleted on success, errors otherwise —
        // including a record that never got attempted at all, e.g. one
        // still queued when a cancel landed): nothing is silently dropped
        // or double-counted.
        const QSet<QString> succeeded = writeOp
            ? QSet<QString>(writeOp->succeededUids().cbegin(), writeOp->succeededUids().cend())
            : QSet<QString>();
        for (const auto &r : batch.creates) {
            if (succeeded.contains(r.id)) ++stats.created; else ++stats.errors;
        }
        for (const auto &r : batch.updates) {
            if (succeeded.contains(r.id)) ++stats.updated; else ++stats.errors;
        }
        for (const auto &id : batch.deletes) {
            if (succeeded.contains(id)) ++stats.deleted; else ++stats.errors;
        }

        // G.9.a closeout: tell the host about every record this batch
        // actually materialized, so a view bound to the host (e.g.
        // PlanStan's ItemLoadingCoordinator via
        // CollectionController::recordChanged) can refresh live instead of
        // waiting for the next fetch cycle or a reopen. Only the side the
        // caller designates as authoritative (PlanStan: the source/primary
        // side of the mapping) notifies — recordChanged's contract re-reads
        // from that side regardless of which side actually changed, so
        // notifying the other side would just be redundant work. Gate
        // unchanged from pre-E5.3 (`ok` = whole batch, zero failures) — only
        // the per-record stats accounting above became granular; host
        // notification granularity is out of E5.3's scope.
        if (ok && notifyHost && m_controller) {
            for (const auto &r : batch.creates)
                if (succeeded.contains(r.id))
                    m_controller->recordChanged(mappingId, r.id,
                        ISyncHost::ChangeKind::Created);
            for (const auto &r : batch.updates)
                if (succeeded.contains(r.id))
                    m_controller->recordChanged(mappingId, r.id,
                        ISyncHost::ChangeKind::Updated);
            for (const auto &id : batch.deletes)
                if (succeeded.contains(id))
                    m_controller->recordChanged(mappingId, id,
                        ISyncHost::ChangeKind::Deleted);
        }

        // E9.2 (sync-excellence campaign, O34): capture BEFORE deleteLater()
        // — resultRevision() is a plain accessor, safe to read here on the
        // worker thread that just settled the op.
        if (outRevision && writeOp)
            *outRevision = writeOp->resultRevision();

        if (writeOp) writeOp->deleteLater();
    };

    // Phase B4 (N2 fix): per-side hashes of the bytes ACTUALLY WRITTEN this
    // sync, keyed by record id. Populated below via a post-write re-fetch
    // through EACH backend's own contentHash computation — not an engine-
    // side hash of the bytes we handed to the writer. BackendRecord.contentHash
    // is explicitly "backend's choice of algorithm" (backendrecord.h); some
    // backends recompute a fresh hash from stored bytes on every read (real
    // backends — LocalBackend, RemoteCalendarBackend, both SHA-256), others
    // may use a different algorithm or even pass a caller-supplied hash
    // through unchanged. Re-fetching after a successful write and reading
    // back THAT backend's own contentHash is the only way to guarantee the
    // saved baseline matches what the very next sync's fetch will see,
    // regardless of the backend's hash semantics. Used by the baseline-save
    // block after a successful apply so each side's baseline reflects its
    // own read-time hash — never the other side's, which is the
    // single-shared-hash bug this phase fixes.
    QHash<QString, QString> writtenTargetHash;
    QHash<QString, QString> writtenSourceHash;

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
                    const QByteArray before = rec.data;
                    rec.data = canonToTgt->apply(rec.data);
                    if (Kalburator::Sync::transcodeEmptiedRecord(before, rec.data)) {
                        writeFailed = true;
                        writeError = QStringLiteral(
                            "transcode produced empty bytes for record '%1' "
                            "demoting canonical -> %2/%3 (unhandled component kind?)")
                                .arg(rec.id, tgtShape.domain.toString(), tgtShape.encoding.toString());
                    }
                }
            }
        }
        auto tgtWriter = opsUCC ? opsUCC->createWriter(tgtBackend) : nullptr;
        if (!tgtWriter)
            tgtWriter = std::make_unique<Kalburator::Shape::DefaultBlobWriter>(tgtBackend);
        applyBatch(tgtWriter.get(), tgtBackend, tgtBlob, tgtColId, toWrite,
                   m_currentRequest.mapping.targetBackend, /*notifyHost=*/false,
                   m_currentResult.targetStats, &m_lastAppliedTargetRevision);
        if (!writeFailed) {
            QList<BackendRecord> refetched;
            QString refetchErr;
            QMetaObject::invokeMethod(tgtBackend, [tgtBlob, tgtColId, &refetched, &refetchErr]() {
                tgtBlob->loadRecordsOrError(tgtColId, refetched, refetchErr);
            }, Qt::BlockingQueuedConnection);
            for (const auto &r : refetched) writtenTargetHash.insert(r.id, r.contentHash);
        }
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
                    const QByteArray before = rec.data;
                    rec.data = canonToSrc->apply(rec.data);
                    if (Kalburator::Sync::transcodeEmptiedRecord(before, rec.data)) {
                        writeFailed = true;
                        writeError = QStringLiteral(
                            "transcode produced empty bytes for record '%1' "
                            "demoting canonical -> %2/%3 (unhandled component kind?)")
                                .arg(rec.id, srcShape.domain.toString(), srcShape.encoding.toString());
                    }
                }
            }
        }
        auto srcWriter = opsUCC ? opsUCC->createWriter(srcBackend) : nullptr;
        if (!srcWriter)
            srcWriter = std::make_unique<Kalburator::Shape::DefaultBlobWriter>(srcBackend);
        applyBatch(srcWriter.get(), srcBackend, srcBlob, srcColId, toWrite,
                   m_currentRequest.mapping.sourceBackend, /*notifyHost=*/true,
                   m_currentResult.sourceStats, &m_lastAppliedSourceRevision);
        if (!writeFailed) {
            QList<BackendRecord> refetched;
            QString refetchErr;
            QMetaObject::invokeMethod(srcBackend, [srcBlob, srcColId, &refetched, &refetchErr]() {
                srcBlob->loadRecordsOrError(srcColId, refetched, refetchErr);
            }, Qt::BlockingQueuedConnection);
            for (const auto &r : refetched) writtenSourceHash.insert(r.id, r.contentHash);
        }
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
        //
        // Phase B4 (N2 fix): `updatedBaselines` tells us WHICH record ids
        // need a fresh baseline (populated the same way it always was,
        // across unifiedHandleConflicts/resumeAfterConflict/the toSource
        // loop/mergeMirrorAToB/mergeMirrorBToA) and, for whichever side
        // did NOT get new bytes this sync, its `.contentHash` is that
        // side's already-current native hash (the entry is literally that
        // side's own unwritten record — see each population site). For
        // whichever side WAS (re)written, prefer the hash of the bytes
        // actually written (writtenSourceHash/writtenTargetHash), captured
        // post-transcode above — never the other side's hash, and never a
        // single shared hash.
        if (m_baselineStore && m_baselineStoreAnchor && !m_unifiedMerge.updatedBaselines.isEmpty()) {
            Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
            QHash<QString, Kalburator::Engine::BaselineEntry> toSave;
            for (const auto &rec : m_unifiedMerge.updatedBaselines) {
                if (rec.id.isEmpty() || rec.isDeleted) continue;
                Kalburator::Engine::BaselineEntry e = toSave.value(rec.id);
                e.id = rec.id;
                if (writtenSourceHash.contains(rec.id))
                    e.sourceHash = writtenSourceHash.value(rec.id);
                else if (e.sourceHash.isEmpty())
                    e.sourceHash = rec.contentHash;
                if (writtenTargetHash.contains(rec.id))
                    e.targetHash = writtenTargetHash.value(rec.id);
                else if (e.targetHash.isEmpty())
                    e.targetHash = rec.contentHash;
                toSave.insert(rec.id, e);
            }
            QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, toSave]() {
                for (auto it = toSave.constBegin(); it != toSave.constEnd(); ++it) {
                    bbs->setBaselineHashesV4(mappingId, it.value().id,
                                             it.value().sourceHash, it.value().targetHash);
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
                QVariantMap collProps;
                runOnBackendThread(srcBackend, [&]() {
                    collProps = opsUCC->collectionProperties(srcBackend, srcColId);
                });
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
    // E1.1 (O30): surface the mapping's unresolved-conflict count in the
    // stats too (targetStats — conflicts are a property of the mapping's
    // reconciliation, not inherently source- or target-owned; targetStats
    // is the side already used as the mapping-level aggregate elsewhere
    // in this function, e.g. baselineProperties above).
    m_currentResult.targetStats.conflicts =
        static_cast<int>(m_currentResult.unresolvedConflicts.size());
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
                                        SyncBackendBase *src,
                                        SyncBackendBase *tgt,
                                        const QString &srcCollectionId,
                                        const QString &tgtCollectionId,
                                        const QVariantMap &baseline,
                                        const SyncMapping &mapping)
{
    if (!ops || !src || !tgt) {
        return;
    }

    QVariantMap srcProps;
    runOnBackendThread(src, [&]() { srcProps = ops->collectionProperties(src, srcCollectionId); });
    QVariantMap tgtProps;
    runOnBackendThread(tgt, [&]() { tgtProps = ops->collectionProperties(tgt, tgtCollectionId); });

    if (srcProps.isEmpty() && tgtProps.isEmpty() && baseline.isEmpty()) {
        return;  // nothing to do
    }

    const MapPropertyDiff diff = computeMapDiff(srcProps, tgtProps, baseline);

    // E11 (audit B7 / FINDINGS O39): applyCollectionProperties() is void and
    // its result was never consumed here even when it ran synchronously —
    // this call was always fire-and-forget from runPropertyPhase's
    // perspective. Now that CalendarDomainOperations::applyCollectionProperties
    // uses updateCalendarAsync internally (no nested QEventLoop on the
    // backend thread), a plain queued marshal onto the backend's own thread
    // is enough; no blocking wait needed. By-value captures because the
    // queued call outlives this function's stack frame.
    if (!diff.toApplyToTarget.isEmpty()) {
        QMetaObject::invokeMethod(tgt, [ops, tgt, tgtCollectionId, props = diff.toApplyToTarget]() {
            ops->applyCollectionProperties(tgt, tgtCollectionId, props);
        });
    }

    if (mapping.mode == SyncMode::TwoWay && !diff.toApplyToSource.isEmpty()) {
        QMetaObject::invokeMethod(src, [ops, src, srcCollectionId, props = diff.toApplyToSource]() {
            ops->applyCollectionProperties(src, srcCollectionId, props);
        });
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
            QMetaObject::invokeMethod(tgt, [ops, tgt, tgtCollectionId, fromSrc]() {
                ops->applyCollectionProperties(tgt, tgtCollectionId, fromSrc);
            });
        }
    }
}

} // namespace Kalburator::Engine
