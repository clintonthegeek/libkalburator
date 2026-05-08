#include <QTest>

#include "contactsdomainplugin.h"
#include "domainregistry.h"
#include "transformationregistry.h"

using namespace Kalburator::Contacts;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::DomainRegistry;
using Kalburator::Shape::TransformationRegistry;

class TestVCardPlugin : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        DomainRegistry::instance().clear();
        TransformationRegistry::instance().clear();
    }

    void canonicalShapeIsContactsVCard()
    {
        const KalburatorDomainContacts plugin;
        const Kalburator::Shape::Shape expected{ DomainId{"contacts"}, EncodingId{"vcard4"} };
        QCOMPARE(plugin.canonicalShape(), expected);
    }

    void domainIsContacts()
    {
        const KalburatorDomainContacts plugin;
        QCOMPARE(plugin.domain().toString(), QStringLiteral("contacts"));
    }

    void catalogueHasRequiredProperties()
    {
        const KalburatorDomainContacts plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"uid"}));
        QVERIFY(cat.hasProperty(PropertyId{"fn"}));
        QVERIFY(cat.hasProperty(PropertyId{"email"}));
        QVERIFY(cat.hasProperty(PropertyId{"tel"}));
    }

    void catalogueHasV4Properties()
    {
        const KalburatorDomainContacts plugin;
        const auto cat = plugin.canonicalCatalogue();
        QVERIFY(cat.hasProperty(PropertyId{"gender"}));
        QVERIFY(cat.hasProperty(PropertyId{"lang"}));
        QVERIFY(cat.hasProperty(PropertyId{"kind"}));
        QVERIFY(cat.hasProperty(PropertyId{"anniversary"}));
    }

    void registerEdgesDeclaresCanonical()
    {
        KalburatorDomainContacts plugin;
        auto& reg = TransformationRegistry::instance();
        plugin.registerEdges(reg);
        QCOMPARE(reg.canonicalFor(DomainId{"contacts"}), plugin.canonicalShape());
    }

    void richnessRankCanonical()
    {
        const KalburatorDomainContacts plugin;
        QCOMPARE(plugin.richnessRank(plugin.canonicalShape()), 10);
    }

    void registersVcard3PeerAndEdges()
    {
        using namespace Kalburator::Shape;
        KalburatorDomainContacts plugin;
        auto& reg = TransformationRegistry::instance();
        plugin.registerEdges(reg);

        const Shape v3{ DomainId{"contacts"}, EncodingId{"vcard3"} };
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };

        QVERIFY(reg.catalogueFor(v3) != nullptr);

        const auto edgesFromV3 = reg.edgesFrom(v3);
        QVERIFY2(std::any_of(edgesFromV3.begin(), edgesFromV3.end(),
            [&](const auto &e) { return e.to == v4; }),
            "expected edge vcard3 -> vcard4");

        const auto edgesFromV4 = reg.edgesFrom(v4);
        QVERIFY2(std::any_of(edgesFromV4.begin(), edgesFromV4.end(),
            [&](const auto &e) { return e.to == v3; }),
            "expected edge vcard4 -> vcard3");
    }

    void peerShapesIncludesVcard3()
    {
        const KalburatorDomainContacts plugin;
        const auto peers = plugin.peerShapes();
        const Kalburator::Shape::Shape v3{ Kalburator::Shape::DomainId{"contacts"},
                                           Kalburator::Shape::EncodingId{"vcard3"} };
        QVERIFY(peers.contains(v3));
    }
};

QTEST_GUILESS_MAIN(TestVCardPlugin)
#include "tst_vcard_plugin.moc"
