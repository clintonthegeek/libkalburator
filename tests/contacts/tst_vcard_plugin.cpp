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
    void canonicalShapeIsContactsCanon()
    {
        const ContactsDomainDefinition def;
        const Kalburator::Shape::Shape expected{ DomainId{"contacts"}, EncodingId{"canon"} };
        QCOMPARE(def.canonicalShape(), expected);
    }

    void domainIsContacts()
    {
        const ContactsDomainDefinition def;
        QCOMPARE(def.domain().toString(), QStringLiteral("contacts"));
    }

    void canonicalCatalogueHasCanonProperties()
    {
        const ContactsDomainDefinition def;
        const auto cat = def.canonicalCatalogue();
        // Canon catalogue fields (schema doc §3)
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
        QVERIFY(cat.hasProperty(PropertyId{"names"}));
        QVERIFY(cat.hasProperty(PropertyId{"emails"}));
        QVERIFY(cat.hasProperty(PropertyId{"phones"}));
        // Google-only fields also present in canon
        QVERIFY(cat.hasProperty(PropertyId{"occupations"}));
        QVERIFY(cat.hasProperty(PropertyId{"interests"}));
    }

    void richnessRankCanonical()
    {
        const ContactsDomainDefinition def;
        // Canon head should have the highest richness rank
        QCOMPARE(def.richnessRank(def.canonicalShape()), 100);
    }

    void stockShapesHasFiveEdges()
    {
        const ContactsStockShapes shapes;
        // canon-identity + v4→canon + canon→v4 + v3→v4 + v4→v3
        QCOMPARE(shapes.edges().size(), 5);
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
