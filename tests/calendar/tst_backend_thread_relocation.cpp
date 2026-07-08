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
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>
#include <QTimer>

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

// H2: a LocalBackend that records which thread each overridden call actually
// executed on, so the property phase (H2.1) and first-sync mirror (H2.2)
// tests can prove calls are marshaled onto the *backend's* thread rather
// than running wherever the caller (SyncEngineWorker's own thread) happens
// to be. Color/description are stubbed rather than backed by real files —
// only the marshaling is under test here, not LocalBackend's metadata I/O.
class ThreadRecordingLocalBackend : public LocalBackend
{
public:
    using LocalBackend::LocalBackend;

    void setStubColor(const QColor &c) { m_stubColor = c; }

    QColor calendarColor(const QString & /*calendarId*/) const override
    {
        m_colorCallThread = QThread::currentThread();
        return m_stubColor;
    }

    QString calendarDescription(const QString & /*calendarId*/) const override
    {
        return QString();
    }

    bool updateCalendar(const QString &collectionId, const QString &calendarId,
                        const QVariantMap &properties) override
    {
        m_updateCallThread = QThread::currentThread();
        m_lastAppliedProps = properties;
        return LocalBackend::updateCalendar(collectionId, calendarId, properties);
    }

    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override
    {
        m_loadRecordsCallThread = QThread::currentThread();
        ++m_loadRecordsCallCount;
        return LocalBackend::loadRecords(collectionId);
    }

    // H5/O23: recordsFromLastFetch() falling back to loadRecordsOrError()
    // (and thus loadRecords()) means the memo wasn't there — i.e. the exact
    // redundant-read path H5 eliminates. loadRecordsCallCount alone can't
    // isolate this: applyBatch's classifyForWriter and the post-write hash-
    // verification refetch also call loadRecords(), for unrelated, legitimate
    // reasons outside H5's scope. Comparing the call count immediately before
    // and after THIS call isolates just the fallback.
    bool recordsFromLastFetch(const QString &collectionId,
                              QList<Kalburator::Sync::BackendRecord> &records,
                              QString &errorMessage) override
    {
        const int before = m_loadRecordsCallCount;
        const bool ok = LocalBackend::recordsFromLastFetch(collectionId, records, errorMessage);
        if (m_loadRecordsCallCount != before) {
            ++m_recordsFromLastFetchFellBackCount;
        }
        return ok;
    }

    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override
    {
        m_createRecordCallThread = QThread::currentThread();
        return LocalBackend::createRecord(collectionId, record);
    }

    bool updateRecord(const Kalburator::Sync::BackendRecord &record) override
    {
        m_updateRecordCallThread = QThread::currentThread();
        return LocalBackend::updateRecord(record);
    }

    bool deleteRecord(const QString &recordId) override
    {
        m_deleteRecordCallThread = QThread::currentThread();
        return LocalBackend::deleteRecord(recordId);
    }

    mutable QThread *m_colorCallThread = nullptr;
    QThread *m_updateCallThread = nullptr;
    QVariantMap m_lastAppliedProps;
    QThread *m_loadRecordsCallThread = nullptr;
    QThread *m_createRecordCallThread = nullptr;
    QThread *m_updateRecordCallThread = nullptr;
    QThread *m_deleteRecordCallThread = nullptr;
    int m_loadRecordsCallCount = 0;
    int m_recordsFromLastFetchFellBackCount = 0;

private:
    QColor m_stubColor;
};

} // namespace

class TstBackendThreadRelocation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void remote_constructThenMove_fetchPushDeleteWork();
    void local_constructThenMove_fetchStartSyncFingerprintRoundtrip();
    void fullEngineRun_relocatedBackends_completesAcrossThreeThreads();
    void gateOps_areDeleted_afterSync();
    void propertyPhase_relocatedBackends_marshaledPerBackend();
    void firstSync_backendsOnDifferentThreads();

    void stallProbe_relocatedBackends_stayResponsive();
    void cancelDuringFastPath_reportsCancelled();

    void singleFetch_localBackends_noRedundantRead();
    void singleFetch_remoteBackend_noRedundantListing();

    void steadyStateWrites_appliesOnBackendThread();

