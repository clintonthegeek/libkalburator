#include "outlinestockshapes.h"
#include "outlinecanonproperties.h"
#include "opmlcanonstages.h"
#include "orgcanonstages.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::LossKind;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Outline {

namespace {

// org <-> canon: node attributes survive as org property drawers (Reversible).
LossProfile attributesReversible()
{
    LossProfile p;
    p.affected.insert(PropertyId{QStringLiteral("attributes")}, LossKind::Reversible);
    return p;
}

// canon -> opml: task fields have no OPML representation (Dropped);
// the free-text note rides as an _note attribute (Reversible).
LossProfile canonToOpmlLoss()
{
    LossProfile p;
    for (const char* k : { "done", "status", "priority", "progress",
                           "start", "due", "completed" })
        p.affected.insert(PropertyId{QString::fromLatin1(k)}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("note")}, LossKind::Reversible);
    return p;
}

} // namespace

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> OutlineStockShapes::peerShapes() const
{
    const Shape::Shape org { DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("org")} };
    const Shape::Shape opml{ DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("opml")} };
    // Peer shapes reuse the canon catalogue: per-encoding (org/opml) property
    // catalogues are a deferred follow-on (design §3.2). The canon catalogue is a
    // superset, mirroring how the note domain registers its markdown peer.
    return {
        { org,  makeOutlineCanonCatalogue() },
        { opml, makeOutlineCanonCatalogue() },
    };
}

QList<Shape::TransformationEdge> OutlineStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape org  { DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("org")} };
    const Shape::Shape opml { DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("opml")} };

    return {
        TransformationEdge{ canon, canon, LossProfile{},           std::make_shared<IdentityStage>() },
        TransformationEdge{ org,   canon, attributesReversible(),   std::make_shared<OrgToCanonStage>() },
        TransformationEdge{ canon, org,   attributesReversible(),   std::make_shared<CanonToOrgStage>() },
        TransformationEdge{ opml,  canon, LossProfile{},            std::make_shared<OpmlToCanonStage>() },
        TransformationEdge{ canon, opml,  canonToOpmlLoss(),        std::make_shared<CanonToOpmlStage>() },
    };
}

} // namespace Kalburator::Outline
