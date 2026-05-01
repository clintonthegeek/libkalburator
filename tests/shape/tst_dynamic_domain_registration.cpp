#include <QTest>

#include "domainplugin.h"
#include "domainregistry.h"
#include "irecorddiffer.h"
#include "irecordmerger.h"
#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace {

// Minimal plugin that introduces a fictitious "office" domain with one
// peer shape and one identity edge. Used to prove dynamic registration
// works without requiring real domain implementations.
class OfficeStubPlugin : public DomainPlugin {
public:
    DomainId domain() const override { return DomainId{"office"}; }
    Shape canonicalShape() const override {
        return { DomainId{"office"}, EncodingId{"canonical"} };
    }
    QList<Shape> peerShapes() const override {
        return { { DomainId{"office"}, EncodingId{"docx"} } };
    }
    PropertyCatalogue canonicalCatalogue() const override { return {}; }
    PropertyCatalogue catalogueFor(const Shape&) const override { return {}; }
    std::unique_ptr<IRecordDiffer> createCanonicalDiffer() const override {
        return nullptr;
    }
    std::unique_ptr<IRecordMerger> createCanonicalMerger() const override {
        return nullptr;
    }
    void registerEdges(TransformationRegistry& r) override {
        r.registerShape(canonicalShape(), {});
        r.registerShape(peerShapes().first(), {});
        r.declareCanonical(domain(), canonicalShape());
        TransformationEdge edge;
        edge.from = peerShapes().first();
        edge.to   = canonicalShape();
        edge.loss = LossProfile{};
        edge.stage = std::make_shared<IdentityStage>();
        r.registerEdge(edge);
    }
    int richnessRank(const Shape&) const override { return 0; }
};

}  // namespace

class TestDynamicDomainRegistration : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        TransformationRegistry::instance().clear();
        DomainRegistry::instance().clear();
    }

    void registersPluginAfterInit_pipelineCompiles() {
        // Stock initialise (as a process normally would).
        DomainRegistry::instance().initialize(TransformationRegistry::instance());

        // Now, post-init, register a third-party plugin.
        DomainRegistry::instance().registerPlugin(
            std::make_shared<OfficeStubPlugin>());

        // Compile a pipeline that uses the dynamically-registered shapes.
        const Shape from { DomainId{"office"}, EncodingId{"docx"} };
        const Shape to   { DomainId{"office"}, EncodingId{"canonical"} };
        const auto pipeline =
            TransformationRegistry::instance().compile(from, to);

        QVERIFY(pipeline.has_value());
    }

    void inspectDoesNotFreeze() {
        DomainRegistry::instance().initialize(TransformationRegistry::instance());

        DomainRegistry::instance().registerPlugin(
            std::make_shared<OfficeStubPlugin>());

        // Probe loss via inspect() — should NOT freeze the domain.
        const Shape from { DomainId{"office"}, EncodingId{"docx"} };
        const Shape to   { DomainId{"office"}, EncodingId{"canonical"} };
        (void)TransformationRegistry::instance().inspect(from, to);

        QVERIFY(!TransformationRegistry::instance().isFrozen(DomainId{"office"}));

        // Now register a second peer — this MUST succeed (not silently
        // rejected) because inspect didn't freeze.
        class SecondOfficePlugin : public OfficeStubPlugin {
        public:
            QList<Shape> peerShapes() const override {
                return { { DomainId{"office"}, EncodingId{"odt"} } };
            }
            void registerEdges(TransformationRegistry& r) override {
                r.registerShape(peerShapes().first(), {});
                TransformationEdge edge;
                edge.from = peerShapes().first();
                edge.to   = canonicalShape();
                edge.loss = LossProfile{};
                edge.stage = std::make_shared<IdentityStage>();
                r.registerEdge(edge);
            }
        };
        DomainRegistry::instance().registerPlugin(std::make_shared<SecondOfficePlugin>());

        const Shape odt { DomainId{"office"}, EncodingId{"odt"} };
        const auto p = TransformationRegistry::instance().compile(odt, to);
        QVERIFY(p.has_value());
    }

    void registrationAfterCompile_isRejected() {
        DomainRegistry::instance().initialize(TransformationRegistry::instance());

        DomainRegistry::instance().registerPlugin(
            std::make_shared<OfficeStubPlugin>());

        // Compile something in the office domain — this freezes it.
        const Shape from { DomainId{"office"}, EncodingId{"docx"} };
        const Shape to   { DomainId{"office"}, EncodingId{"canonical"} };
        QVERIFY(TransformationRegistry::instance().compile(from, to).has_value());

        // Now try to add another peer shape via a second plugin.
        // In debug the registerEdge() asserts; in release it returns
        // silently and the shape doesn't appear. Test the release path
        // (silent rejection) by asking compile() afterward.
        class SecondOfficePlugin : public OfficeStubPlugin {
        public:
            QList<Shape> peerShapes() const override {
                return { { DomainId{"office"}, EncodingId{"odt"} } };
            }
            void registerEdges(TransformationRegistry& r) override {
                r.registerShape(peerShapes().first(), {});
                TransformationEdge edge;
                edge.from = peerShapes().first();
                edge.to   = canonicalShape();
                edge.loss = LossProfile{};
                edge.stage = std::make_shared<IdentityStage>();
                r.registerEdge(edge);
            }
        };

        // We expect this to be rejected (silently in release; the
        // attempt should not panic, but the new shape's pipeline
        // should not compile).
        DomainRegistry::instance().registerPlugin(
            std::make_shared<SecondOfficePlugin>());

        const Shape odt { DomainId{"office"}, EncodingId{"odt"} };
        const auto p = TransformationRegistry::instance().compile(odt, to);
        QVERIFY2(!p.has_value(),
                 "post-freeze peer registration must not appear in compiled pipelines");
    }
};

QTEST_GUILESS_MAIN(TestDynamicDomainRegistration)
#include "tst_dynamic_domain_registration.moc"
