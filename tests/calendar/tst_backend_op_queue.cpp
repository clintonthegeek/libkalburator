// tests/calendar/tst_backend_op_queue.cpp
// E5.1 — per-collection FIFO operation queue (neutral layer, SyncBackendBase).
// RED against pre-E5.1 code: MockBackend/LocalBackend dispatch every
// operation-producing call (fetchItems/pushItems/deleteItems) immediately
// via QTimer::singleShot(0, ...), so two ops on the SAME collection
// interleave instead of serializing. This file pins SyncBackendBase's new
// enqueueOperation() FIFO contract: (a) same-collection ops never overlap,
// (b) different-collection ops DO overlap, (c) cancelling a queued
// (not-yet-started) op never runs its body, (d) a full engine sync over
// LocalBackend<->LocalBackend still completes end-to-end through the queue.

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
#include "localbackend.h"
#include "mockbackend.h"
#include "shaperegistries.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "syncoperation.h"
#include "syncrequest.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"
#include "pluginmanager.h"
#include "stock_plugins.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

using namespace Kalburator::Sync;

class TstBackendOpQueue : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void sameCollectionOpsSerialize();
    void differentCollectionOpsOverlap();
    void cancelQueuedOpNeverRunsBody();
    void localToLocalEngineSyncStillCompletes();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    BackendRegistry m_pluginRegistry;
};

