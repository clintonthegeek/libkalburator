#ifndef KALBURATOR_MAPPINGSCHEDULER_H
#define KALBURATOR_MAPPINGSCHEDULER_H

#include <QHash>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

namespace Kalburator::Engine {

/// Resource-aware FIFO scheduler for sync mappings. Tracks which backend
/// resources each mapping uses so that resource-based cancellation
/// (CancellationReason::ResourceLost) can selectively remove affected
/// mappings from the queue.
///
/// v1 semantics: global capacity-1 (one mapping runs at a time regardless
/// of resource overlap). The resource graph is maintained so that v2 can
/// enable concurrent disjoint-component execution without API changes.
class MappingScheduler {
public:
    using DispatchFn = std::function<void(const QString &mappingId)>;

    /// Enqueue @p mappingIds for execution in FIFO order.
    /// @p resourceMap maps each mapping id to the set of resource ids its
    /// backends require. The @p dispatch callback is invoked immediately
    /// for the first mapping and again each time onCompleted() is called
    /// while more mappings remain.
    void enqueue(const QStringList &mappingIds,
                 const QHash<QString, QSet<QString>> &resourceMap,
                 DispatchFn dispatch);

    /// Report that the mapping with @p mappingId has completed (succeeded,
    /// failed, or was cancelled). Triggers dispatch of the next queued
    /// mapping if any.
    void onCompleted(const QString &mappingId);

    /// Remove all queued (not-yet-dispatched) mappings whose resource set
    /// intersects @p resourceId. Returns the list of removed mapping IDs so
    /// the caller can append cancelled SyncResults for them.
    ///
    /// The currently-active (dispatched but not yet completed) mapping is
    /// NOT removed here — the caller is responsible for cancelling it via
    /// the underlying QFuture mechanism if its resource set also contains
    /// @p resourceId.
    QStringList cancelMappingsTouchingResource(const QString &resourceId);

    /// True if a mapping is currently dispatched (active).
    bool hasActive() const { return !m_active.isEmpty(); }

    /// True if any mappings remain in the queue.
    bool hasQueued() const { return !m_pending.isEmpty(); }

    /// Currently-active mapping id, or empty if idle.
    QString active() const { return m_active; }

    /// Resource set for the currently-active mapping, or empty if idle.
    QSet<QString> activeResources() const;

private:
    void tryDispatch();

    DispatchFn                    m_dispatch;
    QString                       m_active;
    QQueue<QString>               m_pending;
    QHash<QString, QSet<QString>> m_resources; // mapping id → resource set
};

} // namespace Kalburator::Engine

#endif // KALBURATOR_MAPPINGSCHEDULER_H
