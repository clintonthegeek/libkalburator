#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "domainregistry.h"
#include "transformationregistry.h"
#include "domainoperationsregistry.h"
#include "backendregistry.h"
#include "fakes/fake_plugin.h"

using namespace Kalburator;
using namespace KalburatorTests;

static PluginManifest mk(const QString &id, QStringList defines = {}, QStringList req_ = {}) {
    PluginManifest m;
    m.id = id; m.version = QStringLiteral("1"); m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.definesDomains = std::move(defines); m.requiresDomains = std::move(req_);
    return m;
}

class TestPluginManagerLoad : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        Shape::DomainRegistry::instance().clear();
        Shape::TransformationRegistry::instance().clear();
        Shape::DomainOperationsRegistry::instance().clear();
        Sync::BackendRegistry::instance().clear();
    }

    void successPathLoadsBothPlugins() {
        FakePlugin a, b;
        a.dds = { makeTrivialDD(QStringLiteral("d1")) };
        b.scs = { makeTrivialSC(QStringLiteral("d1")) };
        PluginManager pm;
        QVERIFY(pm.loadInProcess({
            {&a, mk(QStringLiteral("a"), {QStringLiteral("d1")})},
            {&b, mk(QStringLiteral("b"), {}, {QStringLiteral("d1")})}
        }));
        QCOMPARE(pm.loaded().size(), 2);
        QVERIFY(pm.rejected().isEmpty());
    }

    void manifestMismatchRejects() {
        FakePlugin a;
        a.dds = { makeTrivialDD(QStringLiteral("d1")) };
        PluginManager pm;
        QVERIFY(!pm.loadInProcess({{&a, mk(QStringLiteral("a"), {QStringLiteral("d2")})}}));
        QCOMPARE(pm.rejected().size(), 1);
        QCOMPARE(pm.rejected().first().error.code, PluginLoadErrorCode::ManifestMismatch);
    }

    void canonicalConflictRejectsSecond() {
        FakePlugin a, b;
        a.dds = { makeTrivialDD(QStringLiteral("d1")) };
        b.dds = { makeTrivialDD(QStringLiteral("d1")) };
        PluginManager pm;
        QVERIFY(!pm.loadInProcess({
            {&a, mk(QStringLiteral("a"), {QStringLiteral("d1")})},
            {&b, mk(QStringLiteral("b"), {QStringLiteral("d1")})}
        }));
        QCOMPARE(pm.loaded().size(), 1);
        QCOMPARE(pm.rejected().size(), 1);
        QCOMPARE(pm.rejected().first().error.code, PluginLoadErrorCode::CanonicalConflict);
    }

    void backendTypeCollisionRejectsSecond() {
        FakePlugin a, b;
        a.bcs = { makeTrivialBC(QStringLiteral("x")) };
        b.bcs = { makeTrivialBC(QStringLiteral("x")) };
        PluginManager pm;
        QVERIFY(!pm.loadInProcess({
            {&a, mk(QStringLiteral("a"))}, {&b, mk(QStringLiteral("b"))}
        }));
        QCOMPARE(pm.loaded().size(), 1);
        QCOMPARE(pm.rejected().first().error.code, PluginLoadErrorCode::BackendTypeCollision);
    }

    void dependentRejectedWhenDependencyFails() {
        FakePlugin a, c;
        a.dds = { makeTrivialDD(QStringLiteral("d1")) };
        // a's manifest claims d2 — mismatch -> a rejected
        c.scs = { makeTrivialSC(QStringLiteral("d1")) };
        PluginManager pm;
        QVERIFY(!pm.loadInProcess({
            {&a, mk(QStringLiteral("a"), {QStringLiteral("d2")})},
            {&c, mk(QStringLiteral("c"), {}, {QStringLiteral("d2")})}
        }));
        QCOMPARE(pm.loaded().size(), 0);
        QCOMPARE(pm.rejected().size(), 2);
    }

    void independentPluginsSurviveOneFailure() {
        FakePlugin good, bad;
        good.dds = { makeTrivialDD(QStringLiteral("dGood")) };
        bad.dds  = { makeTrivialDD(QStringLiteral("dBad")) };
        PluginManager pm;
        QVERIFY(!pm.loadInProcess({
            {&good, mk(QStringLiteral("good"), {QStringLiteral("dGood")})},
            {&bad,  mk(QStringLiteral("bad"),  {QStringLiteral("wrong")})}
        }));
        QCOMPARE(pm.loaded().size(), 1);
        QCOMPARE(pm.loaded().first().id, QStringLiteral("good"));
    }
};

QTEST_APPLESS_MAIN(TestPluginManagerLoad)
#include "tst_pluginmanager_load.moc"
