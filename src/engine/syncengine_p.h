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
#include "shape.h"
#include "synctypes.h"
#include "../sync/syncoperation.h"
#include "shaperegistries.h"
#include "syncengine.h"

#include <QObject>
#include <QList>
#include <QMutex>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <atomic>
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

#endif // KALBURATOR_SYNCENGINE_P_H
