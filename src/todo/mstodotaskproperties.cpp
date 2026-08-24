#include "mstodotaskproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Todo {

PropertyCatalogue makeMsTodoTaskCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"id"},                        PropertyKind::String, QStringLiteral("Id") });
    cat.addProperty({ PropertyId{"title"},                     PropertyKind::String, QStringLiteral("Title") });
    cat.addProperty({ PropertyId{"body"},                      PropertyKind::Json,   QStringLiteral("Body") });
    cat.addProperty({ PropertyId{"bodylastmodifieddatetime"},  PropertyKind::String, QStringLiteral("Body Last Modified") });
    cat.addProperty({ PropertyId{"status"},                    PropertyKind::String, QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"importance"},                PropertyKind::String, QStringLiteral("Importance") });
    cat.addProperty({ PropertyId{"categories"},                PropertyKind::StringList, QStringLiteral("Categories") });
    cat.addProperty({ PropertyId{"duedatetime"},               PropertyKind::Json,   QStringLiteral("Due Date Time") });
    cat.addProperty({ PropertyId{"startdatetime"},             PropertyKind::Json,   QStringLiteral("Start Date Time") });
    cat.addProperty({ PropertyId{"completeddatetime"},         PropertyKind::Json,   QStringLiteral("Completed Date Time") });
    cat.addProperty({ PropertyId{"recurrence"},                PropertyKind::Json,   QStringLiteral("Recurrence") });
    cat.addProperty({ PropertyId{"reminderdatetime"},          PropertyKind::Json,   QStringLiteral("Reminder Date Time") });
    cat.addProperty({ PropertyId{"isreminderon"},              PropertyKind::Boolean,   QStringLiteral("Is Reminder On") });
    cat.addProperty({ PropertyId{"hasattachments"},            PropertyKind::Boolean,   QStringLiteral("Has Attachments") });
    cat.addProperty({ PropertyId{"createddatetime"},           PropertyKind::String, QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastmodifieddatetime"},      PropertyKind::String, QStringLiteral("Last Modified") });
    cat.addProperty({ PropertyId{"extensions"},                PropertyKind::Json,   QStringLiteral("Open Extensions") });

    return cat;
}

} // namespace Kalburator::Todo
