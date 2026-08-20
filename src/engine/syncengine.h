#ifndef KALBURATOR_SYNCENGINE_H
#define KALBURATOR_SYNCENGINE_H

#include "enginediff.h"
#include "mappingqueue.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "shape.h"
#include "synctypes.h"
#include "syncdiff.h"
#include "conflicthandlerregistry.h"
#include "syncenginefuture.h"
#include "../sync/syncoperation.h"  // neutral SyncOperation base; required by await<Op> template
#include "shaperegistries.h"
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
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

namespace Kalburator::Storage {
class BaselineStore;
} // namespace Kalburator::Storage

namespace Kalburator::Conflict {
class ConflictHandlerRegistry;
class IMassDeleteGuard;
} // namespace Kalburator::Conflict

namespace Kalburator::Sync {
class BackendRegistry;
class IBlobBackend;
class ISyncHost;
class SyncBackendBase;
class ICalendarCollection;
class SyncConflictStore;
class ISyncConfigStore;
class ConflictManager;
class DecSyncActiveController;
} // namespace Kalburator::Sync

namespace Kalburator::Engine {

// Forward declarations
class SyncEngine;

// Using declarations for value types from Kalburator::Sync
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::ExecutionOverride;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ConflictInfo;
using Kalburator::Sync::SyncDiff;
using Kalburator::Sync::SyncChange;
using Kalburator::Sync::SyncResult;
using Kalburator::Sync::SyncStats;
using Kalburator::Sync::SyncOperation;  // neutral op base; engine depends only on this (P3.T4)

// Using declarations for pointer/reference types from Kalburator::Sync
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::IBlobBackend;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::SyncBackendBase;
using Kalburator::Sync::ICalendarCollection;
using Kalburator::Sync::SyncConflictStore;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ConflictManager;
using Kalburator::Sync::DecSyncActiveController;

// Using declarations for other types from Kalburator::Sync
using Kalburator::Sync::SyncChangeType;
using Kalburator::Sync::SyncRecord;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::ConflictType;

// ExecutionOverride lives in synctypes.h.

} // namespace Kalburator::Engine

namespace Kalburator::Shape {
class DomainOperations;
}

namespace Kalburator::Engine {

// SyncEngineWorker has been moved to src/engine/syncengine_p.h as part
// of Architectural-redress Plan 1 Task 2 (2026-05-29). It is a private
// implementation detail of SyncEngine; only syncengine.cpp may include
// the private header. External consumers (PlanStan, WildPalms, tests)
// should use the public SyncEngine API below.
class SyncEngineWorker;

// Architectural-redress Plan 1 Task 4 (2026-05-29): canonical request
// type for SyncEngine::runSync. Defined in syncrequest.h, which the
// implementation (syncengine.cpp) includes; only forward-declared
// here because the public surface takes it by const-reference.
struct SyncRequest;

/**
 * @brief Coordinates sync operations between backends according to sync mappings.
 *
 * SyncEngine implements the core sync algorithm:
 *
 * 1. Load sync mappings from the host's ISyncConfigStore
 * 2. For each enabled mapping:
 *    a. Load records from source backend
 *    b. Load records from target backend
 *    c. Load baselines from Storage::BaselineStore
 *    d. Compute 3-way diff
 *    e. Apply changes based on sync mode
 *    f. Handle conflicts according to policy
 *    g. Update baselines in Storage::BaselineStore
 *
 * Persistent storage is split across Storage::BaselineStore (record baselines,
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

    /**
     * @brief Per-mapping fresh revision state captured during the fast-path
     * pre-pass (H4: computed on the worker thread, in
     * SyncEngineWorker::prepareFastPath) and consumed by
     * onWorkerSyncCompleted to persist sync-progress tokens on success.
     * Public (rather than the pre-H4 private nesting) because it now
     * crosses the engine/worker boundary as a signal parameter — see
     * SyncEngineWorker::fastPathReady in syncengine_p.h.
     */
    struct FreshSyncState {
        QString sourceRevision; // empty if source has no Sync::ChangeDetection
        QString targetRevision; // empty if target has no Sync::ChangeDetection
    };

