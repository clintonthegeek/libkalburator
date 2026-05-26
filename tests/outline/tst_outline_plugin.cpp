#include <QTest>

#include "outlinedomaindefinition.h"

using namespace Kalburator::Outline;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;

class TestOutlinePlugin : public QObject {
    Q_OBJECT
private slots:
    void canonicalShapeIsOutlineCanon()
    {
        const OutlineDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"outline"}, EncodingId{"canon"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsOutline()
    {
        const OutlineDomainDefinition def;
        QCOMPARE(def.domain().toString(), QStringLiteral("outline"));
    }

    void catalogueHasTitleAndChildren()
    {
        const OutlineDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"title"}));
        QVERIFY(cat.hasProperty(PropertyId{"children"}));
        QVERIFY(cat.hasProperty(PropertyId{"lastModified"}));
    }

    void richnessRankCanonical()
    {
        const OutlineDomainDefinition def;
        QCOMPARE(def.richnessRank(def.canonicalShape()), 100);
    }
};

QTEST_GUILESS_MAIN(TestOutlinePlugin)
#include "tst_outline_plugin.moc"
