#include <kalburator/shape/defaultblobwriter.h>
#include <kalburator/blob/iblobbackend.h>
#include <kalburator/sync/syncbackendbase.h>

namespace Kalburator::Shape {

bool DefaultBlobWriter::apply(
    const QString &collectionId,
    const QList<Kalburator::Sync::BackendRecord> &creates,
    const QList<Kalburator::Sync::BackendRecord> &updates,
    const QStringList &deletes)
{
    if (!m_backend) return false;

    // E5.3/API-005: route through an explicitly acquired apply capability
    // when the backend is a SyncBackendBase (every real production backend
    // is). Fall back to the pre-E5.3 per-record loop only for a plain legacy
    // IBlobBackend that has no operation surface to acquire from.
    if (auto *base = dynamic_cast<Kalburator::Sync::SyncBackendBase *>(m_backend)) {
        Kalburator::Sync::WriterBatch batch;
        batch.creates = creates;
        batch.updates = updates;
        batch.deletes = deletes;

        auto *applier = base->recordApplier();
        if (!applier) return false;
        Kalburator::Sync::WriteOperation *op = applier->applyRecords(collectionId, batch);
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