    /// Injecting ctor (preferred): the engine reads shape state from
    /// `shape`, which the caller must also have handed to the
    /// PluginManager that populated it. Per-engine isolation lives here.
    explicit SyncEngine(BackendRegistry *registry,
                              ISyncHost *host,
                              Kalburator::Shape::ShapeRegistries &shape,
                              QObject *parent = nullptr);
    ~SyncEngine() override;

    /**
     * @brief Set the BaselineStore for per-record hash-skip (Phase D Task 20).
     *
     * When set, the engine's subsequent-sync blob fetch skips records whose
     * contentHash matches the stored baseline — avoiding unnecessary merge work
     * for unchanged records.
     */
    void setBaselineStore(Kalburator::Storage::BaselineStore *store);
    Kalburator::Storage::BaselineStore *baselineStore() const { return m_baselineStore; }

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
    Kalburator::Conflict::ConflictHandlerRegistry *conflictRegistry()
    {
        return &m_conflictRegistry;
    }
    const Kalburator::Conflict::ConflictHandlerRegistry *conflictRegistry() const
    {
        return &m_conflictRegistry;
    }

    /**
     * Register a synchronous gate consulted before mass deletes are
     * propagated during sync. Non-owning; consumer must outlive the
     * SyncEngine. Pass nullptr to clear. See imassdeleteguard.h for
     * threshold semantics. Default: no guard (deletes proceed
     * unconditionally — backward compatible).
     */
    void setMassDeleteGuard(Kalburator::Conflict::IMassDeleteGuard *guard);
    Kalburator::Conflict::IMassDeleteGuard *massDeleteGuard() const;

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
     * @brief Max mappings dispatched concurrently in a Queue-mode run.
     *
     * Default 1 — bit-identical to pre-parallel behaviour, which is what
     * makes this safe to land for every existing consumer without them
     * changing a line. Clamped to a minimum of 1.
     *
     * Read ONCE at run entry and frozen for the duration of that run;
     * changing it mid-run affects the next run only. This eliminates a
     * whole class of races at no cost.
     *
     * Two things override it downward, both automatically:
     *  - A Monitored run is always 1 (the conflict pause is a
     *    one-at-a-time interaction).
     *  - A backend's maxConcurrentOperations() caps mappings using that
     *    backend's resourceId(), and a backend living on the engine's own
     *    thread is treated as 1 (see capForMapping).
     */
    void setMaxConcurrentMappings(int n);
    int  maxConcurrentMappings() const { return m_maxConcurrentMappings; }

    /**
     * @brief Enable or disable a sync mapping by ID.
     */
    void setMappingEnabled(const QString &mappingId, bool enabled);

    /**
     * @brief Canonical entry point — run sync per the request.
     *
     * Architectural-redress Plan 1 Task 4 (2026-05-29): the four
     * `runSyncFuture()` overloads were collapsed into this single
     * struct-parameterized form. See `engine/syncrequest.h` for the
     * three dispatch shapes (all-enabled / subset / single) and how
     * `mappingIds.size()` selects between them.
     *
     * The future completes with one SyncResult per dispatched mapping
     * (empty list if no work). The future supports cancel() to request
     * cancellation via the existing QFutureWatcher channel.
     */
    QFuture<QList<SyncResult>> runSync(const SyncRequest &request);

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
     * @brief Grow the pool to @p size workers, each on its own QThread,
     * and start any thread not yet running.
     *
     * Idempotent. Never shrinks — a smaller @p size than the current pool
     * is a no-op, so a run that lowers concurrency does not tear down
     * live workers. Public (rather than private, as an internal detail
     * would otherwise be) so tst_worker_pool.cpp can pin the pool's own
     * lifecycle invariants directly, without driving a real sync run —
     * same "public and harmless" rationale as the *ForTest() accessors
     * below. Internal callers (driveQueue, processSingleMapping, etc.)
     * use it exactly as they used the old startWorkerThread().
     */
    void startWorkerPool(int size);

