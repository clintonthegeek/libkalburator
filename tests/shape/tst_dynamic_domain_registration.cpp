#include <QTest>

#include "domainregistry.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace {

/// Register shapes and edges for a fictitious "office" domain directly into
/// the TransformationRegistry — mirrors what PluginManager::loadInProcess()
/// does when it calls ShapeContribution::edges() on each contribution.
void setupOfficeDomain(TransformationRegistry &reg)
{
    const Shape canonical{ DomainId{"office"}, EncodingId{"canonical"} };
    const Shape docx{ DomainId{"office"}, EncodingId{"docx"} };
    reg.registerShape(canonical, {});
    reg.registerShape(docx, {});
    reg.declareCanonical(DomainId{"office"}, canonical);
    TransformationEdge e;
    e.from  = docx;
    e.to    = canonical;
    e.loss  = LossProfile{};
    e.stage = std::make_shared<IdentityStage>();
    reg.registerEdge(e);
    // Identity edge for canonical→canonical
    TransformationEdge id;
    id.from  = canonical;
    id.to    = canonical;
    id.loss  = LossProfile{};
    id.stage = std::make_shared<IdentityStage>();
    reg.registerEdge(id);
}

}  // namespace

class TestDynamicDomainRegistration : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        TransformationRegistry::instance().clear();
        DomainRegistry::instance().clear();
    }

    void registersShapesDirectly_pipelineCompiles() {
        // Register shapes directly — as PluginManager::loadInProcess() does.
        setupOfficeDomain(TransformationRegistry::instance());

        // Compile a pipeline that uses the registered shapes.
        const Shape from{ DomainId{"office"}, EncodingId{"docx"} };
        const Shape to  { DomainId{"office"}, EncodingId{"canonical"} };
        const auto pipeline = TransformationRegistry::instance().compile(from, to);
        QVERIFY(pipeline.has_value());
    }

    void inspectDoesNotFreeze() {
        setupOfficeDomain(TransformationRegistry::instance());

        // Probe loss via inspect() — should NOT freeze the domain.
        const Shape from{ DomainId{"office"}, EncodingId{"docx"} };
        const Shape to  { DomainId{"office"}, EncodingId{"canonical"} };
        (void)TransformationRegistry::instance().inspect(from, to);

        QVERIFY(!TransformationRegistry::instance().isFrozen(DomainId{"office"}));

        // Now register a second peer — must succeed because inspect didn't freeze.
        const Shape odt{ DomainId{"office"}, EncodingId{"odt"} };
        TransformationRegistry::instance().registerShape(odt, {});
        TransformationEdge e;
        e.from  = odt;
        e.to    = { DomainId{"office"}, EncodingId{"canonical"} };
        e.loss  = LossProfile{};
        e.stage = std::make_shared<IdentityStage>();
        TransformationRegistry::instance().registerEdge(e);

        const auto p = TransformationRegistry::instance().compile(odt, to);
        QVERIFY(p.has_value());
    }

    void registrationAfterCompile_isRejected() {
        setupOfficeDomain(TransformationRegistry::instance());

        // Compile something in the office domain — this freezes it.
        const Shape from{ DomainId{"office"}, EncodingId{"docx"} };
        const Shape to  { DomainId{"office"}, EncodingId{"canonical"} };
        QVERIFY(TransformationRegistry::instance().compile(from, to).has_value());

        // Now try to add another peer shape — must be silently rejected
        // because the domain is frozen.
        const Shape odt{ DomainId{"office"}, EncodingId{"odt"} };
        TransformationRegistry::instance().registerShape(odt, {});
        TransformationEdge e;
        e.from  = odt;
        e.to    = to;
        e.loss  = LossProfile{};
        e.stage = std::make_shared<IdentityStage>();
        TransformationRegistry::instance().registerEdge(e);

        // The new shape's pipeline should not compile.
        const auto p = TransformationRegistry::instance().compile(odt, to);
        QVERIFY2(!p.has_value(),
                 "post-freeze peer registration must not appear in compiled pipelines");
    }

    void conflictingCanonicalDeclaration_isRejected() {
        setupOfficeDomain(TransformationRegistry::instance());

        // Try to redeclare canonical with a DIFFERENT shape — must be rejected.
        const Shape originalCanonical{ DomainId{"office"}, EncodingId{"canonical"} };
        const Shape altCanonical{ DomainId{"office"}, EncodingId{"canonical-v2"} };
        TransformationRegistry::instance().registerShape(altCanonical, {});
        TransformationRegistry::instance().declareCanonical(DomainId{"office"}, altCanonical);

        // The original canonical must remain.
        QCOMPARE(TransformationRegistry::instance().canonicalFor(DomainId{"office"}),
                 originalCanonical);
    }

    void multipleContributionsToSameDomain_unionPeers() {
        setupOfficeDomain(TransformationRegistry::instance());

        // Second contribution for same domain, different peer.
        const Shape odt{ DomainId{"office"}, EncodingId{"odt"} };
        TransformationRegistry::instance().registerShape(odt, {});
        TransformationEdge e;
        e.from  = odt;
        e.to    = { DomainId{"office"}, EncodingId{"canonical"} };
        e.loss  = LossProfile{};
        e.stage = std::make_shared<IdentityStage>();
        TransformationRegistry::instance().registerEdge(e);

        // Both peers should now be reachable.
        const Shape canonical{ DomainId{"office"}, EncodingId{"canonical"} };
        const Shape docx    { DomainId{"office"}, EncodingId{"docx"} };
        QVERIFY(TransformationRegistry::instance().compile(docx, canonical).has_value());
        QVERIFY(TransformationRegistry::instance().compile(odt,  canonical).has_value());
    }
};

QTEST_GUILESS_MAIN(TestDynamicDomainRegistration)
#include "tst_dynamic_domain_registration.moc"
