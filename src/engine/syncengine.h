#ifndef KALBURATOR_SYNCENGINE_H
#define KALBURATOR_SYNCENGINE_H

#include "enginediff.h"
#include "shape.h"
#include "synctypes.h"
#include "syncdiff.h"
#include "conflicthandlerregistry.h"
#include "mappingscheduler.h"
#include "syncenginefuture.h"
#include "transcodingrouter.h"
#include "syncoperation.h"  // F2 Task 16: required by await<Op> template
#include <QObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QFuture>
#include <QFutureInterface>
#include <QFutureWatcher>
#include <atomic>
#include <type_traits>

namespace Kalburator::Storage {
class BaselineStore;
} // namespace Kalburator::Storage

namespace Kalburator::Sync {

class BackendRegistry;
// Phase K.5: BlobBaselineStore moved to Kalburator::Storage::BaselineStore.
// Alias preserved in Sync namespace for the cutover window (shim file removed in Task 13);
// internal engine code cuts over to Storage::BaselineStore in Task 2.
using BlobBaselineStore = ::Kalburator::Storage::BaselineStore;
class IBlobBackend;
class ISyncHost;
class SyncBackend;
class ICalendarCollection;
class CalendarBaselineStore;
class SyncConflictStore;
class ISyncConfigStore;
class ConflictManager;
class DecSyncActiveController;
class SyncEngine;

// ExecutionOverride lives in synctypes.h.

namespace QSyncCore {
    class ConflictStore;
    struct ConflictPolicy;
}

} // namespace Kalburator::Sync

namespace Kalburator::Shape {
class DomainPlugin;
}

namespace Kalburator::Sync {

/**
 * @brief Internal worker class — runs sync operations on a background thread.
 *
 * Phase F1 Task 8 (2026-04-29): Formerly a standalone worker class in
 * src/calendar/ (deleted in this task). Folded into the engine's translation
 * unit. SyncEngine instantiates one of these and moves it to its
 * private QThread; the worker reaches collaborators (baseline stores, host,
 * calendar collection) via setDependencies.
 *
 * Sync phases handled here:
 * - Fetching records from source and target backends
 * - Computing 3-way diff via the unified dispatchSync path
 * - Handling conflicts based on mode (monitored/unmonitored)
 * - Applying changes to backends via CalendarPluginWriter / IBlobBackend
 * - Updating baselines
 *
 * Two sync modes are supported:
 * - Unmonitored: Conflicts are queued for later resolution, sync continues
 * - Monitored: Worker pauses on each conflict until user resolves it
 *
 * Signals use Qt::QueuedConnection for thread-safe cross-thread communication.
 */
class SyncEngineWorker : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Sync mode determining conflict handling behavior.
     */
    enum class Mode {
        Monitored,      ///< Pause on each conflict for user resolution
        Unmonitored     ///< Queue conflicts and continue (deferred resolution)
    };
    Q_ENUM(Mode)

    /**
     * @brief Request for a sync operation.
     */
    struct Request {
        SyncMapping mapping;        ///< The sync mapping to execute
        Mode mode = Mode::Unmonitored;  ///< How to handle conflicts
        bool useQuickPath = false;  ///< Use fast 2-way diff (no baselines)
        QString collectionId;       ///< Collection ID for backend operations
        ExecutionOverride override; ///< Task 9: per-call direction override (Default = bidirectional)
    };

    explicit SyncEngineWorker(const TranscodingRouter &router, QObject *parent = nullptr);
    ~SyncEngineWorker() override;

