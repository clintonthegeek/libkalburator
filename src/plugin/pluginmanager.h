// src/plugin/pluginmanager.h
#ifndef KALBURATOR_PLUGIN_PLUGINMANAGER_H
#define KALBURATOR_PLUGIN_PLUGINMANAGER_H

#include <QList>
#include <QString>
#include <QStringList>
#include "manifest.h"
#include "pluginloaderror.h"

namespace Kalburator {

class Plugin;

class PluginManager {
public:
    PluginManager() = default;

    /// Order plugins by inter-plugin domain dependencies.
    /// Returns manifests in load order, or empty list with errors appended.
    /// errorsOut is cleared first.
    QList<PluginManifest> resolve(const QList<PluginManifest> &manifests,
                                  QList<PluginLoadError> *errorsOut) const;
};

} // namespace Kalburator

#endif
