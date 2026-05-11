#include "todostockshapes.h"
#include "icalvtodoproperties.h"
#include "todotxttransformation.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Todo {

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> TodoStockShapes::peerShapes() const
{
    const Shape::Shape todotxt{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("todotxt")} };
    return { { todotxt, makeVTodoCatalogue() } };
}

QList<Shape::TransformationEdge> TodoStockShapes::edges() const
{
    const Shape::Shape canonical{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };
    const Shape::Shape todotxt{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("todotxt")} };

    return {
        // Identity edge: canonical → canonical
        TransformationEdge{
            canonical, canonical,
            LossProfile{},
            std::make_shared<IdentityStage>()
        },
        // ical-vtodo → todotxt (lossy)
        TransformationEdge{
            canonical, todotxt,
            todoTxtLoss(),
            std::make_shared<ICalToTodoTxtStage>()
        },
        // todotxt → ical-vtodo (lossless from todotxt's perspective)
        TransformationEdge{
            todotxt, canonical,
            LossProfile{},
            std::make_shared<TodoTxtToICalStage>()
        },
    };
}

} // namespace Kalburator::Todo
