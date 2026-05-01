#include "icalproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Calendar {

PropertyCatalogue makeICalCatalogue()
{
    PropertyCatalogue cat;

    // Required identity field
    cat.addProperty({ PropertyId{"uid"},           PropertyKind::String,     QStringLiteral("UID"),           false });

    // Core text fields
    cat.addProperty({ PropertyId{"summary"},       PropertyKind::String,     QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"description"},   PropertyKind::String,     QStringLiteral("Description") });
    cat.addProperty({ PropertyId{"location"},      PropertyKind::String,     QStringLiteral("Location") });

    // Date/time fields
    cat.addProperty({ PropertyId{"dtstart"},       PropertyKind::DateTime,   QStringLiteral("Start Time") });
    cat.addProperty({ PropertyId{"dtend"},         PropertyKind::DateTime,   QStringLiteral("End Time") });
    cat.addProperty({ PropertyId{"duration"},      PropertyKind::Duration,   QStringLiteral("Duration") });
    cat.addProperty({ PropertyId{"created"},       PropertyKind::DateTime,   QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastmodified"},  PropertyKind::DateTime,   QStringLiteral("Last Modified") });

    // Status/classification
    cat.addProperty({ PropertyId{"status"},        PropertyKind::String,     QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"priority"},      PropertyKind::Integer,    QStringLiteral("Priority") });

    // Categorization
    cat.addProperty({ PropertyId{"categories"},    PropertyKind::StringList, QStringLiteral("Categories") });

    // Participants (composite; encoded as JSON arrays)
    cat.addProperty({ PropertyId{"attendees"},     PropertyKind::Json,       QStringLiteral("Attendees") });
    cat.addProperty({ PropertyId{"organizer"},     PropertyKind::Json,       QStringLiteral("Organizer") });

    // Rich content
    cat.addProperty({ PropertyId{"attachments"},   PropertyKind::Json,       QStringLiteral("Attachments") });
    cat.addProperty({ PropertyId{"alarms"},        PropertyKind::Json,       QStringLiteral("Alarms") });

    // Recurrence
    cat.addProperty({ PropertyId{"rrule"},         PropertyKind::Json,       QStringLiteral("Recurrence Rule") });

    // Extensibility
    cat.addProperty({ PropertyId{"customproperties"}, PropertyKind::Json,    QStringLiteral("Custom Properties") });

    return cat;
}

} // namespace Kalburator::Calendar
