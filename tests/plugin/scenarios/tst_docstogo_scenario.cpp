#include <QtTest/QtTest>
#include "pluginmanager.h"
#include "transformationregistry.h"
#include "domainregistry.h"
#include "domainoperationsregistry.h"
#include "backendregistry.h"
#include "fake_docstogo_plugin.h"
#include "fake_msoffice_plugin.h"
#include "fake_odf_plugin.h"

using namespace Kalburator;
using namespace KalburatorTests;

class TestDocsToGoScenario : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        Shape::DomainRegistry::instance().clear();
        Shape::TransformationRegistry::instance().clear();
        Shape::DomainOperationsRegistry::instance().clear();
        Sync::BackendRegistry::instance().clear();
    }

    void pdbToOdtPipelineCompiles() {
        FakeDocsToGoPlugin docs;
        FakeMsOfficePlugin ms;
        FakeOdfPlugin odf;
        PluginManager pm;
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
        const auto pipeline = Shape::TransformationRegistry::instance().compile(from, to);
        QVERIFY(pipeline.has_value());
        const QByteArray sample = "hello documents";
        const QByteArray transformed = pipeline->apply(sample);
        QVERIFY(!transformed.isEmpty());
    }

    void msofficeAndOdfBackendsBothRegistered() {
        FakeDocsToGoPlugin docs;
        FakeMsOfficePlugin ms;
        FakeOdfPlugin odf;
        PluginManager pm;
        QVERIFY(pm.loadInProcess({
            {&docs, fakeDocsToGoManifest()},
            {&ms,   fakeMsOfficeManifest()},
            {&odf,  fakeOdfManifest()}
        }));
        QVERIFY(Sync::BackendRegistry::instance().contributionFor(
            QStringLiteral("office-docx")) != nullptr);
        QVERIFY(Sync::BackendRegistry::instance().contributionFor(
            QStringLiteral("office-odt")) != nullptr);
    }

    void loadOrderHandledByResolve() {
        // Submit in reverse dependency order; resolve() must reorder.
        FakeDocsToGoPlugin docs;
        FakeOdfPlugin odf;
        PluginManager pm;
        QVERIFY(pm.loadInProcess({
            {&odf,  fakeOdfManifest()},     // requires office.document
            {&docs, fakeDocsToGoManifest()} // defines office.document
        }));
        QCOMPARE(pm.loaded().size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestDocsToGoScenario)
#include "tst_docstogo_scenario.moc"
