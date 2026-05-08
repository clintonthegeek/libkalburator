#include "contactsdomainplugin.h"

#include "domainregistry.h"
#include "vcardproperties.h"
#include "vcarddiffer.h"
#include "vcardmerger.h"
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
    // palm-address peer: stage stub (real stage lands in G.7).
    return {
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

    registry.registerShape(canonical, canonicalCatalogue());
    registry.declareCanonical(domain(), canonical);

    registry.registerEdge(TransformationEdge{
        canonical, canonical,
        LossProfile{},
        std::make_shared<IdentityStage>()
    });

    // palm-address peer shape registered without a stage for now (G.7).
    registry.registerShape(
        { DomainId{"contacts"}, EncodingId{"palm-address"} }, {});
}

int KalburatorDomainContacts::richnessRank(
    const Kalburator::Shape::Shape &s) const
{
    if (s == canonicalShape())
        return 10;
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
