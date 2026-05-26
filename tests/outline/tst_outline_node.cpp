#include <QTest>
#include <QJsonObject>
#include "outlinenode.h"

using Kalburator::Outline::OutlineNode;

class TestOutlineNode : public QObject {
    Q_OBJECT
private slots:
    void roundTripsScalarFields();
    void roundTripsNestedChildren();
    void omitsAbsentFields();
};

void TestOutlineNode::roundTripsScalarFields()
{
    OutlineNode n;
    n.id = "n1";
    n.text = "Milk";
    n.note = "2% organic";
    n.done = true;
    n.status = "DONE";
    n.priority = 1;
    n.progress = 100;
    n.due = "2026-06-01";
    n.start = "2026-05-01";
    n.created = "2026-01-15";
    n.tags = { "errand", "shop" };
    n.attributes.insert("color", "red");

    const OutlineNode back = OutlineNode::fromJson(n.toJson());
    QCOMPARE(back.id, n.id);
    QCOMPARE(back.text, n.text);
    QCOMPARE(back.note, n.note);
    QCOMPARE(back.done, true);
    QCOMPARE(back.status, QString("DONE"));
    QCOMPARE(back.priority, 1);
    QCOMPARE(back.progress, 100);
    QCOMPARE(back.due, QString("2026-06-01"));
    QCOMPARE(back.start, QString("2026-05-01"));
    QCOMPARE(back.created, QString("2026-01-15"));
    QCOMPARE(back.tags, QStringList({ "errand", "shop" }));
    QCOMPARE(back.attributes.value("color").toString(), QString("red"));
}

void TestOutlineNode::roundTripsNestedChildren()
{
    OutlineNode root;
    root.text = "Groceries";
    OutlineNode a; a.text = "Milk";
    OutlineNode b; b.text = "Bread";
    OutlineNode b1; b1.text = "Sourdough";
    b.children = { b1 };
    root.children = { a, b };

    const OutlineNode back = OutlineNode::fromJson(root.toJson());
    QCOMPARE(back.children.size(), 2);
    QCOMPARE(back.children[0].text, QString("Milk"));
    QCOMPARE(back.children[1].text, QString("Bread"));
    QCOMPARE(back.children[1].children.size(), 1);
    QCOMPARE(back.children[1].children[0].text, QString("Sourdough"));
}

void TestOutlineNode::omitsAbsentFields()
{
    OutlineNode n;
    n.text = "bare";
    const QJsonObject obj = n.toJson();
    QVERIFY(obj.contains("text"));
    QVERIFY(!obj.contains("note"));
    QVERIFY(!obj.contains("status"));
    QVERIFY(!obj.contains("done"));      // false + no other signal -> omitted
    QVERIFY(!obj.contains("priority"));
    QVERIFY(!obj.contains("children"));  // empty -> omitted
    QVERIFY(!obj.contains("start"));
    QVERIFY(!obj.contains("created"));
    QVERIFY(!obj.contains("completed"));
}

QTEST_MAIN(TestOutlineNode)
#include "tst_outline_node.moc"
