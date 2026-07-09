#include "writeoperation.h"

namespace Kalburator::Sync {

WriteOperation::WriteOperation(const QString &calendarId, QObject *parent)
    : SyncOperation(calendarId, parent)
{
}

void WriteOperation::addSucceededUid(const QString &uid)
{
    if (!m_succeededUids.contains(uid)) {
        m_succeededUids.append(uid);
    }
}

void WriteOperation::addFailedUid(const QString &uid)
{
    if (!m_failedUids.contains(uid)) {
        m_failedUids.append(uid);
    }
}

} // namespace Kalburator::Sync
