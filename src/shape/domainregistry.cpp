#include "domainregistry.h"

#include "domaindefinition.h"

namespace Kalburator::Shape {

bool DomainRegistry::registerDefinition(std::shared_ptr<DomainDefinition> def) {
    if (!def) return false;
    const auto id = def->domain();
    if (m_definitions.contains(id)) return false;
    m_definitions.insert(id, std::move(def));
    return true;
}

void DomainRegistry::unregisterDefinition(const DomainId &domain) {
    m_definitions.remove(domain);
}

DomainDefinition* DomainRegistry::definitionFor(const DomainId &domain) const {
    auto it = m_definitions.constFind(domain);
    return (it == m_definitions.constEnd()) ? nullptr : it.value().get();
}

void DomainRegistry::clear() {
    m_definitions.clear();
}

}  // namespace Kalburator::Shape
