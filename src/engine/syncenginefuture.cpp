#include "syncenginefuture.h"

namespace Kalburator::Sync {

SyncEngineFuture::SyncEngineFuture(QFuture<QList<SyncResult>> future)
    : m_future(std::move(future))
    , m_state(std::make_shared<State>())
{
}

QList<SyncResult> SyncEngineFuture::results() const
{
    if (!m_future.isFinished() || m_future.resultCount() == 0)
        return {};
    return m_future.resultAt(0);
}

void SyncEngineFuture::cancel()
{
    cancelWithReason(CancellationReason::UserRequested);
}

void SyncEngineFuture::cancelWithReason(CancellationReason reason,
                                        const QString &resourceId)
{
    if (m_state) {
        m_state->reason     = reason;
        m_state->resourceId = resourceId;
    }
    m_future.cancel();
}

CancellationReason SyncEngineFuture::cancellationReason() const
{
    if (!m_state)
        return CancellationReason::UserRequested;
    return m_state->reason;
}

QString SyncEngineFuture::lostResourceId() const
{
    if (!m_state)
        return {};
    return m_state->resourceId;
}

} // namespace Kalburator::Sync
