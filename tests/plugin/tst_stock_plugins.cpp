#include <QtTest/QtTest>
#include <memory>
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"
#include "domainregistry.h"
#include "domainoperationsregistry.h"
#include "transformationregistry.h"

class TestStockPlugins : public QObject {
    Q_OBJECT
private slots:
    void init() {
        m_pluginRegistry = std::make_unique<Kalburator::Sync::BackendRegistry>();
    }

    void cleanup() {
        Kalburator::Shape::DomainRegistry::instance().clear();
        Kalburator::Shape::TransformationRegistry::instance().clear();
        m_pluginRegistry.reset();
        Kalburator::Shape::DomainOperationsRegistry::instance().clear();
    }

    void universalStorageRegistersBothBackendTypes() {
        Kalburator::PluginManager pm(m_pluginRegistry.get());
        Kalburator::registerStockPlugins(pm);
        QVERIFY(m_pluginRegistry->contributionFor(QStringLiteral("raw-files")) != nullptr);
        QVERIFY(m_pluginRegistry->contributionFor(QStringLiteral("generic-sqlite")) != nullptr);
    }

    void memoDomainRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get());
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("memo")}) != nullptr);
    }

    void blobDomainRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get());
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("blob")}) != nullptr);
    }

    void todoDomainAndTodotxtPeerRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get());
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("todo")}) != nullptr);
        const auto edges = Kalburator::Shape::TransformationRegistry::instance().edgesFrom(
            Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("todo")},
                Kalburator::Shape::EncodingId{QStringLiteral("todotxt")} });
        QVERIFY(!edges.isEmpty());
    }

    void contactsDomainAndVcard3PeerRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get());
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("contacts")}) != nullptr);
        const auto edges = Kalburator::Shape::TransformationRegistry::instance().edgesFrom(
            Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("contacts")},
                Kalburator::Shape::EncodingId{QStringLiteral("vcard3")} });
        QVERIFY(!edges.isEmpty());
    }

    void calendarDomainAndOperationsRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get());
        Kalburator::registerStockPlugins(pm);
        QVERIFY(Kalburator::Shape::DomainRegistry::instance().definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("calendar")}) != nullptr);
        auto *ops = Kalburator::Shape::DomainOperationsRegistry::instance().operationsFor(
            Kalburator::Shape::DomainId{QStringLiteral("calendar")});
        QVERIFY(ops != nullptr);
    }

private:
    std::unique_ptr<Kalburator::Sync::BackendRegistry> m_pluginRegistry;
};

QTEST_MAIN(TestStockPlugins)
#include "tst_stock_plugins.moc"
