#include "deleteincidenceitem.h"
#include "syncbackend.h"
#include "syncoperation.h"
#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

namespace Kalburator::Sync {

DeleteIncidenceItem::DeleteIncidenceItem(const QString &calendarId,
                                          const QString &uid,
                                          KCalendarCore::Incidence::Ptr deletedIncidence,
                                          SyncBackend *backend,
                                          QObject *parent)
    : SyncTransactionItem(calendarId, uid, ItemType::Delete, parent)
    , m_deletedIncidence(deletedIncidence)
{
    setBackend(backend);
}

DeleteIncidenceItem::~DeleteIncidenceItem()
{
    if (m_fetchOp) {
        m_fetchOp->deleteLater();
    }
}

void DeleteIncidenceItem::simulate()
{
    if (uid().isEmpty()) {
        setErrorString(tr("Cannot delete: UID is empty"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    if (!backend()) {
        setErrorString(tr("Cannot delete: backend is null"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    // Async: Fetch existing items to verify the item exists
    m_fetchOp = backend()->fetchItems(calendarId());
    connect(m_fetchOp, &FetchOperation::finished, this, &DeleteIncidenceItem::onFetchFinished);
}

void DeleteIncidenceItem::onFetchFinished()
{
    if (!m_fetchOp) {
        setErrorString(tr("Fetch operation was null"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    if (m_fetchOp->state() == SyncOperation::Failed) {
        setErrorString(tr("Failed to fetch existing items: %1").arg(m_fetchOp->errorString()));
        setSimulationResult(SimulationResult::Error);
        m_fetchOp->deleteLater();
        m_fetchOp = nullptr;
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    // Find the existing item
    const QString targetUid = uid();
    bool found = false;

    for (const auto &item : m_fetchOp->fetchedItems()) {
        if (item->uid() == targetUid) {
            found = true;
            // If we don't have the deleted incidence saved, capture it now
            if (!m_deletedIncidence) {
                m_deletedIncidence = item;
            }
            break;
        }
    }

    if (!found) {
        // Item doesn't exist - this could be a conflict (already deleted elsewhere)
        // or might be acceptable depending on use case
        QString conflictDesc = tr("Item not found: incidence with UID '%1' does not exist in calendar '%2'")
                                  .arg(targetUid, calendarId());
        emit conflictDetected(conflictDesc);
        setSimulationResult(SimulationResult::Conflict);
        m_fetchOp->deleteLater();
        m_fetchOp = nullptr;
        emit simulationFinished(SimulationResult::Conflict);
        return;
    }

    // Item exists, safe to delete
    m_fetchOp->deleteLater();
    m_fetchOp = nullptr;
    setSimulationResult(SimulationResult::Success);
    emit simulationFinished(SimulationResult::Success);
}

bool DeleteIncidenceItem::commit()
{
    if (uid().isEmpty() || !backend()) {
        setErrorString(tr("Cannot commit: UID is empty or backend is null"));
        return false;
    }

    if (isCommitted()) {
        qWarning() << "DeleteIncidenceItem::commit: Already committed:" << uid();
        return true;
    }

    // Delete the incidence from the backend
    DeleteOperation *deleteOp = backend()->deleteItems(calendarId(), {uid()});

    // Wait synchronously for completion
    QEventLoop loop;
    connect(deleteOp, &DeleteOperation::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = deleteOp->state() == SyncOperation::Succeeded &&
                   deleteOp->succeededUids().contains(uid());

    if (!success) {
        QString error = deleteOp->state() == SyncOperation::Failed
            ? deleteOp->errorString()
            : tr("Delete operation did not complete successfully for UID: %1").arg(uid());
        setErrorString(error);
    } else {
        setCommitted(true);
    }

    deleteOp->deleteLater();
    return success;
}

bool DeleteIncidenceItem::rollback()
{
    if (!isCommitted()) {
        // Nothing to rollback
        return true;
    }

    if (!m_deletedIncidence || !backend()) {
        setErrorString(tr("Cannot rollback: deleted incidence or backend is null"));
        return false;
    }

    // Recreate the deleted incidence
    PushOperation *pushOp = backend()->pushItems(calendarId(), {m_deletedIncidence});

    // Wait synchronously for completion
    QEventLoop loop;
    connect(pushOp, &PushOperation::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = pushOp->state() == SyncOperation::Succeeded &&
                   pushOp->succeededUids().contains(m_deletedIncidence->uid());

    if (!success) {
        setErrorString(tr("Rollback failed: could not recreate deleted incidence %1").arg(uid()));
    } else {
        setCommitted(false);
    }

    pushOp->deleteLater();
    return success;
}

QString DeleteIncidenceItem::description() const
{
    QString summary;
    if (m_deletedIncidence) {
        summary = m_deletedIncidence->summary();
        if (summary.isEmpty()) {
            summary = uid();
        }
        return tr("Delete %1: %2 (%3)")
            .arg(m_deletedIncidence->typeStr(), summary, calendarId());
    }

    return tr("Delete incidence: %1 (%2)").arg(uid(), calendarId());
}

QJsonObject DeleteIncidenceItem::toJson() const
{
    QJsonObject obj = SyncTransactionItem::toJson();

    if (m_deletedIncidence) {
        KCalendarCore::ICalFormat format;
        obj[QStringLiteral("icalData")] = format.toICalString(m_deletedIncidence);
    }

    return obj;
}


} // namespace Kalburator::Sync
