#include <QTest>

#include "notedomaindefinition.h"

using namespace Kalburator::Note;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;

class TestNotePlugin : public QObject {
    Q_OBJECT
private slots:
    void canonicalShapeIsNoteCanon()
    {
        const NoteDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"note"}, EncodingId{"canon"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsNote()
    {
        const NoteDomainDefinition def;
        QCOMPARE(def.domain().toString(), QStringLiteral("note"));
    }

    void catalogueHasBodyAndCategories()
    {
        const NoteDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"body"}));
        QVERIFY(cat.hasProperty(PropertyId{"categories"}));
        QVERIFY(cat.hasProperty(PropertyId{"lastmodified"}));
    }

    void richnessRankCanonical()
    {
        const NoteDomainDefinition def;
        QCOMPARE(def.richnessRank(def.canonicalShape()), 100);
    }
};

QTEST_GUILESS_MAIN(TestNotePlugin)
#include "tst_note_plugin.moc"