    /**
     * @brief Stop the worker pool immediately.
     *
     * Called by CollectionController destructor to ensure every pool
     * worker is stopped before any resources it depends on are destroyed.
     * Safe to call multiple times (idempotent).
     */
    void stopWorkerPool();

    // Test-only pool introspection (parallel-sync Task 2). Cheap, const,
    // and harmless in production; exposed rather than friending a test
    // class so the pool's invariants can be pinned without reaching into
    // private state. Ruled 2026-08-12: deliberate plan-mandated public
    // API — a reviewer flagging "production API exists only for tests"
    // should be adjudicated against this ruling, not looped on.
    int  poolSizeForTest() const { return m_pool.size(); }
    bool poolThreadsRunningForTest() const;
    int  distinctPoolThreadCountForTest() const;

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

    /// L2 (spec §5.9): hard cap on fixpoint passes per Queue run.
    static constexpr int kMaxSyncPasses = 3;

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
     *
     * Parallel-sync Task 9: `current` is the number of mappings started
     * this pass — monotonic by construction, which is what a progress bar
     * needs. At concurrency 1 it equals the pre-parallel currentIndex()+1
     * exactly. `message` names the mapping that just started.
     */
    void progressUpdated(int current, int total, const QString &message);

    /**
     * @brief Emitted when sync phase changes.
     *
     * Parallel-sync Task 9: at concurrency 1 this is per-mapping and
     * unchanged from the pre-parallel engine. At N>1 it describes the
     * RUN: Complete and Idle are only emitted once the pool has drained,
     * so a consumer gating on "is a sync happening" (e.g. WildPalms'
     * shouldPauseTickle) stays correct. Per-mapping phase is reported
     * separately via the worker's phaseChanged(mappingId, int).
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

    /**
     * @brief L2 (spec §5.9): emitted when the engine starts a re-pass over
     * dirtied mappings (pass >= 2; the first pass is the run itself and is
     * not announced).
     */
    void syncPassStarted(int pass, int maxPasses);

private:
    /**
     * @brief F2 Task 42: queue driver. Sets up state for a multi-mapping
     * run, runs active controllers + the fast-path pre-pass, and
     * delegates to processQueue() to start dispatching to the worker.
     * Called from runSync()'s all-enabled and subset branches; the
     * filter argument distinguishes "all enabled" (nullopt)
     * from "subset" (set of mapping IDs). Plan 1 Task 4 will fold the
     * two arguments into one SyncRequest struct.
     */
    /// @p queueOverride threads the multi-mapping-applicable subset of a
    /// per-call ExecutionOverride (today: only `clobber`; `direction`
    /// stays a single-mapping concept) to every mapping the queue
    /// dispatches. Callers pass a SANITIZED override — runSync() copies
    /// only the clobber flag, leaving direction at Default.
    void driveQueue(SyncBehavior behavior,
                    std::optional<QSet<QString>> filter = std::nullopt,
                    ExecutionOverride queueOverride = {});

    /**
     * @brief F2 Task 21: single-mapping driver. Dispatches exactly the
     * named mapping to the worker once. Queue iteration is structurally
     * impossible — onWorkerSyncCompleted distinguishes Single vs Queue
     * via m_queue.dispatchMode() and finishes immediately for Single.
     */
    /// @p executionOverride is taken by value because ExecutionOverride's
    /// default-constructed state (Direction::Default) means "no override" —
    /// matching the worker Request::override field's convention. Plan 1
    /// Task 5 (2026-05-29): inlined from the former m_pendingOverride
    /// class member to eliminate the implicit-state-machine residue
    /// (INVARIANTS §4 — "public API answers one question").
    void processSingleMapping(const QString &mappingId,
                              SyncBehavior behavior,
                              ExecutionOverride executionOverride = {});

