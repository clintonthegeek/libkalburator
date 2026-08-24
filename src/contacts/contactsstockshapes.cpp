#include "contactsstockshapes.h"
#include "vcardproperties.h"
#include "vcardcanonstages.h"
#include "googlepersonproperties.h"
#include "googlepersoncanonstages.h"
#include "mscontactproperties.h"
#include "mscontactcanonstages.h"
#include "vcard3to4transformation.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace Kalburator::Contacts {

namespace {

Kalburator::Shape::LossProfile canonToVcard4Loss()
{
    using Kalburator::Shape::PropertyId;
    using Kalburator::Shape::LossKind;
    Kalburator::Shape::LossProfile p;
    // Google-only fields vCard4 cannot hold:
    p.affected.insert(PropertyId{QStringLiteral("occupations")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("interests")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("skills")}, LossKind::Dropped);
    // Reversible: stashed in providerExtras / X- carriers, recoverable:
    p.affected.insert(PropertyId{QStringLiteral("sipAddresses")}, LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("calendarUrls")}, LossKind::Reversible);
    p.affected.insert(PropertyId{QStringLiteral("externalIds")}, LossKind::Reversible);
    return p;
}

}  // namespace

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> ContactsStockShapes::peerShapes() const
{
    const Shape::Shape vcard4{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard4")} };
    const Shape::Shape vcard3{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard3")} };
    // EEE Phase 3 — Google People API `Person` as a peer encoding.
    const Shape::Shape googlePerson{ DomainId{QStringLiteral("contacts")},
                                     EncodingId{QStringLiteral("google-person")} };
    // EEE Phase 3 — Microsoft Graph `contact` as a peer encoding.
    const Shape::Shape msContact{ DomainId{QStringLiteral("contacts")},
                                  EncodingId{QStringLiteral("ms-contact")} };
    return { { vcard4, makeVCardCatalogue() },
             { vcard3, makeVCardCatalogue() },
             { googlePerson, makeGooglePersonCatalogue() },
             { msContact, makeMsContactCatalogue() } };
}

QList<Shape::TransformationEdge> ContactsStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape v4{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard4")} };
    const Shape::Shape v3{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard3")} };
    const Shape::Shape googlePerson{ DomainId{QStringLiteral("contacts")},
                                     EncodingId{QStringLiteral("google-person")} };
    const Shape::Shape msContact{ DomainId{QStringLiteral("contacts")},
                                  EncodingId{QStringLiteral("ms-contact")} };

    return {
        // Identity hub: canon → canon
        TransformationEdge{ canon, canon, LossProfile{}, std::make_shared<IdentityStage>() },
        // vcard4 → canon: lossless promote
        TransformationEdge{ v4, canon, LossProfile{}, std::make_shared<VCard4ToCanonStage>() },
        // canon → vcard4: lossy demote
        TransformationEdge{ canon, v4, canonToVcard4Loss(), std::make_shared<CanonToVCard4Stage>() },
        // vcard3 → vcard4: lossless (existing)
        TransformationEdge{ v3, v4, LossProfile{}, std::make_shared<VCard3To4Stage>() },
        // vcard4 → vcard3: lossy (existing)
        TransformationEdge{ v4, v3, vcard4ToVcard3Loss(), std::make_shared<VCard4To3Stage>() },
        // EEE Phase 3 — google-person ⇄ canon (loss profile declared first:
        // docs/2026-08-23-google-person-edge-loss-profile.md)
        TransformationEdge{ googlePerson, canon, LossProfile{},
                            std::make_shared<GooglePersonToCanonStage>() },
        TransformationEdge{ canon, googlePerson,
                            canonToGooglePersonLoss(),
                            std::make_shared<CanonToGooglePersonStage>() },
        // EEE Phase 3 — ms-contact ⇄ canon (loss profile declared first:
        // docs/2026-08-23-ms-contact-edge-loss-profile.md)
        TransformationEdge{ msContact, canon, LossProfile{},
                            std::make_shared<MsContactToCanonStage>() },
        TransformationEdge{ canon, msContact,
                            canonToMsContactLoss(),
                            std::make_shared<CanonToMsContactStage>() },
    };
}

} // namespace Kalburator::Contacts
