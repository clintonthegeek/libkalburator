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

    /// Register a plugin AFTER initialize() has been called and run
    /// its registerEdges() against the process-wide
    /// TransformationRegistry immediately. Permits third-party
    /// backend plugins to introduce non-stock domains at plugin-load
    /// time. Safe to call from any thread that is not concurrently
    /// calling registerEdges/compile (typical: app startup, single
    /// thread, before sync work begins).
    ///
    /// Constraints (asserted in debug, returns silently in release):
    /// - The TransformationRegistry must not have frozen the affected
    ///   domain yet (i.e. compile() has not been called for any shape
    ///   in this domain). See TransformationRegistry::isFrozen.
    /// - If a plugin for this domain already exists, the new plugin's
    ///   peer shapes and edges are unioned in; canonical-shape
    ///   conflicts error.
    void registerPlugin(std::shared_ptr<DomainPlugin>);

    /// Test-only: drop registrations and reset initialised flag.
    void clear();

private:
    DomainRegistry() = default;

    QList<std::shared_ptr<DomainPlugin>> m_plugins;
    QHash<DomainId, DomainPlugin*> m_byDomain;
    bool m_initialized = false;
    TransformationRegistry* m_registry = nullptr;
};

}  // namespace Kalburator::Shape
