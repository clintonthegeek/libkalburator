#include "todostockshapes.h"
#include "icalvtodoproperties.h"
#include "vtodocanonstages.h"
#include "todotxttransformation.h"
#include "googletaskproperties.h"
#include "googletaskcanonstages.h"
#include "mstodotaskproperties.h"
#include "mstodotaskcanonstages.h"
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
    // EEE Phase 3 — Google Tasks `Task` and Microsoft Graph `todoTask` as
    // peer encodings.
    const Shape::Shape googleTask{ DomainId{QStringLiteral("todo")},
                                   EncodingId{QStringLiteral("google-task")} };
    const Shape::Shape msTodoTask{ DomainId{QStringLiteral("todo")},
                                   EncodingId{QStringLiteral("ms-todotask")} };
    return {
        { vtodo,       makeVTodoCatalogue() },
        { todotxt,     makeVTodoCatalogue() },
        { googleTask,  makeGoogleTaskCatalogue() },
        { msTodoTask,  makeMsTodoTaskCatalogue() },
    };
}

QList<Shape::TransformationEdge> TodoStockShapes::edges() const
{
    const Shape::Shape canon{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("canon")} };
    const Shape::Shape vtodo{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("ical-vtodo")} };
    const Shape::Shape todotxt{ DomainId{QStringLiteral("todo")}, EncodingId{QStringLiteral("todotxt")} };
    const Shape::Shape googleTask{ DomainId{QStringLiteral("todo")},
                                   EncodingId{QStringLiteral("google-task")} };
    const Shape::Shape msTodoTask{ DomainId{QStringLiteral("todo")},
                                   EncodingId{QStringLiteral("ms-todotask")} };

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
        // EEE Phase 3 — google-task ⇄ canon (loss profile declared first:
        // docs/2026-08-23-google-task-edge-loss-profile.md; NO carrier
        // channel exists on the Tasks resource)
        TransformationEdge{ googleTask, canon, LossProfile{},
                            std::make_shared<GoogleTaskToCanonStage>() },
        TransformationEdge{ canon, googleTask,
                            canonToGoogleTaskLoss(),
                            std::make_shared<CanonToGoogleTaskStage>() },
        // EEE Phase 3 — ms-todotask ⇄ canon (loss profile declared first:
        // docs/2026-08-23-ms-todotask-edge-loss-profile.md)
        TransformationEdge{ msTodoTask, canon, LossProfile{},
                            std::make_shared<MsTodoTaskToCanonStage>() },
        TransformationEdge{ canon, msTodoTask,
                            canonToMsTodoTaskLoss(),
                            std::make_shared<CanonToMsTodoTaskStage>() },
    };
}

} // namespace Kalburator::Todo
