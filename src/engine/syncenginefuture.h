#ifndef KALBURATOR_SYNCENGINEFUTURE_H
#define KALBURATOR_SYNCENGINEFUTURE_H

#include "synctypes.h"
#include <QFuture>
#include <QString>
#include <memory>

namespace Kalburator::Engine {

using Kalburator::Sync::SyncResult;

/// Reason a SyncEngineFuture was cancelled. Attached as a side-channel
/// because Qt6's QFuture does not carry cancellation reasons natively.
enum class CancellationReason {
    UserRequested,      ///< Caller explicitly cancelled (default)
    ResourceLost,       ///< A backend resource became unavailable (e.g., Palm cradle disconnect)
    Timeout,            ///< Overall sync exceeded time limit
    UnrecoverableError, ///< Engine-internal failure with no recovery path
};

/// Thin wrapper around QFuture<QList<SyncResult>> that adds a reasoned
/// cancellation side-channel. Returned by SyncEngine::runSyncFuture().
///
/// The wrapper is copyable — copies share the same cancellation-reason
/// state via shared_ptr, so calling cancelWithReason() on any copy is
/// visible to all observers.
class SyncEngineFuture {
public:
    SyncEngineFuture() = default;
    explicit SyncEngineFuture(QFuture<QList<SyncResult>> future);

    // ---- QFuture forwarding ----
    operator QFuture<QList<SyncResult>>() const { return m_future; }
    QFuture<QList<SyncResult>> future() const   { return m_future; }

    bool isStarted()  const { return m_future.isStarted(); }
    bool isFinished() const { return m_future.isFinished(); }
    bool isCanceled() const { return m_future.isCanceled(); }

    /// Returns the results list. Uses resultAt(0) internally to avoid the
    /// Qt6 quirk where results() returns empty after cancellation.
    QList<SyncResult> results() const;

    // ---- Cancellation with reason ----

    /// Cancel with UserRequested reason. Delegates to the underlying
    /// QFuture::cancel() (stops the in-flight mapping and the queue).
    void cancel();

    /// Cancel with an explicit reason. For ResourceLost, also records the
    /// resource ID so callers can inspect which resource was lost.
    void cancelWithReason(CancellationReason reason,
                          const QString &resourceId = {});

    CancellationReason cancellationReason() const;
    QString            lostResourceId()     const;

    bool isValid() const { return m_state != nullptr; }

private:
    struct State {
        CancellationReason reason    = CancellationReason::UserRequested;
        QString            resourceId;
    };
    QFuture<QList<SyncResult>> m_future;
    std::shared_ptr<State>     m_state;
};

} // namespace Kalburator::Engine

#endif // KALBURATOR_SYNCENGINEFUTURE_H