    /**
     * @brief Architectural-redress Plan 8 step 3 (2026-06-10): shared
     * per-run setup. Creates the sole QFutureInterface (m_currentIface)
     * and its cancellation watcher (m_currentWatcher) and returns the
     * future callers observe. Used by both runSync() branches — the
     * single-mapping branch then dispatches via processSingleMapping(),
     * the multi-mapping branch via driveQueue(). Replaces the former
     * dual single/multi interface pair (FINDINGS "From Plan 1").
     */
    QFuture<QList<SyncResult>> beginRun();

    /**
     * @brief F2 Task 21: multi-mapping driver. Iterates the queue
     * candidate list via re-entry from onWorkerSyncCompleted; per-mapping
     * results accumulate inside MappingQueue and are forwarded to
     * m_currentIface (if populated) at run end.
     */
    void processQueue();

    /// Parallel-sync Task 8: dispatch as many eligible mappings as the
    /// effective cap allows, then return. Re-entered from
    /// onWorkerSyncCompleted/onWorkerSyncError after each completion
    /// releases its endpoints and its worker. Replaces advanceQueue()'s
    /// dispatch-one-and-return shape; called from processQueue() too.
    void pumpQueue();

    /// True when @p m may be dispatched right now: neither endpoint is
    /// claimed by an in-flight mapping, and the per-resource cap
    /// (capForMapping) is not already reached by the mappings in flight.
    bool isEligible(const SyncMapping &m) const;

    /// How many in-flight mappings already use either of @p m's backends'
    /// resourceId(), for comparison against capForMapping(m).
    int inFlightCountForMappingResources(const SyncMapping &m) const;

    /**
     * @brief H4: finishes the setup driveQueue() started — either directly
     * (clobber run, cancelled, or no mappings) or as the continuation of
     * onFastPathReady() once the worker's fast-path pre-pass returns.
     * Runs the empty/cancelled teardown, or starts the worker thread and
     * calls processQueue().
     */
    void finishDriveQueueSetup();

    /**
     * @brief E3 (O33b): continuation of driveQueue()'s setup, extracted
     * so it can run either synchronously (no active controllers to
     * dispatch) or as onActiveControllersReady()'s continuation (active
     * controllers were dispatched to the worker first). Runs the
     * fast-path pre-pass dispatch or the clobber/no-fast-path branch,
     * exactly as the tail of driveQueue() did before this split.
     */
    void continueDriveQueueSetup(const std::optional<QSet<QString>> &filter);

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

    /**
     * @brief H4: invoked (queued, worker thread -> engine thread) when
     * SyncEngineWorker::prepareFastPath finishes. Stores the skip set and
     * fresh-state map, then calls finishDriveQueueSetup() — the same
     * continuation driveQueue() itself uses for the clobber/no-fast-path
     * branch. If cancellation landed while the fast path was in flight,
     * finishDriveQueueSetup()'s cancelled-teardown branch fires and no
     * mapping is ever dispatched.
     */
    void onFastPathReady(const QSet<QString> &skipped,
                         const QMap<QString, FreshSyncState> &fresh);

    /**
     * @brief E3 (O33b): invoked (queued, worker thread -> engine thread)
     * when SyncEngineWorker::runActiveControllers finishes the DecSync
     * active-controller loop. Resumes driveQueue()'s setup via
     * continueDriveQueueSetup(), using the filter driveQueue() stashed
     * in m_pendingQueueFilter before dispatching the loop.
     */
    void onActiveControllersReady();

    // F2 Task 17: invoked when m_currentWatcher fires canceled.
    // Forwards to the worker via queued connection.
    void onCancelObserved();

private:
    BackendRegistry *m_registry;
    ISyncHost *m_controller;
    Kalburator::Storage::BaselineStore *m_baselineStore = nullptr;  // Phase D Task 20
    SyncConflictStore *m_conflictStore = nullptr;
    ConflictManager *m_conflictManager = nullptr;
    Kalburator::Conflict::ConflictHandlerRegistry m_conflictRegistry;
    Kalburator::Conflict::IMassDeleteGuard *m_massDeleteGuard = nullptr;
    Kalburator::Shape::ShapeRegistries &m_shape;
    ICalendarCollection *m_collection = nullptr;
    QList<SyncMapping> m_syncMappings;

