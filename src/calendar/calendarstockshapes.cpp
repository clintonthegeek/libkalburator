#include "calendarstockshapes.h"
#include "icalproperties.h"
#include "icalcanonstages.h"
#include "orgicalcanonstages.h"
#include "googleeventproperties.h"
#include "googlecanonstages.h"
#include "calendarcanonproperties.h"
#include "lossprofile.h"

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::LossProfile;
using Kalburator::Shape::TransformationEdge;
using Kalburator::Shape::IdentityStage;

namespace {

Kalburator::Shape::LossProfile canonToOrgIcalLoss()
{
    using Kalburator::Shape::PropertyId;
    using Kalburator::Shape::LossKind;
    Kalburator::Shape::LossProfile p;
    // org-mode cannot hold complex RRULEs; canon->org-ical reduces them to a basic
    // pattern but keeps the original verbatim in X-ORIGINAL-RRULE (Reversible carrier).
    // Classified Simplified (not Dropped): the original is recoverable.
    p.affected.insert(PropertyId{QStringLiteral("recurrence")}, LossKind::Simplified);
    return p;
}

} // namespace

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
    // {calendar, org-ical} is a peer; shares the iCal field-set (same catalogue).
    const Kalburator::Shape::Shape orgIcalShape{ DomainId{QStringLiteral("calendar")},
                                                 EncodingId{QStringLiteral("org-ical")} };
    // {calendar, google-event} is a peer; a single Google Calendar v3 `event`
    // resource as wire-shape JSON (one object per record).
    const Kalburator::Shape::Shape googleEventShape{
        DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("google-event")} };
    return {
        { icalShape,        makeICalCatalogue() },
        { orgIcalShape,     makeICalCatalogue() },
        { googleEventShape, makeGoogleEventCatalogue() },
    };
}

QList<Kalburator::Shape::TransformationEdge> CalendarStockShapes::edges() const
{
    const Kalburator::Shape::Shape canon{ DomainId{QStringLiteral("calendar")},
                                          EncodingId{QStringLiteral("canon")} };
    const Kalburator::Shape::Shape ical{ DomainId{QStringLiteral("calendar")},
                                         EncodingId{QStringLiteral("ical")} };
    const Kalburator::Shape::Shape orgIcal{ DomainId{QStringLiteral("calendar")},
                                             EncodingId{QStringLiteral("org-ical")} };
    const Kalburator::Shape::Shape googleEvent{
        DomainId{QStringLiteral("calendar")}, EncodingId{QStringLiteral("google-event")} };
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
        // Promote: org-ical → canon (lossless un-simplify)
        TransformationEdge{
            orgIcal, canon,
            LossProfile{},
            std::make_shared<OrgICalToCanonStage>()
        },
        // Demote: canon → org-ical (Simplified — complex recurrence reduced)
        TransformationEdge{
            canon, orgIcal,
            canonToOrgIcalLoss(),
            std::make_shared<CanonToOrgICalStage>()
        },
        // EEE Phase 2 — Promote: google-event → canon (lossless; recurrence is
        // already verbatim RFC5545 lines on both sides)
        TransformationEdge{
            googleEvent, canon,
            LossProfile{},
            std::make_shared<GoogleEventToCanonStage>()
        },
        // EEE Phase 2 — Demote: canon → google-event (lossy per
        // docs/2026-08-23-google-event-edge-loss-profile.md)
        TransformationEdge{
            canon, googleEvent,
            canonToGoogleEventLoss(),
            std::make_shared<CanonToGoogleEventStage>()
        },
    };
}

} // namespace Kalburator::Calendar
