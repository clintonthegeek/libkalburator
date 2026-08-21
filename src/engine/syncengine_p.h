#ifndef KALBURATOR_SYNCENGINE_P_H
#define KALBURATOR_SYNCENGINE_P_H

// Private implementation header for SyncEngine.
//
// Architectural-redress Plan 1 Task 2 (2026-05-29): extracted out of
// syncengine.h so that SyncEngineWorker is no longer part of any
// exported header set. Only syncengine.cpp may include this file.
//
// Anyone else (PlanStan, WildPalms, tests) should depend on the public
// SyncEngine API in syncengine.h. SyncEngineWorker is an implementation
// detail of the engine — it has no callers outside this translation unit.

#include "enginediff.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "pipeline.h"
#include "shape.h"
#include "synctypes.h"
#include "../sync/syncoperation.h"
#include "../sync/writeoperation.h"  // E5.3: SyncBackendBase::applyRecords() return type
#include "../sync/writerbatch.h"     // E5.3: SyncBackendBase::applyRecords() batch parameter type
#include "shaperegistries.h"
#include "syncengine.h"

#include <QObject>
#include <QList>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QPair>
#include <QMutex>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <optional>
#include <memory>
#include <type_traits>

namespace Kalburator::Storage {
class BaselineStore;
} // namespace Kalburator::Storage

namespace Kalburator::Conflict {
class IMassDeleteGuard;
} // namespace Kalburator::Conflict

namespace Kalburator::Sync {
class SyncBackendBase;
class BackendRegistry;
class ISyncHost;
class ICalendarCollection;
} // namespace Kalburator::Sync

namespace Kalburator::Shape {
class DomainOperations;
} // namespace Kalburator::Shape

namespace Kalburator::Engine {

using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::ExecutionOverride;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ConflictInfo;
using Kalburator::Sync::SyncResult;
using Kalburator::Sync::SyncOperation;
using Kalburator::Sync::SyncBackendBase;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::ICalendarCollection;
using Kalburator::Sync::WriteOperation;  // E5.3: SyncBackendBase::applyRecords() return type
using Kalburator::Sync::WriterBatch;     // E5.3: SyncBackendBase::applyRecords() batch parameter type

/**
 * @brief Internal worker class — runs sync operations on a background thread.
 *
 * Phase F1 Task 8 (2026-04-29): folded into the engine's translation unit.
 * Architectural-redress Plan 1 Task 2 (2026-05-29): moved out of the public
 * syncengine.h header and into this private header. SyncEngine owns one of
 * these and moves it to its private QThread; the worker reaches collaborators
 * (baseline stores, host, calendar collection) via setDependencies.
 *
 * Sync phases handled here:
 * - Fetching records from source and target backends
 * - Computing 3-way diff via the unified dispatchSync path
 * - Handling conflicts based on mode (monitored/unmonitored)
 * - Applying changes to backends via the RecordWriter / IBlobBackend path
 * - Updating baselines
 *
 * Two sync modes are supported:
 * - Unmonitored: Conflicts are queued for later resolution, sync continues
 * - Monitored: Worker pauses on each conflict until user resolves it
 *
 * Signals use Qt::QueuedConnection for thread-safe cross-thread communication.
 */
class SyncEngineWorker final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Request for a sync operation.
     *
     * Plan 1 Task 2 (2026-05-29): the worker's own Mode enum was
     * deleted in favour of SyncEngine::SyncBehavior — the public API
     * already exposed an identical 2-value enum (Monitored /
     * Unmonitored), so the worker's parallel definition was a
     * duplicate. The field name is `behavior` to match the public
     * API verb.
     */
    struct Request {
        SyncMapping mapping;        ///< The sync mapping to execute
        SyncEngine::SyncBehavior behavior =
            SyncEngine::SyncBehavior::Unmonitored;  ///< Conflict handling mode
        bool useQuickPath = false;  ///< Use fast 2-way diff (no baselines)
        QString collectionId;       ///< Collection ID for backend operations
        ExecutionOverride override; ///< Task 9: per-call direction override (Default = bidirectional)
    };

    explicit SyncEngineWorker(const Kalburator::Shape::ShapeRegistries &shape,
                              QObject *parent = nullptr);
    ~SyncEngineWorker() override;

