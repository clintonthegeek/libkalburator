// Sync-excellence campaign E3 (FINDINGS O33b, promoted from audit §C4).
//
// SyncEngine::driveQueue() used to call DecSyncActiveController::
// runActiveSync() inline, on driveQueue()'s own caller thread (the
// engine/GUI thread) — a §1 role violation: runActiveSync() touches
// backend-owned state (DecSyncCollection reads), which belongs on the
// worker thread like every other backend touch. This test pins that the
// loop now runs on the worker thread: it connects to a signal
// runActiveSync() emits with Qt::DirectConnection (which runs the slot
// on whichever thread the signal was emitted from) and records that
// thread.
//
// Falsifiable: reverting SyncEngine::driveQueue()'s active-controller
// dispatch to the old inline `it.value()->runActiveSync();` loop makes
// this test RED (capturedThread == the test/main thread instead of the
// worker thread).

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QSignalSpy>
#include <atomic>
#include <memory>

#include "decsyncbackend.h"
#include "decsyncactivecontroller.h"
#include "backendregistry.h"
#include "shaperegistries.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "syncoperation.h"

#include "stubsynchost.h"

#include <KCalendarCore/Event>

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr int kSyncTimeoutMs = 30000;

void writeDecsyncInfo(const QString &path)
{
    QDir().mkpath(path);
    QFile file(path + QStringLiteral("/.decsync-info"));
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(R"({"version":2})");
    file.close();
}

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

} // namespace

class TstDecsyncActiveControllerThread : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    /// E3 (O33b): runActiveSync() must execute on the worker thread, not
    /// on driveQueue()'s caller thread.
    void runActiveControllersLoop_executesOnWorkerThread();

private:
    std::unique_ptr<QTemporaryDir> m_tmpDir;
};

void TstDecsyncActiveControllerThread::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());
}

void TstDecsyncActiveControllerThread::cleanup()
{
    m_tmpDir.reset();
}

void TstDecsyncActiveControllerThread::runActiveControllersLoop_executesOnWorkerThread()
{
    const QString decsyncDir = m_tmpDir->path() + QStringLiteral("/DecSync");
    writeDecsyncInfo(decsyncDir);

    DecSyncBackend backend(decsyncDir, QStringLiteral("test-app"));
    QVERIFY(backend.createCalendar(QStringLiteral("coll"), QStringLiteral("active-cal"),
                                   QStringLiteral("Active Cal"), CalendarType::Event));

    // Seed one item as a DIFFERENT DecSync app (same on-disk decsyncDir,
    // different appId — exactly how two real DecSync peers interoperate).
    // This gives runActiveSync() a non-empty uidToAppEntries set, so its
    // per-uid loop runs and emits progressChanged() BEFORE it ever
    // touches the (test-thread-affine, SQLite-backed) controller store —
    // decoupling this thread-affinity pin from that unrelated, pre-
    // existing thread-affinity gap in DecSyncControllerStore (out of
    // E3's scope; §1 role violation noted for "whoever enables DecSync
    // next" per FINDINGS O33).
    {
        DecSyncBackend otherAppBackend(decsyncDir, QStringLiteral("other-app"));
        QVERIFY(otherAppBackend.createCalendar(QStringLiteral("coll"), QStringLiteral("active-cal"),
                                               QStringLiteral("Active Cal"), CalendarType::Event));
        auto *pushOp = otherAppBackend.pushItems(QStringLiteral("active-cal"),
            { makeEvent(QStringLiteral("peer-uid"), QStringLiteral("Peer Event")) });
        QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
        QTRY_COMPARE(pushSpy.count(), 1);
        QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
        delete pushOp;
    }

    DecSyncActiveController *controller = backend.activeController(QStringLiteral("active-cal"));
    QVERIFY(controller);

    std::atomic<QThread*> capturedThread{nullptr};
    QThread *const testThread = QThread::currentThread();
    connect(controller, &DecSyncActiveController::progressChanged,
            controller,
            [&capturedThread](int, int) {
                capturedThread.store(QThread::currentThread());
            },
            Qt::DirectConnection);

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("decsync-1"), &backend);
    StubSyncHost host(&registry);

    Kalburator::Shape::ShapeRegistries shape;
    SyncEngine engine(&registry, &host, shape);
    engine.registerActiveController(QStringLiteral("active-cal"), controller);

    SyncRequest req; // all-enabled: no mapping ids, drives the active-controller loop
    QFuture<QList<SyncResult>> future = engine.runSync(req);

    QTRY_VERIFY_WITH_TIMEOUT(capturedThread.load() != nullptr, kSyncTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);

    QVERIFY2(capturedThread.load() != testThread,
             "runActiveSync() executed on the caller/test thread instead of "
             "the worker thread (E3 / O33b regression)");
}

QTEST_MAIN(TstDecsyncActiveControllerThread)
#include "tst_decsync_active_controller_thread.moc"