private:
    // Harness for the T1.5 GUI-stall probe: a latency-injected
    // RemoteCalendarBackend (source) syncing TwoWay against a LocalBackend
    // (target), both relocated to a dedicated I/O thread, driven through a
    // real SyncEngine. A QTimer on the calling ("GUI") thread ticks every
    // 10 ms while the sync runs; returns the largest observed gap between
    // ticks — the D1 acceptance gate is that this stays under 50 ms.
    qint64 runStallProbe();

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

    // collectionRevision() (Sync::ChangeDetection) computes entirely on
    // ioThread — exercises T1.3's lazy-opened FingerprintStore read path
    // from a relocated backend. (primeRevisionCache()/setCachedFingerprint(),
    // the write side, was deleted as dead machinery — E1.2/O31 — so there
    // is no persisted-cache round-trip left to exercise here.)
    QString freshRevision;
    QMetaObject::invokeMethod(backend, [&]() {
        freshRevision = backend->collectionRevision(calId);
    }, Qt::BlockingQueuedConnection);
    QVERIFY(!freshRevision.isEmpty());
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

// ---- H1.1: fetch-gate ops are cleaned up (no leaked children) --------------
//
// The two fetch gate blocks in SyncEngineWorker::dispatchSync (source ~2111,
// target ~2242) never deleteLater() the FetchOperation they create, and both
// backends parent their ops to `this` (LocalBackend::fetchItems, line 693) —
// so every sync cycle permanently accumulates SyncOperation children. Pins
// H1.1/O23: after a sync completes, neither backend should have any
// SyncOperation children left, and a second cycle must not grow that count.

void TstBackendThreadRelocation::gateOps_areDeleted_afterSync()
{
    QTemporaryDir sourceDir;
    QTemporaryDir targetDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(sourceDir.filePath(calId)));
    QVERIFY(QDir().mkpath(targetDir.filePath(calId)));

    {
        QFile f(sourceDir.filePath(calId + QStringLiteral("/gate-evt-1.ics")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(seedIcs("gate-evt-1"));
    }

    auto *sourceBackend = new LocalBackend(sourceDir.path());
    auto *targetBackend = new LocalBackend(targetDir.path());
    sourceBackend->setDbPath(sourceDir.filePath(QStringLiteral(".kalburator-sync.db")));
    targetBackend->setDbPath(targetDir.filePath(QStringLiteral(".kalburator-sync.db")));

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("gate-source"), sourceBackend);
    registry.registerBackendInstance(QStringLiteral("gate-target"), targetBackend);

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
    mapping.id              = QStringLiteral("gate-mapping");
    mapping.sourceBackend   = QStringLiteral("gate-source");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("gate-target");
    mapping.targetCalendar  = calId;
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});

    auto runOnce = [&]() {
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
    };

    runOnce();
    QTRY_VERIFY(sourceBackend->findChildren<SyncOperation *>().isEmpty());
    QTRY_VERIFY(targetBackend->findChildren<SyncOperation *>().isEmpty());

    // Second cycle pins no per-cycle growth (a leak that only "happened to
    // clear" once would still be a leak).
    runOnce();
    QTRY_VERIFY(sourceBackend->findChildren<SyncOperation *>().isEmpty());
    QTRY_VERIFY(targetBackend->findChildren<SyncOperation *>().isEmpty());

    delete sourceBackend;
    delete targetBackend;
}

