// tests/plugin/tst_pluginmanager_smoke.cpp
#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "backendregistry.h"

class TestPluginManagerSmoke : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        Kalburator::Sync::BackendRegistry::instance().clear();
    }

    void loadsRealSoAndRegistersBackend() {
        Kalburator::PluginManager pm;
        pm.addSearchPath(QStringLiteral(TINY_PLUGIN_DIR));
        QVERIFY(pm.loadAll());
        QCOMPARE(pm.loaded().size(), 1);
        QCOMPARE(pm.loaded().first().id, QStringLiteral("com.kalburator.test.tiny"));
        QVERIFY(Kalburator::Sync::BackendRegistry::instance().contributionFor(QStringLiteral("tiny")) != nullptr);
    }
};

QTEST_MAIN(TestPluginManagerSmoke)
#include "tst_pluginmanager_smoke.moc"
