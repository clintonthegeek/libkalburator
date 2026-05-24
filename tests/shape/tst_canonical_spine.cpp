#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "transformationregistry.h"
#include "propertycatalogue.h"
#include "transformationedge.h"
#include "pipeline.h"
#include "shaperegistries.h"

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
// v1 -> v2 widening: add a v2-only field with a default. Lossless.
class WidenStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& in) const override {
        QJsonObject o = QJsonDocument::fromJson(in).object();
        if (!o.contains(QStringLiteral("v2field")))
            o.insert(QStringLiteral("v2field"), QStringLiteral("default"));
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
};
// v2 -> v1 narrowing: drop the v2-only field.
class NarrowStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray& in) const override {
        QJsonObject o = QJsonDocument::fromJson(in).object();
        o.remove(QStringLiteral("v2field"));
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }
};
Kalburator::Shape::TransformationEdge widenEdge(Kalburator::Shape::Shape f, Kalburator::Shape::Shape t) {
    return { f, t, Kalburator::Shape::LossProfile{}, std::make_shared<WidenStage>() };
}
Kalburator::Shape::TransformationEdge narrowEdge(Kalburator::Shape::Shape f, Kalburator::Shape::Shape t) {
    Kalburator::Shape::LossProfile loss;   // v2field is reversible: re-widen restores the default
    loss.affected.insert(Kalburator::Shape::PropertyId{QStringLiteral("v2field")},
                         Kalburator::Shape::LossKind::Reversible);
    return { f, t, loss, std::make_shared<NarrowStage>() };
}
}  // namespace

class TestCanonicalSpine : public QObject {
    Q_OBJECT
private slots:
    void init() { m_shape = {}; }

    void singleNodeSpineHeadIsCanonical() {
        auto& r = m_shape.transformation;
        r.registerShape(cal("canon"), stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        QCOMPARE(r.canonicalFor(DomainId{QStringLiteral("calendar")}), cal("canon"));
        QCOMPARE(r.canonicalSpine(DomainId{QStringLiteral("calendar")}).size(), 1);
    }

    void appendMakesNewHead() {
        auto& r = m_shape.transformation;
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
        auto& r = m_shape.transformation;
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
        auto& r = m_shape.transformation;
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
        auto& r = m_shape.transformation;
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
        auto& r = m_shape.transformation;
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
        auto& r = m_shape.transformation;
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

    void unchangedPeerEdgeSurvivesVersionBump() {
        auto& r = m_shape.transformation;
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("ical"),   stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        // The "third-party" peer edge — written once, never touched again:
        r.registerEdge(edge(cal("ical"), cal("canon")));
        r.registerEdge(edge(cal("canon"), cal("ical")));

        // Library ships a v2 canon + bridge, WITHOUT touching the peer edge:
        r.registerShape(cal("canon2"), stubCat());
        r.registerEdge(widenEdge(cal("canon"),  cal("canon2")));
        r.registerEdge(narrowEdge(cal("canon2"), cal("canon")));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));

        // The unchanged peer now reaches the new head automatically:
        auto promote = r.compile(cal("ical"), cal("canon2"));
        QVERIFY(promote.has_value());
        const QByteArray out = promote->apply(QByteArray("{\"uid\":\"A\"}"));
        const QJsonObject o = QJsonDocument::fromJson(out).object();
        QVERIFY(o.contains(QStringLiteral("v2field")));   // widened by the auto-inserted bridge
        QCOMPARE(o.value(QStringLiteral("uid")).toString(), QStringLiteral("A"));
    }

    void spineRoundTripIsIdentityForWidenedFields() {
        auto& r = m_shape.transformation;
        r.registerShape(cal("canon"),  stubCat());
        r.registerShape(cal("canon2"), stubCat());
        r.declareCanonical(DomainId{QStringLiteral("calendar")}, cal("canon"));
        r.registerEdge(widenEdge(cal("canon"),  cal("canon2")));
        r.registerEdge(narrowEdge(cal("canon2"), cal("canon")));
        r.appendCanonicalVersion(DomainId{QStringLiteral("calendar")}, cal("canon2"));

        // A v2 record that only uses fields v1 also has must survive v2->v1->v2:
        const QByteArray v2in = QByteArray("{\"uid\":\"A\",\"v2field\":\"default\"}");
        auto down = r.compile(cal("canon2"), cal("canon"));
        auto up   = r.compile(cal("canon"),  cal("canon2"));
        QVERIFY(down.has_value() && up.has_value());
        const QByteArray back = up->apply(down->apply(v2in));
        const QJsonObject o = QJsonDocument::fromJson(back).object();
        QCOMPARE(o.value(QStringLiteral("uid")).toString(), QStringLiteral("A"));
        QCOMPARE(o.value(QStringLiteral("v2field")).toString(), QStringLiteral("default"));

        // And the narrowing declares its single field loss as Reversible:
        QCOMPARE(down->composedLoss().affected.value(
                     PropertyId{QStringLiteral("v2field")}), LossKind::Reversible);
    }

private:
    Kalburator::Shape::ShapeRegistries m_shape;
};

QTEST_GUILESS_MAIN(TestCanonicalSpine)
#include "tst_canonical_spine.moc"
