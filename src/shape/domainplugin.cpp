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

QVariantMap DomainPlugin::collectionProperties(
    Kalburator::Sync::SyncBackend * /*backend*/,
    const QString & /*collectionId*/) const
{
    return {};
}

void DomainPlugin::applyCollectionProperties(
    Kalburator::Sync::SyncBackend * /*backend*/,
    const QString & /*collectionId*/,
    const QVariantMap & /*props*/) const
{
    // no-op
}

} // namespace Kalburator::Shape
