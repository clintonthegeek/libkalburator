// D1 T1.4 — the lib-side proof that backends survive relocation off their
// construction thread.
//
// D1 moves LocalBackend / RemoteCalendarBackend instances to a dedicated I/O
// QThread so their network/filesystem work no longer blocks the GUI thread
// (the 120s freeze, finding N7). This is viable only if every entry point a
// relocated backend exposes still works correctly when driven from a
// different thread than the one that constructed it — that is exactly what
// this file exercises, using the two building blocks Stage 1 landed:
//
//   - T1.1: RemoteCalendarBackend's QNetworkAccessManager is now lazily
//     created (so it acquires the I/O thread's affinity, not the
//     construction thread's).
//   - T1.2/T1.3: CTagStore / FingerprintStore now lazy-open their SQLite
//     connection on first use (same reasoning).
//
// Three cases, per the D1 execution plan (docs/campaign/
// 2026-07-04-d1-threading-execution-plan.md) §3 T1.4:
//   1. RemoteCalendarBackend: construct-then-move, drive fetch/push/delete
//      from the "GUI" thread via invokeMethod(..., BlockingQueuedConnection)
//      (mimicking the engine), assert results + op->thread() == backend->thread().
//   2. LocalBackend: same shape — fetch, startSync (AsyncFileWriter), and a
//      ChangeDetection fingerprint round-trip.
//   3. A full SyncEngine::runSync() with both mapping backends relocated to
//      the same I/O thread. SyncEngine already owns and moveToThread()s its
//      own SyncEngineWorker onto an internal m_workerThread (syncengine.cpp
//      startWorkerThread()), so this test's three participants — the test's
//      own thread, the engine's internal worker thread, and our I/O thread —
//      are already three genuinely distinct threads without any extra
//      plumbing here. This proves invariant §1.1 (GUI ≠ engine-worker ≠
//      backend-I/O) end-to-end.

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
#include "fakecaldavserver.h"
#include "localbackend.h"
#include "pluginmanager.h"
#include "remotecalendarbackend.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "syncconflictstore.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;

namespace {

constexpr const char *kPersonalHref = "/calendars/testuser/personal/";
constexpr int kOpTimeoutMs = 8000;

KCalendarCore::Incidence::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(12, 0), QTimeZone::utc()));
    event->setDtEnd(QDateTime(QDate(2026, 6, 1), QTime(13, 0), QTimeZone::utc()));
    return event;
}

QByteArray seedIcs(const QByteArray &uid)
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
           "BEGIN:VEVENT\r\nUID:" + uid + "\r\n"
           "SUMMARY:Seeded\r\nDTSTART:20260601T120000Z\r\n"
           "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
}

} // namespace

class TstBackendThreadRelocation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void remote_constructThenMove_fetchPushDeleteWork();
    void local_constructThenMove_fetchStartSyncFingerprintRoundtrip();
    void fullEngineRun_relocatedBackends_completesAcrossThreeThreads();

private:
    Kalburator::Shape::ShapeRegistries m_shape;
    BackendRegistry m_pluginRegistry;
};