    bool m_isSyncing = false;
    bool m_cancelled = false;
    SyncResult m_lastResult;

    // Architectural-redress Plan 1 Task 3 (2026-05-29): per-run queue
    // state moved into MappingQueue. Owns the mapping iteration cursor,
    // the per-run SyncResult accumulator, the subset filter, the
    // lost-resource set, and the DispatchMode tag. The engine reads
    // and mutates the queue on its own thread (no locking; see
    // mappingqueue.h class comment). Replaces seven scattered member
    // fields with one collaborator (INVARIANTS §4).
    MappingQueue m_queue;

    // The single per-run interface (Plan 8 step 3 collapsed the former
    // single/multi pair — every public entry now returns
    // QFuture<QList<SyncResult>>). Owned by unique_ptr so the destructor
    // frees any in-flight iface automatically (Plan 4 leak fix — AUDIT
    // MAJOR "raw QFutureInterface* without lifecycle management").
    std::unique_ptr<QFutureInterface<QList<SyncResult>>> m_currentIface;

    // Phase-2 skip optimization
    bool m_skipUnchangedMappings = false;
    QSet<QString> m_skippedMappingIds;

    // Parallel sync (Task 7): concurrency configuration.
    /// Host-requested concurrency. See setMaxConcurrentMappings().
    int m_maxConcurrentMappings = 1;

    /// Concurrency actually in force for the current run, resolved once
    /// at run entry by resolveEffectiveCap().
    int m_effectiveCap = 1;

    /// Resolve the run's concurrency: 1 for Monitored, otherwise the
    /// host-requested value.
    int resolveEffectiveCap(SyncBehavior behavior) const;

    /// Per-resource ceiling for @p m: the min of both backends'
    /// maxConcurrentOperations(), with 0 meaning unlimited, and with a
    /// backend living on the engine's own thread forced to 1.
    /// Returns INT_MAX when nothing constrains it.
    int capForMapping(const SyncMapping &m) const;

    // L2 (sync-graph campaign, spec §5.9): fixpoint-pass state. Reset at
    // the start of every driveQueue() run.
    int m_currentPass = 1;
    // endpointKey -> ids of mappings that wrote it this pass.
    QHash<QString, QSet<QString>> m_passDirtyWriters;
    QList<SyncResult> m_carriedResults; // drained results of earlier passes

    // L1 (sync-graph campaign, spec §5.9): a completed mapping that applied
    // changes invalidates the frozen fast-path skip verdict of every pending
    // mapping sharing one of its endpoints — the pre-pass judged those
    // endpoints before this run wrote them.
    void invalidateSkipsTouching(const SyncMapping &completed);

    // Multi-mapping per-call override (v0.65 clobber). Assigned by
    // driveQueue() at the start of every queue run (so no stale state
    // survives between runs) and stamped onto each per-mapping worker
    // Request in advanceQueue(). Only the clobber flag ever reaches here
    // — runSync() sanitizes direction to Default for multi dispatch.
    ExecutionOverride m_queueOverride;
    QMap<QString, FreshSyncState> m_freshState;

    // F2 Task 17: watcher tracking the in-flight QFuture from runSync.
    // On QFuture::cancel(), QFutureWatcher::canceled fires on the engine
    // thread, and we forward to the worker via queued connection.
    QFutureWatcher<QList<SyncResult>>* m_currentWatcher = nullptr;

