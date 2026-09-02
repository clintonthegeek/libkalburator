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
#include <climits>
#include <memory>

namespace {

/// Returns the list of property ids that will be materially lost when
/// the given pipeline is applied to `canonData`. Only non-reversible
/// loss kinds (Dropped, Simplified, Degraded) are reported, and only
/// for properties that are actually present (non-empty) in the record.
/// Returns an empty list if the pipeline is lossless or no affected
/// properties are present in this record.
///
/// IP.9 / O88: the record's own `_canon.kind` (empty ⇒ vevent, by
/// icalcanonstages.cpp's convention) selects which of an edge's
/// kind-scoped loss profiles applies (TransformationEdge::lossByKind,
/// Pipeline::composedLoss(kind)) — a VTODO or VJOURNAL demoted through
/// the calendar domain's {calendar,canon}→{calendar,ical} edge no longer
/// gets warned about event-only properties it never carried. `canonData`
/// must be parsed for the presence check regardless, so extracting the
/// kind first costs nothing extra.
static QStringList materializedLoss(const Kalburator::Shape::Pipeline &pipe,
                                    const QByteArray &canonData)
{
    const QJsonObject o = Kalburator::Shape::CanonEnvelope::parse(canonData);
    const QString kind = Kalburator::Shape::CanonEnvelope::kind(o);
    const Kalburator::Shape::LossProfile loss = pipe.composedLoss(kind);
    if (loss.isLossless()) return {};
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

// L1 (sync-graph campaign, spec §5.9): a (backendId, calendarId) endpoint
// identity, used to recognize when a completed mapping wrote an endpoint
// another pending mapping also touches.
QString endpointKey(const QString &backend, const QString &calendar)
{
    return backend + QLatin1Char('|') + calendar;
}

// L1/L2: total create+update+delete count for one side's stats.
int statsChangeCount(const Kalburator::Sync::SyncStats &s)
{
    return s.created + s.updated + s.deleted;
}

// L1: whether a completed mapping's SyncResult actually applied any change
// to either side — a settled no-op result (all zero stats) never
// invalidates anything.
bool appliedChanges(const Kalburator::Sync::SyncResult &r)
{
    return statsChangeCount(r.sourceStats) + statsChangeCount(r.targetStats) > 0;
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
    // Parallel-sync Task 2: workers are now created by startWorkerPool(),
    // not here — there is no single m_worker to construct up front.
}

SyncEngine::~SyncEngine()
{
    stopWorkerPool();
    // Architectural-redress Plan 4: the in-flight QFutureInterfaces are owned by
    // unique_ptr members and are freed automatically after this body, fixing the
    // mid-sync memory leak (AUDIT MAJOR "raw QFutureInterface* without lifecycle
    // management"). We deliberately do NOT reportFinished() here: the watchers
    // (m_currentWatcher, parented to this) is torn down by ~QObject
    // immediately after, so emitting finished() now would re-enter the completion
    // slots during teardown. Unblocking a caller that still holds a future while its
    // engine is destroyed mid-sync is a misuse out of Plan 4's scope (see FINDINGS).
}

void SyncEngine::setupWorkerConnections(SyncEngineWorker *worker, bool isControlSlot)
{
    if (!worker) return;

    // Connect worker signals to coordinator slots (Qt::QueuedConnection for cross-thread)
    connect(worker, &SyncEngineWorker::syncStarted,
            this, &SyncEngine::onWorkerSyncStarted, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::phaseChanged,
            this, &SyncEngine::onWorkerPhaseChanged, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::fetchProgress,
            this, &SyncEngine::onWorkerFetchProgress, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::writeProgress,
            this, &SyncEngine::onWorkerWriteProgress, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::conflictDetected,
            this, &SyncEngine::onWorkerConflictDetected, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::conflictPauseRequested,
            this, &SyncEngine::onWorkerConflictPauseRequested, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::syncCompleted,
            this, &SyncEngine::onWorkerSyncCompleted, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::syncError,
            this, &SyncEngine::onWorkerSyncError, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::transcodingWarning,
            this, &SyncEngine::onWorkerTranscodingWarning, Qt::QueuedConnection);

    // Engine→worker command channel (replaces string-form invokeMethod).
    // SyncEngine emits these *Requested signals on a pool worker; Qt's
    // QueuedConnection routes them across the thread boundary to the
    // matching slots on that SAME worker. Signals are public in Qt, so
    // external emit is fine. Self-connections, so wiring them for every
    // pool worker (not just the control slot) is harmless — a worker
    // whose *Requested signal is never emitted just never fires.
    connect(worker, &SyncEngineWorker::processSyncRequested,
            worker, &SyncEngineWorker::processSync, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::observeCancelRequested,
            worker, &SyncEngineWorker::observeCancel, Qt::QueuedConnection);
    connect(worker, &SyncEngineWorker::resumeAfterConflictRequested,
            worker, &SyncEngineWorker::resumeAfterConflict, Qt::QueuedConnection);
    // H4 (O16): dispatches the fast-path pre-pass onto the worker thread.
    connect(worker, &SyncEngineWorker::fastPathRequested,
            worker, &SyncEngineWorker::prepareFastPath, Qt::QueuedConnection);
    // E3 (O33b): dispatches the DecSync active-controller loop onto the
    // worker thread; its completion resumes drive-queue setup.
    connect(worker, &SyncEngineWorker::activeControllersRequested,
            worker, &SyncEngineWorker::runActiveControllers, Qt::QueuedConnection);

    // Parallel-sync Task 2: the fast-path pre-pass and the DecSync
    // active-controller loop are run-level continuations — they must
    // fire exactly once per run, not once per worker. Only slot 0 (the
    // control slot) ever has fastPathRequested/activeControllersRequested
    // emitted on it (via controlWorker()), so gating these two
    // engine-facing completion connections to isControlSlot is what
    // makes that structurally true rather than just conventionally true.
    if (isControlSlot) {
        // H4 (O16): fast-path pre-pass result, reported back from the
        // worker thread to the engine thread.
        connect(worker, &SyncEngineWorker::fastPathReady,
                this, &SyncEngine::onFastPathReady, Qt::QueuedConnection);
        connect(worker, &SyncEngineWorker::activeControllersReady,
                this, &SyncEngine::onActiveControllersReady, Qt::QueuedConnection);
    }

    // Note: each pool worker is deleted explicitly in stopWorkerPool()
    // rather than via finished->deleteLater, since the thread's event
    // loop has exited by the time finished is emitted.
}

void SyncEngine::startWorkerPool(int size)
{
    if (size < 1)
        size = 1;

    while (m_pool.size() < size) {
        WorkerSlot slot;
        slot.thread = new QThread;
        slot.thread->setObjectName(
            QStringLiteral("kalburator-sync-worker-%1").arg(m_pool.size()));
        slot.worker = new SyncEngineWorker(m_shape);

        const bool isControlSlot = m_pool.isEmpty();
        setupWorkerConnections(slot.worker, isControlSlot);

        // Dependencies must be set BEFORE moveToThread. `this` is the
        // thread anchor for BaselineStore access; the mass-delete guard
        // is pushed in directly (no back-pointer).
        slot.worker->setDependencies(m_controller, m_collection,
                                     m_baselineStore,
                                     this,
                                     m_massDeleteGuard,
                                     m_registry);
        slot.worker->moveToThread(slot.thread);
        m_pool.append(slot);
    }

    for (auto &slot : m_pool) {
        if (slot.thread && !slot.thread->isRunning())
            slot.thread->start();
    }

    qDebug() << "SyncEngine: worker pool at size" << m_pool.size();
}

void SyncEngine::stopWorkerPool()
{
    for (auto &slot : m_pool) {
        if (!slot.thread || !slot.thread->isRunning())
            continue;
        if (slot.worker) {
            // Synchronous flag set for immediate visibility, PLUS a queued
            // observeCancel(): cancel() alone never wakes a nested
            // QEventLoop already running on the worker thread (a fetch or
            // write gate) — only cancellationObserved() does, and only
            // observeCancel() emits it. Queued, not direct, because
            // observeCancel() must run ON the worker thread for its
            // DirectConnection-to-loop.quit() wiring to be same-thread-safe;
            // a nested exec() still pumps queued events, so this reaches a
            // parked gate. (E5.3 / O22 residue — preserved verbatim from
            // the pre-pool stopWorkerThread().)
            slot.worker->cancel();
            QMetaObject::invokeMethod(slot.worker, &SyncEngineWorker::observeCancel,
                                      Qt::QueuedConnection);
        }
        slot.thread->quit();
        // E3 (O22 residue): bounded wait with a loud diagnostic on
        // expiry, then an unbounded wait — see waitForWorkerWithDiagnostic's
        // doc comment. Post-E5.3 this is a belt-and-braces backstop; the
        // queued observeCancel() above should make an in-flight write/fetch
        // gate settle well before this bound.
        waitForWorkerWithDiagnostic(slot.thread);
    }

    // After wait() returns each thread's event loop has stopped, so direct
    // deletion is safe from this thread. moveToThread() cannot be used —
    // you may only push an object to another thread from the thread it is
    // currently on.
    for (auto &slot : m_pool) {
        delete slot.worker;
        delete slot.thread;
    }
    m_pool.clear();
    m_inFlight.clear();
    m_inFlightEndpoints.clear();

    qDebug() << "SyncEngine: worker pool stopped";
}

int SyncEngine::leaseWorker()
{
    for (int i = 0; i < m_pool.size(); ++i) {
        if (m_pool.at(i).busyMappingId.isEmpty())
            return i;
    }
    return -1;
}

void SyncEngine::releaseWorker(const QString &mappingId)
{
    // Fix round 1: QHash::take() on a MISSING key returns a
    // default-constructed value — 0 for int — which is a live slot
    // index, not a sentinel. Without this guard, releasing a mapping
    // that was never leased would silently clear slot 0's
    // busyMappingId, freeing a worker that may be genuinely busy. Every
    // dispatch path today (advanceQueue, processSingleMapping) does
    // register via leaseWorker()/m_inFlight before its worker can ever
    // complete, so this should never fire in practice — the qWarning
    // exists so a future dispatch path that forgets to register fails
    // loudly instead of silently corrupting the pool.
    if (!m_inFlight.contains(mappingId)) {
        qWarning() << "SyncEngine::releaseWorker: no in-flight record for mapping"
                   << mappingId << "- ignoring (QHash::take would free slot 0)";
        return;
    }
    const int slot = m_inFlight.take(mappingId);
    if (slot >= 0 && slot < m_pool.size())
        m_pool[slot].busyMappingId.clear();
}

SyncEngineWorker *SyncEngine::controlWorker() const
{
    return m_pool.isEmpty() ? nullptr : m_pool.at(0).worker;
}

void SyncEngine::forEachWorker(const std::function<void(SyncEngineWorker*)> &fn)
{
    for (auto &slot : m_pool) {
        if (slot.worker)
            fn(slot.worker);
    }
}

bool SyncEngine::poolThreadsRunningForTest() const
{
    for (const auto &slot : m_pool) {
        if (slot.thread && slot.thread->isRunning())
            return true;
    }
    return false;
}

int SyncEngine::distinctPoolThreadCountForTest() const
{
    QSet<const QThread*> seen;
    for (const auto &slot : m_pool)
        seen.insert(slot.thread);
    return seen.size();
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

// Bug B (conflict-resolution-repair Task 3, docs/2026-08-21-conflict-info-
// canonical-data-and-unmonitored-resolution-handoff.md §B). Moved out of line
// from the header so the engine can subscribe to the manager's ONE resolution
// channel. ConflictManager has three paths that used to end at
// "write a column in SyncConflictStore and return" — showImmediateDialog(),
// queueForDeferred() -> dock -> applyResolution(), and applyAutoPolicy() — and
// all three emit conflictResolved(). Listening here fixes all three at once.
void SyncEngine::setConflictManager(ConflictManager *manager)
{
    if (m_conflictManager == manager)
        return;
    if (m_conflictManager)
        disconnect(m_conflictManager, &ConflictManager::conflictResolved,
                   this, &SyncEngine::onConflictResolved);
    m_conflictManager = manager;
    if (m_conflictManager) {
        connect(m_conflictManager, &ConflictManager::conflictResolved,
                this, &SyncEngine::onConflictResolved, Qt::UniqueConnection);
    }
}

void SyncEngine::setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard)
{
    m_massDeleteGuard = guard;
    // Propagate to every pool worker's thread via queued invocation so
    // updates take effect on the next sync cycle without unsynchronized
    // state, regardless of which worker picks up the next mapping.
    forEachWorker([guard](SyncEngineWorker *w) {
        QMetaObject::invokeMethod(w,
            [w, guard]() {
                w->setMassDeleteGuardFromEngine(guard);
            },
            Qt::QueuedConnection);
    });
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

    // L2 (spec §5.9): reset fixpoint-pass state for this run. Assigned
    // unconditionally so a previous run's leftovers can never leak in.
    m_currentPass = 1;
    m_passDirtyWriters.clear();
    m_carriedResults.clear();

    // Bug B (conflict-resolution-repair Task 3): reset the resolution
    // follow-up budget for this run, and pick up any resolution a user chose
    // before the app was last closed. Same "assigned unconditionally" rule as
    // the fixpoint state above.
    m_resolutionPasses = 0;
    m_mappingsWithNewResolutions.clear();
    rehydratePendingResolutions();

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

    // Resolved ONCE per run and frozen — see setMaxConcurrentMappings().
    m_effectiveCap = resolveEffectiveCap(behavior);

    // E3 (O33a): every pool worker's own cancellation flag is reset
    // exactly once per run, here at the legitimate new-run entry point —
    // never inside processSync() (see SyncEngineWorker::processSync's
    // comment for the erasure race this replaces). startWorkerPool() is
    // idempotent, so this is safe whether or not the loop below or the
    // fast-path branch needs the pool too.
    startWorkerPool(m_effectiveCap);
    forEachWorker([](SyncEngineWorker *w) {
        QMetaObject::invokeMethod(w, &SyncEngineWorker::resetCancellationFlag,
                                  Qt::QueuedConnection);
    });

    if (!m_activeControllers.isEmpty()) {
        // E3 (O33b): DecSyncActiveController::runActiveSync() touches
        // backend-owned state, so it belongs on the worker thread (audit
        // §1's role rules) — not inline here on driveQueue()'s caller
        // thread. Dispatch the whole loop via the same command-channel
        // pattern as fastPathRequested/prepareFastPath;
        // onActiveControllersReady() resumes setup once every controller
        // has run. m_pendingQueueFilter carries `filter` across the
        // async gap the same way the fast-path branch below carries it
        // synchronously. Always the control slot (slot 0) — see the
        // WorkerSlot comment in syncengine.h.
        emit progressUpdated(0, m_syncMappings.size() + m_activeControllers.size(),
                             tr("Syncing DecSync collections"));
        m_pendingQueueFilter = filter;
        emit controlWorker()->activeControllersRequested(m_activeControllers.values());
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
        // Always the control slot (slot 0) — see the WorkerSlot comment
        // in syncengine.h.
        startWorkerPool(m_effectiveCap);

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
        emit controlWorker()->fastPathRequested(candidates, storedTokens, m_skipUnchangedMappings);
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

    // Start the worker pool for mapping-based sync (idempotent — already
    // running when this is reached via the fast-path branch above, and
    // already sized to this run's m_effectiveCap by driveQueue()).
    startWorkerPool(m_effectiveCap);
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
    // Bug B (conflict-resolution-repair Task 3): same per-run reset +
    // rehydration driveQueue() does — a single-mapping run must apply a
    // pending resolution too, and PlanStan's "Sync Now" is one.
    m_resolutionPasses = 0;
    m_mappingsWithNewResolutions.clear();
    rehydratePendingResolutions();
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

            // Single-mapping dispatch never queues, so this run's
            // concurrency is 1 by construction — recorded explicitly so
            // m_effectiveCap can never carry a previous queue run's value
            // into anything that reads it during this one.
            m_effectiveCap = 1;

            // Start the worker pool if not running
            startWorkerPool(m_effectiveCap);
            // E3 (O33a): legitimate new-run reset of every pool worker's
            // own cancellation flag — see driveQueue()'s matching comment.
            forEachWorker([](SyncEngineWorker *w) {
                QMetaObject::invokeMethod(w, &SyncEngineWorker::resetCancellationFlag,
                                          Qt::QueuedConnection);
            });

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
            // Bug B: hand this mapping's already-chosen resolutions to the run
            // that will apply them (usually empty).
            request.pendingResolutions = pendingResolutionsFor(mapping.id);

            // Parallel-sync Task 2: dispatch through a leased slot, same as
            // advanceQueue()'s multi-mapping dispatch, so m_inFlight stays
            // accurate for releaseWorker() regardless of dispatch mode. At
            // pool size 1 this always leases slot 0 — behaviourally
            // identical to the pre-pool single-worker dispatch.
            const int slot = leaseWorker();
            Q_ASSERT(slot >= 0);
            m_pool[slot].busyMappingId = mapping.id;
            m_inFlight.insert(mapping.id, slot);
            // Command-channel: QueuedConnection routes to the worker thread.
            emit m_pool[slot].worker->processSyncRequested(request);
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

// Bug B (conflict-resolution-repair Task 3, locked decision 2): the
// single-mapping half of the auto follow-up run. A Queue run re-primes through
// pumpQueue()'s existing fixpoint machinery; DispatchMode::Single has no queue
// to re-prime and finishes inside onWorkerSyncCompleted, so it needs its own
// re-dispatch. Deliberately NOT a new runSync(): starting a fresh run from
// inside a completion handler would race m_isSyncing, resolve a second future
// nobody is holding, and re-enter the fast-path setup. This just puts one more
// Request on the wire for the run that is already open.
bool SyncEngine::redispatchForResolutions(const QString &mappingId)
{
    for (const auto &mapping : std::as_const(m_syncMappings)) {
        if (mapping.id != mappingId || !mapping.enabled)
            continue;

        const int slot = leaseWorker();
        if (slot < 0)
            return false;

        SyncEngineWorker::Request request;
        request.mapping      = mapping;
        request.behavior     = m_currentSyncBehavior;
        request.collectionId = m_collection ? m_collection->id() : QString();
        request.useQuickPath = !m_baselineStore ||
                               m_baselineStore->baselinesForMappingV3(mapping.id).isEmpty();
        request.override     = m_queueOverride;
        request.pendingResolutions = pendingResolutionsFor(mapping.id);

        m_pool[slot].busyMappingId = mapping.id;
        m_inFlight.insert(mapping.id, slot);
        m_inFlightEndpoints.insert(endpointKey(mapping.sourceBackend, mapping.sourceCalendar));
        m_inFlightEndpoints.insert(endpointKey(mapping.targetBackend, mapping.targetCalendar));
        emit m_pool[slot].worker->processSyncRequested(request);
        return true;
    }
    return false;
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

    // Parallel-sync Task 2: fan out to every pool worker — cancellation
    // must reach whichever slot(s) are currently in flight, not just one.
    forEachWorker([](SyncEngineWorker *w) {
        emit w->observeCancelRequested();
    });
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
        // All other reasons: stop the entire queue. Fan out to every pool
        // worker — see onCancelObserved()'s matching comment.
        m_cancelled = true;
        forEachWorker([](SyncEngineWorker *w) {
            emit w->observeCancelRequested();
        });
    }
}

void SyncEngine::resumeAfterConflictResolution(ConflictResolution resolution,
                                                     const QString &mergedIcal)
{
    // Parallel-sync Task 2: resumeAfterConflict must go only to the slot
    // holding the conflicted mapping — fanning it out to every worker
    // would incorrectly resume unrelated in-flight syncs. Monitored runs
    // are capped to concurrency 1 (a later task), so the conflicted
    // mapping is always on slot 0 (the control slot) by construction.
    // CORRECTNESS DEPENDS ON THAT CAP: if Monitored is ever allowed to run
    // with concurrency > 1, this must become a real m_inFlight lookup
    // keyed by m_pendingConflict.mappingId instead of a hardcoded slot 0.
    SyncEngineWorker *worker = controlWorker();
    if (!worker) {
        qWarning() << "SyncEngine::resumeAfterConflictResolution - no worker";
        return;
    }

    qDebug() << "SyncEngine::resumeAfterConflictResolution - resolution:"
             << static_cast<int>(resolution);

    emit worker->resumeAfterConflictRequested(resolution, mergedIcal);
}

void SyncEngine::setSkipUnchangedMappings(bool enabled)
{
    m_skipUnchangedMappings = enabled;
    qDebug() << "SyncEngine::setSkipUnchangedMappings:" << enabled;
}

void SyncEngine::setMaxConcurrentMappings(int n)
{
    m_maxConcurrentMappings = qMax(1, n);
}

int SyncEngine::resolveEffectiveCap(SyncBehavior behavior) const
{
    // Monitored pauses on each AskUser conflict, which is a
    // one-conflict-at-a-time interaction. Rather than invent new conflict
    // UI semantics, Monitored runs stay strictly sequential.
    if (behavior == SyncBehavior::Monitored)
        return 1;

    // Test-only sweep override (parallel-sync Task 10). Lets the whole
    // suite run at a forced concurrency to flush out latent
    // single-in-flight assumptions in code no targeted test reaches. Read
    // once. Never consulted unless explicitly set, so production and
    // every consumer are unaffected. Monitored above still wins — that
    // guarantee is not overridable.
    static const int envOverride = []() {
        const QByteArray v = qgetenv("KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS");
        return v.isEmpty() ? 0 : v.toInt();
    }();
    if (envOverride > 0)
        return envOverride;

    return m_maxConcurrentMappings;
}

int SyncEngine::capForMapping(const SyncMapping &m) const
{
    int cap = INT_MAX;
    if (!m_registry)
        return cap;

    for (const QString &backendId : { m.sourceBackend, m.targetBackend }) {
        auto *backend = m_registry->backendInstance(backendId);
        if (!backend)
            continue;

        // A backend living on the engine's own thread would have every
        // worker's BlockingQueuedConnection park on that thread — in a GUI
        // host that is the GUI thread. Already true at concurrency 1; this
        // stops it becoming N times worse, with no host cooperation
        // required. This is what protects a consumer that has not
        // relocated its backends off the GUI thread.
        if (backend->thread() == this->thread()) {
            cap = 1;
            continue;
        }

        const int declared = backend->maxConcurrentOperations();
        if (declared > 0)
            cap = qMin(cap, declared);
    }
    return cap;
}

// F2 Task 21: multi-mapping driver — entry point for a queue run.
// Initializes the index and kicks off the first iteration. Subsequent
// iterations are driven by onWorkerSyncCompleted -> advanceQueue while
// m_queue.dispatchMode() == Queue. This replaces processNextMapping;
// the single-mapping form no longer participates in queue iteration,
// fixing the FINDINGS leak structurally.
void SyncEngine::processQueue()
{
    pumpQueue();
}

bool SyncEngine::isEligible(const SyncMapping &m) const
{
    if (m_inFlightEndpoints.contains(endpointKey(m.sourceBackend, m.sourceCalendar)))
        return false;
    if (m_inFlightEndpoints.contains(endpointKey(m.targetBackend, m.targetCalendar)))
        return false;
    return inFlightCountForMappingResources(m) < capForMapping(m);
}

int SyncEngine::inFlightCountForMappingResources(const SyncMapping &m) const
{
    if (!m_registry)
        return 0;

    QSet<QString> mine;
    for (const QString &backendId : { m.sourceBackend, m.targetBackend }) {
        if (auto *b = m_registry->backendInstance(backendId))
            mine.insert(b->resourceId());
    }
    if (mine.isEmpty())
        return 0;

    int count = 0;
    for (auto it = m_inFlight.constBegin(); it != m_inFlight.constEnd(); ++it) {
        for (const auto &other : std::as_const(m_syncMappings)) {
            if (other.id != it.key())
                continue;
            for (const QString &backendId : { other.sourceBackend, other.targetBackend }) {
                if (auto *b = m_registry->backendInstance(backendId)) {
                    if (mine.contains(b->resourceId())) {
                        ++count;
                        break;
                    }
                }
            }
            break;
        }
    }
    return count;
}

void SyncEngine::pumpQueue()
{
    // Debug log removed - SyncEngineWorker provides detailed timing

    if (m_cancelled) {
        // Parallel-sync Task 8: DRAIN BEFORE FINISHING. The pre-pool code
        // finished m_currentIface here immediately, which under a pool
        // would resolve the future while in-flight workers are still
        // writing results into m_queue. Wait for the last completion to
        // re-enter this function with an empty pool.
        if (!m_inFlight.isEmpty())
            return;

        m_isSyncing = false;
        m_currentPhase = SyncPhase::Idle;
        emit phaseChanged(m_currentPhase);
        m_lastResult.success = false;
        m_lastResult.errorMessage = QStringLiteral("Sync cancelled");
        m_lastResult.endTime = QDateTime::currentDateTime();

        // F2 Task 21: finish the iface (if any) with what we have.
        if (m_currentIface) {
            m_currentIface->reportResult(m_carriedResults + m_queue.drain());
            m_currentIface->reportCanceled();
            m_currentIface->reportFinished();
            m_currentIface.reset();
        }
        m_queue.reset();
        m_inFlightEndpoints.clear();
        return;
    }

    // Parallel-sync Task 8: dispatch every eligible mapping up to the
    // effective cap, not just one. A mapping whose endpoint collides with
    // one already in flight is skipped WITHOUT being consumed — it stays
    // pending for a later pump once that endpoint frees up.
    while (m_inFlight.size() < m_effectiveCap) {
        std::optional<SyncMapping> nextMapping = m_queue.nextEligible(
            [this](const SyncMapping &m) { return isEligible(m); });

        if (!nextMapping.has_value())
            break;

        const SyncMapping &mapping = *nextMapping;

        // G.6 Task 46: ResourceLost skip — if any of this mapping's
        // backends uses a resource that became unavailable, add a
        // cancelled SyncResult and continue without dispatching.
        if (m_queue.hasLostResources() && m_registry) {
            // v0.66: fetch via the registry (neutral SyncBackendBase*) —
            // the host's calendar-typed backendById() cannot represent
            // base-only backends post-Plan-3 (WildPalms dispatchSync RFC).
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
                continue;
            }
        }

        // Phase-2 skip: if this mapping's both endpoints are demonstrably
        // unchanged AND the skip flag is on, short-circuit without
        // dispatching to the worker. Append a successful no-op result so
        // the future caller sees per-mapping completion in resultAt(0).
        if (m_skippedMappingIds.contains(mapping.id)) {
            emit progressUpdated(m_queue.startedCount(), m_queue.totalSize(),
                                 tr("Skipping unchanged %1").arg(mapping.id));

            SyncResult skippedResult;
            skippedResult.success = true;
            skippedResult.startTime = QDateTime::currentDateTime();
            skippedResult.endTime = skippedResult.startTime;
            m_queue.recordResult(skippedResult);
            continue;
        }

        emit progressUpdated(m_queue.startedCount(), m_queue.totalSize(),
                             tr("Syncing %1").arg(mapping.id));

        SyncEngineWorker::Request request;
        request.mapping = mapping;
        request.behavior = m_currentSyncBehavior;
        request.collectionId = m_collection ? m_collection->id() : QString();
        request.useQuickPath = !m_baselineStore || m_baselineStore->baselinesForMappingV3(mapping.id).isEmpty();
        // v0.65: per-run multi-mapping override (clobber only; direction
        // was sanitized to Default by runSync before reaching the queue).
        request.override = m_queueOverride;
        // Bug B: hand this mapping's already-chosen resolutions to the run
        // that will apply them (usually empty).
        request.pendingResolutions = pendingResolutionsFor(mapping.id);

        const int slot = leaseWorker();
        if (slot < 0) {
            // Pool exhausted despite the cap check — put the mapping back
            // and wait for a completion to re-enter.
            m_queue.pushBack(mapping);
            break;
        }
        m_pool[slot].busyMappingId = mapping.id;
        m_inFlight.insert(mapping.id, slot);
        m_inFlightEndpoints.insert(endpointKey(mapping.sourceBackend, mapping.sourceCalendar));
        m_inFlightEndpoints.insert(endpointKey(mapping.targetBackend, mapping.targetCalendar));
        emit m_pool[slot].worker->processSyncRequested(request);

        // NOTE: no recursion — onWorkerSyncCompleted/onWorkerSyncError
        // re-enter pumpQueue() when a slot frees up.
    }

    // Terminal only when the queue can offer nothing AND nothing is
    // running. Blocked purely on endpoint collisions (candidates remain,
    // but none are currently eligible) is NOT terminal — a completion
    // will free an endpoint and re-pump.
    if (!m_inFlight.isEmpty())
        return;
    if (!m_queue.isExhausted())
        return;

    // Bug B (conflict-resolution-repair Task 3, locked decision 2): the
    // resolution re-pass gate, riding the same re-prime machinery as L2 below.
    // A user answers the batch dialog inside onWorkerSyncCompleted, i.e. after
    // the run that detected the conflict has already done its work; without
    // this, nothing visible happens until the host's next tick (~30s in
    // PlanStan). Its own budget (m_resolutionPasses / kMaxResolutionPasses)
    // rather than m_currentPass, so the L2 gate cannot starve it and a
    // resolution that keeps failing to apply cannot loop forever — the set is
    // cleared as the pass is scheduled, so only a NEWLY chosen resolution can
    // schedule another.
    if (m_queue.dispatchMode() == MappingQueue::DispatchMode::Queue &&
        !m_cancelled && m_resolutionPasses < kMaxResolutionPasses &&
        !m_mappingsWithNewResolutions.isEmpty()) {
        QSet<QString> nextIds;
        for (const auto &m : std::as_const(m_syncMappings)) {
            if (m.enabled && m_mappingsWithNewResolutions.contains(m.id))
                nextIds.insert(m.id);
        }
        m_mappingsWithNewResolutions.clear();
        if (!nextIds.isEmpty()) {
            m_carriedResults += m_queue.drain();
            m_passDirtyWriters.clear();
            // The fast-path pre-pass judged these mappings before the user
            // answered; a skip verdict must not suppress the apply.
            m_skippedMappingIds.clear();
            ++m_resolutionPasses;
            qDebug() << "SyncEngine: resolution re-pass" << m_resolutionPasses
                     << "over" << nextIds.size() << "mapping(s)";
            emit syncPassStarted(m_currentPass, kMaxSyncPasses);
            m_queue.prime(m_syncMappings, nextIds);
            pumpQueue();
            return;
        }
    }

    // L2 (spec §5.9): fixpoint re-pass gate. If any mapping this pass
    // wrote an endpoint that a DIFFERENT mapping also touches, re-prime
    // the queue with just those dirtied mappings and run another pass —
    // up to kMaxSyncPasses total — so one Sync converges regardless of
    // mapping order. Single-mapping runs never reach here (DispatchMode::
    // Single finishes in onWorkerSyncCompleted).
    if (m_queue.dispatchMode() == MappingQueue::DispatchMode::Queue &&
        !m_cancelled && m_currentPass < kMaxSyncPasses &&
        !m_passDirtyWriters.isEmpty()) {
        QSet<QString> nextIds;
        for (const auto &m : std::as_const(m_syncMappings)) {
            if (!m.enabled)
                continue;
            for (const auto &key : { endpointKey(m.sourceBackend, m.sourceCalendar),
                                     endpointKey(m.targetBackend, m.targetCalendar) }) {
                const auto writers = m_passDirtyWriters.value(key);
                // Re-run only if someone ELSE dirtied this endpoint — a
                // mapping is already converged with its own writes.
                if (!writers.isEmpty() &&
                    !(writers.size() == 1 && writers.contains(m.id))) {
                    nextIds.insert(m.id);
                    break;
                }
            }
        }
        if (!nextIds.isEmpty()) {
            m_carriedResults += m_queue.drain();
            m_passDirtyWriters.clear();
            m_skippedMappingIds.clear(); // filter already narrows the pass
            ++m_currentPass;
            emit syncPassStarted(m_currentPass, kMaxSyncPasses);
            m_queue.prime(m_syncMappings, nextIds);
            pumpQueue();
            return;
        }
        m_passDirtyWriters.clear();
    }

    // All mappings processed (or filtered out). Parallel-sync Task 9: this
    // is the one place that actually knows a Queue-mode RUN has nothing
    // left — no in-flight mappings, the queue exhausted, and no fixpoint
    // re-pass warranted. Complete precedes Idle, same order a
    // single-mapping run already announces both in.
    m_isSyncing = false;
    m_currentPhase = SyncPhase::Complete;
    emit phaseChanged(m_currentPhase);
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

    // F2 Task 21: finish the iface (if any) with the per-mapping results.
    // The future resolves to the per-mapping list; the aggregate result
    // is observable via lastSyncResult(). L2: carriedResults holds
    // drained results from earlier passes.
    if (m_currentIface) {
        m_currentIface->reportResult(m_carriedResults + m_queue.drain());
        m_currentIface->reportFinished();
        m_currentIface.reset();
    }
    m_queue.reset();
    m_inFlightEndpoints.clear();
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

    // Bug B (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
    // resolution-handoff.md §B): this used to record the conflict and THROW
    // THE RETURNED ID AWAY, so every ConflictInfo in
    // m_pendingUnmonitoredConflicts carried an empty conflictId,
    // ConflictManager::showImmediateDialog re-recorded it to get one, and the
    // engine had no way to map a later conflictResolved(id, ...) back to the
    // record it is about. Capture it.
    //
    // A host with no SyncConflictStore gets a synthesized id: the in-process
    // channel (ConflictManager -> onConflictResolved -> injection into the
    // next run) works perfectly well without persistence, it just does not
    // survive a restart.
    if (m_conflictStore) {
        const QString recordedId = m_conflictStore->recordConflict(enriched);
        if (!recordedId.isEmpty())
            enriched.conflictId = recordedId;
    }
    if (enriched.conflictId.isEmpty())
        enriched.conflictId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Index the id back to what applying a resolution actually needs. The
    // two lastModified values are the staleness guard's reference point
    // (locked decision 3): they are what the records looked like when the
    // user was shown this conflict.
    ConflictIdentity identity;
    identity.mappingId      = enriched.mappingId;
    identity.recordId       = enriched.sourceId;
    identity.sourceModified = enriched.sourceModified;
    identity.targetModified = enriched.targetModified;
    m_conflictIdentity.insert(enriched.conflictId, identity);

    // Emit for UI notifications (status bar, conflict count badge). Emitted
    // AFTER the id is settled so consumers see a populated conflictId — they
    // used to get an empty one here.
    emit conflictDetected(enriched);

    // Collect for batch presentation after sync mapping completes.
    // Previously this called handleConflict() immediately, which created
    // a modal dialog per conflict via nested exec() event loops —
    // causing N stacked dialogs for N conflicts.
    m_pendingUnmonitoredConflicts.append(enriched);
}

// Bug B: the user answered a dialog (or the auto-policy answered for them).
// This is the whole point of the repair — before Task 3 the answer stopped at
// a SyncConflictStore column and the identical conflict re-detected forever.
void SyncEngine::onConflictResolved(const QString &conflictId,
                                    ConflictResolution resolution)
{
    // Neither of these names a version to keep, so there is nothing to apply.
    // (applyAutoPolicy emits for them too — a pre-existing quirk we tolerate
    // rather than change a signal three consumers already bind to.)
    if (resolution == ConflictResolution::Skip ||
        resolution == ConflictResolution::AskUser) {
        return;
    }
    if (m_resolvingMonitoredConflict) {
        // Monitored: the live run is about to apply this itself. Drop the
        // store row instead of queueing — it is resolved AND consumed, and a
        // resolved row left in the table is exactly what rehydration would
        // pick up and re-arm on a later run.
        if (m_conflictStore && !conflictId.isEmpty())
            m_conflictStore->removeConflict(conflictId);
        m_conflictIdentity.remove(conflictId);
        return;
    }
    if (conflictId.isEmpty()) {
        qWarning() << "SyncEngine::onConflictResolved - resolution" << static_cast<int>(resolution)
                   << "arrived with no conflict id; cannot map it to a record."
                      " It will not be applied.";
        return;
    }

    ConflictIdentity identity = m_conflictIdentity.value(conflictId);
    if (identity.recordId.isEmpty() && m_conflictStore) {
        // An id from a PREVIOUS process, answered out of the dock. The store
        // is the only place that remembers what it was about.
        const ConflictInfo stored = m_conflictStore->conflict(conflictId);
        identity.mappingId      = stored.mappingId;
        identity.recordId       = stored.sourceId;
        identity.sourceModified = stored.sourceModified;
        identity.targetModified = stored.targetModified;
    }
    if (identity.recordId.isEmpty()) {
        qWarning() << "SyncEngine::onConflictResolved - unknown conflict id"
                   << conflictId << "- cannot apply the resolution";
        return;
    }

    PendingConflictResolution pending;
    pending.conflictId     = conflictId;
    pending.recordId       = identity.recordId;
    pending.resolution     = resolution;
    pending.sourceModified = identity.sourceModified;
    pending.targetModified = identity.targetModified;
    if (resolution == ConflictResolution::CustomMerge && m_conflictManager) {
        // Per-conflict, not lastMergedIcalData() — the batch loop has already
        // moved on to the next conflict by now (Bug B, ConflictManager side).
        pending.mergedNative = m_conflictManager->mergedDataFor(conflictId);
    }

    m_pendingResolutions[identity.mappingId].insert(identity.recordId, pending);
    m_mappingsWithNewResolutions.insert(identity.mappingId);

    qDebug() << "SyncEngine::onConflictResolved - queued resolution"
             << static_cast<int>(resolution) << "for record" << identity.recordId
             << "of mapping" << identity.mappingId
             << "(conflict" << conflictId << ")";
}

// Bug B, restart durability: PlanStan's live database is full of rows that are
// resolved but were never applied — that is the defect, persisted. Read them
// back at every run entry so the next dispatchSync finally lands them.
void SyncEngine::rehydratePendingResolutions()
{
    if (!m_conflictStore)
        return;

    // resolvedConflicts() is ordered by resolved_at ASCENDING, so when several
    // resolved rows exist for the same (mapping, record) the LAST one folded
    // in — the most recent resolution — wins.
    const auto rows = m_conflictStore->resolvedConflicts();
    for (const auto &row : rows) {
        if (row.info.sourceId.isEmpty())
            continue;
        if (row.resolution == ConflictResolution::Skip ||
            row.resolution == ConflictResolution::AskUser) {
            continue;
        }
        auto &perMapping = m_pendingResolutions[row.info.mappingId];
        const auto existing = perMapping.constFind(row.info.sourceId);
        if (existing != perMapping.constEnd() &&
            !existing->mergedNative.isEmpty()) {
            // An in-process entry wins over a rehydrated one: it carries the
            // CustomMerge payload, which SyncConflictStore does not persist
            // (FINDINGS O52).
            continue;
        }
        PendingConflictResolution pending;
        pending.conflictId     = row.info.conflictId;
        pending.recordId       = row.info.sourceId;
        pending.resolution     = row.resolution;
        pending.sourceModified = row.info.sourceModified;
        pending.targetModified = row.info.targetModified;
        perMapping.insert(row.info.sourceId, pending);
    }
}

QHash<QString, PendingConflictResolution>
SyncEngine::pendingResolutionsFor(const QString &mappingId) const
{
    return m_pendingResolutions.value(mappingId);
}

// Bug B, consume-once. A resolution must apply exactly once: a row left behind
// would silently auto-apply to a future GENUINE conflict for the same record,
// which is a data-loss bug worse than the one this campaign is fixing.
//
// The worker only reports appliedConflictIds on its successful-write branch,
// so a failed apply leaves the resolution pending for the next run to retry —
// the same rule as "only save baselines on successful writes".
void SyncEngine::consumeAppliedResolutions(const SyncResult &result)
{
    const QStringList doneIds = result.appliedConflictIds + result.staleConflictIds;
    if (doneIds.isEmpty())
        return;

    const QSet<QString> done(doneIds.constBegin(), doneIds.constEnd());

    // Scan rather than index by mapping: a resolution rehydrated from a
    // previous process has no m_conflictIdentity entry, and the map is at most
    // "conflicts a user answered but no run has applied yet" deep.
    QStringList emptiedMappings;
    for (auto m = m_pendingResolutions.begin(); m != m_pendingResolutions.end(); ++m) {
        QStringList doomedRecords;
        for (auto rec = m->constBegin(); rec != m->constEnd(); ++rec) {
            if (done.contains(rec->conflictId))
                doomedRecords.append(rec.key());
        }
        for (const QString &recordId : std::as_const(doomedRecords))
            m->remove(recordId);
        if (m->isEmpty())
            emptiedMappings.append(m.key());
    }
    for (const QString &mappingId : std::as_const(emptiedMappings))
        m_pendingResolutions.remove(mappingId);

    for (const QString &id : done) {
        m_conflictIdentity.remove(id);
        if (m_conflictStore)
            m_conflictStore->removeConflict(id);
    }
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

    // In monitored mode, show dialog via ConflictManager.
    //
    // Bug B (conflict-resolution-repair Task 3): the manager emits
    // conflictResolved from in here too, and SyncEngine now listens to it.
    // Flag the call so onConflictResolved knows this particular answer is
    // about to be applied INLINE by the still-running worker
    // (resumeAfterConflictResolution below) and must not also be queued for a
    // later run.
    if (m_conflictManager) {
        m_resolvingMonitoredConflict = true;
        ConflictResolution resolution = m_conflictManager->handleConflict(enriched);
        m_resolvingMonitoredConflict = false;
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

// L1 (sync-graph campaign, spec §5.9): a completed mapping that applied
// changes invalidates the frozen fast-path skip verdict of every pending
// mapping sharing one of its endpoints — the pre-pass judged those
// endpoints before this run wrote them.
void SyncEngine::invalidateSkipsTouching(const SyncMapping &completed)
{
    if (m_skippedMappingIds.isEmpty())
        return;
    const QSet<QString> written{
        endpointKey(completed.sourceBackend, completed.sourceCalendar),
        endpointKey(completed.targetBackend, completed.targetCalendar),
    };
    for (const auto &m : std::as_const(m_syncMappings)) {
        if (!m_skippedMappingIds.contains(m.id))
            continue;
        if (written.contains(endpointKey(m.sourceBackend, m.sourceCalendar)) ||
            written.contains(endpointKey(m.targetBackend, m.targetCalendar))) {
            m_skippedMappingIds.remove(m.id);
        }
    }
}

void SyncEngine::onWorkerSyncCompleted(const QString &mappingId, const SyncResult &result)
{
    // Parallel-sync Task 2: free this mapping's leased slot immediately —
    // before any of the result-processing below, so the slot is available
    // for the next lease as soon as possible.
    releaseWorker(mappingId);

    // Parallel-sync Task 8: release this mapping's endpoint claims so a
    // colliding mapping becomes eligible for the next pump.
    for (const auto &m : std::as_const(m_syncMappings)) {
        if (m.id == mappingId) {
            m_inFlightEndpoints.remove(endpointKey(m.sourceBackend, m.sourceCalendar));
            m_inFlightEndpoints.remove(endpointKey(m.targetBackend, m.targetCalendar));
            break;
        }
    }

    // Bug B (conflict-resolution-repair Task 3), consume-once: drop the
    // resolutions this mapping actually applied (and any it discarded as
    // stale) before anything can re-inject them. Done BEFORE the batch
    // presentation below, because that presentation can legitimately queue a
    // NEW resolution for the same record.
    consumeAppliedResolutions(result);

    // Batch-present any unmonitored conflicts collected during this mapping.
    // handleConflicts() (plural) applies hybrid threshold: shows dialogs for
    // small batches, defers large batches to the dock widget.
    //
    // Bug B: this is where a user's choice enters the engine — synchronously,
    // via ConflictManager::conflictResolved -> onConflictResolved, which fills
    // m_pendingResolutions and marks the mapping in
    // m_mappingsWithNewResolutions. Both the Single branch below and
    // pumpQueue()'s terminal branch read that set to decide whether to run one
    // follow-up pass (locked decision 2), so by the time either looks, the
    // answers are in.
    if (m_conflictManager && !m_pendingUnmonitoredConflicts.isEmpty()
            && !m_conflictPresentationScheduled) {
        m_conflictPresentationScheduled = true;
        QMetaObject::invokeMethod(this, &SyncEngine::presentPendingConflicts,
                                   Qt::QueuedConnection);
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
    // result.appliedTargetRevision/result.appliedSourceRevision) removes the
    // one-cycle re-diff lag for that side — it is a fresher, still-honest
    // value (computed from the pre-fetch snapshot plus exactly the files
    // THIS run wrote, never a full re-scan) than the pre-fetch snapshot
    // alone. Overrides the corresponding FreshSyncState field ONLY when
    // non-empty; a backend that didn't compute one (e.g. RemoteCalendarBackend
    // — no server-side CTag guessing, per design) leaves the pre-fetch value
    // untouched, unchanged from pre-E9.2 behavior. This does not change
    // WHERE tokens are persisted or WHO owns them — still exactly this
    // block, still engine-owned — only the VALUE fed into it.
    //
    // Parallel-sync Task 1: read from the result, not from "the" worker.
    // With a pool there is no single current worker, and the result is
    // already delivered per-mapping.
    if (result.success && m_baselineStore) {
        auto stateIt = m_freshState.constFind(mappingId);
        if (stateIt != m_freshState.constEnd()) {
            FreshSyncState fresh = stateIt.value();
            if (!result.appliedTargetRevision.isEmpty())
                fresh.targetRevision = result.appliedTargetRevision;
            if (!result.appliedSourceRevision.isEmpty())
                fresh.sourceRevision = result.appliedSourceRevision;
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

    // Parallel-sync Task 9: the engine-level phase describes the RUN, not
    // one mapping. In Queue mode, m_inFlight going empty here does NOT
    // mean the run is done — pumpQueue() below may immediately dispatch
    // more pending mappings, or a later fixpoint re-pass may follow.
    // Complete is emitted from pumpQueue()'s own terminal branch instead,
    // the single place that actually knows the run has nothing left.
    // Single-mapping runs have no such ambiguity (finish unconditionally
    // below), so Complete is still emitted right here for that mode.
    // Bug B (locked decision 2): the user answers the dialog AFTER the run
    // that detected the conflict has finished, so without a follow-up nothing
    // visibly happens until the host's next tick (~30s in PlanStan). A Queue
    // run rides pumpQueue()'s existing re-prime machinery; a Single run has no
    // queue to re-prime, so re-dispatch the one mapping here instead — before
    // Complete is announced and before the future is finished, because both
    // are terminal and this run is not over yet.
    const bool singleMode =
        m_queue.dispatchMode() == MappingQueue::DispatchMode::Single;
    if (singleMode && !m_cancelled &&
        m_resolutionPasses < kMaxResolutionPasses &&
        m_mappingsWithNewResolutions.contains(mappingId)) {
        m_mappingsWithNewResolutions.remove(mappingId);
        ++m_resolutionPasses;
        qDebug() << "SyncEngine: re-running mapping" << mappingId
                 << "to apply" << pendingResolutionsFor(mappingId).size()
                 << "newly-chosen conflict resolution(s) (pass"
                 << m_resolutionPasses << "of" << kMaxResolutionPasses << ")";
        // The re-dispatch has to be a fresh Request so it picks up
        // pendingResolutions; everything else about the run (behavior,
        // override, iface, queue mode) is deliberately left in place, so the
        // future still resolves exactly once, with the LAST pass's result.
        if (redispatchForResolutions(mappingId))
            return;
        // Could not re-dispatch (mapping gone, or no free slot): fall through
        // and finish. The resolution stays pending for the host's next run.
        --m_resolutionPasses;
    }

    if (singleMode) {
        m_currentPhase = SyncPhase::Complete;
        emit phaseChanged(m_currentPhase);
    }

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

    // L1 (spec §5.9): a successful mapping that actually wrote something
    // may un-freeze a still-pending mapping's fast-path skip verdict if it
    // shares an endpoint — otherwise propagation across a mapping chain
    // takes one hop per run instead of settling within this one.
    if (m_queue.dispatchMode() == MappingQueue::DispatchMode::Queue &&
        result.success && appliedChanges(result)) {
        for (const auto &m : std::as_const(m_syncMappings)) {
            if (m.id == mappingId) {
                invalidateSkipsTouching(m);
                // L2 (spec §5.9): record which mapping wrote which endpoint
                // this pass, so the exhaustion branch in advanceQueue() can
                // decide whether a re-pass is warranted. Per-side (not the
                // unconditional both-endpoints shape L1's invalidation
                // uses): only the side that actually changed is marked —
                // otherwise a mapping that changed only its target would
                // also falsely mark its unchanged source dirty, tripping a
                // needless extra pass for whatever OTHER mapping shares
                // that source (verified against testChainConvergesInOneRun,
                // which pins exactly one re-pass for a two-hop chain).
                if (statsChangeCount(result.sourceStats) > 0)
                    m_passDirtyWriters[endpointKey(m.sourceBackend, m.sourceCalendar)].insert(m.id);
                if (statsChangeCount(result.targetStats) > 0)
                    m_passDirtyWriters[endpointKey(m.targetBackend, m.targetCalendar)].insert(m.id);
                break;
            }
        }
    }

    // Continue pumping the queue (queue mode only).
    pumpQueue();
}

void SyncEngine::presentPendingConflicts()
{
    m_conflictPresentationScheduled = false;
    if (!m_conflictManager || m_pendingUnmonitoredConflicts.isEmpty())
        return;

    const QList<ConflictInfo> batch = m_pendingUnmonitoredConflicts;
    m_pendingUnmonitoredConflicts.clear();

    QStringList mappingIds;
    for (const auto &c : std::as_const(batch)) {
        if (!mappingIds.contains(c.mappingId))
            mappingIds.append(c.mappingId);
    }
    qDebug() << "SyncEngine: Batch-presenting" << batch.size()
             << "conflict(s) for mapping(s)" << mappingIds;

    // This call can pop a MODAL dialog per conflict and block for as long
    // as the user takes to answer. That is fine now: this method is never
    // reached from inside a worker-completion slot (see its header doc
    // comment / O53), so its own nested event loop cannot cause another
    // onWorkerSyncCompleted() call to re-enter this presentation — any
    // conflict detected by another mapping while this is blocking just
    // accumulates in m_pendingUnmonitoredConflicts (the scheduling guard
    // above stays true throughout, so it is not re-presented here) and is
    // picked up by the reschedule check below.
    m_conflictManager->handleConflicts(batch);

    // NOT reinstating locked decision 2's "same-run" instant reapply here.
    // By the time this method runs, the ORIGINAL run that detected the
    // conflict is guaranteed fully finished — m_isSyncing false,
    // m_currentIface null, m_queue reset() (mode None). A first attempt at
    // "dispatch a fresh runSync() covering the newly-resolved mappings"
    // looked reusable (every dispatchSync() already rehydrates pending
    // resolutions for its own mapping — the same path "resolution survives
    // restart" depends on) but is NOT safe here: this method can run at any
    // point after being scheduled, with no guarantee the engine (or its
    // backends/registry) still exists by then. A real SIGSEGV was caught in
    // tst_syncengine_unification proving exactly this — the deferred call
    // fired after test teardown had already destroyed the backends,
    // dispatchSync() reached a worker thread mid-teardown, and it crashed
    // inside kickFetch() on a dangling SyncBackendBase*. A host closing a
    // collection right after answering a conflict dialog would hit the
    // identical hazard in production.
    //
    // The resolution is not lost — it is already durably recorded (Bug B's
    // rehydration path, m_pendingResolutions / SyncConflictStore) and will
    // apply on whichever sync the host runs next, exactly like a resolution
    // answered after a restart already does. That is a real latency
    // regression versus the old (unsafe) synchronous-presentation behavior
    // for Single-mode / trailing-mapping cases specifically — see FINDINGS
    // O53 follow-up for the accepted tradeoff and why restoring "instant"
    // safely would need its own design pass (a cancellable/awaitable
    // follow-up tied to the engine's lifetime, not a bare runSync() call).

    // Conflicts delivered while handleConflicts() above was blocking (from
    // another mapping's completion, or a nested presentation this method's
    // own scheduling guard suppressed) need their own round.
    if (!m_pendingUnmonitoredConflicts.isEmpty() && !m_conflictPresentationScheduled) {
        m_conflictPresentationScheduled = true;
        QMetaObject::invokeMethod(this, &SyncEngine::presentPendingConflicts,
                                   Qt::QueuedConnection);
    }
}

void SyncEngine::onWorkerSyncError(const QString &mappingId, const QString &errorMessage)
{
    // Parallel-sync Task 2: free this mapping's leased slot immediately —
    // see the matching comment in onWorkerSyncCompleted().
    releaseWorker(mappingId);

    // Parallel-sync Task 8: release this mapping's endpoint claims — see
    // the matching comment in onWorkerSyncCompleted().
    for (const auto &m : std::as_const(m_syncMappings)) {
        if (m.id == mappingId) {
            m_inFlightEndpoints.remove(endpointKey(m.sourceBackend, m.sourceCalendar));
            m_inFlightEndpoints.remove(endpointKey(m.targetBackend, m.targetCalendar));
            break;
        }
    }

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

    // Continue pumping the queue (don't fail the whole batch).
    pumpQueue();
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
// SyncEngineWorker — runs sync operations on a pool worker's QThread
// (parallel-sync Task 2: formerly the single m_workerThread).
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

// ============================================================================
// O55/O56 record-id alias helpers
// ============================================================================

namespace {

constexpr int kMaxAliasChainHops = 8;

// Follow an alias chain from `id` to its component sink (the id no alias
// points away FROM — the id a mapping's baseline rows are keyed under).
// Bounded; a malformed (cyclic) map just stops early.
QString resolveIdThroughAliases(const QHash<QString, QString> &aliases,
                                const QString &id)
{
    QString cur = id;
    for (int hops = 0; hops < kMaxAliasChainHops && aliases.contains(cur);
         ++hops) {
        const QString next = aliases.value(cur);
        if (next == cur)
            break;
        cur = next;
    }
    return cur;
}

// O56 heal: normalize a raw alias map into one where every native id points
// DIRECTLY at its component sink, with cycles broken deterministically
// (lexicographically smallest member wins). Handles stores poisoned by the
// v1.00 crossing defect (bare→prefixed AND prefixed→bare): both ids resolve
// to ONE sink, so the per-record join can no longer split them onto
// different keys. In-memory only — the store's rows are left as-is.
QHash<QString, QString> healedIdAliases(const QHash<QString, QString> &raw)
{
    QHash<QString, QString> sinkOf;
    auto componentSink = [&](const QString &start) -> QString {
        if (sinkOf.contains(start))
            return sinkOf.value(start);
        QList<QString> path{start};
        QSet<QString> visited{start};
        QString cur = start;
        while (raw.contains(cur)) {
            const QString next = raw.value(cur);
            if (next == cur)
                break; // self-alias: already a sink
            if (visited.contains(next)) {
                // Cycle among path[idx..end]: deterministic sink.
                QString best = next;
                const int idx = path.indexOf(next);
                for (int i = idx; i < path.size(); ++i)
                    if (path[i] < best)
                        best = path[i];
                cur = best;
                break;
            }
            visited.insert(next);
            path.append(next);
            cur = next;
        }
        for (const QString &n : std::as_const(path))
            sinkOf.insert(n, cur);
        return cur;
    };

    QHash<QString, QString> out;
    out.reserve(raw.size());
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
        const QString sink = componentSink(it.key());
        if (it.key() != sink)
            out.insert(it.key(), sink);
    }
    return out;
}

} // namespace

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
    // (e.g. SyncEngine::stopWorkerPool()'s direct, non-queued
    // slot.worker->cancel() racing an already-posted processSyncRequested)
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

// Conflict-resolution-repair Task 2 (docs/2026-08-21-conflict-resolution-
// repair-plan.md): the resolution-application switch, lifted out of
// resumeAfterConflict() so it no longer depends on the yielded-run state
// machine. @p op is a parameter, not m_unifiedDiff.toTarget[m_unifiedConflictIdx],
// because Task 3 (Bug B — Unmonitored resolutions are never applied) replays a
// stored resolution mid-walk against an op that is NOT the yielded one.
//
// Everything this touches (m_unifiedMerge, m_currentResult, m_unifiedCanonical,
// m_unifiedMerger, m_unifiedSrcToCanon, m_currentRequest) is per-run state that
// exists in both callers, so the extraction needs no new plumbing.
void SyncEngineWorker::applyConflictResolution(const EngineDiffOp &op,
                                               ConflictResolution resolution,
                                               const QString &mergedNative)
{
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
                // Bug D (docs/2026-08-21-conflict-info-canonical-data-and-
                // unmonitored-resolution-handoff.md §D; PlanStan's
                // docs/bugs/sync-dialog-keepboth-duplicate-not-created.md):
                // this used to byte-patch `data.replace("UID:"+oldId, "UID:"+newId)`,
                // which is an iCal spelling. clone.data is CANONICAL Shape JSON
                // here — dispatchSync promotes both fetched lists before the
                // diff — and the canonical envelope spells the uid `"uid": "…"`
                // (CanonEnvelope::uidKey). So the replace never matched: the
                // clone kept the original uid, both records demoted to the same
                // UID, and the backend's uid-keyed store collapsed them back
                // into one. "Keep Both" produced one item. Rewrite through the
                // envelope helpers instead — they are the single place that
                // knows the key names (INVARIANTS §1).
                if (!clone.data.isEmpty()) {
                    QJsonObject cloneObj =
                        Kalburator::Shape::CanonEnvelope::parse(clone.data);
                    if (cloneObj.isEmpty()) {
                        // Not canonical JSON: an unexpected shape reached the
                        // conflict walk. Refuse to guess at a uid rewrite —
                        // emitting the clone with a colliding uid is what Bug D
                        // was. Fall back to a plain TargetWins-shaped resolution
                        // (source keeps its version, target's version survives
                        // on the source side) so no data is lost or corrupted.
                        qWarning() << "SyncEngineWorker::applyConflictResolution:"
                                   << "Duplicate resolution for" << op.targetRecord.id
                                   << "- record data is not canonical JSON, cannot"
                                      " re-uid the clone; keeping both sides in"
                                      " place instead of writing a colliding copy";
                        m_unifiedMerge.finalSource.append(op.targetRecord);
                        m_unifiedMerge.updatedBaselines.append(op.targetRecord);
                        ++m_unifiedMerge.conflictsResolved;
                        break;
                    }
                    cloneObj.insert(Kalburator::Shape::CanonEnvelope::uidKey(),
                                    clone.id);
                    clone.data =
                        Kalburator::Shape::CanonEnvelope::serialize(cloneObj);
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
            // Bug C (same handoff, §C): resumeAfterConflict has always been
            // handed the caller's hand-merged payload — SyncEngine::
            // onWorkerConflictPauseRequested genuinely fetches it via
            // ConflictManager::lastMergedIcalData() — and always ignored it,
            // running the automatic merger instead. Every "Custom Merge" the
            // user performed was silently discarded.
            //
            // The payload is in the SOURCE backend's NATIVE encoding (it is
            // built from ConflictInfo::sourceIcalData, which buildConflictInfo
            // demotes through m_unifiedCanonToSrc), while everything below the
            // merge expects canonical bytes — unifiedContinueAfterConflicts
            // demotes finalTarget/finalSource through canonToTgt/canonToSrc on
            // the way out. So promote it back up the same way dispatchSync
            // promotes a freshly fetched record.
            QByteArray mergedCanonical;
            if (!mergedNative.isEmpty()) {
                const QByteArray nativeBytes = mergedNative.toUtf8();
                if (!m_unifiedSrcToCanon) {
                    // dispatchSync proves all four pipelines non-null before
                    // the conflict walk begins, so this is unreachable from a
                    // real run; degrade to the automatic merger rather than
                    // deref.
                    qWarning() << "SyncEngineWorker::applyConflictResolution:"
                               << "no promotion pipeline for the supplied merge of"
                               << op.record.id << "- falling back to auto-merge";
                } else {
                    mergedCanonical = m_unifiedSrcToCanon->apply(nativeBytes);
                    if (Kalburator::Sync::transcodeEmptiedRecord(nativeBytes,
                                                                 mergedCanonical)) {
                        // Writing this would blank the record on BOTH sides.
                        qWarning() << "SyncEngineWorker::applyConflictResolution:"
                                   << "promoting the supplied merge of" << op.record.id
                                   << "to canonical produced empty bytes - falling"
                                      " back to auto-merge";
                        mergedCanonical.clear();
                    }
                }
            }
            if (!mergedCanonical.isEmpty()) {
                BackendRecord mergedRecord = op.record;
                mergedRecord.data = mergedCanonical;
                m_unifiedMerge.finalTarget.append(mergedRecord);
                m_unifiedMerge.finalSource.append(mergedRecord);
                m_unifiedMerge.updatedBaselines.append(mergedRecord);
                ++m_unifiedMerge.conflictsResolved;
                break;
            }
            // No usable caller-supplied merge — the pre-Bug-C behaviour:
            // merge automatically, or defer when the domain has no merger.
            if (!m_unifiedMerger) {
                // FINDINGS O50, folded in by Task 3 (Bug B's staleness guard
                // needs the timestamps this used to omit): this branch built
                // the ConflictInfo by hand with only ids and a hardcoded
                // BothModified — no detectedAt, no source/targetModified, no
                // payload — so a conflict deferred OUT OF a resolution reached
                // the host at a lower fidelity than one deferred out of the
                // detection walk. Same builder as the detection walk now.
                m_currentResult.unresolvedConflicts.append(buildConflictInfo(op));
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
            // Skip / AskUser / unsupported → defer. FINDINGS O50, folded in by
            // Task 3 — see the sibling comment in the CustomMerge branch above.
            m_currentResult.unresolvedConflicts.append(buildConflictInfo(op));
            ++m_unifiedMerge.conflictsDeferred;
            break;
        }
    }
}

void SyncEngineWorker::resumeAfterConflict(ConflictResolution resolution, const QString &mergedIcal)
{
    qDebug() << "SyncEngineWorker::resumeAfterConflict - resolution:" << static_cast<int>(resolution);

    if (!m_yieldedForConflict) {
        qWarning() << "SyncEngineWorker::resumeAfterConflict called but not yielded — ignoring";
        return;
    }

    if (m_unifiedConflictIdx < m_unifiedDiff.toTarget.size()) {
        // Bug C: mergedIcal is the caller's hand-merged payload. It used to
        // stop here, unread; applyConflictResolution honours it.
        applyConflictResolution(m_unifiedDiff.toTarget[m_unifiedConflictIdx],
                                resolution, mergedIcal);
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
                (void)tgt->loadRecordsOrError(colId, tgtRecords, mirrorReadErr);
            }, Qt::BlockingQueuedConnection);
    }

    // O55: ids the backend actually assigned to this mirror's creates
    // (requested → assigned). GenericSqliteBackend reads back
    // <collectionId>\x01<origId>; without recording the pairing the next
    // sync's diff cannot join the sides and churns.
    QHash<QString, QString> mirrorAliases;

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
            [tgt, colId, tgtWritable, toCreate, toUpdate, toDelete,
             &mirrorErrors, &mirrorAliases]() {
                for (const auto &sr : toCreate) {
                    // O55: capture the backend-assigned id for the alias
                    // record (requested → assigned). A withheld (read-only)
                    // write is a no-op, not an error — same contract as
                    // pre-O55.
                    QString storedId;
                    if (tgtWritable) {
                        storedId = tgt->createRecord(colId, sr);
                        if (storedId.isEmpty())
                            ++mirrorErrors;
                        else if (storedId != sr.id)
                            mirrorAliases.insert(sr.id, storedId);
                    }
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

    harvestBaselinesAfterFirstSync(request, mirrorAliases);

    if (!tgtWritable) {
        // O46: the mirror writes were withheld because the target reports
        // read-only. That is a no-op success, not a failure — but record it on
        // the result so consumers can badge the edge instead of seeing a silent
        // success with zero stats. Stable prefix "target-readonly:" for parsing.
        result.warnings << QStringLiteral("target-readonly:%1").arg(colId);
    }

    result.success = true;
    result.startTime = m_currentResult.startTime;
    result.endTime = QDateTime::currentDateTime();
    emit syncCompleted(request.mapping.id, result);
    return true;
}

void SyncEngineWorker::harvestBaselinesAfterFirstSync(
    const Request &request, const QHash<QString, QString> &idAliases)
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
    // O55: reverse view (assigned → requested) so the target's read-back
    // hash can be found for a create the backend re-namespaced.
    QHash<QString, QString> assignedToRequested;
    for (auto it = idAliases.constBegin(); it != idAliases.constEnd(); ++it)
        assignedToRequested.insert(it.value(), it.key());
    for (const BackendRecord &r : srcRecords) {
        Kalburator::Engine::BaselineEntry e;
        e.id = r.id;
        e.sourceHash = r.contentHash;
        // Fallback to the source hash only if target somehow doesn't have
        // the record yet (shouldn't happen — the mirror just wrote it —
        // but never silently leave a hash empty; see INVARIANTS "fail
        // loud, never silently-empty"). O55: resolve through the mirror's
        // aliases first — a prefixing backend reads back under the
        // assigned form, not the requested one.
        const QString tgtId = assignedToRequested.contains(r.id)
            ? assignedToRequested.value(r.id) : r.id;
        e.targetHash = tgtHashById.contains(tgtId) ? tgtHashById.value(tgtId) : r.contentHash;
        entries.append(e);
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (m_baselineStore && m_baselineStoreAnchor) {
        Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
        // Marshal to engine thread — BaselineStore (SQLite) is not thread-safe.
        QMetaObject::invokeMethod(m_baselineStoreAnchor,
            [bbs, mappingId, entries, now, idAliases]() {
                for (const auto &e : entries) {
                    bbs->setBaselineHashesV4(mappingId, e.id, e.sourceHash, e.targetHash);
                }
                // O55: persist the mirror's create aliases so later passes'
                // join resolves both sides onto one logical record.
                for (auto it = idAliases.constBegin(); it != idAliases.constEnd(); ++it)
                    bbs->setIdAlias(mappingId, it.value(), it.key());
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

// Parallel-sync Task 3: kick a fetch on @p backend's own thread and return
// the operation WITHOUT awaiting it, so two sides can be in flight at once.
// The await is factored into SyncEngineWorker::awaitFetchOps() below.
static SyncOperation *kickFetch(SyncBackendBase *backend, const QString &colId)
{
    SyncOperation *raw = nullptr;
    QMetaObject::invokeMethod(backend, [backend, colId, &raw]() {
        raw = backend->fetchItems(colId);
    }, Qt::BlockingQueuedConnection);
    return raw;
}

void SyncEngineWorker::awaitFetchOps(const QList<QPointer<SyncOperation>> &ops)
{
    const auto allFinished = [&ops]() {
        for (const auto &op : ops) {
            if (op && !op->isFinished())
                return false;
        }
        return true;
    };

    if (allFinished())
        return;

    QEventLoop loop;
    // Connect BEFORE the re-check: an op may finish between the check and
    // exec(), and the queued finished() event then stays in this thread's
    // queue until exec() drains it. isFinished() (not state()==Running)
    // catches ops that start life Pending and only flip inside their own
    // deferred callback — e.g. LocalBackend::fetchItems (H1.1/O24).
    for (const auto &op : ops) {
        if (!op)
            continue;
        connect(op.data(), &SyncOperation::finished, &loop, [&allFinished, &loop]() {
            if (allFinished())
                loop.quit();
        }, Qt::QueuedConnection);
    }
    connect(this, &SyncEngineWorker::cancellationObserved,
            &loop, &QEventLoop::quit, Qt::DirectConnection);

    if (!allFinished())
        loop.exec();
}

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

    // --- Fetch source + target records (cross-thread, overlapped) ---
    // Parallel-sync Task 3: kick both fetches before awaiting either, so the
    // two sides can be in flight at once instead of strictly additive. Under
    // ExecutionOverride::clobber the two MUST stay sequential — the wipe
    // (below, after the source fetch is read and promoted) sits deliberately
    // between them so a target is never destroyed when the source cannot be
    // read; overlapFetches is false in exactly that case, and the target
    // fetch is kicked later, after the wipe.
    //
    // Phase Ib.5 Task 7: fetchItems() remains a cancellable gating step
    // before loadRecordsOrError(). For backends that support
    // setFetchBlocking (e.g. MockBackend in cancellation tests), fetchItems()
    // returns immediately but starts a background thread that blocks until
    // the test releases the blocker. awaitFetchOps() awaits the op(s) in a
    // QEventLoop so cancellation signals (observeCancel → cancellationObserved)
    // can arrive and abort the sync. Backends that don't override
    // fetchItems() return an immediately-finished NotSupported op, so the
    // loop returns right away and we proceed directly to loadRecordsOrError().
    const bool overlapFetches = !request.override.clobber;

    // Parallel-sync Task 3 review fix: under overlap, the target op is
    // kicked and awaited alongside the source op long before it is
    // actually consumed (the target gate, well below). Several unrelated
    // early returns sit in between — the source-record read's fetchErr
    // check, the transcode-failure check, and the post-promotion
    // cancellation recheck — and NONE of them have any reason to know
    // about the target op, but every one of them MUST dispose it or it
    // leaks: SyncOperations are parented to their backend at construction,
    // so an un-disposed op survives as long as the backend does (i.e. the
    // whole app session), once per triggering sync. Tracking that by hand
    // does not scale — a future early return added anywhere in this region
    // would silently reintroduce the same leak. This guard makes disposal
    // structural instead of remembered: its destructor runs on every exit
    // from this scope (every `return` below, or falling off the end) and
    // disposes whatever op is still tracked and not yet explicitly
    // disposed. disposeSrc()/disposeTgt()/disposeBoth() are what call
    // sites use once an op's outcome (success, failure, or cancellation)
    // has actually been read and it is genuinely done being needed —
    // idempotent (the bool flags), so re-disposing at the destructor (or a
    // second explicit call) is a harmless no-op rather than a double-free:
    // deleteLater() is documented-safe to call more than once on the same
    // object; QObject flushes any still-queued deferred-delete events for
    // itself when it is actually destroyed.
    struct FetchOpGuard {
        QPointer<SyncOperation> src;
        QPointer<SyncOperation> tgt;
        bool srcDisposed = false;
        bool tgtDisposed = false;
        void disposeSrc() {
            if (!srcDisposed) { if (src) src->deleteLater(); srcDisposed = true; }
        }
        void disposeTgt() {
            if (!tgtDisposed) { if (tgt) tgt->deleteLater(); tgtDisposed = true; }
        }
        void disposeBoth() { disposeSrc(); disposeTgt(); }
        ~FetchOpGuard() { disposeBoth(); }
    } fetchOps;

    bool srcFetchSucceeded = false;
    fetchOps.src = kickFetch(srcBackend, srcColId);
    if (overlapFetches)
        fetchOps.tgt = kickFetch(tgtBackend, tgtColId);

    awaitFetchOps(overlapFetches
                      ? QList<QPointer<SyncOperation>>{ fetchOps.src, fetchOps.tgt }
                      : QList<QPointer<SyncOperation>>{ fetchOps.src });

    if (m_cancelled.load(std::memory_order_acquire)) {
        // Mirror the dead await<Op>'s teardown shape (H1.4 deletes it) for
        // whichever op(s) are still in flight: request cancel(), then wait
        // for it to actually settle (ops aren't pre-emptible mid-record).
        for (const auto &op : { fetchOps.src, fetchOps.tgt }) {
            if (op && !op->isFinished()) {
                op->cancel();
                if (!op->isFinished()) {
                    QEventLoop teardownLoop;
                    connect(op.data(), &SyncOperation::finished,
                            &teardownLoop, &QEventLoop::quit, Qt::QueuedConnection);
                    teardownLoop.exec();
                }
            }
        }
    }
    {
        // Cancellation recheck and Fix B share one locked scope, mirroring
        // both the target gate below and the pre-Task-3 shape of this gate
        // (each combined the two checks under a single QMutexLocker) —
        // deliberately not split, to keep source/target symmetric.
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            fetchOps.disposeBoth();
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
        if (fetchOps.src && fetchOps.src->state() == SyncOperation::Failed) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = fetchOps.src->errorString();
            m_currentResult.endTime = QDateTime::currentDateTime();
            // Under overlap the target fetch is already in flight (or
            // already finished) by the time the source gate fails. It is a
            // READ and mutates nothing, so discarding its result is safe.
            // The guard's destructor would catch it regardless, but
            // disposing explicitly here keeps intent visible at the exact
            // point of failure.
            fetchOps.disposeBoth();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
        srcFetchSucceeded = fetchOps.src && fetchOps.src->state() == SyncOperation::Succeeded;
        fetchOps.disposeSrc();
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
    // This ordering is exactly why overlapFetches is false under clobber:
    // the target fetch is kicked here, for the first time, only once the
    // wipe has completed — never before.
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

        // Only now is it safe to read the target. fetchOps.tgtDisposed is
        // still false here — under clobber the target was never kicked
        // (and therefore never disposed) before this point — so the guard
        // covers this freshly-kicked op exactly as it covered the overlap
        // case above.
        fetchOps.tgt = kickFetch(tgtBackend, tgtColId);
        awaitFetchOps({ fetchOps.tgt });
        if (m_cancelled.load(std::memory_order_acquire) && fetchOps.tgt && !fetchOps.tgt->isFinished()) {
            // Mirror the dead await<Op>'s teardown shape (H1.4 deletes it).
            fetchOps.tgt->cancel();
            if (!fetchOps.tgt->isFinished()) {
                QEventLoop teardownLoop;
                connect(fetchOps.tgt.data(), &SyncOperation::finished,
                        &teardownLoop, &QEventLoop::quit, Qt::QueuedConnection);
                teardownLoop.exec();
            }
        }
    }

    // --- Read target records (cross-thread) ---
    // Under overlap (non-clobber), fetchOps.tgt was already kicked and
    // awaited together with the source op above. Under clobber, it was
    // just kicked and awaited immediately above, post-wipe. Either way it
    // is fully settled by this point; this is the first place a
    // cancellation flagged in between (e.g. during source-record promotion)
    // is caught for the target side. From here on, fetchOps's destructor
    // has nothing left to do on either side once this scope disposes tgt
    // (src was already disposed in the combined source gate above).
    bool tgtFetchSucceeded = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_cancelled) {
            fetchOps.disposeTgt();
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
        if (fetchOps.tgt && fetchOps.tgt->state() == SyncOperation::Failed) {
            m_currentResult.success = false;
            m_currentResult.errorMessage = fetchOps.tgt->errorString();
            m_currentResult.endTime = QDateTime::currentDateTime();
            fetchOps.disposeTgt();
            emit syncCompleted(mappingId, m_currentResult);
            return true;
        }
        tgtFetchSucceeded = fetchOps.tgt && fetchOps.tgt->state() == SyncOperation::Succeeded;
        fetchOps.disposeTgt();
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
    QHash<QString, QString> idAliases;
    if (!request.override.clobber && m_baselineStore && m_baselineStoreAnchor) {
        Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
        QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, &baselineEntries, &idAliases]() {
            for (const auto &h : bbs->baselineHashesForMappingV4(mappingId)) {
                Kalburator::Engine::BaselineEntry e;
                e.id         = h.recordId;
                e.sourceHash = h.sourceHash;
                e.targetHash = h.targetHash;
                baselineEntries.append(e);
            }
            // O55: native-id → canonical-id aliases recorded by previous
            // passes' create writes (e.g. GenericSqliteBackend's
            // <collectionId>\x01<origId>). Without resolving these the join
            // below cannot see that both sides hold the same logical record.
            idAliases = bbs->idAliasesForMapping(mappingId);
        }, Qt::BlockingQueuedConnection);
    }

    // O56 heal: a store written by the v1.00 crossing defect can hold BOTH
    // alias directions for one logical record (and one baseline row per id
    // form). Resolve every native id to its component sink so the join sees
    // exactly one key per record; dedupe baseline entries that collapse onto
    // the same sink, preferring the row whose hashes match the CURRENT side
    // records (a stale row matches neither). In-memory only.
    idAliases = healedIdAliases(idAliases);
    if (baselineEntries.size() > 1) {
        QMultiHash<QString, int> bySink;
        for (int i = 0; i < baselineEntries.size(); ++i)
            bySink.insert(resolveIdThroughAliases(idAliases, baselineEntries[i].id), i);
        for (auto git = bySink.constBegin(); git != bySink.constEnd(); ++git) {
            const QList<int> group = bySink.values(git.key());
            if (group.size() < 2)
                continue;
            int keep = group.first();
            for (int idx : group) {
                const auto &e = baselineEntries[idx];
                bool curMatches = false;
                for (const auto &r : std::as_const(sourceRecords))
                    if (!e.sourceHash.isEmpty() && r.contentHash == e.sourceHash)
                        curMatches = true;
                for (const auto &r : std::as_const(targetRecords))
                    if (!e.targetHash.isEmpty() && r.contentHash == e.targetHash)
                        curMatches = true;
                bool keepMatches = false;
                {
                    const auto &k = baselineEntries[keep];
                    for (const auto &r : std::as_const(sourceRecords))
                        if (!k.sourceHash.isEmpty() && r.contentHash == k.sourceHash)
                            keepMatches = true;
                    for (const auto &r : std::as_const(targetRecords))
                        if (!k.targetHash.isEmpty() && r.contentHash == k.targetHash)
                            keepMatches = true;
                }
                if (curMatches && !keepMatches)
                    keep = idx;
            }
            for (int idx : group)
                if (idx != keep)
                    baselineEntries[idx] = {};  // dropped below
        }
        QList<Kalburator::Engine::BaselineEntry> consolidated;
        consolidated.reserve(baselineEntries.size());
        for (const auto &e : std::as_const(baselineEntries))
            if (!e.id.isEmpty())
                consolidated.append(e);
        baselineEntries = consolidated;
    }

    // --- Diff + merge (pure computation, worker thread) ---
    // Phase N.1: per-record diff via the domain plugin's canonical
    // RecordDiffer. Replaces the Phase Ia.5 batch helper blobBatchDiff.
    m_unifiedDiffer = dd->createCanonicalDiffer();
    m_unifiedMerger = dd->createCanonicalMerger();
    const EngineDiff engineDiff = perRecordDiff(
        sourceRecords, targetRecords, baselineEntries,
        canonical, *m_unifiedDiffer, idAliases);

    // O55 fail-loud guard: canonically-equal records under unjoined ids.
    // Applying this diff would cross-create the same logical record toward
    // both sides and churn until the peer empties — while reporting success
    // (the exact WildPalms hub data-loss shape). Refuse the mapping instead.
    if (!engineDiff.identityConflicts.isEmpty()) {
        const auto &first = engineDiff.identityConflicts.first();
        m_currentResult.success = false;
        m_currentResult.errorMessage = QStringLiteral(
            "identity conflict: source record '%1' and target record '%2' "
            "are canonically equal but tracked under unjoined ids with no "
            "baseline between them — refusing to cross-create (backend id "
            "namespace mismatch? see FINDINGS O55)")
                .arg(first.sourceRecord.id, first.targetRecord.id);
        m_currentResult.targetStats.errors += engineDiff.identityConflicts.size();
        qWarning() << "SyncEngineWorker:" << m_currentResult.errorMessage
                   << "-" << engineDiff.identityConflicts.size() << "pair(s)";
        m_currentResult.endTime = QDateTime::currentDateTime();
        emit syncCompleted(mappingId, m_currentResult);
        return true;
    }

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
    m_unifiedIdAliases = idAliases;
    // Bug B (Task 3): per-run accumulator for injected resolutions that were
    // actually applied. Reset alongside m_unifiedMerge — it is the same kind
    // of per-run merge state.
    m_unifiedAppliedConflictIds.clear();
    m_unifiedConflictIdx = 0;
    m_unifiedPolicy   = request.mapping.conflictPolicy;
    m_unifiedOverride = request.override;
    m_unifiedCanonical = canonical;
    // Bug A (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
    // resolution-handoff.md): the conflict walk sees canonical bytes only
    // (both record lists were promoted above), but ConflictInfo's payload
    // fields are contractually the backends' NATIVE encoding. Stash the
    // demotion pipelines — already compiled and proven non-null at the top
    // of this function — so unifiedHandleConflicts can convert back without
    // recompiling. m_unifiedSrcToCanon is the forward direction, kept here
    // for resumeAfterConflict's caller-supplied-merge path.
    m_unifiedSrcToCanon = srcToCanon;
    m_unifiedCanonToSrc = canonToSrc;
    m_unifiedCanonToTgt = canonToTgt;

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

namespace {

/// Bug A (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
/// resolution-handoff.md): demote one side's canonical record bytes back to
/// that backend's native encoding for display.
///
/// Never runs a pipeline over empty bytes: in a ModifyDelete conflict the
/// deleted side's record carries no data at all, and the engine's baselines
/// have been hash-only since Phase B4, so an empty payload is normal here,
/// not an error. Empty in, empty out — a demotion of nothing would at best
/// produce an empty-shell record that the UI would render as a real (blank)
/// version of the item.
QString demoteToNative(const std::optional<Kalburator::Shape::Pipeline> &pipe,
                       const QByteArray &canonicalBytes,
                       const QString &recordId)
{
    if (canonicalBytes.isEmpty())
        return QString();
    if (!pipe) {
        // dispatchSync proved all four pipelines non-null before the walk
        // began, so this is unreachable from a real run; guard rather than
        // deref so a future caller outside that lifetime degrades to "no
        // display data" instead of crashing.
        qWarning() << "SyncEngineWorker: no demotion pipeline for conflict record"
                   << recordId << "- ConflictInfo payload left empty";
        return QString();
    }
    const QByteArray native = pipe->apply(canonicalBytes);
    if (Kalburator::Sync::transcodeEmptiedRecord(canonicalBytes, native)) {
        qWarning() << "SyncEngineWorker: demoting conflict record" << recordId
                   << "from canonical to" << pipe->outputShape().encoding.toString()
                   << "produced empty bytes - ConflictInfo payload left empty";
        return QString();
    }
    return QString::fromUtf8(native);
}

/// Bug B staleness guard (locked decision 3): are these two lastModified
/// values the same instant?
///
/// SECOND granularity on purpose. A resolution rehydrated from
/// SyncConflictStore has round-tripped through
/// QDateTime::toString(Qt::ISODate), which drops sub-second precision — so a
/// millisecond-exact comparison would call every restored resolution stale and
/// silently defeat the whole restart-durability path. Comparing absolute
/// seconds also makes the check time-zone-agnostic.
///
/// Invalid == invalid counts as a match: a ModifyDelete conflict's deleted
/// side legitimately has no lastModified, and treating that as "changed"
/// would make such a conflict unresolvable forever. The cost is that a backend
/// which reports no lastModified at all gets no staleness protection — see
/// FINDINGS O51.
bool sameModifiedInstant(const QDateTime &recorded, const QDateTime &live)
{
    if (recorded.isValid() != live.isValid())
        return false;
    if (!recorded.isValid())
        return true;
    return recorded.toSecsSinceEpoch() == live.toSecsSinceEpoch();
}

} // namespace

// Bug A: ONE builder for BOTH AskUser branches below (monitored yield and
// unmonitored defer). Those two branches used to construct the same
// ConflictInfo field-by-field, twice; that duplication is precisely how
// docs/bugs/sync-conflict-store-duplicate-rows.md happened (the unmonitored
// copy silently lacked the two iCal fields). One builder, no drift.
ConflictInfo SyncEngineWorker::buildConflictInfo(const EngineDiffOp &op) const
{
    ConflictInfo info;
    info.mappingId       = m_currentRequest.mapping.id;
    info.sourceId        = op.record.id;
    info.targetId        = op.targetRecord.id.isEmpty() ? op.record.id
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

    // Bug A: op.record/op.targetRecord carry CANONICAL bytes at this point —
    // dispatchSync promotes BOTH fetched lists to canonical shape before the
    // diff so the per-record differ/merger can work generically, and
    // perRecordDiff copies those promoted records into the op verbatim. The
    // payload fields below are contractually each backend's NATIVE encoding,
    // so demote them back on the way out.
    info.sourceIcalData = demoteToNative(m_unifiedCanonToSrc, op.record.data,
                                         op.record.id);
    info.targetIcalData = demoteToNative(m_unifiedCanonToTgt, op.targetRecord.data,
                                         op.targetRecord.id);
    // The baseline is the source side's history, so it demotes through the
    // SOURCE pipeline. NOTE: this is empty in practice today — perRecordDiff
    // builds baselineRecord as a hash-only shell (perrecorddiff.cpp's
    // baselineShell) because baselines have carried per-side hashes, not
    // bytes, since Phase B4. Wired anyway so the 3-way diff lights up the
    // moment baseline bytes come back; see FINDINGS O48.
    info.baselineIcalData = demoteToNative(m_unifiedCanonToSrc,
                                           op.baselineRecord.data, op.record.id);
    // Name the encoding each payload is actually in, so a consumer does not
    // have to assume iCal (it is, for every calendar backend today — but the
    // fields carry whatever the backend's shape says).
    if (m_unifiedCanonToSrc)
        info.sourceEncoding = m_unifiedCanonToSrc->outputShape().encoding.toString();
    if (m_unifiedCanonToTgt)
        info.targetEncoding = m_unifiedCanonToTgt->outputShape().encoding.toString();

    return info;
}

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
            // Bug B (docs/2026-08-21-conflict-info-canonical-data-and-
            // unmonitored-resolution-handoff.md §B): BEFORE either the
            // Monitored yield or the Unmonitored defer, replay a resolution
            // the user already chose for this record. This is the injection
            // point of locked decision 1 — the whole reason a resolution
            // answered after its run finished ever reaches the data. It runs
            // through applyConflictResolution(), the same helper the Monitored
            // resume uses, so there is exactly one write mechanism
            // (campaign INVARIANTS §1).
            const QString recId = op.record.id.isEmpty() ? op.targetRecord.id
                                                         : op.record.id;
            const auto pendingIt = m_currentRequest.pendingResolutions.constFind(recId);
            if (pendingIt != m_currentRequest.pendingResolutions.constEnd()) {
                const PendingConflictResolution &pending = *pendingIt;
                // Staleness guard (locked decision 3). A resolution names a
                // version to keep; if either side has been edited since the
                // dialog was answered, "keep local" would silently clobber
                // that edit. Discard and re-present instead.
                if (!sameModifiedInstant(pending.sourceModified, op.record.lastModified) ||
                    !sameModifiedInstant(pending.targetModified, op.targetRecord.lastModified)) {
                    qWarning() << "SyncEngineWorker::unifiedHandleConflicts - discarding a"
                                  " stale conflict resolution for" << recId
                               << "in mapping" << m_currentRequest.mapping.id
                               << "- records changed since it was chosen (source"
                               << pending.sourceModified << "->" << op.record.lastModified
                               << ", target" << pending.targetModified << "->"
                               << op.targetRecord.lastModified
                               << "); presenting the conflict again";
                    m_currentResult.staleConflictIds.append(pending.conflictId);
                    // fall through to the normal AskUser handling below
                } else {
                    applyConflictResolution(op, pending.resolution, pending.mergedNative);
                    m_unifiedAppliedConflictIds.append(pending.conflictId);
                    qDebug() << "SyncEngineWorker::unifiedHandleConflicts - applied stored"
                                " resolution" << static_cast<int>(pending.resolution)
                             << "for" << recId;
                    continue;
                }
            }

            if (m_currentRequest.behavior == SyncEngine::SyncBehavior::Monitored) {
                // Yield: store position, set flag, emit signal, return.
                // resumeAfterConflict will re-enter this method from index i.
                m_unifiedConflictIdx = i;
                m_yieldedForConflict = true;

                const ConflictInfo info = buildConflictInfo(op);

                qDebug() << "SyncEngineWorker::unifiedHandleConflicts - yielding for:"
                         << op.record.id;
                emit conflictPauseRequested(info);
                return;
            } else {
                // Unmonitored AskUser: defer to next sync.
                const ConflictInfo info = buildConflictInfo(op);
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
    // Parallel-sync Task 1: no reset needed here — m_currentResult (and
    // therefore its appliedTargetRevision/appliedSourceRevision fields) is
    // already freshly default-constructed once per mapping run, before
    // dispatchSync is ever called (see the `m_currentResult = SyncResult{}`
    // assignment near the top of processSync), and
    // unifiedContinueAfterConflicts runs exactly once per mapping (its
    // three call sites are mutually exclusive, each returning immediately
    // after).

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

    // O56 (WildPalms followup): if the conflict walk deferred ANY conflict
    // unresolved (Unmonitored AskUser), apply NOTHING from this diff — not
    // the deferred record's own ops, and not the sibling toSource/toTarget
    // ops the walk accumulated on the way past. A run that reports failure
    // must not have committed data ("no data movement while a conflict is
    // pending"); previously a phantom delete sharing a diff with a deferred
    // conflict emptied WildPalms' hub while the conflict went unanswered.
    // The diff itself is untouched: answered resolutions replay through
    // pendingResolutions on the next run.
    if (!m_currentResult.unresolvedConflicts.isEmpty()) {
        qWarning() << "SyncEngineWorker: holding ALL writes for mapping"
                   << mappingId << "-"
                   << m_currentResult.unresolvedConflicts.size()
                   << "unresolved conflict(s) pending";
        m_unifiedMerge.finalSource.clear();
        m_unifiedMerge.finalTarget.clear();
        m_unifiedMerge.updatedBaselines.clear();
        m_currentResult.success = false;
        if (m_currentResult.errorMessage.isEmpty())
            m_currentResult.errorMessage = QStringLiteral(
                "%1 unresolved conflict(s); no data was written")
                    .arg(m_currentResult.unresolvedConflicts.size());
        m_currentResult.endTime = QDateTime::currentDateTime();
        m_unifiedDiffer.reset();
        m_unifiedMerger.reset();
        emit syncCompleted(mappingId, m_currentResult);
        return;
    }

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
        // E9.2 (sync-excellence campaign, O34); parallel-sync Task 1:
        // out-param — when non-null, filled with the settled
        // WriteOperation's resultRevision() (may stay empty; the backend
        // didn't compute one, or nothing was written). Callers store it
        // into m_currentResult.appliedTargetRevision /
        // m_currentResult.appliedSourceRevision so onWorkerSyncCompleted
        // (engine thread) can override the pre-fetch FreshSyncState value
        // with a fresher one for the side that actually wrote — carried on
        // the per-mapping SyncResult rather than engine-side worker state.
        QString *outRevision = nullptr,
        // O55: out-param — filled with the settled WriteOperation's
        // idAliases() (requested create-id → backend-assigned id). The
        // caller resolves post-write refetch hashes through it and
        // persists the pairs so later passes can join the sides.
        QHash<QString, QString> *outIdAliases = nullptr)
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
            // O46: surface the withheld write as a visible no-op (not a failure)
            // so consumers can badge the edge. Symmetric prefix by side — a
            // read-only source can't occur by construction, but a TwoWay
            // back-prop apply hitting the same gate reports as source-readonly.
            const bool isSource =
                (backendRegistryId == m_currentRequest.mapping.sourceBackend);
            m_currentResult.warnings << QStringLiteral("%1-readonly:%2")
                .arg(isSource ? QStringLiteral("source") : QStringLiteral("target"),
                     colId);
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
        // thread pair stopWorkerPool() is trying to unwind).
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

        // O55: same lifecycle as resultRevision() — read before deleteLater().
        if (outIdAliases && writeOp) {
            const auto aliases = writeOp->idAliases();
            for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it)
                outIdAliases->insert(it.key(), it.value());
        }

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
    // O55: requested-id → backend-assigned id for THIS run's create writes,
    // per side. Resolves the post-write refetch (keyed by backend-native
    // ids) onto the baseline-key ids, and is persisted after a successful
    // apply so the next pass's join can resolve them too.
    QHash<QString, QString> targetWriteAliases;
    QHash<QString, QString> sourceWriteAliases;

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
                   m_currentResult.targetStats, &m_currentResult.appliedTargetRevision,
                   &targetWriteAliases);
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
                   m_currentResult.sourceStats, &m_currentResult.appliedSourceRevision,
                   &sourceWriteAliases);
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
            // O55: the post-write refetch is keyed by backend-native ids;
            // a create that was re-namespaced (requested → assigned) reads
            // back under the ASSIGNED form. Resolve through this run's
            // aliases before concluding "no read-back hash" — otherwise a
            // prefixing backend's baseline would silently fall back to the
            // OTHER side's hash.
            auto readBackHash = [](const QHash<QString, QString> &byId,
                                   const QHash<QString, QString> &writeAliases,
                                   const QString &id) -> QString {
                if (byId.contains(id))
                    return byId.value(id);
                for (auto it = writeAliases.constBegin();
                     it != writeAliases.constEnd(); ++it) {
                    if (it.key() == id && byId.contains(it.value()))
                        return byId.value(it.value());
                }
                return {};
            };
            for (const auto &rec : m_unifiedMerge.updatedBaselines) {
                if (rec.id.isEmpty() || rec.isDeleted) continue;
                // O56: key the saved baseline at the record's STABLE SINK,
                // not this batch's requested id. A toSource op carries the
                // target-space id; keying it verbatim created a SECOND row
                // for a record whose sink-keyed row already existed — and
                // pass 2 then split the record across two keys again.
                const QString sinkId =
                    resolveIdThroughAliases(m_unifiedIdAliases, rec.id);
                Kalburator::Engine::BaselineEntry e = toSave.value(sinkId);
                e.id = sinkId;
                if (!readBackHash(writtenSourceHash, sourceWriteAliases, rec.id).isEmpty())
                    e.sourceHash = readBackHash(writtenSourceHash, sourceWriteAliases, rec.id);
                else if (e.sourceHash.isEmpty())
                    e.sourceHash = rec.contentHash;
                if (!readBackHash(writtenTargetHash, targetWriteAliases, rec.id).isEmpty())
                    e.targetHash = readBackHash(writtenTargetHash, targetWriteAliases, rec.id);
                else if (e.targetHash.isEmpty())
                    e.targetHash = rec.contentHash;
                toSave.insert(sinkId, e);
            }
            // O56: persist this run's create aliases — but only ones that do
            // NOT cross the existing anchor. The canonical side is
            // chain-resolved to its sink first: an alias whose requested id
            // already resolves to the same sink as the assigned id is a
            // no-op (the v1.00 defect persisted it anyway and crossed the
            // map, splitting the next pass's join). In-memory map updated so
            // later lookups in this function see the accepted rows.
            QList<QPair<QString, QString>> aliasPairs;  // (nativeId, canonicalId)
            for (auto it = targetWriteAliases.constBegin();
                 it != targetWriteAliases.constEnd(); ++it) {
                aliasPairs.append({it.value(), it.key()});
            }
            for (auto it = sourceWriteAliases.constBegin();
                 it != sourceWriteAliases.constEnd(); ++it) {
                aliasPairs.append({it.value(), it.key()});
            }
            QList<QPair<QString, QString>> acceptedAliasPairs;
            for (const auto &pair : std::as_const(aliasPairs)) {
                const QString nativeId    = pair.first;
                const QString sinkCanonical =
                    resolveIdThroughAliases(m_unifiedIdAliases, pair.second);
                if (nativeId == sinkCanonical)
                    continue;  // already anchored here
                if (m_unifiedIdAliases.value(nativeId) == sinkCanonical)
                    continue;  // row already present
                acceptedAliasPairs.append({nativeId, sinkCanonical});
                m_unifiedIdAliases.insert(nativeId, sinkCanonical);
            }
            QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, toSave, acceptedAliasPairs]() {
                // VP.b (W2): persist the whole batch atomically — a
                // master+exception pair must land in ONE transaction (a
                // mid-loop failure rolls the pair back instead of leaving a
                // half-written master or a baseline whose exception is
                // missing). Always wrapped: WAL makes the transaction cheap,
                // and it is correct for a single pair or many records alike.
                // Each setter's bool is ANDed so any failure rolls back all.
                bbs->transaction([&]() {
                    bool allOk = true;
                    for (auto it = toSave.constBegin(); it != toSave.constEnd(); ++it) {
                        allOk = bbs->setBaselineHashesV4(mappingId, it.value().id,
                                                         it.value().sourceHash,
                                                         it.value().targetHash)
                                && allOk;
                    }
                    for (const auto &pair : acceptedAliasPairs)
                        allOk = bbs->setIdAlias(mappingId, pair.first, pair.second)
                                && allOk;
                    return allOk;
                });
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
        // Bug B (conflict-resolution-repair Task 3), consume-once: report the
        // injected resolutions this run applied ONLY here, on the
        // write-succeeded branch. SyncEngine deletes their SyncConflictStore
        // rows when it sees them, so reporting one whose write failed would
        // throw the user's choice away and leave the conflict standing. Same
        // reasoning as the baseline rule immediately above.
        m_currentResult.appliedConflictIds = m_unifiedAppliedConflictIds;

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
