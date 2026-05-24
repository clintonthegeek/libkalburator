#include <QTest>
#include "transformationregistry.h"
#include "propertycatalogue.h"

using namespace Kalburator::Shape;

static Shape cal(const char* enc) {
    return Shape{ DomainId{QStringLiteral("calendar")}, EncodingId{QString::fromUtf8(enc)} };
}
static PropertyCatalogue stubCat() {
    PropertyCatalogue c;
    c.addProperty(PropertyDescriptor{ PropertyId{QStringLiteral("uid")}, PropertyKind::String, QStringLiteral("uid"), false });
    return c;
}

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
};

QTEST_GUILESS_MAIN(TestCanonicalSpine)
#include "tst_canonical_spine.moc"
