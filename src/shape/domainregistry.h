#pragma once

#include <QHash>
#include <memory>

#include "shape.h"

namespace Kalburator::Shape {

class DomainDefinition;

/// Process-wide registry mapping DomainId → DomainDefinition.
///
/// Plugins register their DomainDefinition contributions via
/// PluginManager::loadInProcess() which calls registerDefinition() for
/// each definition returned by Plugin::domainDefinitions(). The engine
/// calls definitionFor() at sync time to retrieve canonical-shape info.
class DomainRegistry {
public:
    static DomainRegistry& instance();

    /// Register a DomainDefinition contributed by a plugin.
    /// Returns false if a definition for that domain is already registered
    /// (first registration wins per domain).
    bool registerDefinition(std::shared_ptr<DomainDefinition> def);

    /// Remove a DomainDefinition registered via registerDefinition().
    /// No-op if the domain was not registered via this method.
    void unregisterDefinition(const DomainId &domain);

    /// Look up a DomainDefinition by domain id. Returns nullptr if not found.
    DomainDefinition* definitionFor(const DomainId &domain) const;

    /// Test-only: drop all registrations.
    void clear();

private:
    DomainRegistry() = default;

    QHash<DomainId, std::shared_ptr<DomainDefinition>> m_definitions;
};

}  // namespace Kalburator::Shape
