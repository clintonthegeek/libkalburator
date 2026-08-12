#ifndef ISYNCHOST_H
#define ISYNCHOST_H

#include <QHash>
#include <QString>

#include "canonicalrecord.h"
#include "lossprofile.h"
#include "synctypes.h"

namespace Kalburator::Sync {

class BackendRegistry;
class SyncBackend;
class ISyncConfigStore;

/**
 * @brief Abstract interface decoupling sync engine from the application shell.
 *
 * G.9.a narrowed this interface to ~7 generic methods; the calendar-typed
 * methods were deleted in Phase G Task 67. New code should implement only
 * the generic lifecycle events below.
 *
 * Threading (parallel-sync pre-flight audit,
 * docs/audits/2026-08-12-parallel-sync-preflight.md): `syncStarted()` and
 * `recordChanged()` are called directly (a plain virtual call, not a
 * queued Qt signal) from `SyncEngineWorker`'s own thread — see
 * `syncStarted` and `recordChanged` below for the exact call sites. Under
 * the N-worker pool, this means they may be called concurrently from any
 * of N different worker threads at once, on the SAME `ISyncHost` instance
 * (one host, shared by every worker). Implementations that mutate
 * GUI-affine state (widgets, models bound to a view) from these two
 * methods MUST marshal to their own thread themselves — the engine does
 * not do it for you, the same contract `IMassDeleteGuard::confirmMassDelete`
 * already documents. PlanStan's `CollectionController::recordChanged()` is
 * the reference implementation: it marshals its model-mutating tail via
 * `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` and its backend
 * re-read via a blocking-queued helper, while its own local logic holds no
 * instance-mutable state (reentrant-safe by construction). The other
 * lifecycle virtuals below (`syncFinished`, `resolveConflict`,
 * `progressChanged`, `phaseChanged`, `errorOccurred`) are not invoked as
 * direct calls anywhere in the engine as of this audit — their observable
 * engine-side equivalents, where they exist, are delivered as ordinary
 * queued Qt signals instead. If that ever changes, the same contract
 * applies to whichever thread newly calls them.
 */
class ISyncHost
{
public:
    virtual ~ISyncHost() = default;

    // ---- Registry access ----
    // Plan 8 step 1 (RFC 2026-06-10, PlanStan-acked): non-pure, with
    // registry-backed defaults. ADDITIVE — hosts that carry their own backend
    // storage (PlanStan CC's m_backends bridge) keep their overrides
    // unchanged; hosts without can inject the registry and drop theirs.

    /**
     * @brief Inject the registry backing the default lookups below.
     *
     * Non-owning: the host app owns the registry and must keep it alive for
     * this host's lifetime (or inject nullptr first). Never required — a
     * host that overrides backendById()/backends() can ignore this entirely.
     */
    virtual void setBackendRegistry(BackendRegistry *registry);

    /**
     * @brief Default: registry lookup + dynamic_cast<SyncBackend*>.
     *
     * nullptr when no registry is injected, the id is unknown, or the
     * registered instance is not a calendar-typed backend — a clean miss,
     * never UB (retires the unchecked static_cast bridges; v0.66 precedent).
     */
    virtual SyncBackend* backendById(const QString &id);

    /**
     * @brief Default: registry walk with the same cast. Non-calendar
     * instances are OMITTED (not inserted as nullptr).
     */
    virtual QHash<QString, SyncBackend*> backends();

    virtual ISyncConfigStore* configStore() = 0;

    // ---- Generic lifecycle events (G.9.a — new in Task 63) ----

    enum class ChangeKind { Created, Updated, Deleted };

    /// Called from SyncEngineWorker's own thread (worker thread, not the
    /// SyncEngine/GUI thread) — see the class-level threading note above.
    virtual void syncStarted(const QString &mappingId,
                             const Kalburator::Shape::LossProfile &pipelineLoss) {}

    virtual void syncFinished(const QString &mappingId,
                              const Kalburator::Sync::SyncResult &result) {}

    /// Called from SyncEngineWorker's own thread (worker thread, not the
    /// SyncEngine/GUI thread), once per successfully-applied record — see
    /// the class-level threading note above. Under the N-worker pool this
    /// may arrive concurrently from multiple worker threads on the same
    /// host instance; implementations must marshal any GUI-affine mutation
    /// themselves.
    virtual void recordChanged(const QString &mappingId,
                               const QString &recordId,
                               ChangeKind kind) {}

    virtual ConflictResolution resolveConflict(const QString &mappingId,
                                               const QString &recordId,
                                               const Kalburator::Shape::CanonicalRecord &source,
                                               const Kalburator::Shape::CanonicalRecord &target,
                                               const Kalburator::Shape::CanonicalRecord &baseline)
    {
        Q_UNUSED(mappingId) Q_UNUSED(recordId)
        Q_UNUSED(source) Q_UNUSED(target) Q_UNUSED(baseline)
        return ConflictResolution::SourceWins;
    }

    virtual void progressChanged(const QString &mappingId,
                                 int current, int total,
                                 const QString &msg) {}

    virtual void phaseChanged(const QString &mappingId, int phase) {}

    virtual void errorOccurred(const QString &mappingId, const QString &msg) {}

protected:
    /// Non-owning; see setBackendRegistry().
    BackendRegistry *m_backendRegistry = nullptr;
};

} // namespace Kalburator::Sync

#endif // ISYNCHOST_H