    /**
     * @brief Set dependencies before moving to thread.
     * Must be called before moveToThread().
     */
    void setDependencies(ISyncHost *host,
                         CalendarBaselineStore *calendarBaselines,
                         ICalendarCollection *collection,
                         BlobBaselineStore *blobBaselines = nullptr,
                         SyncEngine *engine = nullptr);

public slots:
    /**
     * @brief Process a sync operation (called from worker thread).
     */
    void processSync(const SyncEngineWorker::Request &request);

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
    // nested QEventLoops in await<> and the conflict-pause loop.
    void cancellationObserved();

private:
    /// F2 Task 16: run an inner QEventLoop until the operation
    /// finishes OR cancellation is observed. On cancellation, request
    /// the operation's own cancel() and re-enter the loop briefly
    /// waiting for the operation to actually settle (operations are
    /// not pre-emptible at the per-record level once started).
    ///
    /// Returns the same op pointer (caller still owns; typical
    /// idiom: `auto *op = await(backend->fetchItems(id));` then
    /// inspect op->state(), then op->deleteLater()).
    ///
    /// CRITICAL: must be called from the worker thread. Calling
    /// from any other thread will run the inner QEventLoop on
    /// that thread, defeating the cancellation observation
    /// mechanism.
    template <typename Op>
    Op* await(Op *op)
    {
        static_assert(
            std::is_base_of_v<SyncOperation, Op>,
            "await<Op> requires Op to derive from SyncOperation");

        if (!op) return op;
        if (op->isFinished()) return op;

        QEventLoop loop;
        QObject::connect(op, &SyncOperation::finished,
                         &loop, &QEventLoop::quit);
        QObject::connect(this, &SyncEngineWorker::cancellationObserved,
                         &loop, &QEventLoop::quit);
        loop.exec();

        if (m_cancelled.load(std::memory_order_acquire) && !op->isFinished()) {
            op->cancel();
            // Re-enter briefly waiting for the operation's own
            // teardown (operations are not pre-emptible at the
            // per-record level once started).
            if (!op->isFinished()) {
                QEventLoop teardownLoop;
                QObject::connect(op, &SyncOperation::finished,
                                 &teardownLoop, &QEventLoop::quit);
                teardownLoop.exec();
            }
        }

        return op;
    }

    void runPropertyPhase(Kalburator::Shape::DomainPlugin *plugin,
                          SyncBackend *src,
                          SyncBackend *tgt,
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

    QElapsedTimer m_totalTimer;
    QElapsedTimer m_phaseTimer;

    const TranscodingRouter &m_router;
    ISyncHost *m_controller = nullptr;
    CalendarBaselineStore *m_calendarBaselines = nullptr;
    BlobBaselineStore *m_blobBaselines = nullptr;
    ICalendarCollection *m_collection = nullptr;
    // Back-pointer to the owning SyncEngine. dispatchSync uses
    // QMetaObject::invokeMethod(m_engine, ...) to marshal baseline-store
    // access back to the engine thread.
    SyncEngine *m_engine = nullptr;

    Request m_currentRequest;
    SyncResult m_currentResult;
};

/**
 * @brief Coordinates sync operations between backends according to sync mappings.
 *
 * SyncEngine implements the core sync algorithm:
 *
 * 1. Load sync mappings from KalbConfigManager
 * 2. For each enabled mapping:
 *    a. Load records from source backend
 *    b. Load records from target backend
 *    c. Load baselines from CalendarBaselineStore
 *    d. Compute 3-way diff
 *    e. Apply changes based on sync mode
 *    f. Handle conflicts according to policy
 *    g. Update baselines in CalendarBaselineStore
 *
 * Persistent storage is split across CalendarBaselineStore (baselines,
 * last-sync time, property baselines) and SyncConflictStore (conflict records).
 */
class SyncEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Phases of an async sync operation.
     *
     * The sync coordinator progresses through these phases without blocking:
     * Idle → FetchingSource → FetchingTarget → Processing → Complete
     */
    enum class SyncPhase {
        Idle,               ///< No sync in progress
        FetchingSource,     ///< Fetching items from source backend (async)
        FetchingTarget,     ///< Fetching items from target backend (async)
        Processing,         ///< Computing diff and applying changes
        Complete            ///< Sync finished
    };
    Q_ENUM(SyncPhase)

    /**
     * @brief Sync mode determining conflict handling behavior.
     *
     * - Monitored: Pauses on each conflict for immediate user resolution
     * - Unmonitored: Queues conflicts for later resolution via dock widget
     */
    enum class SyncBehavior {
        Monitored,      ///< Pause on conflict, show dialog, resume after resolution
        Unmonitored     ///< Queue conflicts and continue (deferred resolution)
    };
    Q_ENUM(SyncBehavior)

    explicit SyncEngine(BackendRegistry *registry,
                              ISyncHost *host,
                              QObject *parent = nullptr);
    ~SyncEngine() override;

