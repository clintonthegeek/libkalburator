#include "updateincidenceitem.h"
#include "syncbackend.h"
#include "syncoperation.h"
#include "syncdiff.h"  // For SyncRecord::computeHash
#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

namespace Kalburator::Sync {

UpdateIncidenceItem::UpdateIncidenceItem(const QString &calendarId,
                                          KCalendarCore::Incidence::Ptr oldIncidence,
                                          KCalendarCore::Incidence::Ptr newIncidence,
                                          SyncBackend *backend,
                                          QObject *parent)
    : SyncTransactionItem(calendarId,
                          newIncidence ? newIncidence->uid() : QString(),
                          ItemType::Update, parent)
    , m_oldIncidence(oldIncidence)
    , m_newIncidence(newIncidence)
{
    setBackend(backend);
}

UpdateIncidenceItem::UpdateIncidenceItem(const QString &calendarId,
                                          KCalendarCore::Incidence::Ptr oldIncidence,
                                          KCalendarCore::Incidence::Ptr newIncidence,
                                          const QString &expectedVersionHash,
                                          SyncBackend *backend,
                                          QObject *parent)
    : SyncTransactionItem(calendarId,
                          newIncidence ? newIncidence->uid() : QString(),
                          ItemType::Update, parent)
    , m_oldIncidence(oldIncidence)
    , m_newIncidence(newIncidence)
    , m_expectedVersionHash(expectedVersionHash)
{
    setBackend(backend);
}

UpdateIncidenceItem::~UpdateIncidenceItem()
{
    if (m_fetchOp) {
        m_fetchOp->deleteLater();
    }
}

void UpdateIncidenceItem::simulate()
{
    if (!m_newIncidence) {
        setErrorString(tr("Cannot update: new incidence is null"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    if (!m_oldIncidence) {
        setErrorString(tr("Cannot update: old incidence is null (needed for rollback)"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    if (!backend()) {
        setErrorString(tr("Cannot update: backend is null"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    // Async: Fetch existing items to verify the item exists and check for conflicts
    m_fetchOp = backend()->fetchItems(calendarId());
    connect(m_fetchOp, &FetchOperation::finished, this, &UpdateIncidenceItem::onFetchFinished);
}

void UpdateIncidenceItem::onFetchFinished()
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
    const QString targetUid = m_newIncidence->uid();
    KCalendarCore::Incidence::Ptr existing;

    for (const auto &item : m_fetchOp->fetchedItems()) {
        if (item->uid() == targetUid) {
            existing = item;
            break;
        }
    }

    if (!existing) {
        QString conflictDesc = tr("Item not found: incidence with UID '%1' does not exist in calendar '%2'")
                                  .arg(targetUid, calendarId());
        emit conflictDetected(conflictDesc);
        setSimulationResult(SimulationResult::Conflict);
        m_fetchOp->deleteLater();
        m_fetchOp = nullptr;
        emit simulationFinished(SimulationResult::Conflict);
        return;
    }

    // Check for concurrent modification if version hash was provided
    if (!m_expectedVersionHash.isEmpty()) {
        // Use semantic hash to match how version hashes are computed elsewhere
        // (ignores PRODID, DTSTAMP, ordering differences)
        QString currentHash = SyncRecord::computeSemanticHash(existing);

        if (currentHash != m_expectedVersionHash) {
            QString conflictDesc = tr("Concurrent modification: incidence '%1' was modified since last sync")
                                      .arg(targetUid);
            emit conflictDetected(conflictDesc);
            setSimulationResult(SimulationResult::Conflict);
            m_fetchOp->deleteLater();
            m_fetchOp = nullptr;
            emit simulationFinished(SimulationResult::Conflict);
            return;
        }
    }

    // Item exists and no conflicts
    m_fetchOp->deleteLater();
    m_fetchOp = nullptr;
    setSimulationResult(SimulationResult::Success);
    emit simulationFinished(SimulationResult::Success);
}

bool UpdateIncidenceItem::commit()
{
    if (!m_newIncidence || !backend()) {
        setErrorString(tr("Cannot commit: incidence or backend is null"));
        return false;
    }

    if (isCommitted()) {
        qWarning() << "UpdateIncidenceItem::commit: Already committed:" << uid();
        return true;
    }

    // Push the updated incidence to the backend
    PushOperation *pushOp = backend()->pushItems(calendarId(), {m_newIncidence});

    // Wait synchronously for completion
    QEventLoop loop;
    connect(pushOp, &PushOperation::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = pushOp->state() == SyncOperation::Succeeded &&
                   pushOp->succeededUids().contains(m_newIncidence->uid());

    if (!success) {
        QString error = pushOp->state() == SyncOperation::Failed
            ? pushOp->errorString()
            : tr("Push operation did not complete successfully for UID: %1").arg(m_newIncidence->uid());
        setErrorString(error);
    } else {
        setCommitted(true);
    }

    pushOp->deleteLater();
    return success;
}

bool UpdateIncidenceItem::rollback()
{
    if (!isCommitted()) {
        // Nothing to rollback
        return true;
    }

    if (!m_oldIncidence || !backend()) {
        setErrorString(tr("Cannot rollback: old incidence or backend is null"));
        return false;
    }

    // Restore the old version
    PushOperation *pushOp = backend()->pushItems(calendarId(), {m_oldIncidence});

    // Wait synchronously for completion
    QEventLoop loop;
    connect(pushOp, &PushOperation::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = pushOp->state() == SyncOperation::Succeeded &&
                   pushOp->succeededUids().contains(m_oldIncidence->uid());

    if (!success) {
        setErrorString(tr("Rollback failed: could not restore old version of %1").arg(uid()));
    } else {
        setCommitted(false);
    }

    pushOp->deleteLater();
    return success;
}

QString UpdateIncidenceItem::description() const
{
    if (!m_newIncidence) {
        return tr("Update incidence (null)");
    }

    QString summary = m_newIncidence->summary();
    if (summary.isEmpty()) {
        summary = m_newIncidence->uid();
    }

    return tr("Update %1: %2 (%3)")
        .arg(m_newIncidence->typeStr(), summary, calendarId());
}

QJsonObject UpdateIncidenceItem::toJson() const
{
    QJsonObject obj = SyncTransactionItem::toJson();

    KCalendarCore::ICalFormat format;

    if (m_oldIncidence) {
        obj[QStringLiteral("oldIcalData")] = format.toICalString(m_oldIncidence);
    }
    if (m_newIncidence) {
        obj[QStringLiteral("newIcalData")] = format.toICalString(m_newIncidence);
    }
    if (!m_expectedVersionHash.isEmpty()) {
        obj[QStringLiteral("expectedVersionHash")] = m_expectedVersionHash;
    }

    return obj;
}


} // namespace Kalburator::Sync
