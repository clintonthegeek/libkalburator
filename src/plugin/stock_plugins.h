#ifndef KALBURATOR_PLUGIN_STOCK_PLUGINS_H
#define KALBURATOR_PLUGIN_STOCK_PLUGINS_H

#include <QList>
#include <QPair>

#include <kalburator/plugin/manifest.h>

namespace Kalburator {
class PluginManager;
class Plugin;
QList<QPair<Plugin *, PluginManifest>> stockPluginItems();
/// Register all stock plugins compiled into libkalburator. Idempotent.
void registerStockPlugins(PluginManager &pm);
}

#endif