// ---- H2.1: property phase is marshaled per backend (O20) -------------------
//
// SyncEngineWorker::runPropertyPhase calls DomainOperations::collectionProperties
// / applyCollectionProperties directly on the worker thread. Pre-D1 that was
// always the same thread as the backend; D1 relocation makes it a genuinely
// different thread, so those calls are live cross-thread UB against a
// relocated backend. Pins H2.1: each call must execute on the *backend's*
// thread, not the worker's.
void TstBackendThreadRelocation::propertyPhase_relocatedBackends_marshaledPerBackend()
{
    QTemporaryDir sourceDir;
    QTemporaryDir targetDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(sourceDir.filePath(calId)));
    QVERIFY(QDir().mkpath(targetDir.filePath(calId)));

    auto *sourceBackend = new ThreadRecordingLocalBackend(sourceDir.path());
    auto *targetBackend = new ThreadRecordingLocalBackend(targetDir.path());
    sourceBackend->setDbPath(sourceDir.filePath(QStringLiteral(".kalburator-sync.db")));
    targetBackend->setDbPath(targetDir.filePath(QStringLiteral(".kalburator-sync.db")));
    // Source reports a color; target reports none — this is a one-sided
    // diff (computeMapDiff's toApplyToTarget branch), not a conflict, so it
    // exercises the exact applyCollectionProperties(tgt, ...) call site the
    // audit flagged (syncengine.cpp ~3109).
    sourceBackend->setStubColor(QColor(Qt::red));
    targetBackend->setStubColor(QColor()); // invalid -> absent from tgtProps

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("d1-test-propphase-io"));
    ioThread.start();
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
    registry.registerBackendInstance(QStringLiteral("propphase-source"), sourceBackend);
    registry.registerBackendInstance(QStringLiteral("propphase-target"), targetBackend);

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
    mapping.id              = QStringLiteral("propphase-mapping");
    mapping.sourceBackend   = QStringLiteral("propphase-source");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("propphase-target");
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

    QVERIFY2(sourceBackend->m_colorCallThread == &ioThread,
             "collectionProperties(src, ...) did not execute on the source backend's thread");
    QVERIFY2(targetBackend->m_colorCallThread == &ioThread,
             "collectionProperties(tgt, ...) did not execute on the target backend's thread");
    QVERIFY2(targetBackend->m_updateCallThread == &ioThread,
             "applyCollectionProperties(tgt, ...) did not execute on the target backend's thread");
    QCOMPARE(targetBackend->m_lastAppliedProps.value(QStringLiteral("color")).value<QColor>(),
             QColor(Qt::red));
}

// ---- H2.2: dispatchFirstSync marshals each side onto its own thread (O21) --
//
// SyncEngineWorker::dispatchFirstSync's mirror lambda is marshaled once onto
// srcBackend but calls tgt->loadRecordsOrError/createRecord/... from inside
// that same lambda — so the target's calls silently execute on the source's
// thread. Pins H2.2 with source and target relocated to two DIFFERENT I/O
// threads: each backend's calls must land on its own thread.
void TstBackendThreadRelocation::firstSync_backendsOnDifferentThreads()
{
    QTemporaryDir sourceDir;
    QTemporaryDir targetDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(sourceDir.filePath(calId)));
    QVERIFY(QDir().mkpath(targetDir.filePath(calId)));

    {
        QFile f(sourceDir.filePath(calId + QStringLiteral("/first-evt-1.ics")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(seedIcs("first-evt-1"));
    }

    auto *sourceBackend = new ThreadRecordingLocalBackend(sourceDir.path());
    auto *targetBackend = new ThreadRecordingLocalBackend(targetDir.path());
    sourceBackend->setDbPath(sourceDir.filePath(QStringLiteral(".kalburator-sync.db")));
    targetBackend->setDbPath(targetDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThreadA;
    ioThreadA.setObjectName(QStringLiteral("d1-test-firstsync-io-a"));
    ioThreadA.start();
    QThread ioThreadB;
    ioThreadB.setObjectName(QStringLiteral("d1-test-firstsync-io-b"));
    ioThreadB.start();
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(sourceBackend, [sourceBackend]() { delete sourceBackend; },
                                  Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(targetBackend, [targetBackend]() { delete targetBackend; },
                                  Qt::BlockingQueuedConnection);
        ioThreadA.quit();
        ioThreadA.wait();
        ioThreadB.quit();
        ioThreadB.wait();
    });
    sourceBackend->moveToThread(&ioThreadA);
    targetBackend->moveToThread(&ioThreadB);

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("firstsync-source"), sourceBackend);
    registry.registerBackendInstance(QStringLiteral("firstsync-target"), targetBackend);

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
    mapping.id              = QStringLiteral("firstsync-mapping");
    mapping.sourceBackend   = QStringLiteral("firstsync-source");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("firstsync-target");
    mapping.targetCalendar  = calId;
    // dispatchFirstSync's fast path only fires for OneWayUpload with equal
    // shapes and useQuickPath (true here: fresh BaselineStore, no baselines
    // for this mapping yet) and an empty target — all true below.
    mapping.mode            = SyncMode::OneWayUpload;
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

    QVERIFY(QFile::exists(targetDir.filePath(calId + QStringLiteral("/first-evt-1.ics"))));
    QVERIFY2(sourceBackend->m_loadRecordsCallThread == &ioThreadA,
             "source loadRecords did not execute on the source backend's own thread");
    QVERIFY2(targetBackend->m_loadRecordsCallThread == &ioThreadB,
             "target loadRecords did not execute on the target backend's own thread");
    QVERIFY2(targetBackend->m_createRecordCallThread == &ioThreadB,
             "target createRecord did not execute on the target backend's own thread");
}

