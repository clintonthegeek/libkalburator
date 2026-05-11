#include "stock_plugins.h"
#include "pluginmanager.h"
#include "manifest.h"
#include "universalstorageplugin.h"
// Future migrations append: blob, memo, todo, contacts, calendar

namespace Kalburator {

namespace {
PluginManifest mkManifest(const QString &id, QStringList defines = {}, QStringList req_ = {}) {
    PluginManifest m;
    m.id = id;
    m.version = QStringLiteral("1.0");
    m.displayName = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.definesDomains = std::move(defines);
    m.requiresDomains = std::move(req_);
    return m;
}
}

void registerStockPlugins(PluginManager &pm) {
    static UniversalStoragePlugin s_universal;
    QList<QPair<Plugin*, PluginManifest>> items{
        {&s_universal, mkManifest(QStringLiteral("kalburator.universal-storage"))},
    };
    pm.loadInProcess(items);
}

} // namespace Kalburator
