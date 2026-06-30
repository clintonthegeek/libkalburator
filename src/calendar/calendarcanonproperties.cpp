#include "calendarcanonproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Calendar {

Kalburator::Shape::PropertyCatalogue makeCalendarCanonCatalogue()
{
    PropertyCatalogue cat;

    // Required identity field
    cat.addProperty({ PropertyId{"uid"},              PropertyKind::String,     QStringLiteral("UID"),              false });

    // Sequencing
    cat.addProperty({ PropertyId{"sequence"},         PropertyKind::Integer,    QStringLiteral("Sequence") });

    // Timestamps
    cat.addProperty({ PropertyId{"created"},          PropertyKind::DateTime,   QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastModified"},     PropertyKind::DateTime,   QStringLiteral("Last Modified") });

    // Core text fields
    cat.addProperty({ PropertyId{"summary"},          PropertyKind::String,     QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"description"},      PropertyKind::String,     QStringLiteral("Description") });
    cat.addProperty({ PropertyId{"descriptionHtml"},  PropertyKind::String,     QStringLiteral("Description (HTML)") });
    cat.addProperty({ PropertyId{"location"},         PropertyKind::String,     QStringLiteral("Location") });

    // Structured location (Google/MS multi-location)
    cat.addProperty({ PropertyId{"locations"},        PropertyKind::Json,       QStringLiteral("Locations") });

    // Status / classification
    cat.addProperty({ PropertyId{"status"},           PropertyKind::String,     QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"classification"},   PropertyKind::String,     QStringLiteral("Classification") });
    cat.addProperty({ PropertyId{"timeTransparency"}, PropertyKind::String,     QStringLiteral("Time Transparency") });
    cat.addProperty({ PropertyId{"freeBusyStatus"},   PropertyKind::String,     QStringLiteral("Free/Busy Status") });

    // Time
    cat.addProperty({ PropertyId{"start"},            PropertyKind::Json,       QStringLiteral("Start") });
    cat.addProperty({ PropertyId{"end"},              PropertyKind::Json,       QStringLiteral("End") });
    cat.addProperty({ PropertyId{"allDay"},           PropertyKind::Boolean,    QStringLiteral("All Day") });

    // Recurrence (verbatim RFC5545 lines — invariant 3)
    cat.addProperty({ PropertyId{"recurrence"},       PropertyKind::StringList, QStringLiteral("Recurrence") });
    cat.addProperty({ PropertyId{"recurrenceId"},     PropertyKind::Json,       QStringLiteral("Recurrence ID") });
    cat.addProperty({ PropertyId{"recurrenceRange"},  PropertyKind::String,     QStringLiteral("Recurrence Range") });

    // Appearance
    cat.addProperty({ PropertyId{"color"},            PropertyKind::String,     QStringLiteral("Color") });
    cat.addProperty({ PropertyId{"categories"},       PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"url"},              PropertyKind::String,     QStringLiteral("URL") });

    // Participants
    cat.addProperty({ PropertyId{"organizer"},        PropertyKind::Json,       QStringLiteral("Organizer") });
    cat.addProperty({ PropertyId{"attendees"},        PropertyKind::Json,       QStringLiteral("Attendees") });

    // Scheduling
    cat.addProperty({ PropertyId{"responseRequested"},         PropertyKind::Boolean, QStringLiteral("Response Requested") });
    cat.addProperty({ PropertyId{"priority"},                  PropertyKind::Integer, QStringLiteral("Priority") });

    // Rich content
    cat.addProperty({ PropertyId{"alarms"},           PropertyKind::Json,       QStringLiteral("Alarms") });
    cat.addProperty({ PropertyId{"onlineMeeting"},    PropertyKind::Json,       QStringLiteral("Online Meeting") });
    cat.addProperty({ PropertyId{"attachments"},      PropertyKind::Json,       QStringLiteral("Attachments") });

    // Event type and typed properties (Google/MS)
    cat.addProperty({ PropertyId{"eventType"},        PropertyKind::String,     QStringLiteral("Event Type") });
    cat.addProperty({ PropertyId{"typedProperties"},  PropertyKind::Json,       QStringLiteral("Typed Properties") });

    // Guest permissions (Google)
    cat.addProperty({ PropertyId{"guestsCanModify"},          PropertyKind::Boolean, QStringLiteral("Guests Can Modify") });
    cat.addProperty({ PropertyId{"guestsCanInviteOthers"},    PropertyKind::Boolean, QStringLiteral("Guests Can Invite Others") });
    cat.addProperty({ PropertyId{"guestsCanSeeOtherGuests"},  PropertyKind::Boolean, QStringLiteral("Guests Can See Other Guests") });

    // MS Graph flags
    cat.addProperty({ PropertyId{"allowNewTimeProposals"}, PropertyKind::Boolean, QStringLiteral("Allow New Time Proposals") });
    cat.addProperty({ PropertyId{"hideAttendees"},         PropertyKind::Boolean, QStringLiteral("Hide Attendees") });
    cat.addProperty({ PropertyId{"locked"},                PropertyKind::Boolean, QStringLiteral("Locked") });
    cat.addProperty({ PropertyId{"privateCopy"},           PropertyKind::Boolean, QStringLiteral("Private Copy") });

    // --- Union across iCalendar component kinds (VTODO / VJOURNAL) ---
    // The {calendar,canon} shape carries any of VEVENT/VTODO/VJOURNAL (kind-
    // tagged in the envelope). These fields are absent on events but must be
    // catalogued so CanonJsonDiffer detects changes to todo/journal records.
    cat.addProperty({ PropertyId{"due"},             PropertyKind::Json,    QStringLiteral("Due") });
    cat.addProperty({ PropertyId{"completed"},       PropertyKind::DateTime, QStringLiteral("Completed") });
    cat.addProperty({ PropertyId{"percentComplete"}, PropertyKind::Integer, QStringLiteral("Percent Complete") });
    cat.addProperty({ PropertyId{"relatedTo"},       PropertyKind::Json,    QStringLiteral("Related To") });
    cat.addProperty({ PropertyId{"geo"},             PropertyKind::Json,    QStringLiteral("Geo") });

    return cat;
}

QList<Kalburator::Shape::PropertyId> calendarCanonPropertyIds()
{
    QList<PropertyId> ids;
    const PropertyCatalogue cat = makeCalendarCanonCatalogue();
    for (const auto& d : cat.properties())
        ids.append(d.id);
    return ids;
}

}  // namespace Kalburator::Calendar
