#include "calendarstockshapes.h"
#include "icalproperties.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace Kalburator::Calendar {

Kalburator::Shape::DomainId CalendarStockShapes::targetDomain() const
{
    return DomainId{QStringLiteral("calendar")};
}

QList<std::pair<Kalburator::Shape::Shape, Kalburator::Shape::PropertyCatalogue>>
CalendarStockShapes::peerShapes() const
{
    // Calendar has no peer shapes — only the canonical ical shape.
    return {};
}

QList<Kalburator::Shape::TransformationEdge> CalendarStockShapes::edges() const
{
    const Kalburator::Shape::Shape canonical{ DomainId{QStringLiteral("calendar")},
                                              EncodingId{QStringLiteral("ical")} };
    return {
        // Identity edge: canonical → canonical
        TransformationEdge{
            canonical, canonical,
            LossProfile{},
            std::make_shared<IdentityStage>()
        },
    };
}

} // namespace Kalburator::Calendar
