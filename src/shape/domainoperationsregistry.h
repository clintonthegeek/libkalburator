#ifndef KALBURATOR_SHAPE_DOMAINOPERATIONSREGISTRY_H
#define KALBURATOR_SHAPE_DOMAINOPERATIONSREGISTRY_H

#include <QHash>
#include <memory>

#include "domainoperations.h"

namespace Kalburator::Shape {

/// Process-wide registry mapping DomainId → DomainOperations.
///
/// Plugins call registerOperations() at static-init (or explicitly
/// from PluginManager::loadInProcess()) to make their I/O implementation
/// available. The engine calls operationsFor() at sync time to retrieve
/// the right writer factory for a given domain.
///
/// Double-binding the same domain is rejected (registerOperations returns
/// false) to catch accidental duplicate registration at startup.
class DomainOperationsRegistry {
public:
    static DomainOperationsRegistry &instance();

    /// Register @p ops for its targetDomain(). Returns false if a
    /// DomainOperations for that domain is already registered, or if
    /// @p ops is null.
    bool registerOperations(std::shared_ptr<DomainOperations> ops);

    /// Look up the DomainOperations for @p domain. Returns nullptr if
    /// no implementation has been registered.
    DomainOperations *operationsFor(const DomainId &domain) const;

    /// Remove all registrations. Intended for test teardown only.
    void clear();

    /// Remove the registration for @p domain. No-op if not registered.
    void unregister(const DomainId &domain);

private:
    DomainOperationsRegistry() = default;
    QHash<DomainId, std::shared_ptr<DomainOperations>> m_byDomain;
};

} // namespace Kalburator::Shape

#endif // KALBURATOR_SHAPE_DOMAINOPERATIONSREGISTRY_H
