#include "defaultblobwriter.h"
#include "iblobbackend.h"
#include "syncbackendbase.h"

namespace Kalburator::Shape {

bool DefaultBlobWriter::apply(
    const QString &collectionId,
    const QList<Kalburator::Sync::BackendRecord> &creates,
    const QList<Kalburator::Sync::BackendRecord> &updates,
    const QStringList &deletes)
{
    if (!m_backend) return false;

    // E5.3: route through applyRecords() when the backend is a
    // SyncBackendBase (every real production backend is); fall back to the
    // pre-E5.3 per-record loop for a plain IBlobBackend that has no
    // applyRecords() to call (see header comment).
    if (auto *base = dynamic_cast<Kalburator::Sync::SyncBackendBase *>(m_backend)) {
        Kalburator::Sync::WriterBatch batch;
        batch.creates = creates;
        batch.updates = updates;
        batch.deletes = deletes;

        Kalburator::Sync::WriteOperation *op = base->applyRecords(collectionId, batch);
        if (!op) return false;

        // Default applyRecords() (LocalBackend/MockBackend, no async
        // internals) completes synchronously before returning — see the
        // header comment for why this deliberately does not await an async
        // backend's op instead.
        const bool ok = op->isFinished()
            && op->state() == Kalburator::Sync::SyncOperation::Succeeded
            && op->failedUids().isEmpty();
        op->deleteLater();
        return ok;
    }

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
