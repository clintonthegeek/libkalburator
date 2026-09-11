// tests/plugin/tst_pluginmanager_smoke.cpp
#include <QtTest/QtTest>
#include <memory>
#include <kalburator/plugin/pluginmanager.h>
#include <kalburator/shape/shaperegistries.h>
#include <kalburator/sync/backendregistry.h>

class TestPluginManagerSmoke : public QObject {
    Q_OBJECT
private slots:
    void init() {
        m_pluginRegistry = std::make_unique<Kalburator::Sync::BackendRegistry>();
    }

    void cleanup() {
        m_pluginRegistry.reset();
    }

    void loadsRealSoAndRegistersBackend() {
        Kalburator::Shape::ShapeRegistries shape;
        Kalburator::PluginManager pm(m_pluginRegistry.get(), shape);
        pm.addSearchPath(QStringLiteral(TINY_PLUGIN_DIR));
        QVERIFY(pm.loadAll());
        QCOMPARE(pm.loaded().size(), 1);
        QCOMPARE(pm.loaded().first().id, QStringLiteral("com.kalburator.test.tiny"));
        QVERIFY(m_pluginRegistry->contributionFor(QStringLiteral("tiny")) != nullptr);
    }

private:
    std::unique_ptr<Kalburator::Sync::BackendRegistry> m_pluginRegistry;
};

QTEST_MAIN(TestPluginManagerSmoke)
#include "tst_pluginmanager_smoke.moc"
