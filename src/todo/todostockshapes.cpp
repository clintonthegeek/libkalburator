#include "todostockshapes.h"
#include "icalvtodoproperties.h"
#include "vtodocanonstages.h"
#include "todotxttransformation.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::LossKind;

namespace Kalburator::Todo {

QList<std::pair<Shape::Shape, Shape::PropertyCatalogue>> TodoStockShapes::peerShapes() const
{
    // Plan 3 Task B5: ical-vtodo is now a peer (demoted from canonical head).
    // todotxt remains a peer. The canon shape itself is registered by the
    // DomainDefinition spine (PluginManager), not here.
    const Shape::Shape vtodo{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };
    const Shape::Shape todotxt{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("todotxt")} };
    return {
        { vtodo,    makeVTodoCatalogue() },
        { todotxt,  makeVTodoCatalogue() },
    };
}

QList<Shape::TransformationEdge> TodoStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape vtodo{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };
    const Shape::Shape todotxt{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("todotxt")} };

    return {
        // Canon identity hub
        TransformationEdge{ canon,  canon,  LossProfile{},        std::make_shared<IdentityStage>() },
        // vtodo → canon (lossless promote)
        TransformationEdge{ vtodo,  canon,  LossProfile{},        std::make_shared<VTodoToCanonStage>() },
        // canon → vtodo (lossy demote)
        TransformationEdge{ canon,  vtodo,  canonToVtodoLoss(),   std::make_shared<CanonToVTodoStage>() },
        // vtodo → todotxt (lossy, existing — unchanged)
        TransformationEdge{ vtodo,  todotxt, todoTxtLoss(),       std::make_shared<ICalToTodoTxtStage>() },
        // todotxt → vtodo (lossless from todotxt's perspective — existing, unchanged)
        TransformationEdge{ todotxt, vtodo,  LossProfile{},       std::make_shared<TodoTxtToICalStage>() },
    };
}

} // namespace Kalburator::Todo