    /**
     * @brief Set dependencies before moving to thread.
     * Must be called before moveToThread().
     *
     * @param host                  Sync host (controller).
     * @param collection            Calendar collection.
     * @param baselineStore         Baseline store (SQLite-backed, thread-
     *                              affine to its creator thread).
     * @param baselineStoreAnchor   QObject living on the same thread as
     *                              baselineStore — used as a thread anchor
     *                              for QMetaObject::invokeMethod(...,
     *                              BlockingQueuedConnection). Typically the
     *                              owning SyncEngine. Plan 1 Task 2
     *                              (2026-05-29) replaces the previous
     *                              SyncEngine* back-pointer with this
     *                              narrower role-typed handle.
     * @param massDeleteGuard       Optional mass-delete guard. nullptr = no
     *                              guard (deletes proceed unconditionally).
     *                              Previously reached via the back-pointer
     *                              as m_engine->massDeleteGuard().
     * @param registry              Backend registry — the worker fetches
     *                              backends from it as neutral
     *                              `SyncBackendBase*` (v0.66, WildPalms
     *                              dispatchSync RFC). Replaces the previous
     *                              `m_controller->backendById()` lookups,
     *                              whose calendar-typed `SyncBackend*`
     *                              return could not represent base-only
     *                              backends post-Plan-3.
     */
    void setDependencies(ISyncHost *host,
                         ICalendarCollection *collection,
                         Kalburator::Storage::BaselineStore *baselineStore = nullptr,
                         QObject *baselineStoreAnchor = nullptr,
                         Kalburator::Conflict::IMassDeleteGuard *massDeleteGuard = nullptr,
                         Kalburator::Sync::BackendRegistry *registry = nullptr);

public slots:
    /**
     * @brief Process a sync operation (called from worker thread).
     */
    void processSync(const SyncEngineWorker::Request &request);

    /**
     * @brief Plan 1 Task 2 (2026-05-29): update the mass-delete guard
     * after construction. Previously the worker reached the live guard
     * via SyncEngine::massDeleteGuard() through the back-pointer; now
     * SyncEngine pushes updates here via a queued connection so the
     * worker sees a consistent snapshot per sync cycle. Queued from the
     * engine thread (where SyncEngine::setMassDeleteGuard is called) to
     * the worker thread (where dispatchSync reads the value).
     */
    void setMassDeleteGuardFromEngine(Kalburator::Conflict::IMassDeleteGuard *guard);

    /**
     * @brief H4 (O16): the fast-path pre-pass, moved off the engine/caller
     * thread. Batch-queries fresh revisions per backend via
     * Sync::ChangeDetection (marshaled onto each backend's own thread —
     * that marshal now blocks the WORKER, never the engine/caller
     * thread), compares against @p storedTokens, and computes the skip
     * set + per-mapping FreshSyncState exactly as the pre-H4
     * SyncEngine::prepareSyncFastPath did. Emits fastPathReady() back to
     * the engine (queued) when done. @p storedTokens maps mappingId to
     * (source token, target token), read by the engine from BaselineStore
     * before dispatch (fast, local SQLite, engine-thread-affine).
     */
    void prepareFastPath(const QList<SyncMapping> &mappings,
                         const QHash<QString, QPair<QString, QString>> &storedTokens,
                         bool skipEnabled);

    /**
     * @brief Resume after user resolves a conflict (monitored mode).
     * Called from main thread when user completes conflict resolution dialog.
     */
    void resumeAfterConflict(ConflictResolution resolution, const QString &mergedIcal);

    /**
     * @brief Cancel the current sync operation.
     */
    void cancel();

    /**
     * @brief F2 Task 16: invoked via queued connection from
     * SyncEngine::onCancelObserved when a QFutureWatcher::canceled
     * fires on the engine side. Sets m_cancelled and emits
     * cancellationObserved.
     */
    void observeCancel();

    /**
     * @brief E3 (O33a / audit C4): resets the worker's own cancellation
     * flag for a new run. Previously this reset lived, wrongly, at the
     * top of every processSync() call — which could erase a cancel that
     * legitimately landed after a mapping was already queued but before
     * that mapping's processSync began (e.g. SyncEngine::
     * stopWorkerThread()'s direct, non-queued m_worker->cancel() call
     * racing an already-posted processSyncRequested). The reset now
     * happens exactly once per run, dispatched (queued) from
     * SyncEngine's run entry points — driveQueue() and
     * processSingleMapping() — before the first mapping of that run is
     * ever requested. processSync() itself now only checks the flag; see
     * its definition.
     */
    void resetCancellationFlag();

