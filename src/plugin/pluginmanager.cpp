// src/plugin/pluginmanager.cpp
#include "pluginmanager.h"
#include "plugin.h"
#include "domaindefinition.h"
#include "shapecontribution.h"
#include "domainoperations.h"
#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "transformationregistry.h"
#include "shaperegistries.h"
#include "backendcontribution.h"
#include "backendregistry.h"
#include <QDir>
#include <QHash>
#include <QPluginLoader>
#include <QSet>

namespace Kalburator {

QList<PluginManifest> PluginManager::resolve(const QList<PluginManifest> &manifests,
                                              QList<PluginLoadError> *errorsOut) const {
    if (errorsOut) errorsOut->clear();

    // Domain → defining-plugin-id
    QHash<QString, QString> definedBy;
    for (const auto &m : manifests) {
        for (const auto &d : m.definesDomains) {
            definedBy.insert(d, m.id);
        }
    }

    // Build adjacency: pluginId → list of pluginIds it depends on
    QHash<QString, QStringList> deps;
    QHash<QString, PluginManifest> byId;
    bool hadMissing = false;
    for (const auto &m : manifests) {
        byId.insert(m.id, m);
        QStringList ds;
        for (const auto &req : m.requiresDomains) {
            auto it = definedBy.find(req);
            if (it == definedBy.end()) {
                if (errorsOut) {
                    errorsOut->append({
                        PluginLoadErrorCode::MissingDependency, m.id,
                        QStringLiteral("requires domain '%1' but no loaded plugin defines it").arg(req)
                    });
                }
                hadMissing = true;
            } else if (it.value() != m.id) {
                ds.append(it.value());
            }
        }
        deps.insert(m.id, ds);
    }
    if (hadMissing) return {};

    // Kahn's algorithm
    QHash<QString, int> inDegree;
    for (const auto &m : manifests) inDegree.insert(m.id, 0);
    for (auto it = deps.constBegin(); it != deps.constEnd(); ++it) {
        for (const auto &dep : it.value()) inDegree[it.key()] += 1;
    }
    QStringList ready;
    for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
        if (it.value() == 0) ready.append(it.key());
    }
    std::sort(ready.begin(), ready.end());

    QList<PluginManifest> out;
    while (!ready.isEmpty()) {
        const QString id = ready.takeFirst();
        out.append(byId.value(id));
        for (auto it = deps.begin(); it != deps.end(); ++it) {
            if (it.value().removeAll(id) > 0) {
                if (--inDegree[it.key()] == 0) {
                    ready.append(it.key());
                    std::sort(ready.begin(), ready.end());
                }
            }
        }
    }

    if (out.size() != manifests.size()) {
        if (errorsOut) {
            for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
                if (it.value() > 0) {
                    errorsOut->append({
                        PluginLoadErrorCode::DependencyCycle, it.key(),
                        QStringLiteral("dependency cycle includes this plugin")
                    });
                }
            }
        }
        return {};
    }
    return out;
}

PluginManager::PluginManager(Sync::BackendRegistry *registry,
                             Shape::ShapeRegistries &shape)
    : m_backendRegistry(registry)
    , m_shape(shape)
{
    Q_ASSERT(registry);
}

PluginManager::PluginManager(Sync::BackendRegistry *registry)
    : PluginManager(registry, Shape::defaultShapeRegistries())
{
}

void PluginManager::reset() { m_loaded.clear(); m_rejected.clear(); }
QList<PluginManager::LoadedPlugin>   PluginManager::loaded()   const { return m_loaded; }
QList<PluginManager::RejectedPlugin> PluginManager::rejected() const { return m_rejected; }

// ── applyPlugin ──────────────────────────────────────────────────────────────

namespace {
struct PendingRegistrations {
    QList<Shape::DomainId>                     domainsAdded;
    QList<Shape::Shape>                        shapesAdded;
    QList<QPair<Shape::Shape, Shape::Shape>>   edgesAdded;
    QList<Shape::DomainId>                     operationsAdded;
    QStringList                                backendTypesAdded;
};
} // anonymous namespace

