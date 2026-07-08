// Sync-excellence campaign E3 (O22 residue) — bounded-wait teardown.
//
// SyncEngine::stopWorkerThread() used to call m_workerThread.wait() with
// no deadline. If a sync backend lives on the thread calling
// stopWorkerThread() (any consumer that has not relocated its backends
// onto a dedicated I/O thread — WildPalms today), the worker can be
// stuck inside a blocking marshal against that very thread, and the
// unbounded wait() deadlocks forever with no diagnostic. Full
// dissolution of the underlying hazard is E5.3's job (the worker stops
// parking in BlockingQueuedConnection marshals for I/O-length work); E3
// lands the honest interim: a bounded wait with a loud diagnostic on
// expiry, then an unbounded wait (never terminate() — a worker thread
// killed mid-write is worse than a hang).
//
// This test pins waitForWorkerWithDiagnostic() (extracted so it is
// testable without deliberately deadlocking a real SyncEngine): a stub
// QThread that outlives a short deadline must produce a qCritical
// diagnostic naming the "relocate backends" invariant, and the wait must
// still complete once the thread actually finishes.

#include <QtTest>
#include <QThread>
#include <QStringList>
#include <QMutex>
#include <QMutexLocker>

#include "workerteardown.h"

using namespace Kalburator::Engine;

namespace {

class SlowStubThread : public QThread
{
    Q_OBJECT
protected:
    void run() override { QThread::msleep(300); }
};

QStringList *g_capturedCritical = nullptr;
QMutex g_captureMutex;

void captureHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type == QtCriticalMsg) {
        QMutexLocker locker(&g_captureMutex);
        if (g_capturedCritical) {
            g_capturedCritical->append(msg);
        }
    }
}

} // namespace

class TstWorkerTeardown : public QObject
{
    Q_OBJECT

private slots:
    void diagnosticFiresOnExpiry_thenWaitStillCompletes();
    void noDiagnosticWhenThreadFinishesInTime();
};

void TstWorkerTeardown::diagnosticFiresOnExpiry_thenWaitStillCompletes()
{
    SlowStubThread thread;
    thread.start();

    QStringList captured;
    {
        QMutexLocker locker(&g_captureMutex);
        g_capturedCritical = &captured;
    }
    QtMessageHandler previous = qInstallMessageHandler(captureHandler);

    // Deadline shorter than the stub's 300 ms run() body — pins the
    // "expiry logs, then waits unboundedly" path.
    waitForWorkerWithDiagnostic(&thread, 100);

    qInstallMessageHandler(previous);
    {
        QMutexLocker locker(&g_captureMutex);
        g_capturedCritical = nullptr;
    }

    QVERIFY(!thread.isRunning());
    QVERIFY2(!captured.isEmpty(), "expected a qCritical diagnostic on deadline expiry");
    QVERIFY2(captured.first().contains(QStringLiteral("relocate")),
             qPrintable(QStringLiteral("diagnostic missing the relocation invariant: ")
                        + captured.first()));
}

void TstWorkerTeardown::noDiagnosticWhenThreadFinishesInTime()
{
    QThread thread; // default run(): just spins its own event loop, exits fast on quit()
    thread.start();
    thread.quit();

    QStringList captured;
    {
        QMutexLocker locker(&g_captureMutex);
        g_capturedCritical = &captured;
    }
    QtMessageHandler previous = qInstallMessageHandler(captureHandler);

    waitForWorkerWithDiagnostic(&thread, 5000);

    qInstallMessageHandler(previous);
    {
        QMutexLocker locker(&g_captureMutex);
        g_capturedCritical = nullptr;
    }

    QVERIFY(!thread.isRunning());
    QVERIFY2(captured.isEmpty(), "no diagnostic expected when the thread stops in time");
}

QTEST_GUILESS_MAIN(TstWorkerTeardown)
#include "tst_worker_teardown.moc"