    /**
     * @brief E3 (O33b / audit C4): runs the DecSync active-controller
     * loop that used to execute inline on driveQueue()'s caller thread —
     * a §1 role violation (backend touches belong on the worker thread,
     * never the caller/GUI thread). Dispatched from
     * SyncEngine::driveQueue() via the same command-channel pattern as
     * fastPathRequested/prepareFastPath; emits activeControllersReady()
     * when every controller has run so the engine can resume drive-queue
     * setup. Non-owning: the controllers remain owned by whoever
     * registered them (SyncEngine::registerActiveController).
     */
    void runActiveControllers(const QList<DecSyncActiveController*> &controllers);

private slots:
    /**
     * @brief F2 Task 20: handle cancellation that arrives while the
     * worker is yielded for a monitored AskUser conflict.
     *
     * The conflict pause is not a QEventLoop — it's a state-machine
     * yield (`m_yieldedForConflict = true; return;` in
     * handleConflicts()). When cancellation observation fires while
     * yielded, this slot tears the sync down via the cancelled-result
     * path and emits syncCompleted. The conflict that was waiting on
     * user resolution is left untouched in the persistent
     * SyncConflictStore so the next run picks it up.
     *
     * Connected to `cancellationObserved` via DirectConnection in the
     * worker constructor; both signal and slot run on the worker
     * thread (observeCancel() is itself queued onto the worker
     * thread), so direct invocation is safe.
     */
    void onCancelDuringConflictPause();

public:
    /// F2 Task 19: lock-free observation of the cancellation flag,
    /// for use by the cancel oracle installed on the domain adapter.
    /// Reads with acquire ordering — paired with the release store
    /// in observeCancel().
    bool isCancelled() const noexcept
    {
        return m_cancelled.load(std::memory_order_acquire);
    }

signals:
    void syncStarted(const QString &mappingId);
    void phaseChanged(const QString &mappingId, int phase);
    void fetchProgress(const QString &calendarId, int current, int total);
    void writeProgress(const QString &calendarId, int current, int total);
    void conflictDetected(const ConflictInfo &conflict);
    void conflictPauseRequested(const ConflictInfo &conflict);
    void syncCompleted(const QString &mappingId, const SyncResult &result);
    void syncError(const QString &mappingId, const QString &errorMessage);
    void transcodingWarning(const QString &calendarId,
                            const QString &uid,
                            const QStringList &warnings);

    // F2 Task 16: emitted from observeCancel() slot when cancellation
    // is forwarded from the engine side (via Task 17's queued
    // connection). Internal to the engine/worker pair. Used to wake
    // the fetch gate loops (dispatchSync) and the conflict-pause loop.
    void cancellationObserved();

    // Plan 1 Task 2 (2026-05-29) — engine→worker command-channel signals.
    // These replace the string-form QMetaObject::invokeMethod(m_worker,
    // "processSync", ...) calls that previously dispatched commands from
    // SyncEngine to the worker. SyncEngine emits these via
    //   emit m_worker->processSyncRequested(request);
    // which Qt routes through the engine→worker QueuedConnection set up
    // in setupWorkerConnections(). All three are queued cross-thread
    // (worker thread reception) and carry no return value.
    //
    // Q_SIGNALS access is public in Qt; engine-side emit is the
    // intended pattern. Callers other than SyncEngine must NOT emit these.
    void processSyncRequested(const SyncEngineWorker::Request &request);
    void observeCancelRequested();
    void resumeAfterConflictRequested(ConflictResolution resolution,
                                       const QString &mergedIcal);

    /**
     * @brief H4 (O16): dispatches the fast-path pre-pass to the worker.
     * Emitted by SyncEngine::driveQueue via
     *   emit m_worker->fastPathRequested(mappings, storedTokens, skipEnabled);
     * connected QueuedConnection to prepareFastPath(), same command-channel
     * pattern as processSyncRequested above.
     */
    void fastPathRequested(const QList<SyncMapping> &mappings,
                           const QHash<QString, QPair<QString, QString>> &storedTokens,
                           bool skipEnabled);

    /**
     * @brief H4 (O16): reports the fast-path pre-pass result back to the
     * engine thread (queued). Connected to SyncEngine::onFastPathReady.
     */
    void fastPathReady(const QSet<QString> &skipped,
                       const QMap<QString, SyncEngine::FreshSyncState> &fresh);

