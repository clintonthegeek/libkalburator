#ifndef KALBURATOR_SYNC_SYNCRUNCOORDINATOR_H
#define KALBURATOR_SYNC_SYNCRUNCOORDINATOR_H

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QList>

// Forward-declare to avoid pulling heavy headers into callers.
namespace Kalburator::Storage { class BaselineStore; }
namespace Kalburator::Sync {
    class SyncConflictStore;
    class ConflictManager;
}
namespace Kalburator::Engine { class SyncEngine; }

// SyncResult is a value type used in the QFuture signature — needs full type.
#include "synctypes.h"
// SyncEngine::SyncBehavior enum — used in runSync() parameter.
#include "syncengine.h"

namespace Kalburator::Sync {

/**
 * @brief Manages the per-session sync execution lifecycle.
 *
 * Extracted from PlanStan::CollectionController per spec
 * 2026-05-22-collectioncontroller-decomp-and-akonadi-api-design.md
 * (step 2).
 *
 * Owns:
 *  - The QFutureWatcher that observes the SyncEngine run future.
 *  - Signal emission for sync-start and sync-finish events.
 *
 * Borrowed (not owned, must outlive this object):
 *  - SyncEngine* — created by the host (CollectionController) in
 *    initializeSyncInfrastructure() and passed in.
 *  - BaselineStore*, SyncConflictStore*, ConflictManager* — same ownership
 *    rule; host creates them, passes them here for accessor access.
 *
 * What this class does NOT own:
 *  - AppSettings lookup — the host resolves the behavior enum before
 *    calling runSync(behavior), keeping libkalburator app-settings-free.
 *  - Discovery-coordinator state (orphan calendar detection) — remains in
 *    CollectionController, which connects to syncRunFinished() for post-
 *    processing.
 *  - CalendarJournal / crash recovery — host initializes before handing
 *    the engine to this coordinator.
 *  - generateSyncMappingsFromLogicalCalendars() — touches KalbConfigManager
 *    (libkalcal territory); stays in CollectionController per § 4.5 rule.
 *  - maybeInitSyncInfrastructure() / initializeSyncInfrastructure() —
 *    deeply entangled with host-side state (m_kalbFilePath, m_stagingController,
 *    m_backends, m_collection, AppSettings); stays in CollectionController.
 */
class SyncRunCoordinator : public QObject
{
    Q_OBJECT
public:
    /**
     * @param engine        Borrowed. Must be non-null and outlive this object.
     * @param baselineStore Borrowed. May be null (store creation may fail).
     * @param conflictStore Borrowed. May be null.
     * @param conflictMgr   Borrowed. May be null.
     * @param parent        Qt parent.
     */
    explicit SyncRunCoordinator(Kalburator::Engine::SyncEngine *engine,
                                Kalburator::Storage::BaselineStore *baselineStore,
                                Kalburator::Sync::SyncConflictStore *conflictStore,
                                Kalburator::Sync::ConflictManager *conflictMgr,
                                QObject *parent = nullptr);
    ~SyncRunCoordinator() override;

    // ── Run control ───────────────────────────────────────────────────────────

    /**
     * @brief Start a sync run with the given behavior.
     *
     * No-op if the engine is null, has no sync work, or is already syncing.
     * Emits syncRunStarted() when the run begins.
     *
     * @param behavior  Monitored (pause on conflict) or Unmonitored (defer).
     */
    void runSync(Kalburator::Engine::SyncEngine::SyncBehavior behavior);

    /**
     * @brief Returns the QFuture from the most recent runSync() call.
     *
     * Returns a default-constructed (already-finished, empty) future if no
     * sync has been started.
     */
    QFuture<QList<Kalburator::Sync::SyncResult>> currentSyncFuture() const;

    // ── Accessors (borrowed pointers; host retains ownership) ─────────────────

    Kalburator::Engine::SyncEngine *syncEngine() const { return m_engine; }
    Kalburator::Storage::BaselineStore *baselineStore() const { return m_baselineStore; }
    Kalburator::Sync::SyncConflictStore *syncConflictStore() const { return m_conflictStore; }
    Kalburator::Sync::ConflictManager *conflictManager() const { return m_conflictMgr; }

Q_SIGNALS:
    /**
     * @brief Emitted immediately after runSync() kicks off the future.
     *
     * Consumers (e.g. SyncProgressManager) connect to this to call
     * currentSyncFuture() and attach their own watcher.
     */
    void syncRunStarted();

    /**
     * @brief Emitted when the run completes, carrying the aggregate result.
     *
     * success=false if any mapping failed or the future was cancelled.
     * errorMessage is populated on failure.
     */
    void syncRunFinished(const Kalburator::Sync::SyncResult &result);

    /**
     * @brief Emitted alongside syncRunFinished(); convenience bool.
     */
    void allSyncsFinished(bool success);

private Q_SLOTS:
    void onSyncRunFinished();

private:
    Kalburator::Engine::SyncEngine       *m_engine;        // borrowed
    Kalburator::Storage::BaselineStore   *m_baselineStore; // borrowed
    Kalburator::Sync::SyncConflictStore  *m_conflictStore; // borrowed
    Kalburator::Sync::ConflictManager    *m_conflictMgr;   // borrowed

    QFutureWatcher<QList<Kalburator::Sync::SyncResult>> *m_watcher = nullptr;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_SYNCRUNCOORDINATOR_H
