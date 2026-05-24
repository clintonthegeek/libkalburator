#include "domainoperationsregistry.h"

#include "shaperegistries.h"

namespace Kalburator::Shape {

DomainOperationsRegistry &DomainOperationsRegistry::instance()
{
    return defaultShapeRegistries().operations;
}

bool DomainOperationsRegistry::registerOperations(std::shared_ptr<DomainOperations> ops)
{
    if (!ops)
        return false;
    const auto id = ops->targetDomain();
    if (m_byDomain.contains(id))
        return false;
    m_byDomain.insert(id, std::move(ops));
    return true;
}

DomainOperations *DomainOperationsRegistry::operationsFor(const DomainId &domain) const
{
    auto it = m_byDomain.find(domain);
    return (it == m_byDomain.end()) ? nullptr : it.value().get();
}

void DomainOperationsRegistry::clear()
{
    m_byDomain.clear();
}

void DomainOperationsRegistry::unregister(const DomainId &domain)
{
    m_byDomain.remove(domain);
}

} // namespace Kalburator::Shape
