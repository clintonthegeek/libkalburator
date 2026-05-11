// src/plugin/pluginmanager.cpp
#include "pluginmanager.h"
#include <QHash>
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

} // namespace Kalburator
