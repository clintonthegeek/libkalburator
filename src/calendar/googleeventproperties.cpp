#include "googleeventproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Calendar {

PropertyCatalogue makeGoogleEventCatalogue()
{
    PropertyCatalogue cat;

    // Identity
    cat.addProperty({ PropertyId{"icaluid"},        PropertyKind::String,  QStringLiteral("iCalUID") });
    cat.addProperty({ PropertyId{"id"},             PropertyKind::String,  QStringLiteral("Event Id") });

    // Sequencing / timestamps
    cat.addProperty({ PropertyId{"sequence"},       PropertyKind::Integer, QStringLiteral("Sequence") });
    cat.addProperty({ PropertyId{"created"},        PropertyKind::DateTime, QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"updated"},        PropertyKind::DateTime, QStringLiteral("Updated") });

    // Core text fields
    cat.addProperty({ PropertyId{"summary"},        PropertyKind::String,  QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"description"},    PropertyKind::String,  QStringLiteral("Description") });
    cat.addProperty({ PropertyId{"location"},       PropertyKind::String,  QStringLiteral("Location") });

    // Time
    cat.addProperty({ PropertyId{"start"},          PropertyKind::Json,    QStringLiteral("Start") });
    cat.addProperty({ PropertyId{"end"},            PropertyKind::Json,    QStringLiteral("End") });
    cat.addProperty({ PropertyId{"recurrence"},     PropertyKind::StringList, QStringLiteral("Recurrence") });
    cat.addProperty({ PropertyId{"recurringeventid"},   PropertyKind::String, QStringLiteral("Recurring Event Id") });
    cat.addProperty({ PropertyId{"originalstarttime"},  PropertyKind::Json,   QStringLiteral("Original Start Time") });

    // Status / classification / transparency
    cat.addProperty({ PropertyId{"status"},         PropertyKind::String,  QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"visibility"},     PropertyKind::String,  QStringLiteral("Visibility") });
    cat.addProperty({ PropertyId{"transparency"},   PropertyKind::String,  QStringLiteral("Transparency") });

    // Appearance
    cat.addProperty({ PropertyId{"colorid"},        PropertyKind::String,  QStringLiteral("Color Id") });
    cat.addProperty({ PropertyId{"eventtype"},      PropertyKind::String,  QStringLiteral("Event Type") });
    cat.addProperty({ PropertyId{"source"},         PropertyKind::Json,    QStringLiteral("Source") });

    // Participants
    cat.addProperty({ PropertyId{"organizer"},      PropertyKind::Json,    QStringLiteral("Organizer") });
    cat.addProperty({ PropertyId{"attendees"},      PropertyKind::Json,    QStringLiteral("Attendees") });

    // Rich content
    cat.addProperty({ PropertyId{"attachments"},    PropertyKind::Json,    QStringLiteral("Attachments") });
    cat.addProperty({ PropertyId{"reminders"},      PropertyKind::Json,    QStringLiteral("Reminders") });
    cat.addProperty({ PropertyId{"conferencedata"}, PropertyKind::Json,    QStringLiteral("Conference Data") });

    // Extensibility
    cat.addProperty({ PropertyId{"extendedproperties"}, PropertyKind::Json, QStringLiteral("Extended Properties") });

    // Guest permissions and flags (Google booleans)
    cat.addProperty({ PropertyId{"guestscanmodify"},          PropertyKind::Boolean, QStringLiteral("Guests Can Modify") });
    cat.addProperty({ PropertyId{"guestscaninviteothers"},    PropertyKind::Boolean, QStringLiteral("Guests Can Invite Others") });
    cat.addProperty({ PropertyId{"guestscanseeotherguests"},  PropertyKind::Boolean, QStringLiteral("Guests Can See Other Guests") });
    cat.addProperty({ PropertyId{"locked"},                   PropertyKind::Boolean, QStringLiteral("Locked") });
    cat.addProperty({ PropertyId{"privatecopy"},              PropertyKind::Boolean, QStringLiteral("Private Copy") });

    return cat;
}

} // namespace Kalburator::Calendar
