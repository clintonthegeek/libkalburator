#include "syncoperation.h"

namespace Kalburator::Sync {

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
