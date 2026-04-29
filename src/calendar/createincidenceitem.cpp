#include "createincidenceitem.h"
#include "syncbackend.h"
#include "syncoperation.h"
#include <KCalendarCore/ICalFormat>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QList>

namespace Kalburator::Sync {

CreateIncidenceItem::CreateIncidenceItem(const QString &calendarId,
                                          KCalendarCore::Incidence::Ptr incidence,
                                          KCalendarCore::MemoryCalendar *calendar,
                                          SyncBackend *backend,
                                          const TranscodingPlan &plan,
                                          QObject *parent)
    : SyncTransactionItem(calendarId, incidence ? incidence->uid() : QString(),
                          ItemType::Create, parent)
    , m_incidence(incidence)
    , m_calendar(calendar)
    , m_plan(plan)
{
    setBackend(backend);
}

CreateIncidenceItem::~CreateIncidenceItem()
{
    if (m_fetchOp) {
        m_fetchOp->deleteLater();
    }
}

void CreateIncidenceItem::simulate()
{
    if (!m_incidence) {
        setErrorString(tr("Cannot create: incidence is null"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    if (!backend()) {
        setErrorString(tr("Cannot create: backend is null"));
        setSimulationResult(SimulationResult::Error);
        emit simulationFinished(SimulationResult::Error);
        return;
    }

    // Async: Fetch existing items to check for UID collision
    m_fetchOp = backend()->fetchItems(calendarId());
    connect(m_fetchOp, &FetchOperation::finished, this, &CreateIncidenceItem::onFetchFinished);
}

void CreateIncidenceItem::onFetchFinished()
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

    // Check for UID collision
    const QString targetUid = m_incidence->uid();
    for (const auto &existing : m_fetchOp->fetchedItems()) {
        if (existing->uid() == targetUid) {
            QString conflictDesc = tr("UID collision: incidence with UID '%1' already exists in calendar '%2'")
                                      .arg(targetUid, calendarId());
            emit conflictDetected(conflictDesc);
            setSimulationResult(SimulationResult::Conflict);
            m_fetchOp->deleteLater();
            m_fetchOp = nullptr;
            emit simulationFinished(SimulationResult::Conflict);
            return;
        }
    }

    // No collision, safe to create
    m_fetchOp->deleteLater();
    m_fetchOp = nullptr;
    setSimulationResult(SimulationResult::Success);
    emit simulationFinished(SimulationResult::Success);
}

bool CreateIncidenceItem::commit()
{
    if (!m_incidence || !backend()) {
        setErrorString(tr("Cannot commit: incidence or backend is null"));
        return false;
    }

    if (!m_calendar) {
        setErrorString(tr("Cannot commit: calendar is null"));
        return false;
    }

    if (isCommitted()) {
        qWarning() << "CreateIncidenceItem::commit: Already committed:" << uid();
        return true;
    }

    // Use storeItems so the backend applies the transcoding plan and emits
    // transcodingWarning for any lossy conversions. storeItems is void, so
    // capture the write outcome via writeFinished before calling.
    const QString calId = m_calendar->id();
    bool writeSucceeded = true;
    QString writeError;

    auto conn = QObject::connect(
        backend(), &SyncBackend::writeFinished,
        this, [&](const QString &signaledCalId, bool success, const QString &err) {
            if (signaledCalId == calId && !success) {
                writeSucceeded = false;
                writeError = err;
            }
        },
        Qt::DirectConnection);

    backend()->storeItems(m_calendar, {m_incidence}, m_plan);

    QObject::disconnect(conn);

    if (!writeSucceeded) {
        setErrorString(writeError.isEmpty()
            ? tr("storeItems failed for UID: %1").arg(m_incidence->uid())
            : writeError);
        return false;
    }

    setCommitted(true);
    return true;
}

bool CreateIncidenceItem::rollback()
{
    if (!isCommitted()) {
        // Nothing to rollback
        return true;
    }

    if (!backend()) {
        setErrorString(tr("Cannot rollback: backend is null"));
        return false;
    }

    // Delete the created incidence
    DeleteOperation *deleteOp = backend()->deleteItems(calendarId(), {uid()});

    // Wait synchronously for completion
    QEventLoop loop;
    connect(deleteOp, &DeleteOperation::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    bool success = deleteOp->state() == SyncOperation::Succeeded &&
                   deleteOp->succeededUids().contains(uid());

    if (!success) {
        setErrorString(tr("Rollback failed: could not delete created incidence %1").arg(uid()));
    } else {
        setCommitted(false);
    }

    deleteOp->deleteLater();
    return success;
}

QString CreateIncidenceItem::description() const
{
    if (!m_incidence) {
        return tr("Create incidence (null)");
    }

    QString summary = m_incidence->summary();
    if (summary.isEmpty()) {
        summary = m_incidence->uid();
    }

    return tr("Create %1: %2 (%3)")
        .arg(m_incidence->typeStr(), summary, calendarId());
}

QJsonObject CreateIncidenceItem::toJson() const
{
    QJsonObject obj = SyncTransactionItem::toJson();

    if (m_incidence) {
        KCalendarCore::ICalFormat format;
        obj[QStringLiteral("icalData")] = format.toICalString(m_incidence);
    }

    return obj;
}


} // namespace Kalburator::Sync
