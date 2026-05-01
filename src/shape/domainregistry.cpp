#include "domainregistry.h"

#include "domainplugin.h"

namespace Kalburator::Shape {

DomainRegistry& DomainRegistry::instance() {
    static DomainRegistry s_instance;
    return s_instance;
}

void DomainRegistry::registerDomain(std::shared_ptr<DomainPlugin> plugin) {
    if (!plugin) return;
    const auto id = plugin->domain();
    if (m_byDomain.contains(id)) return;  // first registration wins
    m_byDomain.insert(id, plugin.get());
    m_plugins.append(std::move(plugin));
}

DomainPlugin* DomainRegistry::findByDomain(const DomainId& d) const {
    return m_byDomain.value(d, nullptr);
}

QList<DomainPlugin*> DomainRegistry::all() const {
    QList<DomainPlugin*> out;
    out.reserve(m_plugins.size());
    for (const auto& p : m_plugins) out.append(p.get());
    return out;
}

void DomainRegistry::initialize(TransformationRegistry& r) {
    if (m_initialized) return;
    for (const auto& p : m_plugins) {
        p->registerEdges(r);
    }
    m_initialized = true;
}

void DomainRegistry::clear() {
    m_plugins.clear();
    m_byDomain.clear();
    m_initialized = false;
}

}  // namespace Kalburator::Shape
