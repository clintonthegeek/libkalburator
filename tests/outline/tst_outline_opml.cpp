#include <QTest>
#include <QJsonObject>
#include <QJsonArray>
#include "opmlcanonstages.h"
#include "canonenvelope.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::OpmlToCanonStage;
using Kalburator::Outline::CanonToOpmlStage;

class TestOutlineOpml : public QObject {
    Q_OBJECT
private slots:
    void opmlToCanonBuildsTree();
    void canonToOpmlEmitsNestedOutlines();
    void opmlRoundTripPreservesStructureAndText();
    void attributeBagDoesNotDoubleWriteReserved();
};

void TestOutlineOpml::opmlToCanonBuildsTree()
{
    const QByteArray opml =
        "<opml version=\"2.0\"><head><title>L</title></head><body>"
        "<outline text=\"Groceries\">"
        "<outline text=\"Milk\"/><outline text=\"Bread\"/>"
        "</outline></body></opml>";
    const QJsonObject body = CanonEnvelope::parse(OpmlToCanonStage().transform(opml));
    QCOMPARE(body.value("title").toString(), QString("L"));
    const QJsonArray top = body.value("children").toArray();
    QCOMPARE(top.size(), 1);
    QCOMPARE(top[0].toObject().value("text").toString(), QString("Groceries"));
    QCOMPARE(top[0].toObject().value("children").toArray().size(), 2);
}

void TestOutlineOpml::canonToOpmlEmitsNestedOutlines()
{
    QJsonObject body{ {"title","L"}, {"children", QJsonArray{
        QJsonObject{ {"text","P"}, {"children", QJsonArray{ QJsonObject{{"text","C"}} }} } }} };
    CanonEnvelope::stampEnvelope(body, "outline", "u1");
    const QByteArray opml = CanonToOpmlStage().transform(CanonEnvelope::serialize(body));
    QVERIFY(opml.contains("text=\"P\""));
    QVERIFY(opml.contains("text=\"C\""));
    QVERIFY(opml.contains("<title>L</title>"));
}

void TestOutlineOpml::opmlRoundTripPreservesStructureAndText()
{
    const QByteArray opml =
        "<opml version=\"2.0\"><head><title>L</title></head><body>"
        "<outline text=\"A\"><outline text=\"B\"/></outline></body></opml>";
    const QByteArray back = CanonToOpmlStage().transform(OpmlToCanonStage().transform(opml));
    const QJsonObject b1 = CanonEnvelope::parse(OpmlToCanonStage().transform(opml));
    const QJsonObject b2 = CanonEnvelope::parse(OpmlToCanonStage().transform(back));
    QVERIFY(CanonEnvelope::valuesEqual(b1.value("children"), b2.value("children")));
}

void TestOutlineOpml::attributeBagDoesNotDoubleWriteReserved()
{
    // A node with a reserved key in attributes AND the corresponding named field
    // must not produce two copies of the attribute in the serialized OPML.
    QJsonObject node;
    node.insert(QStringLiteral("text"), QStringLiteral("Item"));
    node.insert(QStringLiteral("note"), QStringLiteral("note text"));  // serialised as _note
    // Poison the attributes bag with the reserved key and an extra custom key
    QJsonObject attrs;
    attrs.insert(QStringLiteral("_note"), QStringLiteral("x"));
    attrs.insert(QStringLiteral("color"), QStringLiteral("red"));
    node.insert(QStringLiteral("attributes"), attrs);

    QJsonObject body;
    body.insert(QStringLiteral("children"), QJsonArray{ node });
    CanonEnvelope::stampEnvelope(body, QStringLiteral("outline"), QStringLiteral("u1"));

    const QByteArray opml = CanonToOpmlStage().transform(CanonEnvelope::serialize(body));
    const QString opmlStr = QString::fromUtf8(opml);

    // _note must appear exactly once (from the named field, not the bag)
    QCOMPARE(opmlStr.count(QStringLiteral("_note=")), 1);
    // The non-reserved custom attribute must still be present
    QVERIFY(opmlStr.contains(QStringLiteral("color=\"red\"")));
}

QTEST_MAIN(TestOutlineOpml)
#include "tst_outline_opml.moc"
