#include "stock_plugins.h"
#include "pluginmanager.h"
#include "manifest.h"
#include "universalstorageplugin.h"
#include "blobplugin.h"
#include "memoplugin.h"
#include "todoplugin.h"
#include "contactsplugin.h"
#include "calendarplugin.h"

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
    static Todo::TodoPlugin s_todo;
    static Contacts::ContactsPlugin s_contacts;
    static Calendar::CalendarPlugin s_calendar;
    QList<QPair<Plugin*, PluginManifest>> items{
        {&s_universal, mkManifest(QStringLiteral("kalburator.universal-storage"))},
        {&s_blob, mkManifest(QStringLiteral("kalburator.blob"), {QStringLiteral("blob")})},
        {&s_memo, mkManifest(QStringLiteral("kalburator.memo"), {QStringLiteral("memo")})},
        {&s_todo, mkManifest(QStringLiteral("kalburator.todo"), {QStringLiteral("todo")})},
        {&s_contacts, mkManifest(QStringLiteral("kalburator.contacts"), {QStringLiteral("contacts")})},
        {&s_calendar, mkManifest(QStringLiteral("kalburator.calendar"), {QStringLiteral("calendar")})},
    };
    pm.loadInProcess(items);
}

} // namespace Kalburator
