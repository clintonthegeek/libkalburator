#include "outlinestockshapes.h"
#include "outlinecanonproperties.h"
#include "opmlcanonstages.h"
#include "lossprofile.h"
#ifdef KALBURATOR_HAVE_OUTLINE_ORG
#include "orgcanonstages.h"   // OrgGrove-dependent org<->canon stages
#endif

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::LossKind;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Outline {

namespace {

#ifdef KALBURATOR_HAVE_OUTLINE_ORG
// org → canon: unmapped :PROPERTIES: keys ride in `attributes` (Reversible).
// No data is structurally dropped in this direction.
LossProfile orgToCanonLoss()
{
    LossProfile p;
    p.affected.insert(PropertyId{QStringLiteral("attributes")}, LossKind::Reversible);
    return p;
}

// canon → org: `attributes` Reversible (re-emitted as :PROPERTIES:), but the
// thin OrgGrove adapter has no org representation for progress, created, or id,
// so those three fields are honestly declared Dropped.
LossProfile canonToOrgLoss()
{
    LossProfile p;
    p.affected.insert(PropertyId{QStringLiteral("attributes")}, LossKind::Reversible);
    // The thin OrgGrove adapter has no representation for these per-node fields:
    for (const auto& k : { QStringLiteral("progress"), QStringLiteral("created"), QStringLiteral("id") })
        p.affected.insert(PropertyId{k}, LossKind::Dropped);
    return p;
}
#endif // KALBURATOR_HAVE_OUTLINE_ORG

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
    const Shape::Shape opml{ DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("opml")} };
    // Peer shapes reuse the canon catalogue: per-encoding (org/opml) property
    // catalogues are a deferred follow-on (design §3.2). The canon catalogue is a
    // superset, mirroring how the note domain registers its markdown peer.
    QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> peers;
#ifdef KALBURATOR_HAVE_OUTLINE_ORG
    const Shape::Shape org { DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("org")} };
    peers.append({ org, makeOutlineCanonCatalogue() });
#endif
    peers.append({ opml, makeOutlineCanonCatalogue() });
    return peers;
}

QList<Shape::TransformationEdge> OutlineStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape opml { DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("opml")} };

    QList<Shape::TransformationEdge> e;
    e.append(TransformationEdge{ canon, canon, LossProfile{}, std::make_shared<IdentityStage>() });
#ifdef KALBURATOR_HAVE_OUTLINE_ORG
    const Shape::Shape org { DomainId{QStringLiteral("outline")}, EncodingId{QStringLiteral("org")} };
    e.append(TransformationEdge{ org,   canon, orgToCanonLoss(), std::make_shared<OrgToCanonStage>() });
    e.append(TransformationEdge{ canon, org,   canonToOrgLoss(), std::make_shared<CanonToOrgStage>() });
#endif
    e.append(TransformationEdge{ opml, canon, LossProfile{},     std::make_shared<OpmlToCanonStage>() });
    e.append(TransformationEdge{ canon, opml, canonToOpmlLoss(), std::make_shared<CanonToOpmlStage>() });
    return e;
}

} // namespace Kalburator::Outline
