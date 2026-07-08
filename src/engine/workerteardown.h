#ifndef KALBURATOR_WORKERTEARDOWN_H
#define KALBURATOR_WORKERTEARDOWN_H

class QThread;

namespace Kalburator::Engine {

/**
 * @brief E3 (O22 residue): bounded-wait teardown helper.
 *
 * Full dissolution of the mid-marshal deadlock hazard is E5.3's job (the
 * worker stops parking in BlockingQueuedConnection marshals for
 * I/O-length work). Until then, if a sync backend lives on the thread
 * calling this function (any consumer that has not relocated its
 * backends onto a dedicated I/O thread), the worker can be stuck inside
 * a blocking marshal against that very thread, and an unbounded wait()
 * would deadlock forever.
 *
 * Waits up to @p deadlineMs for @p thread to finish. If it doesn't, logs
 * a loud qCritical() naming the invariant being violated, then waits
 * again with no deadline. Never calls QThread::terminate() — a worker
 * thread killed mid SQL-write or mid network-write is worse than a
 * hang; the diagnostic is the deliverable, not a forced kill.
 */
void waitForWorkerWithDiagnostic(QThread *thread, int deadlineMs = 30000);

} // namespace Kalburator::Engine

#endif // KALBURATOR_WORKERTEARDOWN_H
