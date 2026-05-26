#include "outlinecanonproperties.h"

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

PropertyCatalogue makeOutlineCanonCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"uid"},          PropertyKind::String,   QStringLiteral("UID"),           false });
    cat.addProperty({ PropertyId{"title"},        PropertyKind::String,   QStringLiteral("Title") });
    // created and lastModified are engine-populated record-level fields (like the note
    // domain's), set by the sync engine on ingest/update — not written by file-format stages.
    cat.addProperty({ PropertyId{"created"},      PropertyKind::DateTime, QStringLiteral("Created") });
    cat.addProperty({ PropertyId{"lastModified"}, PropertyKind::DateTime, QStringLiteral("Last Modified") });
    cat.addProperty({ PropertyId{"attributes"},   PropertyKind::Json,     QStringLiteral("Attributes") });
    cat.addProperty({ PropertyId{"children"},     PropertyKind::Json,     QStringLiteral("Children") });
    return cat;
}

}  // namespace Kalburator::Outline
