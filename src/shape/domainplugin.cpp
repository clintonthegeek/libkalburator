#include "domainplugin.h"

#include "defaultblobwriter.h"
#include "syncbackend.h"  // SyncBackend inherits IBlobBackend; upcast is implicit

namespace Kalburator::Shape {

std::unique_ptr<IRecordWriter> DomainPlugin::createWriter(
    Kalburator::Sync::SyncBackend *backend) const
{
    // SyncBackend inherits IBlobBackend (src/calendar/syncbackend.h:117).
    // DefaultBlobWriter holds it as IBlobBackend*; implicit upcast is fine.
    return std::make_unique<DefaultBlobWriter>(backend);
}

} // namespace Kalburator::Shape
