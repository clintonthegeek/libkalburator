#include "mappingqueue.h"

namespace Kalburator::Engine {

void MappingQueue::prime(QList<SyncMapping> mappings,
                         std::optional<QSet<QString>> filter)
{
    m_mappings       = std::move(mappings);
    m_filter         = std::move(filter);
    m_results.clear();
    m_lostResources.clear();
    m_currentIndex   = -1;
    m_exhausted      = false;
    m_dispatchMode   = DispatchMode::Queue;
}

void MappingQueue::primeSingle()
{
    m_mappings.clear();
    m_filter.reset();
    m_results.clear();
    m_lostResources.clear();
    m_currentIndex   = -1;
    m_exhausted      = false;
    m_dispatchMode   = DispatchMode::Single;
}

std::optional<SyncMapping> MappingQueue::next()
{
    if (m_dispatchMode != DispatchMode::Queue) {
        // Single / None modes do not iterate.
        return std::nullopt;
    }

    ++m_currentIndex;
    while (m_currentIndex < m_mappings.size()) {
        const SyncMapping &m = m_mappings[m_currentIndex];
        const bool enabled  = m.enabled;
        const bool inFilter = !m_filter.has_value()
                              || m_filter->contains(m.id);
        if (enabled && inFilter) {
            return m;
        }
        ++m_currentIndex;
    }

    // Past the end.
    m_exhausted = true;
    return std::nullopt;
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
    m_currentIndex = -1;
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
