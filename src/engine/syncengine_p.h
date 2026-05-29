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
#include "syncoperation.h"
#include "shaperegistries.h"
#include "syncengine.h"

#include <QObject>
#include <QList>
#include <QMutex>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <type_traits>

namespace Kalburator::Storage {
class BaselineStore;
} // namespace Kalburator::Storage

namespace Kalburator::Sync {
class SyncBackend;
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
using Kalburator::Sync::SyncBackend;
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

    explicit SyncEngineWorker(const Kalburator::Shape::ShapeRegistries &shape,
                              QObject *parent = nullptr);
    ~SyncEngineWorker() override;

    /**
     * @brief Set dependencies before moving to thread.
     * Must be called before moveToThread().
     */
    void setDependencies(ISyncHost *host,
                         ICalendarCollection *collection,
                         Kalburator::Storage::BaselineStore *baselineStore = nullptr,
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

    void runPropertyPhase(Kalburator::Shape::DomainOperations *ops,
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
    // Phase N.1: domain plugin's canonical differ + merger, acquired once per
    // dispatchSync and retained across AskUser pause/resume.
    std::unique_ptr<Kalburator::Shape::RecordDiffer> m_unifiedDiffer;
    std::unique_ptr<Kalburator::Shape::RecordMerger> m_unifiedMerger;

    QElapsedTimer m_totalTimer;
    QElapsedTimer m_phaseTimer;

    const Kalburator::Shape::ShapeRegistries &m_shape;
    ISyncHost *m_controller = nullptr;
    Kalburator::Storage::BaselineStore *m_baselineStore = nullptr;
    ICalendarCollection *m_collection = nullptr;
    // Back-pointer to the owning SyncEngine. dispatchSync uses
    // QMetaObject::invokeMethod(m_engine, ...) to marshal baseline-store
    // access back to the engine thread.
    SyncEngine *m_engine = nullptr;

    Request m_currentRequest;
    SyncResult m_currentResult;
};

} // namespace Kalburator::Engine

Q_DECLARE_METATYPE(Kalburator::Engine::SyncEngineWorker::Request)
Q_DECLARE_METATYPE(Kalburator::Engine::SyncEngineWorker::Mode)

#endif // KALBURATOR_SYNCENGINE_P_H
