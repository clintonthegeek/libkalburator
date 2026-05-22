// tests/plugin/tst_pluginmanager_resolve.cpp
#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "manifest.h"
#include "pluginloaderror.h"
#include "backendregistry.h"

using namespace Kalburator;

static PluginManifest mk(const QString &id, QStringList defines = {}, QStringList requires_ = {}) {
    PluginManifest m;
    m.id = id;
    m.version = QStringLiteral("1.0");
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.definesDomains = std::move(defines);
    m.requiresDomains = std::move(requires_);
    return m;
}

class TestPluginManagerResolve : public QObject {
    Q_OBJECT
private slots:
    void linearOrder() {
        Sync::BackendRegistry registry;
        PluginManager pm(&registry);
        QList<PluginManifest> in{
            mk(QStringLiteral("b"), {}, {QStringLiteral("d1")}),
            mk(QStringLiteral("a"), {QStringLiteral("d1")}),
        };
        QList<PluginLoadError> errs;
        const auto order = pm.resolve(in, &errs);
        QVERIFY(errs.isEmpty());
        QCOMPARE(order.size(), 2);
        QCOMPARE(order.first().id, QStringLiteral("a"));
        QCOMPARE(order.last().id,  QStringLiteral("b"));
    }

    void missingDependencyReportsError() {
        Sync::BackendRegistry registry;
        PluginManager pm(&registry);
        QList<PluginManifest> in{
            mk(QStringLiteral("b"), {}, {QStringLiteral("d1")}),
        };
        QList<PluginLoadError> errs;
        const auto order = pm.resolve(in, &errs);
        QVERIFY(order.isEmpty());
        QCOMPARE(errs.size(), 1);
        QCOMPARE(errs.first().code, PluginLoadErrorCode::MissingDependency);
        QCOMPARE(errs.first().pluginId, QStringLiteral("b"));
    }

    void cycleReportsError() {
        Sync::BackendRegistry registry;
        PluginManager pm(&registry);
        QList<PluginManifest> in{
            mk(QStringLiteral("a"), {QStringLiteral("dA")}, {QStringLiteral("dB")}),
            mk(QStringLiteral("b"), {QStringLiteral("dB")}, {QStringLiteral("dA")}),
        };
        QList<PluginLoadError> errs;
        const auto order = pm.resolve(in, &errs);
        QVERIFY(order.isEmpty());
        QVERIFY(!errs.isEmpty());
        QCOMPARE(errs.first().code, PluginLoadErrorCode::DependencyCycle);
    }

    void independentPluginsAllScheduled() {
        Sync::BackendRegistry registry;
        PluginManager pm(&registry);
        QList<PluginManifest> in{
            mk(QStringLiteral("x"), {QStringLiteral("dX")}),
            mk(QStringLiteral("y"), {QStringLiteral("dY")}),
        };
        QList<PluginLoadError> errs;
        const auto order = pm.resolve(in, &errs);
        QVERIFY(errs.isEmpty());
        QCOMPARE(order.size(), 2);
    }
};

QTEST_APPLESS_MAIN(TestPluginManagerResolve)
#include "tst_pluginmanager_resolve.moc"
