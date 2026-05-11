#include "domainregistry.h"

#include "domaindefinition.h"
#include "domainplugin.h"
#include "transformationregistry.h"

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
    m_registry = &r;
    if (m_initialized) return;
    for (const auto& p : m_plugins) {
        p->registerEdges(r);
    }
    m_initialized = true;
}

void DomainRegistry::registerPlugin(std::shared_ptr<DomainPlugin> plugin)
{
    Q_ASSERT(plugin);
    if (!plugin) return;
    Q_ASSERT_X(m_initialized, "registerPlugin",
               "DomainRegistry::registerPlugin() called before initialize()");
    if (!m_initialized) return;

    // Append to plugin list and (if new) index by domain.
    const auto domain = plugin->domain();
    if (!m_byDomain.contains(domain)) {
        m_byDomain.insert(domain, plugin.get());
    }
    m_plugins.append(plugin);

    // Drive its edges into the registry immediately. Subsequent calls
    // are no-ops courtesy of the registry's idempotent register*().
    plugin->registerEdges(*m_registry);
}

void DomainRegistry::clear() {
    m_plugins.clear();
    m_byDomain.clear();
    m_initialized = false;
    m_registry = nullptr;
    m_definitions.clear();
}

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

}  // namespace Kalburator::Shape
