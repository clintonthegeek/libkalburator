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
};

QTEST_GUILESS_MAIN(TestVCardPlugin)
#include "tst_vcard_plugin.moc"