    /**
     * @brief Set the CalendarBaselineStore for iCal/property baselines.
     *
     * Must be called before runSync() to enable baseline tracking.
     * If not set, sync operations still work but every sync is treated
     * as a first sync (no 3-way merge).
     */
    void setCalendarBaselineStore(CalendarBaselineStore *store);
    CalendarBaselineStore *calendarBaselineStore() const { return m_calendarBaselines; }

    /**
     * @brief Set the BlobBaselineStore for per-record hash-skip (Phase D Task 20).
     *
     * When set, the engine's subsequent-sync blob fetch skips records whose
     * contentHash matches the stored baseline — avoiding unnecessary merge work
     * for unchanged records.
     */
    void setBlobBaselineStore(BlobBaselineStore *store);
    BlobBaselineStore *blobBaselineStore() const { return m_blobBaselines; }

    /**
     * @brief Set the SyncConflictStore for persistent conflict records.
     */
    void setSyncConflictStore(SyncConflictStore *store);
    SyncConflictStore *syncConflictStore() const { return m_conflictStore; }

    /**
     * @brief Set the ConflictManager for handling user-resolved conflicts.
     *
     * When set, conflicts with AskUser policy will be presented to the user
     * via the ConflictManager's dialog, and the resolution will be applied
     * immediately. If not set, such conflicts are recorded but not resolved.
     */
    void setConflictManager(ConflictManager *manager) { m_conflictManager = manager; }

    /**
     * @brief Per-backend ConflictHandler registry (Audit 3, Phase B).
     *
     * The library consults this when it owns the sync session. External
     * orchestrators (e.g. Wild Palms' SyncConduitBase) can query it to
     * populate their own SyncContext's conflictHandler pointer before
     * driving a sync.
     *
     * The registry is owned by the coordinator and lives for its
     * lifetime. Register handlers before starting sync operations.
     */
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry *conflictRegistry()
    {
        return &m_conflictRegistry;
    }
    const Kalburator::Sync::QSyncCore::ConflictHandlerRegistry *conflictRegistry() const
    {
        return &m_conflictRegistry;
    }

    /**
     * @brief Load sync mappings from the collection's .kalb configuration.
     *
     * Reads the syncMappings array from KalbConfigManager and populates
     * the internal mapping list. Call this after loading a collection.
     */
    void loadSyncMappings(ICalendarCollection *collection);

    /**
     * @brief Get current sync mappings.
     */
    const QList<SyncMapping>& syncMappings() const { return m_syncMappings; }

    /**
     * @brief Set sync mappings directly (used for auto-generated mappings).
     */
    void setSyncMappings(const QList<SyncMapping> &mappings) { m_syncMappings = mappings; }

    /**
     * @brief Set collection directly (for testing without loadSyncMappings).
     */
    void setCollection(ICalendarCollection *collection);

    /**
     * @brief Check if any sync mappings are configured.
     */
    bool hasSyncMappings() const { return !m_syncMappings.isEmpty(); }

    /**
     * @brief Register a DecSync active controller for standalone sync.
     *
     * Calendars registered here will be included in syncAll() even
     * without a SyncMapping. The controller runs its own sync loop.
     *
     * @param calendarId Calendar ID managed by the controller
     * @param controller The active controller instance (not owned by coordinator)
     */
    void registerActiveController(const QString &calendarId,
                                   DecSyncActiveController *controller);

    /**
     * @brief Remove a registered active controller.
     */
    void unregisterActiveController(const QString &calendarId);

    /**
     * @brief Check if any sync work is available (mappings or controllers).
     */
    bool hasSyncWork() const;

    /**
     * @brief Phase-2 perf: when true, the coordinator's pre-pass drops
     * mappings from the work queue if both endpoints are demonstrably
     * unchanged since the last successful sync (matching CTag for Remote
     * endpoints, matching fingerprint for Local endpoints, and a stored
     * baseline exists). Default false. Single-mapping runSync(mappingId)
     * never skips regardless of this setting.
     */
    void setSkipUnchangedMappings(bool enabled);
    bool skipUnchangedMappings() const { return m_skipUnchangedMappings; }

    /**
     * @brief Enable or disable a sync mapping by ID.
     */
    void setMappingEnabled(const QString &mappingId, bool enabled);