    /**
     * @brief E3 (O33b): dispatches the DecSync active-controller loop to
     * the worker. Emitted by SyncEngine::driveQueue via
     *   emit m_worker->activeControllersRequested(controllers);
     * connected QueuedConnection to runActiveControllers(), same
     * command-channel pattern as fastPathRequested above.
     */
    void activeControllersRequested(const QList<DecSyncActiveController*> &controllers);

    /**
     * @brief E3 (O33b): reports that every active controller has run
     * (queued, worker thread -> engine thread). Connected to
     * SyncEngine::onActiveControllersReady.
     */
    void activeControllersReady();

private:
    void runPropertyPhase(Kalburator::Shape::DomainOperations *ops,
                          SyncBackendBase *src,
                          SyncBackendBase *tgt,
                          const QString &srcCollectionId,
                          const QString &tgtCollectionId,
                          const QVariantMap &baseline,
                          const SyncMapping &mapping);

    // First-sync dispatch via the engine's blob mirror (Phase D Task 21)
    bool dispatchFirstSync(const Request &request);
    void harvestBaselinesAfterFirstSync(const Request &request);

    // Unified domain dispatch (Phase Ia.5 Task 8). Compiles per-mapping
    // shape pipelines and runs the diff/merge/apply path.
    bool dispatchSync(const Request &request);

    // Unified-path AskUser pause/resume helpers.
    void unifiedHandleConflicts();
    void unifiedContinueAfterConflicts();

    /// Bug A: build the ConflictInfo the AskUser branches present, from a
    /// Conflict @p op plus m_currentRequest. Demotes each side's canonical
    /// bytes (and the baseline) back to that backend's native encoding via
    /// m_unifiedCanonToSrc / m_unifiedCanonToTgt, and names the encoding
    /// each payload is in. Shared by BOTH AskUser branches of
    /// unifiedHandleConflicts (monitored yield and unmonitored defer) so
    /// they cannot drift apart again — that drift is exactly what
    /// docs/bugs/sync-conflict-store-duplicate-rows.md was.
    ConflictInfo buildConflictInfo(const EngineDiffOp &op) const;

    /// Conflict-resolution-repair Task 2: fold ONE resolved conflict into
    /// m_unifiedMerge — the only code in the engine that turns a
    /// ConflictResolution into records to write.
    ///
    /// Extracted verbatim (bar Bugs C and D) from resumeAfterConflict()'s
    /// switch so it is independent of the yielded-run state machine:
    /// @p op is passed in rather than read from
    /// m_unifiedDiff.toTarget[m_unifiedConflictIdx], because Task 3's
    /// Unmonitored injection path calls this mid-walk with an op that is
    /// not the yielded one. It still reads the per-run state both callers
    /// share (m_unifiedCanonical / m_unifiedMerger / m_unifiedSrcToCanon /
    /// m_currentRequest) and writes m_unifiedMerge / m_currentResult.
    ///
    /// @param mergedNative the caller's hand-merged payload in the SOURCE
    ///        backend's native encoding, honoured only for CustomMerge and
    ///        only when non-empty (Bug C); empty falls back to the
    ///        automatic m_unifiedMerger.
    void applyConflictResolution(const EngineDiffOp &op,
                                 ConflictResolution resolution,
                                 const QString &mergedNative);

    /// Parallel-sync Task 3: block until every non-null op in @p ops is
    /// finished, or until cancellation is observed. Replaces the two
    /// hand-rolled per-side await loops in dispatchSync's fetch gates with
    /// one helper that can wait on one op (clobber's sequential path) or
    /// several at once (the overlapped source+target fetch).
    void awaitFetchOps(const QList<QPointer<SyncOperation>> &ops);

    QMutex m_mutex;

    // F2 Task 14: cancellation observation flag. Set by observeCancel()
    // slot (added in Task 17) when QFutureWatcher::canceled fires on the
    // engine side and the engine forwards via queued connection.
    // Upgraded from plain bool so concurrent observers see writes
    // without taking m_mutex.
    std::atomic<bool> m_cancelled{false};
    bool m_yieldedForConflict = false;

