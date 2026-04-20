#ifndef SYNCCOORDINATOR_H
#define SYNCCOORDINATOR_H

#include "synctypes.h"
#include "syncdiff.h"
#include "syncworker.h"
#include <QObject>
#include <QList>
#include <QPointer>
#include <QThread>

class BackendRegistry;
class ISyncHost;
class ICalendarCollection;
class SyncStore;
class ISyncConfigStore;
class ConflictManager;
class DecSyncActiveController;

/**
 * @brief Coordinates sync operations between backends according to sync mappings.
 *
 * SyncCoordinator implements the core sync algorithm:
 *
 * 1. Load sync mappings from KalbConfigManager
 * 2. For each enabled mapping:
 *    a. Load records from source backend
 *    b. Load records from target backend
 *    c. Load baselines from SyncStore
 *    d. Compute 3-way diff
 *    e. Apply changes based on sync mode
 *    f. Handle conflicts according to policy
 *    g. Update baselines in SyncStore
 *
 * The SyncStore provides persistent storage for:
 * - Identity mappings (local UID <-> remote ID)
 * - Version hashes for change detection
 * - Baseline data for 3-way merge
 * - Unresolved conflict tracking
 */
class SyncCoordinator : public QObject
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

    explicit SyncCoordinator(BackendRegistry *registry,
                              ISyncHost *host,
                              QObject *parent = nullptr);
    ~SyncCoordinator() override;

    /**
     * @brief Set the SyncStore for persistent sync metadata.
     *
     * Must be called before runSync() to enable baseline tracking
     * and conflict detection. If not set, sync operations will still
     * work but won't persist baselines (every sync is treated as first sync).
     */
    void setSyncStore(SyncStore *store);

    /**
     * @brief Get the current SyncStore.
     */
    SyncStore* syncStore() const { return m_syncStore; }

    /**
     * @brief Set the ConflictManager for handling user-resolved conflicts.
     *
     * When set, conflicts with AskUser policy will be presented to the user
     * via the ConflictManager's dialog, and the resolution will be applied
     * immediately. If not set, such conflicts are recorded but not resolved.
     */
    void setConflictManager(ConflictManager *manager) { m_conflictManager = manager; }

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
    void setCollection(ICalendarCollection *collection) { m_collection = collection; }

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
    SyncStore *m_syncStore = nullptr;
    ConflictManager *m_conflictManager = nullptr;
    ICalendarCollection *m_collection = nullptr;
    QList<SyncMapping> m_syncMappings;

    bool m_isSyncing = false;
    bool m_cancelled = false;
    int m_currentMappingIndex = -1;
    SyncResult m_lastResult;

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

#endif // SYNCCOORDINATOR_H
