#include <QTest>
#include "transformationregistry.h"
#include "propertycatalogue.h"
#include "transformationedge.h"
#include "pipeline.h"

using namespace Kalburator::Shape;

static Shape cal(const char* enc) {
    return Shape{ DomainId{QStringLiteral("calendar")}, EncodingId{QString::fromUtf8(enc)} };
}
static PropertyCatalogue stubCat() {
    PropertyCatalogue c;
    c.addProperty(PropertyDescriptor{ PropertyId{QStringLiteral("uid")}, PropertyKind::String, QStringLiteral("uid"), false });
    return c;
}

namespace {
class IdentityStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& in) const override { return in; }
};
Kalburator::Shape::TransformationEdge edge(Kalburator::Shape::Shape from,
                                           Kalburator::Shape::Shape to) {
    return Kalburator::Shape::TransformationEdge{
        from, to, Kalburator::Shape::LossProfile{}, std::make_shared<IdentityStage>() };
}
}  // namespace

class TestCanonicalSpine : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { TransformationRegistry::instance().clear(); }

    void singleNodeSpineHeadIsCanonical() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"), stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        QCOMPARE(r.canonicalFor(DomainId{QStringLiteral("calendar")}), cal("canon"));
        QCOMPARE(r.canonicalSpine(DomainId{QStringLiteral("calendar")}).size(), 1);
    }

    void appendMakesNewHead() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        QCOMPARE(r.canonicalFor(DomainId{QStringLiteral("calendar")}), cal("canon2"));   // head moved
        const auto spine = r.canonicalSpine(DomainId{QStringLiteral("calendar")});
        QCOMPARE(spine.size(), 2);
        QCOMPARE(spine.first(), cal("canon"));
        QCOMPARE(spine.last(),  cal("canon2"));
    }

    void singleNodeReproducesPeerToCanon() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"), stubCat());
        r.registerShape(cal("ical"),  stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.registerEdge(edge(cal("canon"), cal("ical")));
        auto promote = r.compile(cal("ical"), cal("canon"));
        QVERIFY(promote.has_value());
        QCOMPARE(promote->edges().size(), 1);
        auto demote = r.compile(cal("canon"), cal("ical"));
        QVERIFY(demote.has_value());
        QCOMPARE(demote->edges().size(), 1);
    }

    void singleNodePeerToPeerGoesThroughHub() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"), stubCat());
        r.registerShape(cal("ical"),  stubCat());
        r.registerShape(cal("org"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.registerEdge(edge(cal("canon"), cal("org")));
        auto p = r.compile(cal("ical"), cal("org"));
        QVERIFY(p.has_value());
        QCOMPARE(p->edges().size(), 2);   // ical -> canon -> org
    }

    void twoNodeSpinePromotesPeerToHead() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"),  cal("canon")));   // peer attaches at v1
        r.registerEdge(edge(cal("canon"), cal("ical")));
        r.registerEdge(edge(cal("canon"), cal("canon2")));  // bridge v1 -> v2
        r.registerEdge(edge(cal("canon2"), cal("canon")));  // bridge v2 -> v1
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        // peer (attached at v1) promoted to head v2: ical -> canon -> canon2
        auto promote = r.compile(cal("ical"), cal("canon2"));
        QVERIFY(promote.has_value());
        QCOMPARE(promote->edges().size(), 2);
        // head v2 demoted back to peer: canon2 -> canon -> ical
        auto demote = r.compile(cal("canon2"), cal("ical"));
        QVERIFY(demote.has_value());
        QCOMPARE(demote->edges().size(), 2);
    }

    void unbridgedSpineGapFailsToCompile() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        // no bridge canon->canon2 registered:
        QVERIFY(!r.compile(cal("ical"), cal("canon2")).has_value());
    }

    void appendAfterCompileIsRejected() {
        auto& r = TransformationRegistry::instance();
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(edge(cal("ical"), cal("canon")));
        // First non-identity compile freezes the calendar domain:
        QVERIFY(r.compile(cal("ical"), cal("canon")).has_value());
        QVERIFY(r.isFrozen(DomainId{QStringLiteral("calendar")}));
        // Appending a version now must be ignored (spine stays size 1):
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));
        QCOMPARE(r.canonicalSpine(DomainId{QStringLiteral("calendar")}).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestCanonicalSpine)
#include "tst_canonical_spine.moc"