    // Unified-path pause/resume state (valid while m_yieldedForConflict is true).
    EngineDiff m_unifiedDiff;
    EngineMerge m_unifiedMerge;
    int m_unifiedConflictIdx = 0;
    ConflictResolution m_unifiedPolicy = ConflictResolution::SourceWins;
    ExecutionOverride m_unifiedOverride;
    Kalburator::Shape::Shape m_unifiedCanonical;
    // Bug A (docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-
    // resolution-handoff.md): dispatchSync promotes BOTH fetched record
    // lists to canonical shape before diffing, so every EngineDiffOp the
    // conflict walk sees carries canonical bytes. The conflict UI needs
    // each side's NATIVE encoding back, and resumeAfterConflict needs the
    // forward direction for a caller-supplied merge. dispatchSync already
    // compiles all four pipelines (and proves them non-null) before the
    // walk begins; stash the three the conflict code needs alongside the
    // canonical shape rather than recompiling them per conflict.
    // (m_unifiedSrcToCanon is the promote direction, used by
    // resumeAfterConflict's CustomMerge path — Task 2.)
    std::optional<Kalburator::Shape::Pipeline> m_unifiedSrcToCanon;
    std::optional<Kalburator::Shape::Pipeline> m_unifiedCanonToSrc;
    std::optional<Kalburator::Shape::Pipeline> m_unifiedCanonToTgt;
    // Phase N.1: domain plugin's canonical differ + merger, acquired once per
    // dispatchSync and retained across AskUser pause/resume.
    std::unique_ptr<Kalburator::Shape::RecordDiffer> m_unifiedDiffer;
    std::unique_ptr<Kalburator::Shape::RecordMerger> m_unifiedMerger;

    QElapsedTimer m_totalTimer;
    QElapsedTimer m_phaseTimer;

    const Kalburator::Shape::ShapeRegistries &m_shape;
    ISyncHost *m_controller = nullptr;
    // v0.66 (WildPalms dispatchSync RFC): backend lookups go through the
    // registry (neutral SyncBackendBase*), not ISyncHost::backendById
    // (calendar-typed SyncBackend*, which cannot represent base-only
    // backends post-Plan-3). Same read pattern hosts already used from
    // this thread, so no new threading semantics.
    BackendRegistry *m_registry = nullptr;
    Kalburator::Storage::BaselineStore *m_baselineStore = nullptr;
    ICalendarCollection *m_collection = nullptr;

    // Plan 1 Task 2 (2026-05-29): thread anchor used to marshal
    // BaselineStore access back to the thread that owns it (BaselineStore
    // wraps SQLite and is thread-affine to its creator). Replaces the
    // previous SyncEngine *m_engine back-pointer. The worker no longer
    // knows it is owned by a SyncEngine — it only knows there is a
    // QObject living on the right thread.
    QPointer<QObject> m_baselineStoreAnchor;

    // Plan 1 Task 2 (2026-05-29): mass-delete guard, previously reached
    // through m_engine->massDeleteGuard(). Non-owning; consumer must
    // outlive the worker. nullptr = no guard.
    Kalburator::Conflict::IMassDeleteGuard *m_massDeleteGuard = nullptr;

    Request m_currentRequest;
    SyncResult m_currentResult;
};

} // namespace Kalburator::Engine

Q_DECLARE_METATYPE(Kalburator::Engine::SyncEngineWorker::Request)
// SyncEngineWorker::Mode was collapsed into SyncEngine::SyncBehavior in
// Plan 1 Task 2 (2026-05-29). SyncBehavior already has Q_ENUM in
// syncengine.h; no separate Q_DECLARE_METATYPE needed for the worker.

// H4: SyncEngine::FreshSyncState's Q_DECLARE_METATYPE lives in the public
// syncengine.h, right after the struct — moc-generated code for SyncEngine
// (built from syncengine.h alone, without this private header) must see it
// there, not here, or it implicitly instantiates the unregistered-type
// fallback first and later conflicts with an explicit declaration in this
// file. QList<SyncMapping>, QSet<QString>, and
// QHash<QString,QPair<QString,QString>> need no declaration at all — Qt's
// generic QMetaTypeId templates for QList/QSet/QHash/QMap/QPair (qmetatype.h's
// Q_DECLARE_SEQUENTIAL_CONTAINER_METATYPE / Q_DECLARE_ASSOCIATIVE_CONTAINER_
// METATYPE / std::pair machinery) already cover them once their element
// types are registered. qRegisterMetaType calls for all of these live
// alongside the other engineWorkerMetatypesRegistered registrations in
// syncengine.cpp.

#endif // KALBURATOR_SYNCENGINE_P_H
