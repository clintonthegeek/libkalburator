#include <QtTest>
#include <QJsonObject>

#include "canonenvelope.h"
#include "canonjsondiffer.h"

using namespace Kalburator::Shape;

class TestCanonJsonDiffMerge : public QObject
{
    Q_OBJECT
private slots:
    void envelopeStampsCanonAndUid()
    {
        QJsonObject o;
        o.insert("summary", "hi");
        CanonEnvelope::stampEnvelope(o, "calendar", "evt-1");
        QCOMPARE(CanonEnvelope::uid(o), QString("evt-1"));
        QCOMPARE(o.value("_canon").toObject().value("domain").toString(), QString("calendar"));
        QCOMPARE(o.value("_canon").toObject().value("v").toInt(), 1);
        QCOMPARE(o.value("summary").toString(), QString("hi"));  // untouched
    }

    void parseSerializeRoundTripsUnknownKeys()
    {
        const QByteArray in = R"({"uid":"x","futureKey":{"a":1}})";
        QJsonObject o = CanonEnvelope::parse(in);
        QVERIFY(o.contains("futureKey"));  // unknown key retained
        QByteArray out = CanonEnvelope::serialize(o);
        QJsonObject o2 = CanonEnvelope::parse(out);
        QVERIFY(CanonEnvelope::valuesEqual(o.value("futureKey"), o2.value("futureKey")));
    }

    void valuesEqualIsKeyOrderIndependent()
    {
        QJsonObject a{{"x",1},{"y",2}};
        QJsonObject b{{"y",2},{"x",1}};
        QVERIFY(CanonEnvelope::valuesEqual(a, b));
    }

    void differMarksChangedPropertyOnly()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("summary")}, PropertyId{QStringLiteral("location")} });
        CanonicalRecord src; src.data = R"({"summary":"new","location":"home"})";
        CanonicalRecord base; base.data = R"({"summary":"old","location":"home"})";
        const QSet<PropertyId> changed = d.diff(src, base);
        QVERIFY(changed.contains(PropertyId{QStringLiteral("summary")}));
        QVERIFY(!changed.contains(PropertyId{QStringLiteral("location")}));
    }

    void differIgnoresProviderExtrasAndCanon()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("summary")} });
        CanonicalRecord src; src.data = R"({"summary":"x","providerExtras":{"x-ms":{"a":1}},"_canon":{"v":1}})";
        CanonicalRecord base; base.data = R"({"summary":"x","providerExtras":{"x-ms":{"a":2}},"_canon":{"v":1}})";
        QVERIFY(d.diff(src, base).isEmpty());   // providerExtras change does not register
        QVERIFY(d.equal(src, base));
    }

    void differTreatsCompositeAsWhole()
    {
        CanonJsonDiffer d({ PropertyId{QStringLiteral("attendees")} });
        CanonicalRecord src; src.data = R"({"attendees":[{"email":"a@x"},{"email":"b@x"}]})";
        CanonicalRecord base; base.data = R"({"attendees":[{"email":"a@x"}]})";
        QVERIFY(d.diff(src, base).contains(PropertyId{QStringLiteral("attendees")}));
    }
};

QTEST_MAIN(TestCanonJsonDiffMerge)
#include "tst_canonjson_diff_merge.moc"
