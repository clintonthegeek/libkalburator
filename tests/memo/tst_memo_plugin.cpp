#include <QTest>

#include "memodomaindefinition.h"

using namespace Kalburator::Memo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;

class TestMemoPlugin : public QObject {
    Q_OBJECT
private slots:
    void canonicalShapeIsMemoText()
    {
        const MemoDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"memo"}, EncodingId{"text"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsMemo()
    {
        const MemoDomainDefinition def;
        QCOMPARE(def.domain().toString(), QStringLiteral("memo"));
    }

    void catalogueHasBodyAndCategories()
    {
        const MemoDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"body"}));
        QVERIFY(cat.hasProperty(PropertyId{"categories"}));
        QVERIFY(cat.hasProperty(PropertyId{"lastmodified"}));
    }

    void richnessRankCanonical()
    {
        const MemoDomainDefinition def;
        QCOMPARE(def.richnessRank(def.canonicalShape()), 10);
    }
};

QTEST_GUILESS_MAIN(TestMemoPlugin)
#include "tst_memo_plugin.moc"
