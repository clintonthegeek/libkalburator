#include "contactsstockshapes.h"
#include "vcardproperties.h"
#include "vcardcanonstages.h"
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
    return { { vcard4, makeVCardCatalogue() }, { vcard3, makeVCardCatalogue() } };
}

QList<Shape::TransformationEdge> ContactsStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape v4{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard4")} };
    const Shape::Shape v3{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard3")} };

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
    };
}

} // namespace Kalburator::Contacts
