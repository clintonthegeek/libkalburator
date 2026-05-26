#include <QTest>
#include "outlinecanonproperties.h"

using namespace Kalburator::Shape;
using Kalburator::Outline::makeOutlineCanonCatalogue;

class TestOutlineCatalogue : public QObject {
    Q_OBJECT
private slots:
    void hasRecordLevelProperties();
    void uidIsRequired();
};

void TestOutlineCatalogue::hasRecordLevelProperties()
{
    const PropertyCatalogue cat = makeOutlineCanonCatalogue();
    QVERIFY(cat.hasProperty(PropertyId{"uid"}));
    QVERIFY(cat.hasProperty(PropertyId{"title"}));
    QVERIFY(cat.hasProperty(PropertyId{"created"}));
    QVERIFY(cat.hasProperty(PropertyId{"lastModified"}));
    QVERIFY(cat.hasProperty(PropertyId{"attributes"}));
    QVERIFY(cat.hasProperty(PropertyId{"children"}));
}

void TestOutlineCatalogue::uidIsRequired()
{
    const PropertyCatalogue cat = makeOutlineCanonCatalogue();
    const PropertyDescriptor* uid = cat.find(PropertyId{"uid"});
    QVERIFY(uid != nullptr);
    QCOMPARE(uid->optional, false);
}

QTEST_MAIN(TestOutlineCatalogue)
#include "tst_outline_catalogue.moc"
