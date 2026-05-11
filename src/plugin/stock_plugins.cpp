#include "stock_plugins.h"
#include "pluginmanager.h"
#include "manifest.h"
#include "universalstorageplugin.h"
#include "blobplugin.h"
#include "memoplugin.h"
// Future migrations append: todo, contacts, calendar

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
    static Blob::BlobPlugin s_blob;
    static Memo::MemoPlugin s_memo;
    QList<QPair<Plugin*, PluginManifest>> items{
        {&s_universal, mkManifest(QStringLiteral("kalburator.universal-storage"))},
        {&s_blob, mkManifest(QStringLiteral("kalburator.blob"), {QStringLiteral("blob")})},
        {&s_memo, mkManifest(QStringLiteral("kalburator.memo"), {QStringLiteral("memo")})},
    };
    pm.loadInProcess(items);
}

} // namespace Kalburator
