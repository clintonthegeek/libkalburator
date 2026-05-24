#include <QTest>

#include "contactsdomaindefinition.h"
#include "contactsstockshapes.h"

using namespace Kalburator::Contacts;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;

class TestVCardPlugin : public QObject {
    Q_OBJECT
private slots:
    void canonicalShapeIsContactsVCard()
    {
        const ContactsDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"contacts"}, EncodingId{"vcard4"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsContacts()
    {
        const ContactsDomainDefinition def;
        QCOMPARE(def.domain().toString(), QStringLiteral("contacts"));
    }

    void catalogueHasRequiredProperties()
    {
        const ContactsDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
        QVERIFY(cat.hasProperty(PropertyId{"fn"}));
        QVERIFY(cat.hasProperty(PropertyId{"email"}));
        QVERIFY(cat.hasProperty(PropertyId{"tel"}));
    }

    void catalogueHasV4Properties()
    {
        const ContactsDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"gender"}));
        QVERIFY(cat.hasProperty(PropertyId{"lang"}));
        QVERIFY(cat.hasProperty(PropertyId{"kind"}));
        QVERIFY(cat.hasProperty(PropertyId{"anniversary"}));
    }

    void richnessRankCanonical()
    {
        const ContactsDomainDefinition def;
        QCOMPARE(def.richnessRank(def.canonicalShape()), 10);
    }

    void stockShapesHasThreeEdges()
    {
        const ContactsStockShapes shapes;
        // identity + vcard3→vcard4 + vcard4→vcard3
        QCOMPARE(shapes.edges().size(), 3);
    }

    void stockShapesPeerContainsVcard3()
    {
        const ContactsStockShapes shapes;
        const Kalburator::Shape::Shape v3{ DomainId{"contacts"}, EncodingId{"vcard3"} };
        const auto peers = shapes.peerShapes();
        QVERIFY(std::any_of(peers.begin(), peers.end(),
            [&](const auto &p) { return p.first == v3; }));
    }

    void stockShapesDoesNotIncludePalmAddress()
    {
        const ContactsStockShapes shapes;
        const Kalburator::Shape::Shape palmAddr{
            Kalburator::Shape::DomainId{"contacts"},
            Kalburator::Shape::EncodingId{"palm-address"} };
        const auto peers = shapes.peerShapes();
        QVERIFY(!std::any_of(peers.begin(), peers.end(),
            [&](const auto &p) { return p.first == palmAddr; }));
    }
};

QTEST_GUILESS_MAIN(TestVCardPlugin)
#include "tst_vcard_plugin.moc"
