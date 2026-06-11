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
#include "mappingscheduler.h"
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

    /// F2 Task 21 helper: pop the next enabled+in-filter mapping from
    /// MappingQueue and dispatch it; called from processQueue() and
    /// from onWorkerSyncCompleted() during a Queue run.
    void advanceQueue();

    /**
     * @brief Pre-pass: collect fresh revision tokens from every backend that
     * implements Sync::ChangeDetection (one batched query per backend). For
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
        QString sourceRevision; // empty if source has no Sync::ChangeDetection
        QString targetRevision; // empty if target has no Sync::ChangeDetection
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

    // Multi-mapping per-call override (v0.65 clobber). Assigned by
    // driveQueue() at the start of every queue run (so no stale state
    // survives between runs) and stamped onto each per-mapping worker
    // Request in advanceQueue(). Only the clobber flag ever reaches here
    // — runSync() sanitizes direction to Default for multi dispatch.
    ExecutionOverride m_queueOverride;
    QMap<QString, FreshSyncState> m_freshState;

    // G.6 Task 44: resource-aware FIFO scheduler. Tracks mapping→resource
    // sets for cancelWithReason(ResourceLost). The engine still drives
    // execution via advanceQueue; the scheduler is consulted for
    // resource-based selective cancellation.
    MappingScheduler m_scheduler;

    // F2 Task 17: watcher tracking the in-flight QFuture from runSync.
    // On QFuture::cancel(), QFutureWatcher::canceled fires on the engine
    // thread, and we forward to the worker via queued connection.
    QFutureWatcher<QList<SyncResult>>* m_currentWatcher = nullptr;

    // Worker thread infrastructure
    QThread m_workerThread;
    SyncEngineWorker *m_worker = nullptr;
    SyncBehavior m_currentSyncBehavior = SyncBehavior::Unmonitored;
    ConflictInfo m_pendingConflict;  // For monitored mode dialog
    QList<ConflictInfo> m_pendingUnmonitoredConflicts;  // Batch for post-sync presentation

    // Sync state tracking
    SyncPhase m_currentPhase = SyncPhase::Idle;
    // P1.T3 (2026-05-29): the former m_currentMappingResult member was
    // dead — set once in onWorkerSyncCompleted and never read by any
    // slot or accessor. Removed rather than folded into MappingQueue.

    QMap<QString, DecSyncActiveController*> m_activeControllers;

    // Helper to set up worker connections
    void setupWorkerConnections();
    void startWorkerThread();
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

#endif // KALBURATOR_SYNCENGINE_H