// ---- T1.5: GUI-stall probe -------------------------------------------------
//
// An earlier version of this probe also asserted the inverse — backends left
// on the caller's thread should show a stall > 50ms. That assertion turned
// out not to hold for this harness: davSyncRequest's nested QEventLoop::exec()
// keeps processing other pending events (including our heartbeat QTimer)
// while it waits for the network reply, so a single latency-injected
// round-trip doesn't manifest as a naive heartbeat gap even when the backend
// shares the calling thread. The historical 120s freeze (finding N7) is a
// bulk-operation phenomenon, not something one small fetch reproduces — the
// authoritative freeze reproduction is T3.3's live verification against a
// real server. Dropped the inverse rather than keep an assertion that
// doesn't actually discriminate anything.
//
// The other thing that inverse attempt exposed: giving FakeCalDavServer a
// response delay via QTimer::singleShot only simulates real network latency
// if the server can actually service that deferred callback independent of
// whatever the "GUI" thread is doing. Since the fast-path pre-check
// (prepareSyncFastPath(), called synchronously from runSync() on the calling
// thread before the worker thread starts) now correctly marshals its
// ChangeDetection calls via BlockingQueuedConnection when the backend has
// been relocated (see runOnBackendThread() in syncengine.cpp), that pre-check
// parks the calling thread without pumping its own event loop — so if the
// fake server lived on that same thread, its deferred response would never
// fire and the whole run would deadlock. Giving the server its own thread
// (mimicking a real, independent CalDAV server process) fixes this and is
// more realistic besides.
qint64 TstBackendThreadRelocation::runStallProbe()
{
    auto *server = new FakeCalDavServer();
    server->setResponseDelayMs(200);
    server->setSeedEvents(QString::fromLatin1(kPersonalHref),
                          {seedIcs("stall-evt-1")});
    QThread serverThread;
    serverThread.setObjectName(QStringLiteral("d1-test-fake-server"));
    serverThread.start();
    server->moveToThread(&serverThread);

    bool listening = false;
    QUrl baseUrl;
    QMetaObject::invokeMethod(server, [server, &listening, &baseUrl]() {
        listening = server->startListening();
        baseUrl = server->baseUrl();
    }, Qt::BlockingQueuedConnection);
    if (!listening) {
        QMetaObject::invokeMethod(server, [server]() { delete server; }, Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
        return -1;
    }
    auto serverGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(server, [server]() { delete server; }, Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
    });

    QTemporaryDir cacheDir;
    QTemporaryDir localDir;
    if (!cacheDir.isValid() || !localDir.isValid()) return -1;
    const QString calId = QStringLiteral("Personal");
    if (!QDir().mkpath(localDir.filePath(calId))) return -1;

    auto *remoteBackend = new RemoteCalendarBackend(baseUrl,
                                                     QStringLiteral("testuser"),
                                                     QStringLiteral("testpass"));
    remoteBackend->setCacheDir(cacheDir.path());
    remoteBackend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));
    remoteBackend->registerCalendarUrl(
        calId, baseUrl.toString() + QString::fromLatin1(kPersonalHref).mid(1));

    auto *localBackend = new LocalBackend(localDir.path());
    localBackend->setDbPath(localDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("d1-test-stall-io"));
    ioThread.start();
    // See the T1.4 cases above for why deletion goes through invokeMethod on
    // the backend's own thread before a relocated I/O thread stops.
    auto backendGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(remoteBackend, [remoteBackend]() { delete remoteBackend; },
                                  Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(localBackend, [localBackend]() { delete localBackend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    remoteBackend->moveToThread(&ioThread);
    localBackend->moveToThread(&ioThread);

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("stall-remote"), remoteBackend);
    registry.registerBackendInstance(QStringLiteral("stall-local"), localBackend);

    Test::StubSyncHost host(&registry);
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(calId);
    host.stubCollection()->addCalendarWithId(calId, hostCal);

    QTemporaryDir engineDbDir;
    if (!engineDbDir.isValid()) return -1;
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
    mapping.id              = QStringLiteral("stall-mapping");
    mapping.sourceBackend   = QStringLiteral("stall-remote");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("stall-local");
    mapping.targetCalendar  = calId;
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});

    // Heartbeat on the calling ("GUI") thread: ticks every 10ms; the largest
    // observed gap between ticks is how long this thread was ever stalled.
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 lastTick = 0;
    qint64 maxGapMs = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
        const qint64 now = elapsed.elapsed();
        const qint64 gap = now - lastTick;
        if (gap > maxGapMs) maxGapMs = gap;
        lastTick = now;
    });
    heartbeat.start();

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = engine.runSync(req);
    int waited = 0;
    constexpr int kSyncTimeoutMs = 30000;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(5);
        waited += 5;
    }
    heartbeat.stop();

    if (!future.isFinished() || future.isCanceled()) return -1;
    return maxGapMs;
}

