#include "calendarstockshapes.h"
#include "icalproperties.h"
#include "icalcanonstages.h"
#include "calendarcanonproperties.h"
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
    // {calendar, ical} is a peer (not the canonical head — that is {calendar, canon}).
    const Kalburator::Shape::Shape icalShape{ DomainId{QStringLiteral("calendar")},
                                              EncodingId{QStringLiteral("ical")} };
    return {
        { icalShape, makeICalCatalogue() },
    };
}

QList<Kalburator::Shape::TransformationEdge> CalendarStockShapes::edges() const
{
    const Kalburator::Shape::Shape canon{ DomainId{QStringLiteral("calendar")},
                                          EncodingId{QStringLiteral("canon")} };
    const Kalburator::Shape::Shape ical{ DomainId{QStringLiteral("calendar")},
                                         EncodingId{QStringLiteral("ical")} };
    return {
        // Identity edge: canon → canon (hub)
        TransformationEdge{
            canon, canon,
            LossProfile{},
            std::make_shared<IdentityStage>()
        },
        // Promote: ical → canon (lossless)
        TransformationEdge{
            ical, canon,
            LossProfile{},
            std::make_shared<ICalToCanonStage>()
        },
        // Demote: canon → ical (lossy — drops/simplifies vendor-only fields)
        TransformationEdge{
            canon, ical,
            canonToIcalLoss(),
            std::make_shared<CanonToICalStage>()
        },
    };
}

} // namespace Kalburator::Calendar
