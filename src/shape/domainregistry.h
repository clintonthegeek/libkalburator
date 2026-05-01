#pragma once

#include <QHash>
#include <QList>
#include <memory>

#include "shape.h"

namespace Kalburator::Shape {

class DomainPlugin;
class TransformationRegistry;

/// Process-wide registry of DomainPlugin instances. Stock plugins
/// register themselves at static-init via a registrar object in
/// each plugin's translation unit; initialize() then calls
/// registerEdges() on each, populating the TransformationRegistry.
class DomainRegistry {
public:
    static DomainRegistry& instance();

    void registerDomain(std::shared_ptr<DomainPlugin>);
    DomainPlugin* findByDomain(const DomainId&) const;
    QList<DomainPlugin*> all() const;

    /// Call registerEdges() on each plugin in registration order,
    /// populating `r`. Idempotent — safe to call multiple times;
    /// subsequent calls are no-ops.
    void initialize(TransformationRegistry& r);

    /// Test-only: drop registrations and reset initialised flag.
    void clear();

private:
    DomainRegistry() = default;

    QList<std::shared_ptr<DomainPlugin>> m_plugins;
    QHash<DomainId, DomainPlugin*> m_byDomain;
    bool m_initialized = false;
};

}  // namespace Kalburator::Shape