void TstBackendThreadRelocation::stallProbe_relocatedBackends_stayResponsive()
{
    const qint64 maxGap = runStallProbe();
    QVERIFY(maxGap >= 0);
    // The D1 acceptance gate: the calling ("GUI") thread must never stall
    // more than 50ms through a full sync cycle over a latency-injected fake
    // server, once both mapping backends live on their own I/O thread.
    QVERIFY2(maxGap < 50,
             qPrintable(QStringLiteral(
                 "GUI-analog thread stalled %1ms with backends relocated to their own I/O thread")
                            .arg(maxGap)));
}

// H4: pins the O16 fast-path fix's cancellation semantics. Pre-H4,
// prepareSyncFastPath() runs synchronously on runSync()'s caller thread, so
// runSync() itself blocks for as long as the backend's fresh-token query
// takes (here, the fake server's injected 200ms delay) — there is no window
// in which the caller can observe an in-flight fast path and cancel it.
// Post-H4, runSync() returns near-instantly (the fast path is dispatched to
// the worker thread via a queued signal), leaving a real window in which
// future.cancel() can land while the worker is still awaiting the delayed
// server response. The RED assertion below (runSync() must return inside
// 50ms) is the direct O16 proof; the rest of the test pins that a
// cancellation landing in that window is honoured (no mapping dispatched,
// future reports canceled).
void TstBackendThreadRelocation::cancelDuringFastPath_reportsCancelled()
{
    // The FakeCalDavServer must live on its own thread, not the test/"GUI"
    // thread: pre-H4, runSync() blocks the GUI thread for the fast-path
    // duration, so if the server shared that thread it could never pump its
    // own QTcpServer/socket events to answer the delayed request — a
    // self-deadlock distinct from the bug under test. See runStallProbe()
    // above, which uses the same shape for the same reason.
    auto *server = new FakeCalDavServer();
    server->setResponseDelayMs(200);
    server->setSeedEvents(QString::fromLatin1(kPersonalHref), {seedIcs("cancel-evt-1")});
    QThread serverThread;
    serverThread.setObjectName(QStringLiteral("d1-test-cancel-fake-server"));
    serverThread.start();
    server->moveToThread(&serverThread);

    bool listening = false;
    QUrl baseUrl;
    QMetaObject::invokeMethod(server, [server, &listening, &baseUrl]() {
        listening = server->startListening();
        baseUrl = server->baseUrl();
    }, Qt::BlockingQueuedConnection);
    QVERIFY(listening);
    auto serverGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(server, [server]() { delete server; }, Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
    });

    QTemporaryDir cacheDir;
    QTemporaryDir localDir;
    QVERIFY(cacheDir.isValid() && localDir.isValid());
    const QString calId = QStringLiteral("Personal");
    QVERIFY(QDir().mkpath(localDir.filePath(calId)));

    auto *remoteBackend = new RemoteCalendarBackend(baseUrl,
                                                     QStringLiteral("testuser"),
                                                     QStringLiteral("testpass"));
    remoteBackend->setCacheDir(cacheDir.path());
    remoteBackend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));
    remoteBackend->registerCalendarUrl(
        calId, baseUrl.toString() + QString::fromLatin1(kPersonalHref).mid(1));

    auto *localBackend = new LocalBackend(localDir.path());
    localBackend->setDbPath(localDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("d1-test-cancel-io"));
    ioThread.start();
    auto backendGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(remoteBackend, [remoteBackend]() { delete remoteBackend; },
                                  Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(localBackend, [localBackend]() { delete localBackend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    remoteBackend->moveToThread(&ioThread);
    localBackend->moveToThread(&ioThread);

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("cancel-remote"), remoteBackend);
    registry.registerBackendInstance(QStringLiteral("cancel-local"), localBackend);

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
    mapping.id              = QStringLiteral("cancel-mapping");
    mapping.sourceBackend   = QStringLiteral("cancel-remote");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("cancel-local");
    mapping.targetCalendar  = calId;
    mapping.mode            = SyncMode::TwoWay;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});
    engine.setSkipUnchangedMappings(true);

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;

    QElapsedTimer callTimer;
    callTimer.start();
    auto future = engine.runSync(req);
    const qint64 callElapsedMs = callTimer.elapsed();
    QVERIFY2(callElapsedMs < 50,
             qPrintable(QStringLiteral(
                 "runSync() blocked the caller for %1ms — the fast-path pre-pass "
                 "must run on the worker thread (O16), not synchronously here")
                            .arg(callElapsedMs)));

    future.cancel();

    int waited = 0;
    constexpr int kWaitTimeoutMs = 5000;
    while (!future.isFinished() && waited < kWaitTimeoutMs) {
        QTest::qWait(5);
        waited += 5;
    }
    QVERIFY(future.isFinished());
    QVERIFY(future.isCanceled());

    // No mapping dispatch should have happened: the cancellation landed
    // while the fast path was still in flight on the worker thread.
    QVERIFY(QDir(localDir.filePath(calId)).entryList(QDir::Files).isEmpty());
}

