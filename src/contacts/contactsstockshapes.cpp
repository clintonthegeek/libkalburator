#include "contactsstockshapes.h"
#include "vcardproperties.h"
#include "vcard3to4transformation.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Contacts {

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> ContactsStockShapes::peerShapes() const
{
    const Shape::Shape vcard3{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard3")} };
    return { { vcard3, makeVCardCatalogue() } };
}

QList<Shape::TransformationEdge> ContactsStockShapes::edges() const
{
    const Shape::Shape canonical{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard4")} };
    const Shape::Shape vcard3{ DomainId{QStringLiteral("contacts")}, EncodingId{QStringLiteral("vcard3")} };

    return {
        // Identity edge: canonical → canonical
        TransformationEdge{
            canonical, canonical,
            LossProfile{},
            std::make_shared<IdentityStage>()
        },
        // vcard3 → vcard4: lossless (v3 is a subset of v4 under Addressee pivot)
        TransformationEdge{
            vcard3, canonical,
            LossProfile{},
            std::make_shared<VCard3To4Stage>()
        },
        // vcard4 → vcard3: lossy
        TransformationEdge{
            canonical, vcard3,
            vcard4ToVcard3Loss(),
            std::make_shared<VCard4To3Stage>()
        },
    };
}

} // namespace Kalburator::Contacts
