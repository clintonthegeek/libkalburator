#include "stock_plugins.h"
#include "pluginmanager.h"
#include "manifest.h"
#include "universalstorageplugin.h"
#include "blobplugin.h"
#include "noteplugin.h"
#include "outlineplugin.h"
#include "todoplugin.h"
#include "contactsplugin.h"
#include "calendarplugin.h"
#include "caldavproviderplugin.h"
#include "carddavproviderplugin.h"
#include "multiprotocoldavproviderplugin.h"
#ifdef HAVE_AKONADI
#include "akonadiproviderplugin.h"
#endif

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
    static Note::NotePlugin s_note;
    static Outline::OutlinePlugin s_outline;
    static Todo::TodoPlugin s_todo;
    static Contacts::ContactsPlugin s_contacts;
    static Calendar::CalendarPlugin s_calendar;
    static CalDavProviderPlugin s_caldav;
    static CardDavProviderPlugin s_carddav;
    static MultiProtocolDavProviderPlugin s_multiprotodav;
#ifdef HAVE_AKONADI
    static AkonadiProviderPlugin s_akonadi;
#endif
    QList<QPair<Plugin*, PluginManifest>> items{
        {&s_universal, mkManifest(QStringLiteral("kalburator.universal-storage"))},
        {&s_blob, mkManifest(QStringLiteral("kalburator.blob"), {QStringLiteral("blob")})},
        {&s_note, mkManifest(QStringLiteral("kalburator.note"), {QStringLiteral("note")})},
        {&s_outline, mkManifest(QStringLiteral("kalburator.outline"), {QStringLiteral("outline")})},
        {&s_todo, mkManifest(QStringLiteral("kalburator.todo"), {QStringLiteral("todo")})},
        {&s_contacts, mkManifest(QStringLiteral("kalburator.contacts"), {QStringLiteral("contacts")})},
        {&s_calendar, mkManifest(QStringLiteral("kalburator.calendar"), {QStringLiteral("calendar")})},
        {&s_caldav,   mkManifest(QStringLiteral("kalburator.provider.caldav"))},
        {&s_carddav,  mkManifest(QStringLiteral("kalburator.provider.carddav"))},
        {&s_multiprotodav, mkManifest(QStringLiteral("kalburator.provider.multiproto-dav"))},
#ifdef HAVE_AKONADI
        {&s_akonadi,  mkManifest(QStringLiteral("kalburator.provider.akonadi"))},
#endif
    };
    pm.loadInProcess(items);
}

} // namespace Kalburator
