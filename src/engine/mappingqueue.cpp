#include "mappingqueue.h"

namespace Kalburator::Engine {

void MappingQueue::prime(QList<SyncMapping> mappings,
                         std::optional<QSet<QString>> filter)
{
    m_mappings       = std::move(mappings);
    m_filter         = std::move(filter);
    m_results.clear();
    m_lostResources.clear();
    m_pending.clear();
    for (const auto &m : std::as_const(m_mappings)) {
        const bool enabled  = m.enabled;
        const bool inFilter = !m_filter.has_value() || m_filter->contains(m.id);
        if (enabled && inFilter)
            m_pending.append(m);
    }
    m_startedCount   = 0;
    m_exhausted      = m_pending.isEmpty();
    m_dispatchMode   = DispatchMode::Queue;
}

void MappingQueue::primeSingle()
{
    m_mappings.clear();
    m_filter.reset();
    m_results.clear();
    m_lostResources.clear();
    m_pending.clear();
    m_startedCount   = 0;
    m_exhausted      = false;
    m_dispatchMode   = DispatchMode::Single;
}

std::optional<SyncMapping> MappingQueue::next()
{
    return nextEligible([](const SyncMapping &) { return true; });
}

std::optional<SyncMapping> MappingQueue::nextEligible(
    const std::function<bool(const SyncMapping &)> &predicate)
{
    if (m_dispatchMode != DispatchMode::Queue) {
        // Single / None modes do not iterate.
        return std::nullopt;
    }

    for (int i = 0; i < m_pending.size(); ++i) {
        if (predicate(m_pending.at(i))) {
            SyncMapping m = m_pending.takeAt(i);
            ++m_startedCount;
            m_exhausted = m_pending.isEmpty();
            return m;
        }
    }
    // Nothing pending satisfies the predicate right now. Not the same as
    // exhausted — a predicate rejection (endpoint collision) leaves
    // candidates that may become eligible once something else completes.
    return std::nullopt;
}

void MappingQueue::pushBack(const SyncMapping &m)
{
    // The pool was exhausted despite the cap check after nextEligible()
    // already counted this mapping as started — undo that, it never
    // actually dispatched. Front of the list so it is tried again first.
    m_pending.prepend(m);
    if (m_startedCount > 0)
        --m_startedCount;
    m_exhausted = false;
}

void MappingQueue::recordResult(SyncResult result)
{
    if (m_dispatchMode != DispatchMode::Queue) {
        // Single-mapping runs report directly on the QFutureInterface;
        // accumulating here would leak into the next Queue run.
        return;
    }
    m_results.append(std::move(result));
}

QList<SyncResult> MappingQueue::drain()
{
    QList<SyncResult> out;
    out.swap(m_results);
    return out;
}

void MappingQueue::reset()
{
    m_mappings.clear();
    m_filter.reset();
    m_results.clear();
    m_lostResources.clear();
    m_pending.clear();
    m_startedCount = 0;
    m_exhausted    = false;
    m_dispatchMode = DispatchMode::None;
}

void MappingQueue::markResourceLost(const QString &resourceId)
{
    if (resourceId.isEmpty())
        return;
    m_lostResources.insert(resourceId);
}

bool MappingQueue::isResourceLost(const QString &resourceId) const
{
    return m_lostResources.contains(resourceId);
}

} // namespace Kalburator::Engine