std::optional<PluginLoadError>
PluginManager::applyPlugin(Plugin *plugin, const PluginManifest &m) {
    PendingRegistrations pending;
    auto fail = [&](PluginLoadErrorCode c, const QString &detail) -> PluginLoadError {
        for (const auto &b : pending.backendTypesAdded)
            m_backendRegistry->unregisterContribution(b);
        for (const auto &d : pending.operationsAdded)
            m_shape.operations.unregister(d);
        m_shape.transformation.unregisterEdges(pending.edgesAdded);
        m_shape.transformation.unregisterShapes(pending.shapesAdded);
        for (const auto &d : pending.domainsAdded)
            m_shape.domain.unregisterDefinition(d);
        return PluginLoadError{ c, m.id, detail };
    };

    const QSet<QString> definesSet(m.definesDomains.begin(), m.definesDomains.end());
    const QSet<QString> definesOrRequires = definesSet
        + QSet<QString>(m.requiresDomains.begin(), m.requiresDomains.end());

    // 1. DomainDefinitions
    for (const auto &def : plugin->domainDefinitions()) {
        const QString d = def->domain().toString();
        if (!definesSet.contains(d))
            return fail(PluginLoadErrorCode::ManifestMismatch,
                QStringLiteral("plugin defines domain '%1' not in manifest definesDomains").arg(d));
        if (!m_shape.domain.registerDefinition(def))
            return fail(PluginLoadErrorCode::CanonicalConflict,
                QStringLiteral("domain '%1' already defined by another plugin").arg(d));
        pending.domainsAdded.append(def->domain());
        if (m_shape.transformation.isFrozen(def->domain()))
            return fail(PluginLoadErrorCode::FreezeViolation,
                QStringLiteral("transformation registry already frozen for '%1'").arg(d));
        // Build the versioned canonical spine. For single-node spines the
        // default canonicalSpine() returns [{canonicalShape(), canonicalCatalogue()}],
        // which is equivalent to the old declareCanonical-only path.
        // For versioned spines (e.g. [vcard4, contacts+canon]) the first entry
        // is declared as the v1 root and each subsequent entry is appended.
        {
            const auto spine = def->canonicalSpine();
            if (!spine.isEmpty()) {
                const auto &[rootShape, rootCat] = spine.first();
                m_shape.transformation.registerShape(rootShape, rootCat);
                m_shape.transformation.declareCanonical(def->domain(), rootShape);
                pending.shapesAdded.append(rootShape);
                for (int i = 1; i < spine.size(); ++i) {
                    const auto &[s, cat] = spine.at(i);
                    m_shape.transformation.registerShape(s, cat);
                    m_shape.transformation.appendCanonicalVersion(def->domain(), s);
                    pending.shapesAdded.append(s);
                }
            } else {
                // Fallback: single-node spine (should not happen with default impl).
                m_shape.transformation.registerShape(def->canonicalShape(), def->canonicalCatalogue());
                m_shape.transformation.declareCanonical(def->domain(), def->canonicalShape());
                pending.shapesAdded.append(def->canonicalShape());
            }
        }
    }

    // 2. ShapeContributions
    for (const auto &sc : plugin->shapeContributions()) {
        const QString td = sc->targetDomain().toString();
        if (!definesOrRequires.contains(td))
            return fail(PluginLoadErrorCode::ManifestMismatch,
                QStringLiteral("shape contribution targets '%1' not in manifest").arg(td));
        if (m_shape.transformation.isFrozen(sc->targetDomain()))
            return fail(PluginLoadErrorCode::FreezeViolation,
                QStringLiteral("transformation registry frozen for '%1'").arg(td));
        QSet<Shape::Shape> knownInThisContribution;
        for (const auto &pair : sc->peerShapes()) {
            m_shape.transformation.registerShape(pair.first, pair.second);
            pending.shapesAdded.append(pair.first);
            knownInThisContribution.insert(pair.first);
        }
        for (const auto &edge : sc->edges()) {
            auto endpointRegistered = [&](const Shape::Shape &s) {
                return knownInThisContribution.contains(s)
                    || m_shape.transformation.catalogueFor(s) != nullptr;
            };
            if (!endpointRegistered(edge.from) || !endpointRegistered(edge.to))
                return fail(PluginLoadErrorCode::EdgeEndpointUnregistered,
                    QStringLiteral("edge endpoint not registered"));
            m_shape.transformation.registerEdge(edge);
            pending.edgesAdded.append({edge.from, edge.to});
        }
    }

    // 3. DomainOperations
    for (const auto &ops : plugin->domainOperations()) {
        if (!m_shape.operations.registerOperations(ops))
            return fail(PluginLoadErrorCode::DoubleBinding,
                QStringLiteral("domain operations for '%1' already bound").arg(ops->targetDomain().toString()));
        pending.operationsAdded.append(ops->targetDomain());
    }

    // 4. BackendContributions
    for (const auto &bc : plugin->backendContributions()) {
        if (!m_backendRegistry->registerContribution(bc))
            return fail(PluginLoadErrorCode::BackendTypeCollision,
                QStringLiteral("backend type '%1' already registered").arg(bc->backendType()));
        pending.backendTypesAdded.append(bc->backendType());
    }

    return std::nullopt;
}

