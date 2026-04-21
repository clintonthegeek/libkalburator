#include "syncoperation.h"
#include <QUuid>
#include <QDebug>

namespace Kalburator::Sync {

int SyncOperation::s_nextOperationId = 1;

SyncOperation::SyncOperation(const QString &calendarId, QObject *parent)
    : QObject(parent)
    , m_calendarId(calendarId)
{
    m_operationId = QStringLiteral("op-%1").arg(s_nextOperationId++);
}

SyncOperation::~SyncOperation()
{
    // If operation is still running when destroyed, log a warning
    if (m_state == Running) {
        qWarning() << "SyncOperation" << m_operationId << "destroyed while still running";
    }
}

bool SyncOperation::isFinished() const
{
    return m_state == Succeeded || m_state == Failed || m_state == Cancelled;
}

void SyncOperation::cancel()
{
    if (isFinished()) {
        return;
    }

    qDebug() << "SyncOperation" << m_operationId << "cancellation requested";
    setState(Cancelled);
}

void SyncOperation::setState(State newState)
{
    if (m_state == newState) {
        return;
    }

    // Don't allow state changes after reaching terminal state
    if (isFinished()) {
        qWarning() << "SyncOperation" << m_operationId
                   << "attempted state change from" << m_state << "to" << newState
                   << "after reaching terminal state";
        return;
    }

    m_state = newState;
    emit stateChanged(newState);

    if (isFinished()) {
        emit finished();
    }
}

void SyncOperation::setProgress(int percent)
{
    if (m_progress == percent) {
        return;
    }

    m_progress = percent;
    emit progressChanged(percent);
}

void SyncOperation::setErrorString(const QString &error)
{
    m_errorString = error;
}

void SyncOperation::start()
{
    if (m_state != Pending) {
        qWarning() << "SyncOperation" << m_operationId
                   << "start() called but state is" << m_state;
        return;
    }

    setState(Running);
}

void SyncOperation::complete()
{
    if (m_state != Running) {
        qWarning() << "SyncOperation" << m_operationId
                   << "complete() called but state is" << m_state;
        return;
    }

    setState(Succeeded);
}

void SyncOperation::fail(const QString &errorString)
{
    setErrorString(errorString);
    setState(Failed);
}

// FetchOperation

FetchOperation::FetchOperation(const QString &calendarId, QObject *parent)
    : SyncOperation(calendarId, parent)
{
}

void FetchOperation::setFetchedItems(const QList<KCalendarCore::Incidence::Ptr> &items)
{
    m_fetchedItems = items;
}

// PushOperation

PushOperation::PushOperation(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items,
                             QObject *parent)
    : SyncOperation(calendarId, parent)
    , m_requestedItems(items)
{
}

void PushOperation::addSucceededUid(const QString &uid)
{
    if (!m_succeededUids.contains(uid)) {
        m_succeededUids.append(uid);
    }
}

void PushOperation::addFailedUid(const QString &uid)
{
    if (!m_failedUids.contains(uid)) {
        m_failedUids.append(uid);
    }
}

void PushOperation::setSucceededUids(const QStringList &uids)
{
    m_succeededUids = uids;
}

void PushOperation::setFailedUids(const QStringList &uids)
{
    m_failedUids = uids;
}

// DeleteOperation

DeleteOperation::DeleteOperation(const QString &calendarId,
                                 const QStringList &uids,
                                 QObject *parent)
    : SyncOperation(calendarId, parent)
    , m_requestedUids(uids)
{
}

void DeleteOperation::addSucceededUid(const QString &uid)
{
    if (!m_succeededUids.contains(uid)) {
        m_succeededUids.append(uid);
    }
}

void DeleteOperation::addFailedUid(const QString &uid)
{
    if (!m_failedUids.contains(uid)) {
        m_failedUids.append(uid);
    }
}

void DeleteOperation::setSucceededUids(const QStringList &uids)
{
    m_succeededUids = uids;
}

void DeleteOperation::setFailedUids(const QStringList &uids)
{
    m_failedUids = uids;
}


} // namespace Kalburator::Sync
