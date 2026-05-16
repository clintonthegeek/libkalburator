#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"
#include "backendcontribution.h"
#include "providermanager.h"

using namespace Kalburator;

class TestAkonadiProviderPluginRegistration : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        Sync::BackendRegistry::instance().clear();
    }

    void registerStockPluginsRegistersAkonadiContribution() {
        PluginManager pm;
        registerStockPlugins(pm);
        QVERIFY(Sync::BackendRegistry::instance().contributionFor(
            QStringLiteral("akonadi")) != nullptr);
    }

    void akonadiContributionHasCorrectBackendType() {
        PluginManager pm;
        registerStockPlugins(pm);
        auto *contrib = Sync::BackendRegistry::instance().contributionFor(
            QStringLiteral("akonadi"));
        QVERIFY(contrib != nullptr);
        QCOMPARE(contrib->backendType(), QStringLiteral("akonadi"));
    }
};

QTEST_GUILESS_MAIN(TestAkonadiProviderPluginRegistration)
#include "tst_akonadiprovider_plugin_registration.moc"