void TstBackendThreadRelocation::initTestCase()
{
    Kalburator::PluginManager pm(&m_pluginRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

// ---- Case 1: RemoteCalendarBackend, construct-then-move -------------------

void TstBackendThreadRelocation::remote_constructThenMove_fetchPushDeleteWork()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    // Construct + configure on the "GUI" thread (this test thread), exactly
    // as CollectionController does today — before D1's relocation point.
    // Heap-allocated (not stack) so cleanup can delete it via invokeMethod
    // on its own thread — see ioThreadGuard below.
    auto *backend = new RemoteCalendarBackend(server.baseUrl(),
                                              QStringLiteral("testuser"),
                                              QStringLiteral("testpass"));
    backend->setCacheDir(cacheDir.path());
    backend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("d1-test-remote-io"));
    ioThread.start();
    // Guarantees cleanup runs even if a QVERIFY/QCOMPARE below fails and
    // returns early (QTest's macros do a bare `return`, which would
    // otherwise skip manual cleanup and destroy a running QThread — fatal).
    // Deletes backend via invokeMethod on its OWN thread before stopping
    // ioThread: CTagStore's dtor closes a QSqlDatabase connection, and Qt's
    // per-thread SQL connection registry only allows closing it from the
    // thread that opened it — do it after the thread has stopped and you
    // get a silent "does not belong to the calling thread" warning instead
    // of a clean close. Same ordering applies to PlanStan's T3.2.
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(backend, [backend]() { delete backend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    backend->moveToThread(&ioThread);

    // loadCalendars: real discovery walk (T1.1's lazily-created QNAM opens
    // its first connection here, on ioThread).
    QSignalSpy loadFinishedSpy(backend, SIGNAL(loadCalendarsFinished(QString,bool,QString)));
    QMetaObject::invokeMethod(backend, [&]() {
        backend->loadCalendars(QStringLiteral("personal-coll"));
    }, Qt::BlockingQueuedConnection);
    QTRY_COMPARE_WITH_TIMEOUT(loadFinishedSpy.count(), 1, kOpTimeoutMs);
    QVERIFY(loadFinishedSpy.first().at(1).toBool());

    // fetchItems
    FetchOperation *fetchOp = nullptr;
    QMetaObject::invokeMethod(backend, [&]() {
        fetchOp = backend->fetchItems(QStringLiteral("Personal"));
    }, Qt::BlockingQueuedConnection);
    QVERIFY(fetchOp != nullptr);
    QCOMPARE(fetchOp->thread(), backend->thread());
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), kOpTimeoutMs);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    delete fetchOp;

    // pushItems (also exercises T1.2's lazy CTagStore open via clearCtag())
    const QList<KCalendarCore::Incidence::Ptr> pushed{
        makeEvent(QStringLiteral("reloc-push-1"), QStringLiteral("Relocated Push"))};
    PushOperation *pushOp = nullptr;
    QMetaObject::invokeMethod(backend, [&]() {
        pushOp = backend->pushItems(QStringLiteral("Personal"), pushed);
    }, Qt::BlockingQueuedConnection);
    QVERIFY(pushOp != nullptr);
    QCOMPARE(pushOp->thread(), backend->thread());
    QTRY_VERIFY_WITH_TIMEOUT(pushOp->isFinished(), kOpTimeoutMs);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    QVERIFY(server.hasEvent(QString::fromLatin1(kPersonalHref),
                            QStringLiteral("reloc-push-1")));
    delete pushOp;

    // deleteItems
    DeleteOperation *delOp = nullptr;
    QMetaObject::invokeMethod(backend, [&]() {
        delOp = backend->deleteItems(QStringLiteral("Personal"),
                                     QStringList{QStringLiteral("reloc-push-1")});
    }, Qt::BlockingQueuedConnection);
    QVERIFY(delOp != nullptr);
    QCOMPARE(delOp->thread(), backend->thread());
    QTRY_VERIFY_WITH_TIMEOUT(delOp->isFinished(), kOpTimeoutMs);
    QCOMPARE(delOp->state(), SyncOperation::Succeeded);
    QVERIFY(!server.hasEvent(QString::fromLatin1(kPersonalHref),
                             QStringLiteral("reloc-push-1")));
    delete delOp;
}

// ---- Case 2: LocalBackend, construct-then-move -----------------------------

void TstBackendThreadRelocation::local_constructThenMove_fetchStartSyncFingerprintRoundtrip()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(root.filePath(calId)));

    auto *backend = new LocalBackend(root.path());
    backend->setDbPath(root.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("d1-test-local-io"));
    ioThread.start();
    // See the matching guard in the Remote case above for why deletion goes
    // through invokeMethod on the backend's own thread before ioThread stops.
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(backend, [backend]() { delete backend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    backend->moveToThread(&ioThread);

    // fetchItems against the (empty) relocated calendar directory.
    FetchOperation *fetchOp = nullptr;
    QMetaObject::invokeMethod(backend, [&]() {
        fetchOp = backend->fetchItems(calId);
    }, Qt::BlockingQueuedConnection);
    QVERIFY(fetchOp != nullptr);
    QCOMPARE(fetchOp->thread(), backend->thread());
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), kOpTimeoutMs);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    delete fetchOp;

    // startSync staged writes — exercises the internal AsyncFileWriter from
    // a relocated backend.
    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(calId);
    const QList<KCalendarCore::Incidence::Ptr> creations{
        makeEvent(QStringLiteral("reloc-local-1"), QStringLiteral("Relocated Local"))};

    QSignalSpy completedSpy(backend, &LocalBackend::syncCompleted);
    QMetaObject::invokeMethod(backend, [&]() {
        backend->startSync(QStringLiteral("local-coll"), &cal, creations, {}, {});
    }, Qt::BlockingQueuedConnection);
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, kOpTimeoutMs);
    QVERIFY(QFile::exists(root.filePath(calId + QStringLiteral("/reloc-local-1.ics"))));

    // Fingerprint round-trip via Sync::ChangeDetection — exercises T1.3's
    // lazy-opened FingerprintStore entirely on ioThread. collectionRevision()
    // only computes; the cache is populated by primeRevisionCache() (the
    // engine's job after comparing revisions), which is what actually drives
    // FingerprintStore::set() — cachedCollectionRevision() alone would stay
    // empty forever without it.
    QString freshRevision;
    QMetaObject::invokeMethod(backend, [&]() {
        freshRevision = backend->collectionRevision(calId);
    }, Qt::BlockingQueuedConnection);
    QVERIFY(!freshRevision.isEmpty());

    QMetaObject::invokeMethod(backend, [&]() {
        backend->primeRevisionCache({{calId, freshRevision}});
    }, Qt::BlockingQueuedConnection);

    QString cachedRevision;
    QMetaObject::invokeMethod(backend, [&]() {
        cachedRevision = backend->cachedCollectionRevision(calId);
    }, Qt::BlockingQueuedConnection);
    QCOMPARE(cachedRevision, freshRevision);
}

