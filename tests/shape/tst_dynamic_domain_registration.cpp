#include <QTest>

#include "domainregistry.h"
#include "recorddiffer.h"
#include "recordmerger.h"
#include "transformationregistry.h"
#include "shaperegistries.h"

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
    void init() { m_shape = {}; }

    void registersShapesDirectly_pipelineCompiles() {
        // Register shapes directly — as PluginManager::loadInProcess() does.
        setupOfficeDomain(m_shape.transformation);

        // Compile a pipeline that uses the registered shapes.
        const Shape from{ DomainId{"office"}, EncodingId{"docx"} };
        const Shape to  { DomainId{"office"}, EncodingId{"canonical"} };
        const auto pipeline = m_shape.transformation.compile(from, to);
        QVERIFY(pipeline.has_value());
    }

    void inspectDoesNotFreeze() {
        setupOfficeDomain(m_shape.transformation);

        // Probe loss via inspect() — should NOT freeze the domain.
        const Shape from{ DomainId{"office"}, EncodingId{"docx"} };
        const Shape to  { DomainId{"office"}, EncodingId{"canonical"} };
        (void)m_shape.transformation.inspect(from, to);

        QVERIFY(!m_shape.transformation.isFrozen(DomainId{"office"}));

        // Now register a second peer — must succeed because inspect didn't freeze.
        const Shape odt{ DomainId{"office"}, EncodingId{"odt"} };
        m_shape.transformation.registerShape(odt, {});
        TransformationEdge e;
        e.from  = odt;
        e.to    = { DomainId{"office"}, EncodingId{"canonical"} };
        e.loss  = LossProfile{};
        e.stage = std::make_shared<IdentityStage>();
        m_shape.transformation.registerEdge(e);

        const auto p = m_shape.transformation.compile(odt, to);
        QVERIFY(p.has_value());
    }

    void registrationAfterCompile_isRejected() {
        setupOfficeDomain(m_shape.transformation);

        // Compile something in the office domain — this freezes it.
        const Shape from{ DomainId{"office"}, EncodingId{"docx"} };
        const Shape to  { DomainId{"office"}, EncodingId{"canonical"} };
        QVERIFY(m_shape.transformation.compile(from, to).has_value());

        // Now try to add another peer shape — must be silently rejected
        // because the domain is frozen.
        const Shape odt{ DomainId{"office"}, EncodingId{"odt"} };
        m_shape.transformation.registerShape(odt, {});
        TransformationEdge e;
        e.from  = odt;
        e.to    = to;
        e.loss  = LossProfile{};
        e.stage = std::make_shared<IdentityStage>();
        m_shape.transformation.registerEdge(e);

        // The new shape's pipeline should not compile.
        const auto p = m_shape.transformation.compile(odt, to);
        QVERIFY2(!p.has_value(),
                 "post-freeze peer registration must not appear in compiled pipelines");
    }

    void conflictingCanonicalDeclaration_isRejected() {
        setupOfficeDomain(m_shape.transformation);

        // Try to redeclare canonical with a DIFFERENT shape — must be rejected.
        const Shape originalCanonical{ DomainId{"office"}, EncodingId{"canonical"} };
        const Shape altCanonical{ DomainId{"office"}, EncodingId{"canonical-v2"} };
        m_shape.transformation.registerShape(altCanonical, {});
        m_shape.transformation.declareCanonical(DomainId{"office"}, altCanonical);

        // The original canonical must remain.
        QCOMPARE(m_shape.transformation.canonicalFor(DomainId{"office"}),
                 originalCanonical);
    }

    void multipleContributionsToSameDomain_unionPeers() {
        setupOfficeDomain(m_shape.transformation);

        // Second contribution for same domain, different peer.
        const Shape odt{ DomainId{"office"}, EncodingId{"odt"} };
        m_shape.transformation.registerShape(odt, {});
        TransformationEdge e;
        e.from  = odt;
        e.to    = { DomainId{"office"}, EncodingId{"canonical"} };
        e.loss  = LossProfile{};
        e.stage = std::make_shared<IdentityStage>();
        m_shape.transformation.registerEdge(e);

        // Both peers should now be reachable.
        const Shape canonical{ DomainId{"office"}, EncodingId{"canonical"} };
        const Shape docx    { DomainId{"office"}, EncodingId{"docx"} };
        QVERIFY(m_shape.transformation.compile(docx, canonical).has_value());
        QVERIFY(m_shape.transformation.compile(odt,  canonical).has_value());
    }

private:
    Kalburator::Shape::ShapeRegistries m_shape;
};

QTEST_GUILESS_MAIN(TestDynamicDomainRegistration)
#include "tst_dynamic_domain_registration.moc"