// ---- H5: single-fetch pipeline (O23 remainder) -----------------------------
//
// dispatchSync's fetch gate blocks call fetchItems() as a cancellable gating
// step, then IMMEDIATELY re-read the same collection via loadRecordsOrError()
// — a second, fully redundant pass over the same data (a second full
// directory scan for LocalBackend; a second listing+multiget round-trip for
// RemoteCalendarBackend, which re-derives records by calling fetchItems()
// again internally). H5 adds SyncBackendBase::recordsFromLastFetch(), served
// from a single-shot memo captured at the gate's own fetchItems() call, and
// wires dispatchSync to use it instead of loadRecordsOrError() when the gate
// op succeeded.
void TstBackendThreadRelocation::singleFetch_localBackends_noRedundantRead()
{
    QTemporaryDir sourceDir;
    QTemporaryDir targetDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(sourceDir.filePath(calId)));
    QVERIFY(QDir().mkpath(targetDir.filePath(calId)));

    {
        QFile f(sourceDir.filePath(calId + QStringLiteral("/single-fetch-evt-1.ics")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(seedIcs("single-fetch-evt-1"));
    }

    auto *sourceBackend = new ThreadRecordingLocalBackend(sourceDir.path());
    auto *targetBackend = new ThreadRecordingLocalBackend(targetDir.path());
    sourceBackend->setDbPath(sourceDir.filePath(QStringLiteral(".kalburator-sync.db")));
    targetBackend->setDbPath(targetDir.filePath(QStringLiteral(".kalburator-sync.db")));

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("single-fetch-source"), sourceBackend);
    registry.registerBackendInstance(QStringLiteral("single-fetch-target"), targetBackend);

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
    mapping.id              = QStringLiteral("single-fetch-mapping");
    mapping.sourceBackend   = QStringLiteral("single-fetch-source");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("single-fetch-target");
    mapping.targetCalendar  = calId;
    // One-way: isolates the read-side redundancy H5 fixes from unrelated
    // post-write harvest re-reads (a create on the target legitimately
    // triggers its own baseline re-read elsewhere in dispatchSync — out of
    // H5's scope, which is only the "Fetch source/target records" gate
    // blocks). TwoWay would conflate the two.
    mapping.mode            = SyncMode::OneWayUpload;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});

    auto runOnce = [&]() {
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
    };

    // Cycle 1: target starts empty, so this is dispatchFirstSync's "inline
    // blob mirror" path — a separate, out-of-scope code path with its own
    // reads. Only cycle 2, a steady-state incremental sync through the main
    // dispatchSync gate, is what H5 targets.
    runOnce();
    QVERIFY(QFile::exists(targetDir.filePath(calId + QStringLiteral("/single-fetch-evt-1.ics"))));

    {
        QFile f(sourceDir.filePath(calId + QStringLiteral("/single-fetch-evt-2.ics")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(seedIcs("single-fetch-evt-2"));
    }
    runOnce();
    QVERIFY(QFile::exists(targetDir.filePath(calId + QStringLiteral("/single-fetch-evt-2.ics"))));

    // The single-fetch pipeline means dispatchSync's own read block never
    // falls back to loadRecordsOrError() (and thus never calls loadRecords())
    // when the gate's own fetchItems() already succeeded — it serves the
    // gate's own fetch results via recordsFromLastFetch() instead. Measuring
    // the fallback specifically (not total loadRecords() calls) is required:
    // this cycle's create also triggers legitimate, out-of-scope loadRecords()
    // calls elsewhere (classifyForWriter's diff classification, the
    // post-write hash-verification refetch) that have nothing to do with H5.
    QCOMPARE(sourceBackend->m_recordsFromLastFetchFellBackCount, 0);
    QCOMPARE(targetBackend->m_recordsFromLastFetchFellBackCount, 0);

    delete sourceBackend;
    delete targetBackend;
}

void TstBackendThreadRelocation::singleFetch_remoteBackend_noRedundantListing()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());
    server.setSeedEvents(QString::fromLatin1(kPersonalHref), {seedIcs("single-fetch-remote-evt-1")});

    QTemporaryDir cacheDir;
    QTemporaryDir localDir;
    QVERIFY(cacheDir.isValid() && localDir.isValid());
    const QString calId = QStringLiteral("Personal");
    QVERIFY(QDir().mkpath(localDir.filePath(calId)));

    auto *remoteBackend = new RemoteCalendarBackend(server.baseUrl(),
                                                     QStringLiteral("testuser"),
                                                     QStringLiteral("testpass"));
    remoteBackend->setCacheDir(cacheDir.path());
    remoteBackend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));
    remoteBackend->registerCalendarUrl(
        calId, server.baseUrl().toString() + QString::fromLatin1(kPersonalHref).mid(1));

    auto *localBackend = new LocalBackend(localDir.path());
    localBackend->setDbPath(localDir.filePath(QStringLiteral(".kalburator-sync.db")));

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("single-fetch-remote"), remoteBackend);
    registry.registerBackendInstance(QStringLiteral("single-fetch-local"), localBackend);

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
    mapping.id              = QStringLiteral("single-fetch-remote-mapping");
    mapping.sourceBackend   = QStringLiteral("single-fetch-remote");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("single-fetch-local");
    mapping.targetCalendar  = calId;
    // One-way: isolates the read-side redundancy H5 fixes on the remote
    // source from unrelated post-write harvest re-reads on the target
    // (out of H5's scope). See the Local test's comment for the same
    // reasoning.
    mapping.mode            = SyncMode::OneWayUpload;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});

    auto runOnce = [&]() {
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
    };

    // Cycle 1 establishes the mapping (first-sync overhead is out of H5's
    // scope — dispatchFirstSync's own reads are a separate code path that
    // also goes through loadRecordsOrError -> fetchItems(), independent of
    // H5). Only cycle 2, a steady-state incremental sync, is what H5
    // targets, so the fetchStarted spy is attached only for it.
    runOnce();
    QVERIFY(QFile::exists(localDir.filePath(calId + QStringLiteral("/single-fetch-remote-evt-1.ics"))));

    QSignalSpy fetchStartedSpy(remoteBackend, &Kalburator::Sync::SyncBackendBase::fetchStarted);
    server.setSeedEvents(QString::fromLatin1(kPersonalHref), {seedIcs("single-fetch-remote-evt-2")});
    runOnce();
    QVERIFY(QFile::exists(localDir.filePath(calId + QStringLiteral("/single-fetch-remote-evt-2.ics"))));

    // A single incremental fetch pass must call fetchItems() (and thus emit
    // fetchStarted) exactly once. Pre-H5, dispatchSync's loadRecordsOrError()
    // fallback re-derives records by calling RemoteCalendarBackend::
    // loadRecords(), which calls fetchItems() again — a second, fully
    // redundant listing+multiget round trip.
    QCOMPARE(fetchStartedSpy.count(), 1);

    delete remoteBackend;
    delete localBackend;
}

