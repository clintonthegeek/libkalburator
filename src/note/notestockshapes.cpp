#include "notestockshapes.h"
#include "noteproperties.h"
#include "markdowncanonstages.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::LossKind;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Note {

namespace {
// The frontmatter rides in providerExtras (side channel), not as a first-class
// canon property, so both markdown<->canon edges declare it Reversible.
LossProfile frontmatterReversible() {
    LossProfile p;
    p.affected.insert(PropertyId{QStringLiteral("frontmatter")}, LossKind::Reversible);
    return p;
}
} // namespace

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> NoteStockShapes::peerShapes() const
{
    const Shape::Shape markdown{ DomainId{QStringLiteral("note")}, EncodingId{QStringLiteral("markdown")} };
    return { { markdown, makeNoteCatalogue() } };
}

QList<Shape::TransformationEdge> NoteStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("note")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape markdown{ DomainId{QStringLiteral("note")}, EncodingId{QStringLiteral("markdown")} };

    return {
        TransformationEdge{ canon,    canon,    LossProfile{},            std::make_shared<IdentityStage>() },
        TransformationEdge{ markdown, canon,    frontmatterReversible(),  std::make_shared<MarkdownToCanonStage>() },
        TransformationEdge{ canon,    markdown, frontmatterReversible(),  std::make_shared<CanonToMarkdownStage>() },
    };
}

} // namespace Kalburator::Note