    // Worker pool infrastructure (parallel-sync Task 2). Replaces the
    // single m_workerThread/m_worker pair. At size 1 this is exactly the
    // pre-pool code path; Task 7 adds the concurrency knob that grows it.
    //
    // Slot 0 is the CONTROL SLOT: the fast-path pre-pass
    // (prepareFastPath) and the DecSync active-controller loop always run
    // there, and only slot 0's fastPathReady/activeControllersReady are
    // connected to the engine, so those continuations never fire twice.
    // Both run before any mapping is dispatched, so slot 0 is free.
    struct WorkerSlot {
        QThread          *thread = nullptr;
        SyncEngineWorker *worker = nullptr;
        QString           busyMappingId;   ///< empty = free
    };
    QList<WorkerSlot> m_pool;

    /// mappingId -> pool slot index, for every mapping currently dispatched.
    QHash<QString, int> m_inFlight;

    /// Parallel-sync Task 8: endpointKey()s currently claimed by an
    /// in-flight mapping. A mapping is only eligible for dispatch when
    /// NEITHER of its endpoints is in here — that is what stops two
    /// mappings diffing and applying against the same (backend, calendar)
    /// at once. The per-collection FIFO does NOT provide this guarantee:
    /// it serialises operations, not diff/apply cycles.
    QSet<QString> m_inFlightEndpoints;

    SyncBehavior m_currentSyncBehavior = SyncBehavior::Unmonitored;
    ConflictInfo m_pendingConflict;  // For monitored mode dialog
    QList<ConflictInfo> m_pendingUnmonitoredConflicts;  // Batch for post-sync presentation

    // Sync state tracking
    SyncPhase m_currentPhase = SyncPhase::Idle;
    // P1.T3 (2026-05-29): the former m_currentMappingResult member was
    // dead — set once in onWorkerSyncCompleted and never read by any
    // slot or accessor. Removed rather than folded into MappingQueue.

    QMap<QString, DecSyncActiveController*> m_activeControllers;

    // E3 (O33b): the subset filter driveQueue() was called with, stashed
    // across the async active-controller dispatch so
    // onActiveControllersReady() -> continueDriveQueueSetup() can resume
    // with the same filter driveQueue() would have used synchronously.
    std::optional<QSet<QString>> m_pendingQueueFilter;

    /// Wire one pool worker's signals. @p isControlSlot gates the two
    /// run-level continuations (fastPathReady, activeControllersReady)
    /// so they are connected for slot 0 only and can never fire twice.
    void setupWorkerConnections(SyncEngineWorker *worker, bool isControlSlot);

    /// Index of a free pool slot, or -1 if every worker is busy.
    int leaseWorker();

    /// Mark @p mappingId's slot free and drop it from m_inFlight.
    void releaseWorker(const QString &mappingId);

    /// Slot 0's worker — the fixed home of the fast-path pre-pass and the
    /// active-controller loop. Null before startWorkerPool().
    SyncEngineWorker *controlWorker() const;

    /// Invoke @p fn for every worker currently in the pool.
    void forEachWorker(const std::function<void(SyncEngineWorker*)> &fn);
};

} // namespace Kalburator::Engine

// K.5.5 compatibility: consumers use Sync::SyncEngine
namespace Kalburator::Sync {
using SyncEngine = Kalburator::Engine::SyncEngine;
// Note: SyncEngineWorker moved to src/engine/syncengine_p.h (Plan 1 Task 2).
// It is no longer part of the public API surface.
} // namespace Kalburator::Sync

// Metatype declarations for SyncEngineWorker::Request / ::Mode live in
// the private header syncengine_p.h alongside the class.

// H4: declared here (not syncengine_p.h) because FreshSyncState now
// crosses the SyncEngine/SyncEngineWorker signal boundary — moc-generated
// code for SyncEngine (built from this header alone) must see the
// specialization before any implicit instantiation of the unregistered-
// type fallback, which a declaration in the private header (included only
// by syncengine.cpp) would not guarantee.
Q_DECLARE_METATYPE(Kalburator::Engine::SyncEngine::FreshSyncState)

#endif // KALBURATOR_SYNCENGINE_H
