#include "blobstockshapes.h"
#include "transformationedge.h"
#include "lossprofile.h"

namespace Kalburator::Blob {

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

QList<TransformationEdge> BlobStockShapes::edges() const {
    const Shape::Shape canonical{ DomainId{QStringLiteral("blob")}, EncodingId{QStringLiteral("raw")} };
    return { TransformationEdge{ canonical, canonical, LossProfile{}, std::make_shared<IdentityStage>() } };
}

} // namespace Kalburator::Blob
