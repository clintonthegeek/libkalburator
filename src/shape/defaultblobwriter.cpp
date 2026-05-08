#include "defaultblobwriter.h"
#include "iblobbackend.h"

namespace Kalburator::Shape {

bool DefaultBlobWriter::apply(
    const QString &collectionId,
    const QList<Kalburator::Sync::BackendRecord> &creates,
    const QList<Kalburator::Sync::BackendRecord> &updates,
    const QStringList &deletes)
{
    if (!m_backend) return false;
    bool ok = true;
    for (const auto &r : creates) {
        if (m_backend->createRecord(collectionId, r).isEmpty()) ok = false;
    }
    for (const auto &r : updates) {
        if (!m_backend->updateRecord(r)) ok = false;
    }
    for (const auto &id : deletes) {
        if (!m_backend->deleteRecord(id)) ok = false;
    }
    return ok;
}

} // namespace Kalburator::Shape
