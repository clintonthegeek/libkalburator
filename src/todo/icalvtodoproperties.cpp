#include "icalvtodoproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Todo {

PropertyCatalogue makeVTodoCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"uid"},              PropertyKind::String,     QStringLiteral("UID"),             false });

    cat.addProperty({ PropertyId{"summary"},          PropertyKind::String,     QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"description"},      PropertyKind::String,     QStringLiteral("Description") });

    cat.addProperty({ PropertyId{"dtstart"},          PropertyKind::DateTime,   QStringLiteral("Start Date") });
    cat.addProperty({ PropertyId{"due"},              PropertyKind::DateTime,   QStringLiteral("Due Date") });
    cat.addProperty({ PropertyId{"completed"},        PropertyKind::DateTime,   QStringLiteral("Completed") });
    cat.addProperty({ PropertyId{"created"},          PropertyKind::DateTime,   QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastmodified"},     PropertyKind::DateTime,   QStringLiteral("Last Modified") });

    cat.addProperty({ PropertyId{"status"},           PropertyKind::String,     QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"priority"},         PropertyKind::Integer,    QStringLiteral("Priority") });
    cat.addProperty({ PropertyId{"percentcomplete"},  PropertyKind::Integer,    QStringLiteral("Percent Complete") });

    cat.addProperty({ PropertyId{"categories"},       PropertyKind::StringList, QStringLiteral("Categories") });

    cat.addProperty({ PropertyId{"attendees"},        PropertyKind::Json,       QStringLiteral("Attendees") });
    cat.addProperty({ PropertyId{"organizer"},        PropertyKind::Json,       QStringLiteral("Organizer") });

    cat.addProperty({ PropertyId{"attachments"},      PropertyKind::Json,       QStringLiteral("Attachments") });
    cat.addProperty({ PropertyId{"alarms"},           PropertyKind::Json,       QStringLiteral("Alarms") });

    cat.addProperty({ PropertyId{"rrule"},            PropertyKind::Json,       QStringLiteral("Recurrence Rule") });

    cat.addProperty({ PropertyId{"customproperties"}, PropertyKind::Json,       QStringLiteral("Custom Properties") });

    return cat;
}

} // namespace Kalburator::Todo
