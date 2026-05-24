#include <QtTest/QtTest>
#include <memory>
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "backendregistry.h"
#include "shaperegistries.h"

class TestStockPlugins : public QObject {
    Q_OBJECT
private slots:
    void init() {
        m_pluginRegistry = std::make_unique<Kalburator::Sync::BackendRegistry>();
        m_shape = Kalburator::Shape::ShapeRegistries{};
    }

    void cleanup() {
        m_pluginRegistry.reset();
    }

    void universalStorageRegistersBothBackendTypes() {
        Kalburator::PluginManager pm(m_pluginRegistry.get(), m_shape);
        Kalburator::registerStockPlugins(pm);
        QVERIFY(m_pluginRegistry->contributionFor(QStringLiteral("raw-files")) != nullptr);
        QVERIFY(m_pluginRegistry->contributionFor(QStringLiteral("generic-sqlite")) != nullptr);
    }

    void memoDomainRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get(), m_shape);
        Kalburator::registerStockPlugins(pm);
        QVERIFY(m_shape.domain.definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("memo")}) != nullptr);
    }

    void blobDomainRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get(), m_shape);
        Kalburator::registerStockPlugins(pm);
        QVERIFY(m_shape.domain.definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("blob")}) != nullptr);
    }

    void todoDomainAndTodotxtPeerRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get(), m_shape);
        Kalburator::registerStockPlugins(pm);
        QVERIFY(m_shape.domain.definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("todo")}) != nullptr);
        const auto edges = m_shape.transformation.edgesFrom(
            Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("todo")},
                Kalburator::Shape::EncodingId{QStringLiteral("todotxt")} });
        QVERIFY(!edges.isEmpty());
    }

    void contactsDomainAndVcard3PeerRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get(), m_shape);
        Kalburator::registerStockPlugins(pm);
        QVERIFY(m_shape.domain.definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("contacts")}) != nullptr);
        const auto edges = m_shape.transformation.edgesFrom(
            Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("contacts")},
                Kalburator::Shape::EncodingId{QStringLiteral("vcard3")} });
        QVERIFY(!edges.isEmpty());
    }

    void calendarDomainAndOperationsRegistered() {
        Kalburator::PluginManager pm(m_pluginRegistry.get(), m_shape);
        Kalburator::registerStockPlugins(pm);
        QVERIFY(m_shape.domain.definitionFor(
            Kalburator::Shape::DomainId{QStringLiteral("calendar")}) != nullptr);
        auto *ops = m_shape.operations.operationsFor(
            Kalburator::Shape::DomainId{QStringLiteral("calendar")});
        QVERIFY(ops != nullptr);
    }

private:
    std::unique_ptr<Kalburator::Sync::BackendRegistry> m_pluginRegistry;
    Kalburator::Shape::ShapeRegistries m_shape;
};

QTEST_MAIN(TestStockPlugins)
#include "tst_stock_plugins.moc"
