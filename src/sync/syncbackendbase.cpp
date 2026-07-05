#include "syncbackendbase.h"

#include "syncoperation.h"

#include <QDebug>

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
