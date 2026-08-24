#include "googletaskproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Todo {

PropertyCatalogue makeGoogleTaskCatalogue()
{
    PropertyCatalogue cat;

    cat.addProperty({ PropertyId{"id"},             PropertyKind::String,     QStringLiteral("Id") });
    cat.addProperty({ PropertyId{"kind"},           PropertyKind::String,     QStringLiteral("Kind") });
    cat.addProperty({ PropertyId{"etag"},           PropertyKind::String,     QStringLiteral("ETag") });
    cat.addProperty({ PropertyId{"title"},          PropertyKind::String,     QStringLiteral("Title") });
    cat.addProperty({ PropertyId{"notes"},          PropertyKind::String,     QStringLiteral("Notes") });
    cat.addProperty({ PropertyId{"status"},         PropertyKind::String,     QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"due"},            PropertyKind::String,     QStringLiteral("Due") });
    cat.addProperty({ PropertyId{"completed"},      PropertyKind::String,     QStringLiteral("Completed") });
    cat.addProperty({ PropertyId{"updated"},        PropertyKind::String,     QStringLiteral("Updated") });
    cat.addProperty({ PropertyId{"parent"},         PropertyKind::String,     QStringLiteral("Parent") });
    cat.addProperty({ PropertyId{"position"},       PropertyKind::String,     QStringLiteral("Position") });
    cat.addProperty({ PropertyId{"deleted"},        PropertyKind::Boolean,       QStringLiteral("Deleted") });
    cat.addProperty({ PropertyId{"hidden"},         PropertyKind::Boolean,       QStringLiteral("Hidden") });
    cat.addProperty({ PropertyId{"links"},          PropertyKind::Json,       QStringLiteral("Links") });
    cat.addProperty({ PropertyId{"webviewlink"},    PropertyKind::String,     QStringLiteral("Web View Link") });
    cat.addProperty({ PropertyId{"selflink"},       PropertyKind::String,     QStringLiteral("Self Link") });
    cat.addProperty({ PropertyId{"assignmentinfo"}, PropertyKind::Json,       QStringLiteral("Assignment Info") });

    return cat;
}

} // namespace Kalburator::Todo
