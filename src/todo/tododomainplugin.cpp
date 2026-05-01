#include "tododomainplugin.h"

#include "domainregistry.h"
#include "icalvtodoproperties.h"
#include "icalvtododiffer.h"
#include "icalvtodomerger.h"
#include "todotxttransformation.h"
#include "transformationregistry.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::PropertyCatalogue;
using Kalburator::Shape::IRecordDiffer;
using Kalburator::Shape::IRecordMerger;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::TransformationRegistry;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Todo {

DomainId KalburatorDomainTodo::domain() const
{
    return DomainId{"todo"};
}

Kalburator::Shape::Shape KalburatorDomainTodo::canonicalShape() const
{
    return { DomainId{"todo"}, EncodingId{"ical-vtodo"} };
}

QList<Kalburator::Shape::Shape> KalburatorDomainTodo::peerShapes() const
{
    return {
        { DomainId{"todo"}, EncodingId{"todotxt"} },
    };
}

PropertyCatalogue KalburatorDomainTodo::canonicalCatalogue() const
{
    return makeVTodoCatalogue();
}

PropertyCatalogue KalburatorDomainTodo::catalogueFor(
    const Kalburator::Shape::Shape &s) const
{
    if (s == canonicalShape())
        return makeVTodoCatalogue();
    return {};
}

std::unique_ptr<IRecordDiffer> KalburatorDomainTodo::createCanonicalDiffer() const
{
    return std::make_unique<IRecordDifferVTodo>();
}

std::unique_ptr<IRecordMerger> KalburatorDomainTodo::createCanonicalMerger() const
{
    return std::make_unique<IRecordMergerVTodo>();
}

void KalburatorDomainTodo::registerEdges(TransformationRegistry &registry)
{
    const auto canonical = canonicalShape();
    const auto todotxt   = Kalburator::Shape::Shape{ DomainId{"todo"}, EncodingId{"todotxt"} };

    registry.registerShape(canonical, canonicalCatalogue());
    registry.declareCanonical(domain(), canonical);

    // Identity edge: canonical ↔ canonical
    registry.registerEdge(TransformationEdge{
        canonical, canonical,
        LossProfile{},
        std::make_shared<IdentityStage>()
    });

    // ical-vtodo → todotxt (lossy)
    registry.registerEdge(TransformationEdge{
        canonical, todotxt,
        todoTxtLoss(),
        std::make_shared<ICalToTodoTxtStage>()
    });

    // todotxt → ical-vtodo (lossless from todotxt's perspective,
    // but information in todotxt is already reduced)
    registry.registerEdge(TransformationEdge{
        todotxt, canonical,
        LossProfile{},
        std::make_shared<TodoTxtToICalStage>()
    });
}

int KalburatorDomainTodo::richnessRank(
    const Kalburator::Shape::Shape &s) const
{
    if (s == canonicalShape())
        return 10;
    if (s.encoding == EncodingId{"todotxt"})
        return 3;
    return 0;
}

} // namespace Kalburator::Todo

namespace {

struct TodoPluginRegistrar {
    TodoPluginRegistrar() {
        Kalburator::Shape::DomainRegistry::instance().registerDomain(
            std::make_shared<Kalburator::Todo::KalburatorDomainTodo>());
    }
};

static TodoPluginRegistrar s_todoPluginRegistrar;

} // namespace
