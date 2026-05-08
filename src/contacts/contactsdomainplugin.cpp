#include "contactsdomainplugin.h"

#include "domainregistry.h"
#include "vcardproperties.h"
#include "vcarddiffer.h"
#include "vcardmerger.h"
#include "vcard3to4transformation.h"
#include "transformationregistry.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::IRecordDiffer;
using Kalburator::Shape::IRecordMerger;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::TransformationRegistry;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Contacts {

DomainId KalburatorDomainContacts::domain() const
{
    return DomainId{"contacts"};
}

Kalburator::Shape::Shape KalburatorDomainContacts::canonicalShape() const
{
    return { DomainId{"contacts"}, EncodingId{"vcard4"} };
}

QList<Kalburator::Shape::Shape> KalburatorDomainContacts::peerShapes() const
{
    return {
        { DomainId{"contacts"}, EncodingId{"vcard3"} },
        // palm-address peer placeholder — Task 12 covers WP's
        // dynamic registration of (contacts, palm).
        { DomainId{"contacts"}, EncodingId{"palm-address"} },
    };
}

PropertyCatalogue KalburatorDomainContacts::canonicalCatalogue() const
{
    return makeVCardCatalogue();
}

PropertyCatalogue KalburatorDomainContacts::catalogueFor(
    const Kalburator::Shape::Shape &s) const
{
    if (s == canonicalShape())
        return makeVCardCatalogue();
    if (s == Kalburator::Shape::Shape{ DomainId{"contacts"}, EncodingId{"vcard3"} })
        return makeVCardCatalogue();   // same property set; v3 is subset
    return {};
}

std::unique_ptr<IRecordDiffer> KalburatorDomainContacts::createCanonicalDiffer() const
{
    return std::make_unique<IRecordDifferVCard>();
}

std::unique_ptr<IRecordMerger> KalburatorDomainContacts::createCanonicalMerger() const
{
    return std::make_unique<IRecordMergerVCard>();
}

void KalburatorDomainContacts::registerEdges(TransformationRegistry &registry)
{
    const auto canonical = canonicalShape();
    const auto vcard3    = Kalburator::Shape::Shape{ DomainId{"contacts"},
                                                     EncodingId{"vcard3"} };

    registry.registerShape(canonical, canonicalCatalogue());
    registry.registerShape(vcard3,    catalogueFor(vcard3));
    registry.declareCanonical(domain(), canonical);

    // Identity edge: canonical <-> canonical
    registry.registerEdge(TransformationEdge{
        canonical, canonical,
        LossProfile{},
        std::make_shared<IdentityStage>()
    });

    // vcard3 -> vcard4: lossless (v3 is a subset of v4 under Addressee pivot)
    registry.registerEdge(TransformationEdge{
        vcard3, canonical,
        LossProfile{},
        std::make_shared<VCard3To4Stage>()
    });

    // vcard4 -> vcard3: lossy
    registry.registerEdge(TransformationEdge{
        canonical, vcard3,
        vcard4ToVcard3Loss(),
        std::make_shared<VCard4To3Stage>()
    });

    // palm-address peer placeholder; Task 12 removes.
    registry.registerShape(
        { DomainId{"contacts"}, EncodingId{"palm-address"} }, {});
}

int KalburatorDomainContacts::richnessRank(
    const Kalburator::Shape::Shape &s) const
{
    if (s == canonicalShape()) return 10;
    if (s.encoding == EncodingId{"vcard3"}) return 8;
    return 0;
}

} // namespace Kalburator::Contacts

namespace {

struct ContactsPluginRegistrar {
    ContactsPluginRegistrar() {
        Kalburator::Shape::DomainRegistry::instance().registerDomain(
            std::make_shared<Kalburator::Contacts::KalburatorDomainContacts>());
    }
};

static ContactsPluginRegistrar s_contactsPluginRegistrar;

} // namespace
