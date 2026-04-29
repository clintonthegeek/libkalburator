#ifndef KALBURATOR_SYNCENGINE_H
#define KALBURATOR_SYNCENGINE_H

#include "synctypes.h"
#include "syncdiff.h"
#include "syncworker.h"
#include "calendardomainadapter.h"
#include "conflicthandlerregistry.h"
#include "transcodingrouter.h"
#include <QObject>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QThread>

namespace Kalburator::Sync {

class BackendRegistry;
class BlobBaselineStore;
class ISyncHost;
class ICalendarCollection;
class CalendarBaselineStore;
class SyncConflictStore;
class ISyncConfigStore;
class ConflictManager;
class DecSyncActiveController;

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
     * When set, SyncWorker's subsequent-sync blob fetch skips records whose
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
     * This signal is forwarded from SyncWorker when an incidence requires
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
    SyncWorker *m_worker = nullptr;
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

#endif // KALBURATOR_SYNCENGINE_H
