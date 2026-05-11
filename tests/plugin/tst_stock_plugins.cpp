#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"

class TestStockPlugins : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        Kalburator::Sync::BackendRegistry::instance().clear();
    }

    void universalStorageRegistersBothBackendTypes() {
        Kalburator::PluginManager pm;
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Sync::BackendRegistry::instance().contributionFor(QStringLiteral("raw-files")) != nullptr);
        QVERIFY(Kalburator::Sync::BackendRegistry::instance().contributionFor(QStringLiteral("generic-sqlite")) != nullptr);
    }
};

QTEST_MAIN(TestStockPlugins)
#include "tst_stock_plugins.moc"
