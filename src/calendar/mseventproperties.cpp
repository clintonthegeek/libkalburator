#include "mseventproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Calendar {

PropertyCatalogue makeMsEventCatalogue()
{
    PropertyCatalogue cat;

    // Identity
    cat.addProperty({ PropertyId{"uid"},            PropertyKind::String,  QStringLiteral("UID (= iCalUId)") });
    cat.addProperty({ PropertyId{"id"},             PropertyKind::String,  QStringLiteral("Graph Id") });
    cat.addProperty({ PropertyId{"changekey"},      PropertyKind::String,  QStringLiteral("Change Key") });

    // Timestamps
    cat.addProperty({ PropertyId{"createddatetime"},     PropertyKind::DateTime, QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastmodifieddatetime"}, PropertyKind::DateTime, QStringLiteral("Last Modified") });

    // Core text fields
    cat.addProperty({ PropertyId{"subject"},        PropertyKind::String,  QStringLiteral("Subject") });
    cat.addProperty({ PropertyId{"body"},           PropertyKind::Json,    QStringLiteral("Body") });
    cat.addProperty({ PropertyId{"bodypreview"},    PropertyKind::String,  QStringLiteral("Body Preview") });
    cat.addProperty({ PropertyId{"location"},       PropertyKind::Json,    QStringLiteral("Location") });
    cat.addProperty({ PropertyId{"locations"},      PropertyKind::Json,    QStringLiteral("Locations") });

    // Time
    cat.addProperty({ PropertyId{"start"},          PropertyKind::Json,    QStringLiteral("Start") });
    cat.addProperty({ PropertyId{"end"},            PropertyKind::Json,    QStringLiteral("End") });
    cat.addProperty({ PropertyId{"isallday"},       PropertyKind::Boolean, QStringLiteral("All Day") });
    cat.addProperty({ PropertyId{"recurrence"},     PropertyKind::Json,    QStringLiteral("Recurrence") });
    cat.addProperty({ PropertyId{"originalstart"},  PropertyKind::String,  QStringLiteral("Original Start") });
    cat.addProperty({ PropertyId{"type"},           PropertyKind::String,  QStringLiteral("Record Topology") });
    cat.addProperty({ PropertyId{"seriesmasterid"}, PropertyKind::String,  QStringLiteral("Series Master Id") });
    cat.addProperty({ PropertyId{"cancelledoccurrences"}, PropertyKind::Json, QStringLiteral("Cancelled Occurrences") });

    // Status / classification / free-busy
    cat.addProperty({ PropertyId{"iscancelled"},    PropertyKind::Boolean, QStringLiteral("Cancelled") });
    cat.addProperty({ PropertyId{"sensitivity"},    PropertyKind::String,  QStringLiteral("Sensitivity") });
    cat.addProperty({ PropertyId{"showas"},         PropertyKind::String,  QStringLiteral("Show As") });
    cat.addProperty({ PropertyId{"importance"},     PropertyKind::String,  QStringLiteral("Importance") });

    // Appearance
    cat.addProperty({ PropertyId{"categories"},     PropertyKind::Json,    QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"weblink"},        PropertyKind::String,  QStringLiteral("Web Link") });

    // Participants
    cat.addProperty({ PropertyId{"organizer"},         PropertyKind::Json,    QStringLiteral("Organizer") });
    cat.addProperty({ PropertyId{"attendees"},         PropertyKind::Json,    QStringLiteral("Attendees") });
    cat.addProperty({ PropertyId{"responsestatus"},    PropertyKind::Json,    QStringLiteral("Response Status") });
    cat.addProperty({ PropertyId{"responserequested"}, PropertyKind::Boolean, QStringLiteral("Response Requested") });

    // Rich content
    cat.addProperty({ PropertyId{"isreminderon"},             PropertyKind::Boolean, QStringLiteral("Reminder On") });
    cat.addProperty({ PropertyId{"reminderminutesbeforestart"}, PropertyKind::Integer, QStringLiteral("Reminder Minutes Before Start") });
    cat.addProperty({ PropertyId{"isonlinemeeting"},          PropertyKind::Boolean, QStringLiteral("Online Meeting") });
    cat.addProperty({ PropertyId{"onlinemeeting"},            PropertyKind::Json,    QStringLiteral("Online Meeting Info") });
    cat.addProperty({ PropertyId{"onlinemeetingprovider"},    PropertyKind::String,  QStringLiteral("Online Meeting Provider") });
    cat.addProperty({ PropertyId{"attachments"},              PropertyKind::Json,    QStringLiteral("Attachments") });

    // Flags (MS-only)
    cat.addProperty({ PropertyId{"allownewtimeproposals"}, PropertyKind::Boolean, QStringLiteral("Allow New Time Proposals") });
    cat.addProperty({ PropertyId{"hideattendees"},         PropertyKind::Boolean, QStringLiteral("Hide Attendees") });
    cat.addProperty({ PropertyId{"isorganizer"},           PropertyKind::Boolean, QStringLiteral("Is Organizer") });
    cat.addProperty({ PropertyId{"isdraft"},               PropertyKind::Boolean, QStringLiteral("Is Draft") });
    cat.addProperty({ PropertyId{"transactionid"},         PropertyKind::String,  QStringLiteral("Transaction Id") });

    return cat;
}

} // namespace Kalburator::Calendar
