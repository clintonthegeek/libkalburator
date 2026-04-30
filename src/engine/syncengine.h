#ifndef KALBURATOR_SYNCENGINE_H
#define KALBURATOR_SYNCENGINE_H

#include "synctypes.h"
#include "syncdiff.h"
#include "blobdomainadapter.h"
#include "blobsyncresult.h"  // BlobSyncResult / BlobSyncStats — split out F1 Task 10
#include "calendardomainadapter.h"
#include "conflicthandlerregistry.h"
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
#include <KCalendarCore/Incidence>
#include <atomic>
#include <type_traits>

namespace Kalburator::Sync {

class BackendRegistry;
class BlobBaselineStore;
class IBlobBackend;
class ISyncHost;
class ICalendarCollection;
class CalendarBaselineStore;
class SyncConflictStore;
class ISyncConfigStore;
class ConflictManager;
class DecSyncActiveController;
class SyncEngine;

namespace QSyncCore {
    class ConflictStore;
    struct ConflictPolicy;
}

/**
 * @brief Internal worker class — runs sync operations on a background thread.
 *
 * Phase F1 Task 8 (2026-04-29): Formerly a standalone worker class in
 * src/calendar/ (deleted in this task). Folded into the engine's translation
 * unit. SyncEngine instantiates one of these and moves it to its
 * private QThread; the worker reaches the rest of the engine's collaborators
 * (CalendarDomainAdapter, baseline stores, host) via setDependencies.
 *
 * Sync phases handled here:
 * - Fetching records from source and target backends
 * - Computing 3-way diff (delegated to CalendarDomainAdapter since F1 Task 5)
 * - Handling conflicts based on mode (monitored/unmonitored)
 * - Applying changes to backends (delegated to CalendarDomainAdapter)
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
                         CalendarDomainAdapter *calendarAdapter = nullptr,
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

signals:
    void syncStarted(const QString &mappingId);
    void phaseChanged(const QString &mappingId, int phase);
    void fetchProgress(const QString &calendarId, int current, int total);
    void itemReady(const QString &calendarId,
                   const KCalendarCore::Incidence::Ptr &incidence,
                   int changeType);
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

    // Property sync phases (run before incidence sync)
    void fetchCalendarProperties();
    void computePropertyDiff();
    void applyPropertyChanges();
    void updatePropertyBaselines();

    // Sync phases
    void fetchSourceRecords();
    void fetchTargetRecords();
    void computeDiff();
    void handleConflicts();
    void applyChanges();
    void updateBaselines();

    // First-sync dispatch via the engine's blob mirror (Phase D Task 21)
    bool dispatchFirstSync(const Request &request);
    void harvestBaselinesAfterFirstSync(const Request &request);

    // Blob-view helpers (Phase D Task 19)
    void fetchRecordsViaBlob(const QString &backendId,
                             const QString &calendarId,
                             QList<SyncRecord> &out);

    // Conflict handling
    void handleConflictUnmonitored(const SyncChange &change);
    void applyMonitoredResolution(const SyncChange &change,
                                   ConflictResolution resolution,
                                   const QString &mergedIcal);
    void resolveConflictAutomatically(const SyncChange &change,
                                       ConflictResolution policy);

    void continueAfterConflicts();

    void applyChangesToBackend(const QString &backendId,
                               const QString &calendarId,
                               const QList<SyncChange> &changes,
                               bool useTargetRecord = false);

    QMutex m_mutex;

    // F2 Task 14: pointers to the QFutureInterface for the current run.
    // Only one is populated at a time; the unused one is nullptr.
    // The engine constructs/destroys these around runSync calls.
    // Not used yet; Tasks 15-21 wire them in.
    QFutureInterface<SyncResult>* m_currentSingleIface = nullptr;
    QFutureInterface<QList<SyncResult>>* m_currentMultiIface = nullptr;

    // F2 Task 14: cancellation observation flag. Set by observeCancel()
    // slot (added in Task 17) when QFutureWatcher::canceled fires on the
    // engine side and the engine forwards via queued connection.
    // Upgraded from plain bool so concurrent observers see writes
    // without taking m_mutex.
    std::atomic<bool> m_cancelled{false};
    bool m_fetchFailed = false;
    QString m_fetchErrorMessage;
    bool m_applyFailed = false;
    QString m_applyErrorMessage;

    enum class ConflictPhase { ToTarget, ToSource, Done };
    ConflictPhase m_conflictPhase = ConflictPhase::Done;
    int m_conflictIndex = 0;
    bool m_yieldedForConflict = false;

    QElapsedTimer m_totalTimer;
    QElapsedTimer m_phaseTimer;
    qint64 m_propertyFetchMs = 0;
    qint64 m_propertyDiffMs = 0;
    qint64 m_propertyApplyMs = 0;
    qint64 m_sourceFetchMs = 0;
    qint64 m_targetFetchMs = 0;
    qint64 m_diffMs = 0;

    const TranscodingRouter &m_router;
    ISyncHost *m_controller = nullptr;
    CalendarBaselineStore *m_calendarBaselines = nullptr;
    BlobBaselineStore *m_blobBaselines = nullptr;
    ICalendarCollection *m_collection = nullptr;
    CalendarDomainAdapter *m_calendarAdapter = nullptr;
    // F1 Task 10: back-pointer so dispatchFirstSync can drive the
    // engine's own runBlobMirror facade, replacing the standalone
    // BlobSyncEngine that was deleted alongside it.
    SyncEngine *m_engine = nullptr;

    Request m_currentRequest;
    QList<SyncRecord> m_sourceRecords;
    QList<SyncRecord> m_targetRecords;
    SyncDiff m_currentDiff;
    SyncResult m_currentResult;
    QList<SyncChange> m_resolvedToTarget;
    QList<SyncChange> m_resolvedToSource;
    int m_resolvedToSourceConflictStart = 0;

    CalendarPropertyRecord m_sourceProperties;
    CalendarPropertyRecord m_targetProperties;
    CalendarPropertyDiff m_propertyDiff;
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
     * @brief Run sync for all enabled mappings with specified mode.
     *
     * This is the main entry point for sync operations.
     * The entire sync runs in a background thread.
     *
     * @param mode Sync mode (Monitored or Unmonitored)
     */
    void runSync(SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Run sync for a specific mapping.
     *
     * @param mappingId The mapping ID to sync
     * @param mode Sync mode (Monitored or Unmonitored)
     */
    void runSync(const QString &mappingId, SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Run sync for one mapping. Future completes with the result.
     *
     * F2 transitional shim that delegates to runSync(mappingId, behavior)
     * and captures completion via the syncCompleted signal. Group 4
     * Task 42 deletes the void runSync and renames this method to runSync.
     *
     * The QFuture supports cancel() to request cancellation; the
     * cancellation channel is wired in Group 2 Task 17.
     */
    QFuture<SyncResult> runSyncFuture(
        const QString &mappingId,
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    /**
     * @brief Run sync for all enabled mappings. Future completes with
     *        the per-mapping result list (one entry per enabled mapping).
     *
     * F2 transitional shim. See runSyncFuture(mappingId, behavior).
     */
    QFuture<QList<SyncResult>> runSyncFuture(
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    // --- One-shot blob API (F1 Task 6) ---
    //
    // Synchronous blob-typed sync facade for ad-hoc callers (WildPalms's
    // SyncRunner). Replaces the legacy BlobSyncEngine's twoWayWithBaseline
    // / mirror, deleted in F1 Task 10. Behavior is byte-for-byte parity
    // with the predecessor — only the call site changes. These run
    // synchronously on the calling thread (no worker thread).

    /// Three-way blob sync consulting a baseline store. Propagates
    /// deletions correctly and dispatches conflicts to per-backend
    /// handlers looked up via the registry. On success commits new
    /// baselines reflecting the synced state.
    BlobSyncResult runBlobTwoWay(IBlobBackend *a,
                                 IBlobBackend *b,
                                 const QString &collectionId,
                                 const QString &mappingId,
                                 BlobBaselineStore *baseline,
                                 QSyncCore::ConflictHandlerRegistry *handlers,
                                 QSyncCore::ConflictStore *conflicts,
                                 const QSyncCore::ConflictPolicy &policy);

    /// One-way mirror: source → target. Records present in target but
    /// not in source are deleted; records present in both with
    /// matching contentHash are left untouched.
    BlobSyncResult runBlobMirror(IBlobBackend *source,
                                 IBlobBackend *target,
                                 const QString &collectionId);

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
     * @brief Cancel any running sync operation.
     */
    void cancelSync();

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

signals:
    /**
     * @brief Emitted when sync starts for a mapping.
     */
    void syncStarted(const QString &mappingId);

    /**
     * @brief Emitted when sync completes for a mapping.
     */
    void syncCompleted(const QString &mappingId, const SyncResult &result);

    /**
     * @brief Emitted when a conflict is detected.
     */
    void conflictDetected(const ConflictInfo &conflict);

    /**
     * @brief Emitted to report sync progress.
     */
    void progressUpdated(int current, int total, const QString &message);

    /**
     * @brief Emitted when all sync operations complete.
     */
    void allSyncsCompleted(const SyncResult &aggregateResult);

    /**
     * @brief Emitted when sync phase changes.
     */
    void phaseChanged(SyncPhase phase);

    /**
     * @brief Emitted when an item is fetched during sync.
     *
     * This signal is forwarded from the backend's itemFetched signal
     * to allow real-time UI updates as items are downloaded.
     */
    void itemFetched(const QString &calendarId,
                     const KCalendarCore::Incidence::Ptr &incidence);

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
    void processNextMapping();

    /**
     * @brief Phase-1 + Phase-2 pre-pass. Collects fresh CTags from
     * RemoteBackends (one PROPFIND per parent URL) and fresh fingerprints
     * from LocalBackends. For each mapping, if both endpoints' fresh
     * state matches the stored baseline AND skipUnchangedMappings() is
     * true, the mapping ID is added to m_skippedMappingIds. Fresh state
     * is also stashed in m_freshState for write-back on success.
     *
     * Idempotent and best-effort. Network failures / missing baselines
     * yield "no skip" (safe default).
     */
    void prepareSyncFastPath();

    /**
     * @brief Per-mapping fresh state captured during prepareSyncFastPath
     * and consumed by onWorkerSyncCompleted to persist baselines on success.
     */
    struct FreshSyncState {
        QString sourceCtag;        // empty if source is not RemoteBackend
        QString sourceFingerprint; // empty if source is not LocalBackend
        QString targetCtag;        // empty if target is not RemoteBackend
        QString targetFingerprint; // empty if target is not LocalBackend
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
    void onWorkerItemReady(const QString &calendarId,
                           const KCalendarCore::Incidence::Ptr &incidence,
                           int changeType);
    void onWorkerWriteProgress(const QString &calendarId, int current, int total);
    void onWorkerConflictDetected(const ConflictInfo &conflict);
    void onWorkerConflictPauseRequested(const ConflictInfo &conflict);
    void onWorkerSyncCompleted(const QString &mappingId, const SyncResult &result);
    void onWorkerSyncError(const QString &mappingId, const QString &errorMessage);
    void onWorkerTranscodingWarning(const QString &calendarId, const QString &uid,
                                     const QStringList &warnings);

private:
    BackendRegistry *m_registry;
    ISyncHost *m_controller;
    CalendarBaselineStore *m_calendarBaselines = nullptr;
    BlobBaselineStore *m_blobBaselines = nullptr;  // Phase D Task 20
    SyncConflictStore *m_conflictStore = nullptr;
    ConflictManager *m_conflictManager = nullptr;
    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry m_conflictRegistry;
    TranscodingRouter m_transcodingRouter;
    CalendarDomainAdapter m_calendarAdapter;
    BlobDomainAdapter m_blobAdapter;
    ICalendarCollection *m_collection = nullptr;
    QList<SyncMapping> m_syncMappings;

    bool m_isSyncing = false;
    bool m_cancelled = false;
    int m_currentMappingIndex = -1;
    SyncResult m_lastResult;

    // Phase-2 skip optimization
    bool m_skipUnchangedMappings = false;
    QSet<QString> m_skippedMappingIds;
    QMap<QString, FreshSyncState> m_freshState;

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
