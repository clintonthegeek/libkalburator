#ifndef KALBURATOR_PLUGIN_STOCK_PLUGINS_H
#define KALBURATOR_PLUGIN_STOCK_PLUGINS_H

namespace Kalburator {
class PluginManager;
/// Register all stock plugins compiled into libkalburator. Idempotent.
void registerStockPlugins(PluginManager &pm);
}

#endif
