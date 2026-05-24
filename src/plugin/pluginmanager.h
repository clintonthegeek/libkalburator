// src/plugin/pluginmanager.h
#ifndef KALBURATOR_PLUGIN_PLUGINMANAGER_H
#define KALBURATOR_PLUGIN_PLUGINMANAGER_H

#include <optional>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include "manifest.h"
#include "pluginloaderror.h"

namespace Kalburator {

class Plugin;
namespace Sync { class BackendRegistry; }
namespace Shape { struct ShapeRegistries; }

class PluginManager {
public:
    /// Injecting ctor (preferred): contributions are registered into
    /// `shape`, which must be the same bundle the consuming SyncEngine reads.
    PluginManager(Sync::BackendRegistry *registry, Shape::ShapeRegistries &shape);

    /// Transitional overload binding to the process-global default bundle
    /// (Ambient Context; FINDINGS O7).
    explicit PluginManager(Sync::BackendRegistry *registry);

    struct LoadedPlugin   { QString id; PluginManifest manifest; Plugin *plugin; };
    struct RejectedPlugin { PluginManifest manifest; PluginLoadError error; };

    /// Order plugins by inter-plugin domain dependencies.
    /// Returns manifests in load order, or empty list with errors appended.
    /// errorsOut is cleared first.
    QList<PluginManifest> resolve(const QList<PluginManifest> &manifests,
                                  QList<PluginLoadError> *errorsOut) const;

    /// Load already-constructed Plugin* instances in-process.
    /// Dispatches contributions to registries, unwinds on failure.
    /// Returns true iff all plugins loaded without errors.
    bool loadInProcess(const QList<QPair<Plugin*, PluginManifest>> &items);

    QList<LoadedPlugin>   loaded()   const;
    QList<RejectedPlugin> rejected() const;

    void addSearchPath(const QString &path);
    void discover();
    bool loadAll();

    /// Clear loaded/rejected lists (but does NOT unwind registry state).
    void reset();

private:
    struct Discovered { PluginManifest manifest; QString modulePath; };
    QStringList       m_searchPaths;
    QList<Discovered> m_discovered;
    QList<QObject*>   m_instances;   // plugin objects from QPluginLoader, kept alive

    QList<LoadedPlugin>   m_loaded;
    QList<RejectedPlugin> m_rejected;

    Sync::BackendRegistry *m_backendRegistry = nullptr;   // borrowed, non-null
    Shape::ShapeRegistries &m_shape;

    std::optional<PluginLoadError> applyPlugin(Plugin *plugin,
                                                const PluginManifest &manifest);
};

} // namespace Kalburator

#endif