// ---- Case 3: full SyncEngine run against relocated backends ---------------

void TstBackendThreadRelocation::fullEngineRun_relocatedBackends_completesAcrossThreeThreads()
{
    QTemporaryDir sourceDir;
    QTemporaryDir targetDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(sourceDir.filePath(calId)));
    QVERIFY(QDir().mkpath(targetDir.filePath(calId)));

    // Seed the source directly on disk before any backend thread exists —
    // plain filesystem I/O, no backend-affinity concerns.
    {
        QFile f(sourceDir.filePath(calId + QStringLiteral("/reloc-evt-1.ics")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(seedIcs("reloc-evt-1"));
    }

    auto *sourceBackend = new LocalBackend(sourceDir.path()); // unparented
    auto *targetBackend = new LocalBackend(targetDir.path()); // unparented
    sourceBackend->setDbPath(sourceDir.filePath(QStringLiteral(".kalburator-sync.db")));
    targetBackend->setDbPath(targetDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("d1-test-engine-io"));
    ioThread.start();
    // Single guard for the whole relocation lifecycle: delete each backend
    // via invokeMethod on its own thread (so CTagStore/FingerprintStore's
    // SQL connection closes from the thread that opened it — see the Remote
    // case above), then quit+wait the I/O thread. Using one guard (rather
    // than separate cleanup at the end of the function) means an early
    // QVERIFY failure still runs this instead of destroying a running
    // QThread / leaking the backends.
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(sourceBackend, [sourceBackend]() { delete sourceBackend; },
                                  Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(targetBackend, [targetBackend]() { delete targetBackend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    sourceBackend->moveToThread(&ioThread);
    targetBackend->moveToThread(&ioThread);

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("reloc-source"), sourceBackend);
    registry.registerBackendInstance(QStringLiteral("reloc-target"), targetBackend);

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

    // The engine itself is constructed on (and never moved from) this test
    // thread — invariant §1.2. Its own internal worker thread (started
    // lazily inside runSync()) is what gives this test three genuinely
    // distinct threads without any extra plumbing here.
    SyncEngine engine(&registry, &host, m_shape);
    engine.setBaselineStore(&baselines);
    engine.setSyncConflictStore(&conflictStore);
    engine.setConflictManager(&conflictManager);
    engine.setCollection(host.stubCollection());

    SyncMapping mapping;
    mapping.id              = QStringLiteral("reloc-mapping");
    mapping.sourceBackend   = QStringLiteral("reloc-source");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("reloc-target");
    mapping.targetCalendar  = calId;
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);

    int waited = 0;
    constexpr int kSyncTimeoutMs = 30000;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY(future.isFinished());
    QVERIFY(!future.isCanceled());

    // The two-way sync converged with both mapping backends parked on a
    // dedicated I/O thread, distinct from both this test thread and the
    // engine's own internal worker thread — invariant §1.1, end to end.
    QVERIFY(QFile::exists(targetDir.filePath(calId + QStringLiteral("/reloc-evt-1.ics"))));
    QCOMPARE(sourceBackend->thread(), &ioThread);
    QCOMPARE(targetBackend->thread(), &ioThread);
}

QTEST_GUILESS_MAIN(TstBackendThreadRelocation)
#include "tst_backend_thread_relocation.moc"
