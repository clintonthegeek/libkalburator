#include "workerteardown.h"

#include <QThread>
#include <QDebug>

namespace Kalburator::Engine {

void waitForWorkerWithDiagnostic(QThread *thread, int deadlineMs)
{
    if (!thread) {
        return;
    }

    if (thread->wait(deadlineMs)) {
        return;
    }

    qCritical().noquote()
        << QStringLiteral(
               "SyncEngine: worker thread did not stop within %1 ms. "
               "A sync backend lives on the thread calling "
               "stopWorkerPool() — relocate backends onto a dedicated "
               "I/O thread (see planstan-backend-io), or destroy the "
               "engine from a different thread. Waiting with no deadline "
               "rather than terminating: a worker thread killed "
               "mid-write is worse than a hang.")
               .arg(deadlineMs);

    thread->wait();
}

} // namespace Kalburator::Engine
