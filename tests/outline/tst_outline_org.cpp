#include <QTest>
#include <QJsonObject>
#include <QJsonArray>
#include "orgcanonstages.h"
#include "canonenvelope.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::OrgToCanonStage;
using Kalburator::Outline::CanonToOrgStage;

class TestOutlineOrg : public QObject {
    Q_OBJECT
private slots:
    void parsesHeadlineFacets();
    void parsesNesting();
    void roundTripsRichNode();
    void roundTripsNestedDepth();
};

void TestOutlineOrg::parsesHeadlineFacets()
{
    const QByteArray org =
        "* TODO [#A] Buy milk :errand:shop:\n"
        "DEADLINE: <2026-06-01>\n"
        "Some body text\n";
    const QJsonObject body = CanonEnvelope::parse(OrgToCanonStage().transform(org));
    const QJsonObject n = body.value("children").toArray()[0].toObject();
    QCOMPARE(n.value("text").toString(), QString("Buy milk"));
    QCOMPARE(n.value("status").toString(), QString("TODO"));
    QCOMPARE(n.value("priority").toInt(), 1);          // A->1
    QCOMPARE(n.value("due").toString(), QString("2026-06-01"));
    QCOMPARE(n.value("note").toString().trimmed(), QString("Some body text"));
    const QJsonArray tags = n.value("tags").toArray();
    QCOMPARE(tags.size(), 2);
}

void TestOutlineOrg::parsesNesting()
{
    const QByteArray org = "* Parent\n** Child\n*** Grandchild\n";
    const QJsonObject body = CanonEnvelope::parse(OrgToCanonStage().transform(org));
    const QJsonObject p = body.value("children").toArray()[0].toObject();
    QCOMPARE(p.value("text").toString(), QString("Parent"));
    const QJsonObject c = p.value("children").toArray()[0].toObject();
    QCOMPARE(c.value("text").toString(), QString("Child"));
    QCOMPARE(c.value("children").toArray()[0].toObject().value("text").toString(),
             QString("Grandchild"));
}

void TestOutlineOrg::roundTripsRichNode()
{
    const QByteArray org =
        "* TODO [#B] Task :work:\n"
        "DEADLINE: <2026-07-01>\n"
        "Body line\n";
    const QByteArray back = CanonToOrgStage().transform(OrgToCanonStage().transform(org));
    const QJsonObject a = CanonEnvelope::parse(OrgToCanonStage().transform(org));
    const QJsonObject b = CanonEnvelope::parse(OrgToCanonStage().transform(back));
    QVERIFY(CanonEnvelope::valuesEqual(a.value("children"), b.value("children")));
}

void TestOutlineOrg::roundTripsNestedDepth()
{
    const QByteArray org = "* A\n** B\n*** C\n";
    const QByteArray back = CanonToOrgStage().transform(OrgToCanonStage().transform(org));
    // Pin that toOrg threads level correctly at depth >=2 (not covered by roundTripsRichNode).
    QVERIFY(back.contains("* A"));
    QVERIFY(back.contains("** B"));
    QVERIFY(back.contains("*** C"));
}

QTEST_MAIN(TestOutlineOrg)
#include "tst_outline_org.moc"
