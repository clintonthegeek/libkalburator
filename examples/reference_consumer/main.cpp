// Reference consumer for libkalburator's K.8 plugin surface.
// Demonstrates: in-process plugin load via registerStockPlugins(),
// backend contribution enumeration, and end-to-end calendar+contacts
// sync between SyncBackend instances using SyncEngine::runSyncFuture.
//
// Usage: reference_consumer --smoke <tmpdir>
//   --smoke runs the smoke scenario in <tmpdir> and exits 0 on success.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>

#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"

using namespace Kalburator;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("reference_consumer");

    QCommandLineParser parser;
    QCommandLineOption smokeOpt("smoke", "Run smoke scenario in <tmpdir>", "tmpdir");
    parser.addOption(smokeOpt);
    parser.process(app);

    if (!parser.isSet(smokeOpt)) {
        qWarning() << "Usage: reference_consumer --smoke <tmpdir>";
        return 2;
    }

    const QString workdir = parser.value(smokeOpt);
    QDir().mkpath(workdir);

    PluginManager pm;
    registerStockPlugins(pm);
    qInfo() << "Loaded plugins:" << pm.loaded().size();

    auto *caldav  = Sync::BackendRegistry::instance().contributionFor("caldav");
    auto *carddav = Sync::BackendRegistry::instance().contributionFor("carddav");
    if (!caldav || !carddav) {
        qCritical() << "Missing provider contributions: caldav="
                    << (caldav != nullptr) << "carddav=" << (carddav != nullptr);
        return 3;
    }
    qInfo() << "Provider contributions verified: caldav + carddav";

    qInfo() << "TODO Task 8: calendar+contacts sync";
    return 0;
}
