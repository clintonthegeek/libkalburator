#include <QtTest/QtTest>
#include <memory>
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "backendregistry.h"
#include "backendcontribution.h"
#include "providermanager.h"

using namespace Kalburator;

class TestAkonadiProviderPluginRegistration : public QObject {
    Q_OBJECT
private slots:
    void init() {
        m_pluginRegistry = std::make_unique<Sync::BackendRegistry>();
    }

    void cleanup() {
        m_pluginRegistry.reset();
    }

    void registerStockPluginsRegistersAkonadiContribution() {
        Shape::ShapeRegistries shape;
        PluginManager pm(m_pluginRegistry.get(), shape);
        registerStockPlugins(pm);
        QVERIFY(m_pluginRegistry->contributionFor(
            QStringLiteral("akonadi")) != nullptr);
    }

    void akonadiContributionHasCorrectBackendType() {
        Shape::ShapeRegistries shape;
        PluginManager pm(m_pluginRegistry.get(), shape);
        registerStockPlugins(pm);
        auto *contrib = m_pluginRegistry->contributionFor(
            QStringLiteral("akonadi"));
        QVERIFY(contrib != nullptr);
        QCOMPARE(contrib->backendType(), QStringLiteral("akonadi"));
    }

private:
    std::unique_ptr<Sync::BackendRegistry> m_pluginRegistry;
};

QTEST_GUILESS_MAIN(TestAkonadiProviderPluginRegistration)
#include "tst_akonadiprovider_plugin_registration.moc"
