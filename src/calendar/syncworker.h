#ifndef SYNCWORKER_H
#define SYNCWORKER_H

#include "synctypes.h"
#include "syncdiff.h"

#include <QObject>
#include <QMutex>
#include <QElapsedTimer>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

class BackendRegistry;
class ISyncHost;
class BlobBaselineStore;
class CalendarBaselineStore;
class ICalendarCollection;
class SyncBackend;

/**
 * @brief Worker class that performs sync operations in a background thread.
 *
 * SyncWorker runs in a QThread and handles the entire sync process:
 * - Fetching records from source and target backends
 * - Computing 3-way diff
 * - Handling conflicts based on mode (monitored/unmonitored)
 * - Applying changes to backends
 * - Updating baselines
 *
 * Two sync modes are supported:
 * - Unmonitored: Conflicts are queued for later resolution, sync continues
 * - Monitored: Worker pauses on each conflict until user resolves it
 *
 * Signals use Qt::QueuedConnection for thread-safe cross-thread communication.
 */
class SyncWorker : public QObject
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

    explicit SyncWorker(QObject *parent = nullptr);
    ~SyncWorker() override;

    /**
     * @brief Set dependencies before moving to thread.
     * Must be called before moveToThread().
     */
    void setDependencies(ISyncHost *host,
                         CalendarBaselineStore *calendarBaselines,
                         ICalendarCollection *collection,
                         BlobBaselineStore *blobBaselines = nullptr);

public slots:
    /**
     * @brief Process a sync operation (called from worker thread).
     */
    void processSync(const SyncWorker::Request &request);

    /**
     * @brief Resume after user resolves a conflict (monitored mode).
     * Called from main thread when user completes conflict resolution dialog.
     */
    void resumeAfterConflict(ConflictResolution resolution, const QString &mergedIcal);

    /**
     * @brief Cancel the current sync operation.
     */
    void cancel();

signals:
    /**
     * @brief Emitted when sync starts for a mapping.
     */
    void syncStarted(const QString &mappingId);

    /**
     * @brief Emitted when sync phase changes.
     */
    void phaseChanged(const QString &mappingId, int phase);

    /**
     * @brief Emitted to report fetch progress.
     */
    void fetchProgress(const QString &calendarId, int current, int total);

    /**
     * @brief Emitted when an item is ready for model update.
     * Main thread should add this to GlobalIncidenceModel.
     */
    void itemReady(const QString &calendarId,
                   const KCalendarCore::Incidence::Ptr &incidence,
                   int changeType);  // SyncChangeType cast to int for cross-thread

    /**
     * @brief Emitted to report write progress.
     */
    void writeProgress(const QString &calendarId, int current, int total);

    /**
     * @brief Emitted when a conflict is detected (unmonitored mode).
     * Main thread should queue this in the dock widget.
     */
    void conflictDetected(const ConflictInfo &conflict);

    /**
     * @brief Emitted when sync needs to pause for conflict resolution (monitored mode).
     * Main thread should show dialog, then call resumeAfterConflict().
     */
    void conflictPauseRequested(const ConflictInfo &conflict);

    /**
     * @brief Emitted when sync completes for a mapping.
     */
    void syncCompleted(const QString &mappingId, const SyncResult &result);

    /**
     * @brief Emitted when an error occurs that prevents sync from continuing.
     */
    void syncError(const QString &mappingId, const QString &errorMessage);

    /**
     * @brief Emitted when transcoding causes potential data loss.
     *
     * This signal is emitted for each incidence that requires lossy transcoding
     * when syncing between backends with different capabilities.
     *
     * @param calendarId The calendar being synced
     * @param uid The UID of the incidence being transcoded
     * @param warnings List of warning messages about data loss
     */
    void transcodingWarning(const QString &calendarId,
                            const QString &uid,
                            const QStringList &warnings);

private:
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

    // First-sync dispatch via BlobSyncEngine (Phase D Task 21)
    // Called when there is no CalendarBaselineStore baseline yet for the mapping.
    // Routes through BlobSyncEngine::mirror (OneWayUpload) or twoWayNaive (other
    // modes), then seeds CalendarBaselineStore + BlobBaselineStore so subsequent
    // syncs use the 3-way merge path.
    void dispatchFirstSync(const Request &request);
    void harvestBaselinesAfterFirstSync(const Request &request);

    // Blob-view helpers (Phase D Task 19)
    // Fetch records via IBlobBackend::modifiedSince, translating BackendRecord
    // back to SyncRecord.  Used for the subsequent-sync path (!useQuickPath).
    // Fills `out` with translated records; sets m_fetchFailed on error.
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

    // Post-conflict continuation (signal-based flow)
    void continueAfterConflicts();

    // Backend operations
    void applyChangesToBackend(const QString &backendId,
                               const QString &calendarId,
                               const QList<SyncChange> &changes,
                               bool useTargetRecord = false);

    // Thread synchronization
    QMutex m_mutex;
    bool m_cancelled = false;
    bool m_fetchFailed = false;  // Set when fetch phase fails - abort sync to prevent data loss
    QString m_fetchErrorMessage;  // Error message from failed fetch
    bool m_applyFailed = false;   // Set when apply phase (startSync) fails - skip baseline update
    QString m_applyErrorMessage;  // Error message from failed apply

    // Signal-based conflict handling state (monitored mode)
    // Instead of blocking the worker thread with QWaitCondition, we yield
    // by returning from handleConflicts() and resume via resumeAfterConflict().
    enum class ConflictPhase { ToTarget, ToSource, Done };
    ConflictPhase m_conflictPhase = ConflictPhase::Done;
    int m_conflictIndex = 0;             // Current index in toTarget/toSource list
    bool m_yieldedForConflict = false;   // True when paused waiting for resolution

    // Timing state (persists across yield/resume for final log)
    QElapsedTimer m_totalTimer;
    QElapsedTimer m_phaseTimer;
    qint64 m_propertyFetchMs = 0;
    qint64 m_propertyDiffMs = 0;
    qint64 m_propertyApplyMs = 0;
    qint64 m_sourceFetchMs = 0;
    qint64 m_targetFetchMs = 0;
    qint64 m_diffMs = 0;

    // Dependencies (set before moveToThread)
    ISyncHost *m_controller = nullptr;
    CalendarBaselineStore *m_calendarBaselines = nullptr;
    BlobBaselineStore *m_blobBaselines = nullptr;  // Phase D Task 20: per-record hash skip
    ICalendarCollection *m_collection = nullptr;

    // Current sync state
    Request m_currentRequest;
    QList<SyncRecord> m_sourceRecords;
    QList<SyncRecord> m_targetRecords;
    SyncDiff m_currentDiff;
    SyncResult m_currentResult;
    QList<SyncChange> m_resolvedToTarget;
    QList<SyncChange> m_resolvedToSource;
    int m_resolvedToSourceConflictStart = 0; ///< Index where conflict-resolved entries begin

    // Property sync state
    CalendarPropertyRecord m_sourceProperties;
    CalendarPropertyRecord m_targetProperties;
    CalendarPropertyDiff m_propertyDiff;
};

// Register metatypes for cross-thread signal/slot

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::SyncWorker::Request)
Q_DECLARE_METATYPE(Kalburator::Sync::SyncWorker::Mode)

#endif // SYNCWORKER_H
