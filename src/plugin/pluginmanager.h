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

class PluginManager {
public:
    PluginManager() = default;

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

    /// Clear loaded/rejected lists (but does NOT unwind registry state).
    void reset();

private:
    QList<LoadedPlugin>   m_loaded;
    QList<RejectedPlugin> m_rejected;

    std::optional<PluginLoadError> applyPlugin(Plugin *plugin,
                                                const PluginManifest &manifest);
};

} // namespace Kalburator

#endif