// ── loadInProcess ────────────────────────────────────────────────────────────

bool PluginManager::loadInProcess(const QList<QPair<Plugin*, PluginManifest>> &items) {
    reset();
    QList<PluginManifest> manifests;
    for (const auto &p : items) manifests.append(p.second);

    QList<PluginLoadError> resolveErrors;
    const auto order = resolve(manifests, &resolveErrors);
    for (const auto &e : resolveErrors) {
        for (const auto &p : items) if (p.second.id == e.pluginId) {
            m_rejected.append({p.second, e}); break;
        }
    }
    if (order.isEmpty() && !resolveErrors.isEmpty()) return false;

    QHash<QString, Plugin*> byId;
    for (const auto &p : items) byId.insert(p.second.id, p.first);

    QSet<QString> failedDomains;
    for (const auto &manifest : order) {
        bool skip = false;
        for (const auto &req : manifest.requiresDomains) {
            if (failedDomains.contains(req)) {
                m_rejected.append({manifest, {PluginLoadErrorCode::MissingDependency, manifest.id,
                    QStringLiteral("dependency '%1' failed to load").arg(req)}});
                skip = true; break;
            }
        }
        if (skip) {
            for (const auto &d : manifest.definesDomains) failedDomains.insert(d);
            continue;
        }
        Plugin *p = byId.value(manifest.id);
        auto err = applyPlugin(p, manifest);
        if (err) {
            m_rejected.append({manifest, *err});
            for (const auto &d : manifest.definesDomains) failedDomains.insert(d);
        } else {
            m_loaded.append({manifest.id, manifest, p});
        }
    }
    return m_rejected.isEmpty();
}

// ── discover / loadAll ───────────────────────────────────────────────────────

void PluginManager::addSearchPath(const QString &p) { m_searchPaths.append(p); }

void PluginManager::discover() {
    m_discovered.clear();
    for (const auto &dir : m_searchPaths) {
        const QDir d(dir);
        const auto files = d.entryList(
            {QStringLiteral("*.so"), QStringLiteral("*.dll"), QStringLiteral("*.dylib")},
            QDir::Files);
        for (const auto &f : files) {
            const QString full = d.absoluteFilePath(f);
            QPluginLoader loader(full);
            const auto meta = loader.metaData().value(QStringLiteral("MetaData")).toObject();
            QString err;
            const auto m = PluginManifest::fromJson(meta, &err);
            if (!m.has_value()) {
                m_rejected.append({PluginManifest{}, {
                    PluginLoadErrorCode::ManifestParseFailed, full, err
                }});
                continue;
            }
            m_discovered.append({*m, full});
        }
    }
}

bool PluginManager::loadAll() {
    if (m_discovered.isEmpty()) discover();

    // Collect instantiation failures separately — loadInProcess() calls reset()
    // which would wipe them if we added them to m_rejected first.
    QList<RejectedPlugin> instantiationFailures;
    QList<QPair<Plugin*, PluginManifest>> items;

    for (const auto &d : m_discovered) {
        auto *loader = new QPluginLoader(d.modulePath);
        QObject *obj = loader->instance();
        Plugin *p = qobject_cast<Plugin*>(obj);
        if (!p) {
            instantiationFailures.append({d.manifest, {
                PluginLoadErrorCode::InstantiationFailed, d.manifest.id, loader->errorString()
            }});
            delete loader;
            continue;
        }
        m_instances.append(obj);
        // Keep loader alive so the .so stays mapped for the process lifetime.
        loader->setParent(nullptr);
        // (loader is intentionally leaked here — it must outlive the plugin object)
        items.append({p, d.manifest});
    }

    const bool ok = loadInProcess(items);
    // loadInProcess called reset(), so now re-add the instantiation failures.
    m_rejected.append(instantiationFailures);
    return ok && instantiationFailures.isEmpty();
}

} // namespace Kalburator
