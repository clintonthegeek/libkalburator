#ifndef KALBURATOR_TRANSCODINGROUTER_H
#define KALBURATOR_TRANSCODINGROUTER_H

#include "transcodingplan.h"
#include <QString>

namespace Kalburator::Sync {

class TranscodingRegistry;

/// Routes transcoding decisions for a per-engine. Owns no state of
/// its own beyond a reference to the registry it queries. The
/// registry must outlive the router. Production code constructs the
/// router with TranscodingRegistry::instance(); tests can construct
/// a fresh registry per test for isolation.
///
/// Phase E semantics: gate is `sourceType != targetType`; capability
/// objects are not consulted. Capability-aware routing is deferred
/// to Phase F (see 04n-phase-e-transcoding-design.md §7).
class TranscodingRouter
{
public:
    explicit TranscodingRouter(TranscodingRegistry& registry);

    TranscodingPlan plan(const QString& sourceType,
                         const QString& targetType) const;

private:
    TranscodingRegistry& m_registry;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_TRANSCODINGROUTER_H
