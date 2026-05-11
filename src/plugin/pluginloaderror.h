// src/plugin/pluginloaderror.h
#ifndef KALBURATOR_PLUGIN_PLUGINLOADERROR_H
#define KALBURATOR_PLUGIN_PLUGINLOADERROR_H

#include <QString>

namespace Kalburator {

enum class PluginLoadErrorCode {
    MissingDependency,
    DependencyCycle,
    ManifestMismatch,
    DoubleBinding,
    CanonicalConflict,
    BackendTypeCollision,
    FreezeViolation,
    EdgeEndpointUnregistered,
    InstantiationFailed,
    ManifestParseFailed,
};

struct PluginLoadError {
    PluginLoadErrorCode code;
    QString             pluginId;
    QString             detail;
};

} // namespace Kalburator

#endif
