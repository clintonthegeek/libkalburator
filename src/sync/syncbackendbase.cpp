#include "syncbackendbase.h"

#include "syncoperation.h"

#include <QDebug>
#include <QTimer>

namespace Kalburator::Sync {

// ============================================================================
// SyncBackendBase implementation
// ============================================================================

SyncBackendBase::SyncBackendBase(QObject *parent)
    : QObject(parent)
{
}

QString SyncBackendBase::resourceId() const
{
    return QStringLiteral("backend:") +
        QString::number(reinterpret_cast<quintptr>(this), 16);
}

Kalburator::Shape::Shape SyncBackendBase::shapeFor(const QString &) const
{
    auto shapes = nativeShapes();
    if (shapes.isEmpty())
        return Kalburator::Shape::Shape::Any();
    return shapes.first();
}

// ============================================================================
// Operation-Based API (default implementations)
// ============================================================================

SyncOperation* SyncBackendBase::fetchItems(const QString &calendarId)
{
    // NotSupported (not Failed): backends that read solely via loadRecords()
    // don't override fetchItems and rely on the engine ignoring this op and
    // proceeding to loadRecordsOrError(). Returning NotSupported lets the
    // engine tell this apart from an overriding backend's genuine fetch failure
    // (which IS Failed and must fail the mapping). See the engine fetch gate.
    auto *op = new SyncOperation(calendarId, this);
    const QString errorMsg = QStringLiteral("fetchItems() not implemented by this backend");
    op->notSupported(errorMsg);
    emit fetchFinished(calendarId, false, errorMsg);
    return op;
}

bool SyncBackendBase::recordsFromLastFetch(const QString &collectionId,
                                           QList<BackendRecord> &records,
                                           QString &errorMessage)
{
    return loadRecordsOrError(collectionId, records, errorMessage);
}

SyncOperation* SyncBackendBase::deleteItems(const QString &calendarId,
                                            const QStringList &uids)
{
    Q_UNUSED(uids);
    auto *op = new SyncOperation(calendarId, this);
    op->fail(QStringLiteral("deleteItems() not implemented by this backend"));
    return op;
}

// ============================================================================
// E5.3: write-path entry point (default synchronous adapter)
// ============================================================================

WriteOperation* SyncBackendBase::applyRecords(const QString &collectionId,
                                              const WriterBatch &batch)
{
    auto *op = new WriteOperation(collectionId, this);
    op->setState(SyncOperation::Running);

    // Same order DefaultBlobWriter::apply() always used: creates, then
    // updates, then deletes. Routes through the exact same virtuals so
    // MockBackend's FailurePoint injection (OnStoreItems/OnPush/OnDelete)
    // keeps working unchanged for backends that reach applyRecords() via
    // this default (LocalBackend, MockBackend — no async internals).
    for (const auto &r : batch.creates) {
        if (createRecord(collectionId, r).isEmpty()) {
            op->addFailedUid(r.id);
        } else {
            op->addSucceededUid(r.id);
        }
    }
    for (const auto &r : batch.updates) {
        if (!updateRecord(r)) {
            op->addFailedUid(r.id);
        } else {
            op->addSucceededUid(r.id);
        }
    }
    for (const auto &id : batch.deletes) {
        if (!deleteRecord(id)) {
            op->addFailedUid(id);
        } else {
            op->addSucceededUid(id);
        }
    }

    // Precedent (PushOperation/DeleteOperation via RemoteCalendarBackend's
    // settleIfDone): fail only when something was attempted and NOTHING
    // succeeded; otherwise complete (partial failures are reported via
    // failedUids(), not a Failed op state) — this call returns already
    // finished (isFinished() true), so callers never need to await it.
    const int totalAttempted = static_cast<int>(
        batch.creates.size() + batch.updates.size() + batch.deletes.size());
    if (totalAttempted > 0 && op->succeededUids().isEmpty() && !op->failedUids().isEmpty()) {
        op->fail(QStringLiteral("applyRecords: all %1 record(s) failed to apply")
                    .arg(totalAttempted));
    } else {
        op->complete();
    }
    return op;
}

// ============================================================================
// Operation Tracking
// ============================================================================

bool SyncBackendBase::hasPendingOperations() const
{
    for (auto it = m_pendingOperations.constBegin(); it != m_pendingOperations.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            return true;
        }
    }
    return false;
}

bool SyncBackendBase::hasPendingOperationsFor(const QString &calendarId) const
{
    return !m_pendingOperations.value(calendarId).isEmpty();
}

QList<SyncOperation*> SyncBackendBase::pendingOperations() const
{
    QList<SyncOperation*> all;
    for (auto it = m_pendingOperations.constBegin(); it != m_pendingOperations.constEnd(); ++it) {
        all.append(it.value());
    }
    return all;
}

QList<SyncOperation*> SyncBackendBase::pendingOperationsFor(const QString &calendarId) const
{
    return m_pendingOperations.value(calendarId);
}

void SyncBackendBase::cancelOperationsFor(const QString &calendarId)
{
    QList<SyncOperation*> ops = m_pendingOperations.value(calendarId);
    for (SyncOperation *op : ops) {
        if (!op->isFinished()) {
            op->cancel();
        }
    }
}

void SyncBackendBase::cancelAllOperations()
{
    for (auto it = m_pendingOperations.begin(); it != m_pendingOperations.end(); ++it) {
        for (SyncOperation *op : it.value()) {
            if (!op->isFinished()) {
                op->cancel();
            }
        }
    }
}