// ---- H8.5 / O27: steady-state writes honor RecordWriter::BackendThread ----
//
// applyBatch's BackendThread branch must marshal writer->apply() to the
// backend's own thread (recordwriter.h:34). Pre-H8.5 it ran apply() — and
// thus updateRecord()/deleteRecord() — directly on the worker thread, so
// every steady-state CalDAV update did QNAM I/O + etag-cache SQL cross-thread
// (live UB, the D1 class). This pins the fix with a target backend on a
// dedicated I/O thread and asserts the record writes execute there, not on
// the engine worker thread.
void TstBackendThreadRelocation::steadyStateWrites_appliesOnBackendThread()
{
    QTemporaryDir sourceDir;
    QTemporaryDir targetDir;
    QVERIFY(sourceDir.isValid() && targetDir.isValid());
    const QString calId = QStringLiteral("cal-1");
    QVERIFY(QDir().mkpath(sourceDir.filePath(calId)));
    QVERIFY(QDir().mkpath(targetDir.filePath(calId)));

    // A SUMMARY-varied ics so an edit produces a different fingerprint (and
    // thus a genuine steady-state update, not a no-op skip).
    auto icsWithSummary = [](const QByteArray &uid, const QByteArray &summary) -> QByteArray {
        return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
               "BEGIN:VEVENT\r\nUID:" + uid + "\r\n"
               "SUMMARY:" + summary + "\r\nDTSTART:20260601T120000Z\r\n"
               "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    };

    const QString evt1Path = sourceDir.filePath(calId + QStringLiteral("/o27-evt-1.ics"));
    const QString evt2Path = sourceDir.filePath(calId + QStringLiteral("/o27-evt-2.ics"));
    {
        QFile f(evt1Path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(icsWithSummary("o27-evt-1", "Original"));
    }
    {
        QFile f(evt2Path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(icsWithSummary("o27-evt-2", "ToDelete"));
    }

    // Source stays on the test thread; target is relocated to a dedicated I/O
    // thread. OneWayUpload means source→target writes hit the target only, so
    // the relocated backend is exactly where the steady-state update/delete
    // land — the O27 surface.
    auto *sourceBackend = new ThreadRecordingLocalBackend(sourceDir.path());
    auto *targetBackend = new ThreadRecordingLocalBackend(targetDir.path());
    sourceBackend->setDbPath(sourceDir.filePath(QStringLiteral(".kalburator-sync.db")));
    targetBackend->setDbPath(targetDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("d1-test-o27-io"));
    ioThread.start();
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(targetBackend, [targetBackend]() { delete targetBackend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
        delete sourceBackend;
    });
    targetBackend->moveToThread(&ioThread);

    BackendRegistry registry;
    registry.registerBackendInstance(QStringLiteral("o27-source"), sourceBackend);
    registry.registerBackendInstance(QStringLiteral("o27-target"), targetBackend);

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
    mapping.id              = QStringLiteral("o27-mapping");
    mapping.sourceBackend   = QStringLiteral("o27-source");
    mapping.sourceCalendar  = calId;
    mapping.targetBackend   = QStringLiteral("o27-target");
    mapping.targetCalendar  = calId;
    mapping.mode            = SyncMode::OneWayUpload;
    mapping.conflictPolicy  = ConflictResolution::LastWriteWins;
    mapping.enabled         = true;
    engine.setSyncMappings({mapping});

    auto runOnce = [&]() {
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
    };

    // Cycle 1: first sync mirrors both events onto the (empty) target. This is
    // dispatchFirstSync's blob-mirror path (already correctly marshaled since
    // H2.2) — not what O27 is about; it just establishes the baseline.
    runOnce();
    QVERIFY(QFile::exists(targetDir.filePath(calId + QStringLiteral("/o27-evt-1.ics"))));
    QVERIFY(QFile::exists(targetDir.filePath(calId + QStringLiteral("/o27-evt-2.ics"))));

    // Steady-state edit: modify evt-1 (→ update) and remove evt-2 (→ delete).
    {
        QFile f(evt1Path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(icsWithSummary("o27-evt-1", "Edited"));
    }
    QVERIFY(QFile::remove(evt2Path));

    // Cycle 2 goes through the steady-state dispatchSync/applyBatch path — the
    // O27 surface. Pre-H8.5, updateRecord()/deleteRecord() run on the engine
    // worker thread; post-H8.5 they must run on the target backend's I/O thread.
    runOnce();

    QVERIFY(targetBackend->m_updateRecordCallThread != nullptr);
    QVERIFY(targetBackend->m_deleteRecordCallThread != nullptr);
    QCOMPARE(targetBackend->m_updateRecordCallThread, &ioThread);
    QCOMPARE(targetBackend->m_deleteRecordCallThread, &ioThread);
}

QTEST_GUILESS_MAIN(TstBackendThreadRelocation)
#include "tst_backend_thread_relocation.moc"