    /**
     * @brief Run sync for one mapping. Future completes with the result.
     *
     * The QFuture supports cancel() to request cancellation; the
     * cancellation channel is wired in Group 2 Task 17.
     *
     * F2 Task 42 deleted the void runSync overloads in favor of these
     * QFuture-returning forms. The name `runSyncFuture` is kept (rather
     * than renaming back to `runSync`) because the QFuture return type
     * is the load-bearing detail at call sites; renaming would churn
     * every Group 3 consumer migration without a clarity gain.
     */
    QFuture<SyncResult> runSyncFuture(
        const QString &mappingId,
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Run a single mapping with a per-call execution override.
     *
     * Used by WildPalms's Copy Palm→PC / Copy PC→Palm modes to run
     * a mapping as a one-way mirror without persisting that direction
     * on the mapping itself. The full implementation honoring the
     * override lands in Task 9; this declaration with a stub body
     * (Task 7) lets Task 8 write failing tests against the API.
     */
    QFuture<SyncResult> runSyncFuture(
        const QString &mappingId,
        const ExecutionOverride &executionOverride,
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Run sync for all enabled mappings. Future completes with
     *        the per-mapping result list (one entry per enabled mapping).
     */
    QFuture<QList<SyncResult>> runSyncFuture(
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Run sync for a subset of mappings identified by their IDs.
     *        Only mappings in `ids` that are enabled are dispatched.
     *        Future completes with one SyncResult per dispatched mapping.
     *        G.6 Task 43.
     */
    QFuture<QList<SyncResult>> runSyncFuture(
        const QList<QString> &ids,
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Resume sync after user resolves a conflict (monitored mode only).
     *
     * Called by UI when user completes the conflict resolution dialog.
     *
     * @param resolution The user's chosen resolution
     * @param mergedIcal Optional merged iCal data (for CustomMerge resolution)
     */
    void resumeAfterConflictResolution(ConflictResolution resolution,
                                        const QString &mergedIcal = QString());

    /**
     * @brief Check if a sync operation is currently running.
     */
    bool isSyncing() const { return m_isSyncing; }

    /**
     * @brief Get the result of the last sync operation.
     */
    SyncResult lastSyncResult() const { return m_lastResult; }

    /**
     * @brief Stop the worker thread immediately.
     *
     * Called by CollectionController destructor to ensure the worker thread
     * is stopped before any resources it depends on are destroyed.
     * Safe to call multiple times (idempotent).
     */
    void stopWorkerThread();

    /**
     * @brief G.6 Task 46: cancel the current queue run with an explicit reason.
     *
     * For ResourceLost + non-empty @p resourceId: marks the resource as
     * lost and skips any pending mappings whose backends use that resource.
     * The in-flight mapping is also cancelled if it uses that resource.
     * Mappings whose backends do NOT use the resource continue normally.
     *
     * For all other reasons (or empty resourceId): equivalent to cancelling
     * the underlying QFuture — stops the entire queue.
     */
    void cancelWithReason(CancellationReason reason,
                          const QString &resourceId = {});

signals:
    /**
     * @brief Emitted when sync starts for a mapping.
     */
    void syncStarted(const QString &mappingId);

    /**
     * @brief Emitted when a conflict is detected.
     */
    void conflictDetected(const ConflictInfo &conflict);

    /**
     * @brief Emitted to report sync progress.
     */
    void progressUpdated(int current, int total, const QString &message);

    /**
     * @brief Emitted when sync phase changes.
     */
    void phaseChanged(SyncPhase phase);

    /**
     * @brief Emitted to report item-level fetch progress.
     */
    void fetchProgress(const QString &calendarId, int current, int total);

    /**
     * @brief Emitted to report item-level write progress.
     */
    void writeProgress(const QString &calendarId, int current, int total);

    /**
     * @brief Emitted when transcoding causes potential data loss.
     *
     * This signal is forwarded from the inner worker when an incidence requires
     * lossy transcoding between backends with different capabilities.
     *
     * @param calendarId The calendar being synced
     * @param uid The UID of the incidence being transcoded
     * @param warnings List of warning messages about data loss
     */
    void transcodingWarning(const QString &calendarId,
                            const QString &uid,
                            const QStringList &warnings);

private:
    /**
     * @brief F2 Task 21: dispatch-mode tag tracking which entry path
     * is currently driving the worker. Replaces the implicit shared
     * state where both runSync overloads ran through processNextMapping
     * and the single-mapping form double-dispatched after worker
     * completion (see FINDINGS "SyncEngine::runSync(mappingId) is leaky").
     */
    enum class DispatchMode {
        None,    ///< No sync in flight.
        Single,  ///< runSyncFuture(mappingId, ...)
        Queue    ///< runSyncFuture(behavior)
    };

    /**
     * @brief F2 Task 42: queue driver. Sets up state for a multi-mapping
     * run, runs active controllers + the fast-path pre-pass, and
     * delegates to processQueue() to start dispatching to the worker.
     * Called only from runSyncFuture(behavior); the previous void
     * runSync(behavior) public API was deleted in F2 Task 42.
     */
    void driveQueue(SyncBehavior behavior);

    /**
     * @brief F2 Task 21: single-mapping driver. Dispatches exactly the
     * named mapping to the worker once. Queue iteration is structurally
     * impossible — onWorkerSyncCompleted distinguishes Single vs Queue
     * via m_dispatchMode and finishes immediately for Single.
     */
    void processSingleMapping(const QString &mappingId, SyncBehavior behavior);

    /**
     * @brief F2 Task 21: multi-mapping driver. Iterates m_syncMappings
     * via re-entry from onWorkerSyncCompleted; per-mapping result is
     * forwarded to m_currentMultiIface (if populated) at the end.
     */
    void processQueue();

    /// F2 Task 21 helper: advance m_currentMappingIndex to the next
    /// enabled mapping (or past the end) and dispatch it; called from
    /// processQueue() and from onWorkerSyncCompleted() during a Queue run.
    void advanceQueue();

    /**
     * @brief Pre-pass: collect fresh revision tokens from every backend that
     * implements Backend::ChangeDetection (one batched query per backend). For
     * each mapping, if both endpoints' fresh revision matches the stored baseline
     * AND skipUnchangedMappings() is true, the mapping is skipped. Fresh state
     * is stashed in m_freshState for write-back via primeRevisionCache() on success.
     *
     * Idempotent and best-effort. Missing revisions or baselines yield "no skip".
     */
    void prepareSyncFastPath();

    /**
     * @brief Per-mapping fresh state captured during prepareSyncFastPath
     * and consumed by onWorkerSyncCompleted to persist baselines on success.
     */
    struct FreshSyncState {
        QString sourceRevision; // empty if source has no Backend::ChangeDetection
        QString targetRevision; // empty if target has no Backend::ChangeDetection
    };

    // Helper methods for sync algorithm
    void updateSyncMetadata(const SyncMapping &mapping, const SyncDiff &diff,
                            const QList<SyncChange> &resolvedToTarget,
                            const QList<SyncChange> &resolvedToSource);

private slots:
    // Worker thread signal handlers
    void onWorkerSyncStarted(const QString &mappingId);
    void onWorkerPhaseChanged(const QString &mappingId, int phase);
    void onWorkerFetchProgress(const QString &calendarId, int current, int total);
    void onWorkerWriteProgress(const QString &calendarId, int current, int total);
    void onWorkerConflictDetected(const ConflictInfo &conflict);
    void onWorkerConflictPauseRequested(const ConflictInfo &conflict);
    void onWorkerSyncCompleted(const QString &mappingId, const SyncResult &result);
    void onWorkerSyncError(const QString &mappingId, const QString &errorMessage);
    void onWorkerTranscodingWarning(const QString &calendarId, const QString &uid,
                                     const QStringList &warnings);

    // F2 Task 17: invoked when m_singleWatcher or m_multiWatcher
    // fires canceled. Forwards to the worker via queued connection.
    void onCancelObserved();

private:
    BackendRegistry *m_registry;
    ISyncHost *m_controller;
    CalendarBaselineStore *m_calendarBaselines = nullptr;
    BlobBaselineStore *m_blobBaselines = nullptr;  // Phase D Task 20
    SyncConflictStore *m_conflictStore = nullptr;
    ConflictManager *m_conflictManager = nullptr;
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry m_conflictRegistry;
    TranscodingRouter m_transcodingRouter;
    ICalendarCollection *m_collection = nullptr;
    QList<SyncMapping> m_syncMappings;

    bool m_isSyncing = false;
    bool m_cancelled = false;
    int m_currentMappingIndex = -1;
    SyncResult m_lastResult;

    // Task 9: per-call override set by runSyncFuture(mappingId, override, ...)
    // and consumed + cleared by processSingleMapping before dispatching the
    // worker Request. Cleared to Default after embedding in the Request so
    // a subsequent no-override runSyncFuture(mappingId, ...) call does not
    // inherit it.
    ExecutionOverride m_pendingOverride;

    // F2 Task 21: which entry-path drove the current run. Read by
    // onWorkerSyncCompleted to decide whether to advance the queue
    // or finish the single-mapping future.
    DispatchMode m_dispatchMode = DispatchMode::None;

    // F2 Task 21: pointers to the QFutureInterface for the current run.
    // Only one is populated at a time (matched to m_dispatchMode); the
    // unused one is nullptr. Owned by the engine — populated by the
    // runSyncFuture overloads, cleared+deleted in onWorkerSyncCompleted
    // (Single) or after queue iteration completes (Queue). The void
    // runSync overloads leave both nullptr (legacy signal callers).
    QFutureInterface<SyncResult>* m_currentSingleIface = nullptr;
    QFutureInterface<QList<SyncResult>>* m_currentMultiIface = nullptr;

    // F2 Task 21: per-mapping results accumulated during a Queue run,
    // reported via m_currentMultiIface->reportResult() at the end.
    QList<SyncResult> m_queueResults;

    // Phase-2 skip optimization
    bool m_skipUnchangedMappings = false;
    QSet<QString> m_skippedMappingIds;
    QMap<QString, FreshSyncState> m_freshState;

    // G.6 Task 43: subset filter for runSyncFuture(QList<QString>).
    // m_hasMappingFilter distinguishes "no filter" from "empty filter".
    // When true, advanceQueue skips mappings whose id is not in m_mappingIdFilter.
    // An empty m_mappingIdFilter with m_hasMappingFilter=true means "run nothing".
    bool m_hasMappingFilter = false;
    QSet<QString> m_mappingIdFilter;

    // G.6 Task 46: resource IDs that became unavailable mid-queue.
    // advanceQueue adds a cancelled SyncResult for any pending mapping
    // whose source or target backend's resourceId() is in this set.
    // Cleared at queue completion or when a new queue run starts.
    QSet<QString> m_lostResources;

    // G.6 Task 44: resource-aware FIFO scheduler. Tracks mapping→resource
    // sets for cancelWithReason(ResourceLost). The engine still drives
    // execution via advanceQueue; the scheduler is consulted for
    // resource-based selective cancellation.
    MappingScheduler m_scheduler;

    // F2 Task 17: watchers tracking the in-flight QFuture from
    // runSyncFuture. Only one is populated at a time (one for
    // single-mapping, one for multi-mapping). On QFuture::cancel(),
    // QFutureWatcher::canceled fires on the engine thread, and
    // we forward to the worker via queued connection.
    QFutureWatcher<SyncResult>* m_singleWatcher = nullptr;
    QFutureWatcher<QList<SyncResult>>* m_multiWatcher = nullptr;

    // Worker thread infrastructure
    QThread m_workerThread;
    SyncEngineWorker *m_worker = nullptr;
    SyncBehavior m_currentSyncBehavior = SyncBehavior::Unmonitored;
    ConflictInfo m_pendingConflict;  // For monitored mode dialog
    QList<ConflictInfo> m_pendingUnmonitoredConflicts;  // Batch for post-sync presentation

    // Sync state tracking
    SyncPhase m_currentPhase = SyncPhase::Idle;
    SyncResult m_currentMappingResult;

    QMap<QString, DecSyncActiveController*> m_activeControllers;

    // Helper to set up worker connections
    void setupWorkerConnections();
    void startWorkerThread();
};

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::SyncEngineWorker::Request)
Q_DECLARE_METATYPE(Kalburator::Sync::SyncEngineWorker::Mode)

#endif // KALBURATOR_SYNCENGINE_H