void TstBackendOpQueue::initTestCase()
{
    Kalburator::PluginManager pm(&m_pluginRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

namespace {

KCalendarCore::Incidence::Ptr makeEvent(const QString &uid)
{
    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(uid);
    event->setSummary(QStringLiteral("test event ") + uid);
    return event;
}

} // namespace

void TstBackendOpQueue::sameCollectionOpsSerialize()
{
    MockBackend backend;
    backend.setOperationDelay(60);
    backend.addIncidence(QStringLiteral("cal-a"), makeEvent(QStringLiteral("e1")));

    auto *op1 = backend.fetchItems(QStringLiteral("cal-a"));
    auto *op2 = backend.fetchItems(QStringLiteral("cal-a"));
    QVERIFY(op1);
    QVERIFY(op2);

    // Both ops are queued behind SyncBackendBase's enqueueOperation(), which
    // always defers the actual start functor by one event-loop tick — so
    // right after the synchronous calls above, NEITHER has started yet.
    QCOMPARE(op1->state(), SyncOperation::Pending);
    QCOMPARE(op2->state(), SyncOperation::Pending);

    QSignalSpy op2Started(op2, &SyncOperation::started);

    // op1 gets its turn first (FIFO) and starts running.
    QTRY_VERIFY_WITH_TIMEOUT(op1->state() == SyncOperation::Running, 2000);
    // While op1 is still running, op2 must NOT have started — this is the
    // serialization contract the pre-E5.1 code violates (both would start
    // on the same tick).
    QCOMPARE(op2Started.count(), 0);
    QCOMPARE(op2->state(), SyncOperation::Pending);

    QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 3000);
    QCOMPARE(op1->state(), SyncOperation::Succeeded);

    // Only now may op2 start and run to completion.
    QTRY_VERIFY_WITH_TIMEOUT(op2->state() == SyncOperation::Running
                                  || op2->isFinished(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(op2->isFinished(), 3000);
    QCOMPARE(op2->state(), SyncOperation::Succeeded);

    op1->deleteLater();
    op2->deleteLater();
}

void TstBackendOpQueue::differentCollectionOpsOverlap()
{
    MockBackend backend;
    backend.setOperationDelay(80);
    backend.addIncidence(QStringLiteral("cal-a"), makeEvent(QStringLiteral("e1")));
    backend.addIncidence(QStringLiteral("cal-b"), makeEvent(QStringLiteral("e2")));

    auto *opA = backend.fetchItems(QStringLiteral("cal-a"));
    auto *opB = backend.fetchItems(QStringLiteral("cal-b"));
    QVERIFY(opA);
    QVERIFY(opB);

    // Unrelated collections must NOT serialize against each other: both
    // reach Running before either finishes.
    QTRY_VERIFY_WITH_TIMEOUT(opA->state() == SyncOperation::Running, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(opB->state() == SyncOperation::Running, 2000);
    QVERIFY(!opA->isFinished());
    QVERIFY(!opB->isFinished());

    QTRY_VERIFY_WITH_TIMEOUT(opA->isFinished(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(opB->isFinished(), 3000);
    QCOMPARE(opA->state(), SyncOperation::Succeeded);
    QCOMPARE(opB->state(), SyncOperation::Succeeded);

    opA->deleteLater();
    opB->deleteLater();
}

void TstBackendOpQueue::cancelQueuedOpNeverRunsBody()
{
    MockBackend backend;
    backend.setOperationDelay(60);
    backend.addIncidence(QStringLiteral("cal-a"), makeEvent(QStringLiteral("e1")));

    auto *op1 = backend.fetchItems(QStringLiteral("cal-a"));
    auto *op2 = backend.fetchItems(QStringLiteral("cal-a"));
    QVERIFY(op1);
    QVERIFY(op2);

    QSignalSpy op2Started(op2, &SyncOperation::started);

    // op1 is running; op2 is still queued behind it (not yet dequeued).
    QTRY_VERIFY_WITH_TIMEOUT(op1->state() == SyncOperation::Running, 2000);
    QCOMPARE(op2->state(), SyncOperation::Pending);

    op2->cancel();
    QCOMPARE(op2->state(), SyncOperation::Cancelled);

    // Let op1 finish; op2 must never transition to Running — its body must
    // never run because it was cancelled while still queued.
    QTRY_VERIFY_WITH_TIMEOUT(op1->isFinished(), 3000);
    QTest::qWait(150);
    QCOMPARE(op2Started.count(), 0);
    QCOMPARE(op2->state(), SyncOperation::Cancelled);

    op1->deleteLater();
    op2->deleteLater();
}

void TstBackendOpQueue::localToLocalEngineSyncStillCompletes()
{
    QTemporaryDir sourceDir;
    QTemporaryDir targetDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(sourceDir.filePath(calId)));
    QVERIFY(QDir().mkpath(targetDir.filePath(calId)));

    {
        QFile f(sourceDir.filePath(calId + QStringLiteral("/opqueue-evt-1.ics")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                "BEGIN:VEVENT\r\nUID:opqueue-evt-1\r\n"
                "SUMMARY:Seeded\r\nDTSTART:20260601T120000Z\r\n"
                "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    }

    LocalBackend sourceBackend(sourceDir.path());
    LocalBackend targetBackend(targetDir.path());

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("opq-source"), &sourceBackend);
    registry.registerBackendInstance(QStringLiteral("opq-target"), &targetBackend);

    Test::StubSyncHost host(&registry);
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(calId);
    host.stubCollection()->addCalendarWithId(calId, hostCal);

    QTemporaryDir engineDbDir;
    QVERIFY(engineDbDir.isValid());
    const QString engineDbPath = engineDbDir.filePath(QStringLiteral(".kalburator-sync.db"));
    Kalburator::Storage::BaselineStore baselines(engineDbPath);
    SyncConflictStore conflictStore(engineDbPath);
    ConflictManager conflictManager;
    conflictManager.setSyncConflictStore(&conflictStore);

    SyncEngine engine(&registry, &host, m_shape);
    engine.setBaselineStore(&baselines);
    engine.setSyncConflictStore(&conflictStore);
    engine.setConflictManager(&conflictManager);
    engine.setCollection(host.stubCollection());

    SyncMapping mapping;
    mapping.id              = QStringLiteral("opq-mapping");
    mapping.sourceBackend   = QStringLiteral("opq-source");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("opq-target");
    mapping.targetCalendar  = calId;
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);

    int waited = 0;
    constexpr int kSyncTimeoutMs = 15000;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY(future.isFinished());
    QVERIFY(!future.isCanceled());

    QVERIFY(QFile::exists(targetDir.filePath(calId + QStringLiteral("/opqueue-evt-1.ics"))));
}

QTEST_MAIN(TstBackendOpQueue)
#include "tst_backend_op_queue.moc"
