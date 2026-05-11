#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"
#include "domainregistry.h"
#include "transformationregistry.h"

class TestStockPlugins : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        Kalburator::Shape::DomainRegistry::instance().clear();
        Kalburator::Shape::TransformationRegistry::instance().clear();
        Kalburator::Sync::BackendRegistry::instance().clear();
    }

    void universalStorageRegistersBothBackendTypes() {
        Kalburator::PluginManager pm;
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Sync::BackendRegistry::instance().contributionFor(QStringLiteral("raw-files")) != nullptr);
        QVERIFY(Kalburator::Sync::BackendRegistry::instance().contributionFor(QStringLiteral("generic-sqlite")) != nullptr);
    }

    void memoDomainRegistered() {
        Kalburator::PluginManager pm;
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("memo")}) != nullptr);
    }

    void blobDomainRegistered() {
        Kalburator::PluginManager pm;
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("blob")}) != nullptr);
    }

    void todoDomainAndTodotxtPeerRegistered() {
        Kalburator::PluginManager pm;
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("todo")}) != nullptr);
        const auto edges = Kalburator::Shape::TransformationRegistry::instance().edgesFrom(
            Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("todo")},
                Kalburator::Shape::EncodingId{QStringLiteral("todotxt")} });
        QVERIFY(!edges.isEmpty());
    }
};

QTEST_MAIN(TestStockPlugins)
#include "tst_stock_plugins.moc"