void SyncBackendBase::registerOperation(SyncOperation *op)
{
    if (!op) return;

    const QString calId = op->calendarId();
    m_pendingOperations[calId].append(op);

    connect(op, &SyncOperation::finished, this, [this, op]() {
        unregisterOperation(op);
    });
}

void SyncBackendBase::unregisterOperation(SyncOperation *op)
{
    if (!op) return;

    const QString calId = op->calendarId();
    QList<SyncOperation*> &ops = m_pendingOperations[calId];
    ops.removeAll(op);

    if (ops.isEmpty()) {
        m_pendingOperations.remove(calId);
    }
}

// ============================================================================
// E5.1: per-collection FIFO operation queue
// ============================================================================

void SyncBackendBase::enqueueOperation(const QString &collectionId, SyncOperation *op,
                                       std::function<void()> startFunctor)
{
    if (!op) return;

    registerOperation(op);

    connect(op, &SyncOperation::finished, this, [this, collectionId, op]() {
        onOperationSettled(collectionId, op);
    });
    connect(op, &QObject::destroyed, this, [this, collectionId, op]() {
        onOperationSettled(collectionId, op);
    });

    m_opQueue[collectionId].append({QPointer<SyncOperation>(op), std::move(startFunctor)});
    maybeStartNext(collectionId);
}

void SyncBackendBase::onOperationSettled(const QString &collectionId, SyncOperation *op)
{
    if (m_opInFlight.value(collectionId).data() == op) {
        m_opInFlight.remove(collectionId);
    } else {
        QList<QueuedOp> &queue = m_opQueue[collectionId];
        for (int i = 0; i < queue.size(); ++i) {
            if (queue.at(i).op.data() == op) {
                queue.removeAt(i);
                break;
            }
        }
    }
    maybeStartNext(collectionId);
}

void SyncBackendBase::maybeStartNext(const QString &collectionId)
{
    if (m_opInFlight.contains(collectionId)) {
        return; // something is already in flight for this collection
    }

    QList<QueuedOp> &queue = m_opQueue[collectionId];
    while (!queue.isEmpty()) {
        QueuedOp entry = queue.takeFirst();
        SyncOperation *op = entry.op.data();
        if (!op || op->isFinished()) {
            // Destroyed before it ever started, or cancelled while queued
            // (contract: a queued-not-started op never runs its body) — try
            // the next one instead.
            continue;
        }

        m_opInFlight[collectionId] = op;
        // Deferred: every enqueueOperation() caller gets its op pointer
        // back before its start body runs, so it can connect signals first
        // — the same guarantee each backend's own QTimer::singleShot(0, ...)
        // gave before this shared queue existed.
        QTimer::singleShot(0, this, [startFunctor = std::move(entry.startFunctor),
                                     opPtr = entry.op]() {
            if (opPtr.isNull() || opPtr->isFinished()) {
                return;
            }
            startFunctor();
        });
        // If startFunctor() above completes op synchronously once the timer
        // fires, its `finished` signal (direct connection, same thread)
        // re-enters onOperationSettled()/maybeStartNext() and drains further
        // queued entries there; nothing more to do on this call stack.
        return;
    }

    if (queue.isEmpty()) {
        m_opQueue.remove(collectionId);
    }
}

// ============================================================================
// IBlobBackend default implementations
// ============================================================================

QString SyncBackendBase::backendId() const
{
    return backendType();
}

QString SyncBackendBase::displayName() const
{
    return backendType();
}

bool SyncBackendBase::isAvailable() const
{
    return true;
}

bool SyncBackendBase::supportsBatch() const
{
    return false;
}

bool SyncBackendBase::supportsDeleteTracking() const
{
    return false;
}

void SyncBackendBase::beginBatch() {}

bool SyncBackendBase::commitBatch() { return true; }

void SyncBackendBase::rollbackBatch() {}

QList<BackendRecord> SyncBackendBase::loadRecords(const QString &collectionId)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "loadRecords(" << collectionId << ")";
    return {};
}

std::optional<BackendRecord> SyncBackendBase::loadRecord(const QString &recordId)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "loadRecord(" << recordId << ")";
    return std::nullopt;
}

QString SyncBackendBase::createRecord(const QString &collectionId, const BackendRecord &record)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "createRecord(" << collectionId << ")";
    Q_UNUSED(record);
    return {};
}

bool SyncBackendBase::updateRecord(const BackendRecord &record)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "updateRecord";
    Q_UNUSED(record);
    return false;
}

bool SyncBackendBase::deleteRecord(const QString &recordId)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "deleteRecord(" << recordId << ")";
    return false;
}

QList<BackendRecord> SyncBackendBase::modifiedSince(const QString &collectionId,
                                                     const QDateTime &since)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "modifiedSince(" << collectionId << ")";
    Q_UNUSED(since);
    return {};
}

QStringList SyncBackendBase::deletedSince(const QString &collectionId, const QDateTime &since)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "deletedSince(" << collectionId << ")";
    Q_UNUSED(since);
    return {};
}

QList<CollectionInfo> SyncBackendBase::availableCollections()
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "availableCollections";
    return {};
}

CollectionInfo SyncBackendBase::collectionInfo(const QString &collectionId)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "collectionInfo(" << collectionId << ")";
    return {};
}

QString SyncBackendBase::createCollection(const CollectionInfo &info)
{
    qWarning() << "SyncBackendBase default IBlobBackend impl invoked on"
               << metaObject()->className()
               << "createCollection";
    Q_UNUSED(info);
    return {};
}

} // namespace Kalburator::Sync
