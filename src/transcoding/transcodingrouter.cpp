#include "transcodingrouter.h"
#include "transcodingregistry.h"

namespace Kalburator::Sync {

TranscodingRouter::TranscodingRouter(TranscodingRegistry& registry)
    : m_registry(registry)
{}

TranscodingPlan TranscodingRouter::plan(const QString& sourceType,
                                        const QString& targetType) const
{
    if (sourceType.isEmpty() || targetType.isEmpty()
        || sourceType == targetType) {
        return TranscodingPlan{};
    }
    auto transcoders = m_registry.findTranscoders(sourceType, targetType);
    if (transcoders.isEmpty()) {
        return TranscodingPlan{};
    }
    TranscodingPlan plan;
    plan.transcoders = transcoders;
    plan.routingDecision = QStringLiteral("source=%1 target=%2, %3 transcoders")
        .arg(sourceType, targetType)
        .arg(transcoders.size());
    return plan;
}

} // namespace Kalburator::Sync
