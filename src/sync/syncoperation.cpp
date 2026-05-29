#include "syncoperation.h"
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
    if (m_state.load(std::memory_order_acquire) == Running) {
        qWarning() << "SyncOperation" << m_operationId << "destroyed while still running";
    }
}

bool SyncOperation::isFinished() const noexcept
{
    const State s = m_state.load(std::memory_order_acquire);
    return s == Succeeded || s == Failed || s == Cancelled;
}

bool SyncOperation::cancelRequested() const noexcept
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

void SyncOperation::cancel()
{
    // Mark cancellation requested (part of the F2 contract). Idempotent:
    // calling cancel() twice has no additional effect.
    m_cancelRequested.store(true, std::memory_order_release);

    if (isFinished()) {
        return;
    }

    qDebug() << "SyncOperation" << m_operationId << "cancellation requested";
    // Backwards-compatibility: existing backend run-bodies poll
    // op->state() == Cancelled to detect cancellation. Until those are
    // migrated to use cancelRequested(), keep the eager state flip.
    setState(Cancelled);
}

void SyncOperation::setState(State newState)
{
    // Idempotent compare-exchange loop. Treats terminal-to-terminal as a
    // silent no-op (no warning, no signal re-emission). Same-state is a
    // no-op for any state. Non-terminal transitions emit started on
    // Pending->Running and finished on transition into a terminal state.
    State expected = m_state.load(std::memory_order_acquire);
    while (true) {
        if (expected == newState) {
            return; // same-state: no-op
        }
        const bool wasTerminal =
            expected == Succeeded || expected == Failed || expected == Cancelled;
        const bool willBeTerminal =
            newState == Succeeded || newState == Failed || newState == Cancelled;
        if (wasTerminal) {
            // Already in a terminal state. Per the F2 contract, terminal
            // is sticky: ignore further transitions silently.
            return;
        }
        if (m_state.compare_exchange_weak(expected, newState,
                                          std::memory_order_release,
                                          std::memory_order_acquire)) {
            emit stateChanged(newState);
            if (expected == Pending && newState == Running) {
                emit started();
            }
            if (willBeTerminal) {
                emit finished();
            }
            return;
        }
        // expected was reloaded by compare_exchange_weak; retry
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

void SyncOperation::setError(const QString &message)
{
    m_errorString = message;
    setState(Failed);
}

void SyncOperation::start()
{
    const State s = m_state.load(std::memory_order_acquire);
    if (s != Pending) {
        qWarning() << "SyncOperation" << m_operationId
                   << "start() called but state is" << s;
        return;
    }

    setState(Running);
}

void SyncOperation::complete()
{
    const State s = m_state.load(std::memory_order_acquire);
    if (s != Running) {
        qWarning() << "SyncOperation" << m_operationId
                   << "complete() called but state is" << s;
        return;
    }

    setState(Succeeded);
}

void SyncOperation::fail(const QString &errorString)
{
    setErrorString(errorString);
    setState(Failed);
}

} // namespace Kalburator::Sync
