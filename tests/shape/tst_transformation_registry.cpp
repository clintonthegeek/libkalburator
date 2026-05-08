#include <QTest>

#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace {

class PrefixStage : public TransformationStage {
public:
    explicit PrefixStage(QByteArray p) : m_prefix(std::move(p)) {}
    QByteArray transform(const QByteArray& s) const override { return m_prefix + s; }
private:
    QByteArray m_prefix;
};

LossProfile lossy(const QString& dropped) {
    LossProfile p;
    p.level = LossLevel::IntraDomainLossy;
    p.dropped.insert(PropertyId{dropped});
    return p;
}

PropertyCatalogue makeStubCatalogue() {
    PropertyCatalogue c;
    c.addProperty({ PropertyId{QStringLiteral("uid")}, PropertyKind::String, {}, false });
    return c;
}

Shape calIcal()  { return { DomainId{"calendar"}, EncodingId{"ical"} }; }
Shape calOrg()   { return { DomainId{"calendar"}, EncodingId{"org"} }; }
Shape calPalm()  { return { DomainId{"calendar"}, EncodingId{"palm-datebook"} }; }
Shape conVcard() { return { DomainId{"contacts"}, EncodingId{"vcard4"} }; }

}  // namespace

class TestTransformationRegistry : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        TransformationRegistry::instance().clear();
    }

    void registerAndLookupCatalogue() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        QVERIFY(r.catalogueFor(calIcal()) != nullptr);
        QVERIFY(r.catalogueFor(calOrg()) == nullptr);
    }

    void declareAndLookupCanonical() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.declareCanonical(DomainId{"calendar"}, calIcal());
        QCOMPARE(r.canonicalFor(DomainId{"calendar"}), calIcal());

        // Unknown domain returns Any.
        QVERIFY(r.canonicalFor(DomainId{"unknown"}).isAny());
    }

    void registerEdgeAndEdgesFrom() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        TransformationEdge e{ calIcal(), calOrg(), lossy(QStringLiteral("attendees")),
                              std::make_shared<PrefixStage>("Org-") };
        r.registerEdge(e);
        const auto out = r.edgesFrom(calIcal());
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().to, calOrg());
    }

    void compileIdentitySameShape() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        auto p = r.compile(calIcal(), calIcal());
        QVERIFY(p.has_value());
        QVERIFY(p->isIdentity());
        QCOMPARE(p->inputShape(), calIcal());
        QCOMPARE(p->outputShape(), calIcal());
    }

    void compileToAnyIsIdentity() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        auto p = r.compile(calIcal(), Shape::Any());
        QVERIFY(p.has_value());
        QVERIFY(p->isIdentity());
        QCOMPARE(p->apply("hello"), QByteArray("hello"));
    }

    void compileFromAnyReturnsNullopt() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        auto p = r.compile(Shape::Any(), calIcal());
        QVERIFY(!p.has_value());
    }

    void compileCrossDomainReturnsNullopt() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(),  makeStubCatalogue());
        r.registerShape(conVcard(), makeStubCatalogue());
        auto p = r.compile(calIcal(), conVcard());
        QVERIFY(!p.has_value());
    }

    void compileSingleLegFromCanonical() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        r.declareCanonical(DomainId{"calendar"}, calIcal());
        r.registerEdge({ calIcal(), calOrg(), lossy(QStringLiteral("attendees")),
                         std::make_shared<PrefixStage>("Org-") });
        auto p = r.compile(calIcal(), calOrg());
        QVERIFY(p.has_value());
        QCOMPARE(p->edges().size(), 1);
        QCOMPARE(p->apply("foo"), QByteArray("Org-foo"));
    }

    void compileSingleLegToCanonical() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        r.declareCanonical(DomainId{"calendar"}, calIcal());
        r.registerEdge({ calOrg(), calIcal(), {},
                         std::make_shared<PrefixStage>("Ical-") });
        auto p = r.compile(calOrg(), calIcal());
        QVERIFY(p.has_value());
        QCOMPARE(p->edges().size(), 1);
    }

    void compileTwoLegThroughHub() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        r.registerShape(calPalm(), makeStubCatalogue());
        r.declareCanonical(DomainId{"calendar"}, calIcal());
        r.registerEdge({ calOrg(),  calIcal(), {},
                         std::make_shared<PrefixStage>("FromOrg-") });
        r.registerEdge({ calIcal(), calPalm(), lossy(QStringLiteral("attendees")),
                         std::make_shared<PrefixStage>("ToPalm-") });
        auto p = r.compile(calOrg(), calPalm());
        QVERIFY(p.has_value());
        QCOMPARE(p->edges().size(), 2);
        QCOMPARE(p->apply("x"), QByteArray("ToPalm-FromOrg-x"));
        QCOMPARE(p->composedLoss().level, LossLevel::IntraDomainLossy);
    }

    void compileNoPathReturnsNullopt() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        r.declareCanonical(DomainId{"calendar"}, calIcal());
        // No edge registered; compile returns nullopt.
        auto p = r.compile(calIcal(), calOrg());
        QVERIFY(!p.has_value());
    }

    void inspectReturnsLossProfile() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        r.declareCanonical(DomainId{"calendar"}, calIcal());
        r.registerEdge({ calIcal(), calOrg(), lossy(QStringLiteral("attendees")),
                         std::make_shared<PrefixStage>("Org-") });
        const LossProfile lp = r.inspect(calIcal(), calOrg());
        QCOMPARE(lp.level, LossLevel::IntraDomainLossy);
        QVERIFY(lp.dropped.contains(PropertyId{QStringLiteral("attendees")}));
    }

    void registeredShapesEnumerated() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        const auto shapes = r.registeredShapes();
        QCOMPARE(shapes.size(), 2);
    }

    void idempotentEdgeReregistration() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(calIcal(), makeStubCatalogue());
        r.registerShape(calOrg(),  makeStubCatalogue());
        TransformationEdge e1{ calIcal(), calOrg(), lossy(QStringLiteral("attendees")),
                               std::make_shared<PrefixStage>("Org-") };
        r.registerEdge(e1);
        // Re-register identical edge: idempotent (no assert).
        r.registerEdge(e1);
        QCOMPARE(r.edgesFrom(calIcal()).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestTransformationRegistry)
#include "tst_transformation_registry.moc"
