#include <QtTest/QtTest>
#include <memory>
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "backendregistry.h"
#include "fake_docstogo_plugin.h"
#include "fake_msoffice_plugin.h"
#include "fake_odf_plugin.h"

using namespace Kalburator;
using namespace KalburatorTests;

class TestDocsToGoScenario : public QObject {
    Q_OBJECT
private slots:
    void init() {
        m_pluginRegistry = std::make_unique<Sync::BackendRegistry>();
        m_shape = Shape::ShapeRegistries{};
    }

    void cleanup() {
        m_pluginRegistry.reset();
    }

    void pdbToOdtPipelineCompiles() {
        FakeDocsToGoPlugin docs;
        FakeMsOfficePlugin ms;
        FakeOdfPlugin odf;
        PluginManager pm(m_pluginRegistry.get(), m_shape);
        QVERIFY(pm.loadInProcess({
            {&docs, fakeDocsToGoManifest()},
            {&ms,   fakeMsOfficeManifest()},
            {&odf,  fakeOdfManifest()}
        }));
        QCOMPARE(pm.loaded().size(), 3);
        const Shape::Shape from{ Shape::DomainId{QStringLiteral("office.document")},
                                  Shape::EncodingId{QStringLiteral("pdb-word")} };
        const Shape::Shape to  { Shape::DomainId{QStringLiteral("office.document")},
                                  Shape::EncodingId{QStringLiteral("odt")} };
        const auto pipeline = m_shape.transformation.compile(from, to);
        QVERIFY(pipeline.has_value());
        const QByteArray sample = "hello documents";
        const QByteArray transformed = pipeline->apply(sample);
        QVERIFY(!transformed.isEmpty());
    }

    void msofficeAndOdfBackendsBothRegistered() {
        FakeDocsToGoPlugin docs;
        FakeMsOfficePlugin ms;
        FakeOdfPlugin odf;
        PluginManager pm(m_pluginRegistry.get(), m_shape);
        QVERIFY(pm.loadInProcess({
            {&docs, fakeDocsToGoManifest()},
            {&ms,   fakeMsOfficeManifest()},
            {&odf,  fakeOdfManifest()}
        }));
        QVERIFY(m_pluginRegistry->contributionFor(
            QStringLiteral("office-docx")) != nullptr);
        QVERIFY(m_pluginRegistry->contributionFor(
            QStringLiteral("office-odt")) != nullptr);
    }

    void loadOrderHandledByResolve() {
        // Submit in reverse dependency order; resolve() must reorder.
        FakeDocsToGoPlugin docs;
        FakeOdfPlugin odf;
        PluginManager pm(m_pluginRegistry.get(), m_shape);
        QVERIFY(pm.loadInProcess({
            {&odf,  fakeOdfManifest()},     // requires office.document
            {&docs, fakeDocsToGoManifest()} // defines office.document
        }));
        QCOMPARE(pm.loaded().size(), 2);
    }

private:
    std::unique_ptr<Sync::BackendRegistry> m_pluginRegistry;
    Shape::ShapeRegistries m_shape;
};

QTEST_GUILESS_MAIN(TestDocsToGoScenario)
#include "tst_docstogo_scenario.moc"
