#include <QtTest/QtTest>
#include <memory>
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"
#include "backendcontribution.h"
#include "providermanager.h"

using namespace Kalburator;

class TestProviderPluginRegistration : public QObject {
    Q_OBJECT
private slots:
    void init() {
        m_pluginRegistry = std::make_unique<Sync::BackendRegistry>();
    }

    void cleanup() {
        m_pluginRegistry.reset();
    }

    void registerStockPluginsRegistersCalDavContribution() {
        PluginManager pm(m_pluginRegistry.get());
        registerStockPlugins(pm);
        QVERIFY(m_pluginRegistry->contributionFor(
            QStringLiteral("caldav")) != nullptr);
    }

    void registerStockPluginsRegistersCardDavContribution() {
        PluginManager pm(m_pluginRegistry.get());
        registerStockPlugins(pm);
        QVERIFY(m_pluginRegistry->contributionFor(
            QStringLiteral("carddav")) != nullptr);
    }

    void providerManagerCtorDoesNotAutoRegisterAnymore() {
        // Without registerStockPlugins(), constructing a ProviderManager
        // must NOT register caldav/carddav anymore.
        Sync::BackendRegistry registry;
        Sync::ProviderManager pm(&registry);
        QCOMPARE(registry.contributionFor(QStringLiteral("caldav")),
                 static_cast<Sync::BackendContribution*>(nullptr));
        QCOMPARE(registry.contributionFor(QStringLiteral("carddav")),
                 static_cast<Sync::BackendContribution*>(nullptr));
    }

private:
    std::unique_ptr<Sync::BackendRegistry> m_pluginRegistry;
};

QTEST_GUILESS_MAIN(TestProviderPluginRegistration)
#include "tst_provider_plugin_registration.moc"
