// E5.2 (audit B7) — the re-entrancy pin.
//
// A backend-thread synchronous operation body that spins a nested
// QEventLoop::exec() to await a network reply keeps pumping the backend
// thread's event queue while it waits. Any app- or engine-side call
// marshaled onto the backend thread mid-wait therefore runs *nested inside*
// the suspended operation body — re-entrancy against half-mutated backend
// state (pendingCtag, m_lastRawIcsByUid, the EtagCache, the content cache):
// the named candidate mechanism for the historical N5 corruption class.
//
// This test pins exactly that: during an in-flight fetchItems whose CTag
// PROPFIND is stalled by a latency-injected fake server, a queued call
// delivered onto the backend thread must observe reentrancyDepth() == 0 —
// i.e. it ran BETWEEN operation-body activations, never nested inside one.
//
// RED (pre-E5.2): fetchItems' body calls the synchronous fetchFreshCtag,
// whose nested QEventLoop pumps the queued probe mid-wait while the body's
// ReentryGuard is still held -> probe observes depth 1 -> FAIL.
// GREEN (post-E5.2): the CTag PROPFIND is async; the body returns to the
// event loop before the network wait, so the probe runs at depth 0.

#include <QtTest/QtTest>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>

#include <atomic>

#include <KCalendarCore/Event>

#include "fakecaldavserver.h"
#include "remotecalendarbackend.h"
#include "syncbackend.h"
#include "filteredcollectionbackend.h"
#include "changedetection.h"
#include "recordfilter.h"

using namespace Kalburator::Sync;
using Kalburator::Shape::PropertyId;
using Kalburator::Shape::RecordFilter;
using Kalburator::Sinks::FilteredCollectionBackend;

namespace {

constexpr const char *kPersonalHref = "/calendars/testuser/personal/";
constexpr int kOpTimeoutMs = 15000;

QByteArray seedIcs(const QByteArray &uid)
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
           "BEGIN:VEVENT\r\nUID:" + uid + "\r\n"
           "SUMMARY:Seeded\r\nDTSTART:20260601T120000Z\r\n"
           "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
}

} // namespace

class TstBackendReentrancyPin : public QObject
{
    Q_OBJECT

private slots:
    void queuedCallDuringFetch_neverRunsNested();
    void fastPathRevisionQuery_throughFilteredView_neverRunsNested();
    void concurrentFetchesOnSameCollection_serialize();

    // E5.3 RED (d): the write path (applyRecords) extends the same pin over
    // a write — an app-side call marshaled onto the backend thread during an
    // in-flight applyRecords() must never run nested inside it.
    void applyRecordsInFlight_neverRunsNested();
};

void TstBackendReentrancyPin::queuedCallDuringFetch_neverRunsNested()
{
    // --- fake server on its own thread, with injected latency + a CTag -----
    auto *server = new FakeCalDavServer();
    server->setSeedEvents(QString::fromLatin1(kPersonalHref), {seedIcs("reentry-evt-1")});
    // A configured CTag makes both the calendar-list PROPFIND (discovery
    // stashes it as pendingCtag) and the Depth:0 PROPFIND (fetchFreshCtag)
    // return it — so fetch #1 commits a stored CTag and fetch #2 then enters
    // the fetchFreshCtag path this test probes.
    server->setCollectionCtag(QString::fromLatin1(kPersonalHref), QStringLiteral("ctag-1"));
    // Latency injected via the fake's deferred-timer path (never a blocking
    // sleep): every response, including the Depth:0 CTag PROPFIND, is held
    // ~300ms, giving the queued probe a wide window to be pumped mid-wait.
    server->setResponseDelayMs(300);

    QThread serverThread;
    serverThread.setObjectName(QStringLiteral("reentry-fake-server"));
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
        QMetaObject::invokeMethod(server, [server]() { delete server; },
                                  Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
    });

    // --- backend relocated to its own I/O thread ---------------------------
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    auto *backend = new RemoteCalendarBackend(baseUrl,
                                              QStringLiteral("testuser"),
                                              QStringLiteral("testpass"));
    backend->setCacheDir(cacheDir.path());
    backend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("reentry-backend-io"));
    ioThread.start();
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(backend, [backend]() { delete backend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    backend->moveToThread(&ioThread);

    // Discovery walk — stashes the calendar-list CTag as pendingCtag.
    QSignalSpy loadFinishedSpy(backend, SIGNAL(loadCalendarsFinished(QString,bool,QString)));
    QMetaObject::invokeMethod(backend, [&]() {
        backend->loadCalendars(QStringLiteral("personal-coll"));
    }, Qt::BlockingQueuedConnection);
    QTRY_COMPARE_WITH_TIMEOUT(loadFinishedSpy.count(), 1, kOpTimeoutMs);
    QVERIFY(loadFinishedSpy.first().at(1).toBool());

    // Fetch #1 — no stored CTag yet, so it does a full list+multiget and, on
    // success, commits pendingCtag ("ctag-1") to the persistent CTag store.
    FetchOperation *fetch1 = nullptr;
    QMetaObject::invokeMethod(backend, [&]() {
        fetch1 = backend->fetchItems(QStringLiteral("Personal"));
    }, Qt::BlockingQueuedConnection);
    QVERIFY(fetch1 != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(fetch1->isFinished(), kOpTimeoutMs);
    QCOMPARE(fetch1->state(), SyncOperation::Succeeded);
    delete fetch1;

    // --- the probe: fetch #2 (enters fetchFreshCtag) + a queued depth read --
    //
    // Post fetch #2, then wait until its operation reaches Running — the body
    // has begun and is now inside (or entering) fetchFreshCtag's ~300ms wait.
    // ONLY THEN post the depth probe, so it is delivered DURING that wait
    // rather than in the gap before the deferred body runs. Today that wait is
    // a nested QEventLoop that pumps the probe while the body's ReentryGuard
    // is held -> probe observes depth 1 (RED). Post-E5.2 the wait is async and
    // the body has already returned to the main event loop -> depth 0 (GREEN).
    std::atomic<int> observedDepth{-1};
    std::atomic<bool> probeRan{false};
    std::atomic<FetchOperation *> fetch2{nullptr};

    QMetaObject::invokeMethod(backend, [&]() {
        fetch2.store(backend->fetchItems(QStringLiteral("Personal")));
    }, Qt::QueuedConnection);

    QTRY_VERIFY_WITH_TIMEOUT(fetch2.load() != nullptr
                             && fetch2.load()->state() == SyncOperation::Running,
                             kOpTimeoutMs);

    QMetaObject::invokeMethod(backend, [&]() {
        observedDepth.store(backend->reentrancyDepth());
        probeRan.store(true);
    }, Qt::QueuedConnection);

    QTRY_VERIFY_WITH_TIMEOUT(probeRan.load(), kOpTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fetch2.load()->isFinished(), kOpTimeoutMs);
    QCOMPARE(fetch2.load()->state(), SyncOperation::Succeeded);
    delete fetch2.load();

    // The pin: a call marshaled onto the backend thread mid-fetch must never
    // run nested inside the operation body (audit B7).
    QCOMPARE(observedDepth.load(), 0);
}

// E5.2 amendment A6 — the fast-path revision query through a filtered view.
//
// A `FilteredCollectionBackend` wrapping a CalDAV backend does NOT override
// the plural `collectionRevisions`, so pre-fix it inherits the default plural
// loop over the singular `collectionRevision`, which forwards to the parent's
// synchronous `fetchAllCtags` -> `davSyncRequest`'s nested QEventLoop ON THE
// BACKEND THREAD. Driven by the engine's backend-thread fast-path, that is a
// surviving B7-family loop the concrete-class fetchItems conversion does not
// close.
//
// This pins the async fix at the interface: the engine fast-path calls
// `collectionRevisionsAsync`, which the FCB forwards to the parent's async
// override (no nested loop). A call marshaled onto the backend thread during
// the in-flight revision query must observe reentrancyDepth() == 0.
//
// RED (pre-A6): FCB has no async override; the default adapts the synchronous
// `collectionRevisions`, whose fetchAllCtags holds the ReentryGuard across the
// nested loop that pumps the queued probe mid-wait -> depth 1 -> FAIL.
// GREEN (post-A6): FCB forwards `collectionRevisionsAsync` to the parent's
// `fetchAllCtagsAsync` (davSyncRequestAsync, no nested loop) -> depth 0.
void TstBackendReentrancyPin::fastPathRevisionQuery_throughFilteredView_neverRunsNested()
{
    auto *server = new FakeCalDavServer();
    server->setSeedEvents(QString::fromLatin1(kPersonalHref), {seedIcs("rev-evt-1")});
    server->setCollectionCtag(QString::fromLatin1(kPersonalHref), QStringLiteral("ctag-A"));
    server->setResponseDelayMs(300);

    QThread serverThread;
    serverThread.setObjectName(QStringLiteral("rev-fake-server"));
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
        QMetaObject::invokeMethod(server, [server]() { delete server; },
                                  Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
    });

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    auto *backend = new RemoteCalendarBackend(baseUrl,
                                              QStringLiteral("testuser"),
                                              QStringLiteral("testpass"));
    backend->setCacheDir(cacheDir.path());
    backend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));

    // A filtered view over the CalDAV backend's "Personal" collection — the
    // topology amendment A6 closes. Parent pointer is borrowed; both objects
    // live on the same backend I/O thread.
    auto *view = new FilteredCollectionBackend(
        backend, QStringLiteral("remote-parent"),
        QStringLiteral("Personal"), QStringLiteral("v-work"),
        RecordFilter{ PropertyId{"categories"}, RecordFilter::Op::Contains,
                      QStringLiteral("Work") });

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("rev-backend-io"));
    ioThread.start();
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(backend, [backend, view]() {
            delete view;
            delete backend;
        }, Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    backend->moveToThread(&ioThread);
    view->moveToThread(&ioThread);

    // Discovery so the parent's davUrlFor("Personal") resolves to the real
    // calendar href — the revision PROPFIND then hits a real (delayed) 207.
    QSignalSpy loadFinishedSpy(backend, SIGNAL(loadCalendarsFinished(QString,bool,QString)));
    QMetaObject::invokeMethod(backend, [&]() {
        backend->loadCalendars(QStringLiteral("personal-coll"));
    }, Qt::BlockingQueuedConnection);
    QTRY_COMPARE_WITH_TIMEOUT(loadFinishedSpy.count(), 1, kOpTimeoutMs);
    QVERIFY(loadFinishedSpy.first().at(1).toBool());

    // Kick the revision query on the FILTERED VIEW's ChangeDetection interface
    // (exactly what the engine fast-path does), then immediately post the depth
    // probe onto the backend thread. Both invokes are queued to the same I/O
    // thread in order: pre-fix the query invoke runs synchronously and spins the
    // nested loop, which pumps the probe mid-wait (depth 1); post-fix the query
    // invoke kicks the async PROPFIND and returns, so the probe runs at depth 0.
    std::atomic<int> observedDepth{-1};
    std::atomic<bool> probeRan{false};
    std::atomic<bool> queryDone{false};

    ChangeDetection *cd = view;
    QMetaObject::invokeMethod(view, [&]() {
        cd->collectionRevisionsAsync(
            {QStringLiteral("v-work")},
            [&](QMap<QString, QString>) { queryDone.store(true); });
    }, Qt::QueuedConnection);

    QMetaObject::invokeMethod(backend, [&]() {
        observedDepth.store(backend->reentrancyDepth());
        probeRan.store(true);
    }, Qt::QueuedConnection);

    QTRY_VERIFY_WITH_TIMEOUT(probeRan.load(), kOpTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(queryDone.load(), kOpTimeoutMs);

    // The pin: the revision query through the filtered view must never run its
    // network wait as a backend-thread nested loop (audit B7 / amendment A6).
    QCOMPARE(observedDepth.load(), 0);
}

// E5.2 (op-queue wiring) — CalDAV fetchItems now routes through E5.1's
// per-collection FIFO queue, so two fetches on the SAME collection serialize:
// the second's body must not start until the first finishes.
//
// RED (pre-wiring): fetchItems dispatched its body via its own
// QMetaObject::invokeMethod(this, ...), so two concurrent fetches on one
// collection both went Running and ran their bodies interleaved -> the second
// is Running while the first still is -> FAIL.
// GREEN (post-wiring): the second op stays Pending, held in the queue, until
// the first settles.
void TstBackendReentrancyPin::concurrentFetchesOnSameCollection_serialize()
{
    auto *server = new FakeCalDavServer();
    server->setSeedEvents(QString::fromLatin1(kPersonalHref), {seedIcs("serialize-evt-1")});
    server->setCollectionCtag(QString::fromLatin1(kPersonalHref), QStringLiteral("ctag-S"));
    server->setResponseDelayMs(300);

    QThread serverThread;
    serverThread.setObjectName(QStringLiteral("serialize-fake-server"));
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
        QMetaObject::invokeMethod(server, [server]() { delete server; },
                                  Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
    });

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    auto *backend = new RemoteCalendarBackend(baseUrl,
                                              QStringLiteral("testuser"),
                                              QStringLiteral("testpass"));
    backend->setCacheDir(cacheDir.path());
    backend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("serialize-backend-io"));
    ioThread.start();
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(backend, [backend]() { delete backend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    backend->moveToThread(&ioThread);

    QSignalSpy loadFinishedSpy(backend, SIGNAL(loadCalendarsFinished(QString,bool,QString)));
    QMetaObject::invokeMethod(backend, [&]() {
        backend->loadCalendars(QStringLiteral("personal-coll"));
    }, Qt::BlockingQueuedConnection);
    QTRY_COMPARE_WITH_TIMEOUT(loadFinishedSpy.count(), 1, kOpTimeoutMs);
    QVERIFY(loadFinishedSpy.first().at(1).toBool());

    // Fetch #1 commits a stored CTag; subsequent fetches then take the ~300ms
    // async CTag-PROPFIND path, giving a wide serialization window.
    FetchOperation *warmup = nullptr;
    QMetaObject::invokeMethod(backend, [&]() {
        warmup = backend->fetchItems(QStringLiteral("Personal"));
    }, Qt::BlockingQueuedConnection);
    QVERIFY(warmup != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(warmup->isFinished(), kOpTimeoutMs);
    QCOMPARE(warmup->state(), SyncOperation::Succeeded);
    delete warmup;

    // Two fetches on the SAME collection, posted back to back.
    std::atomic<FetchOperation *> fa{nullptr};
    std::atomic<FetchOperation *> fb{nullptr};
    QMetaObject::invokeMethod(backend, [&]() {
        fa.store(backend->fetchItems(QStringLiteral("Personal")));
        fb.store(backend->fetchItems(QStringLiteral("Personal")));
    }, Qt::BlockingQueuedConnection);
    QVERIFY(fa.load() != nullptr);
    QVERIFY(fb.load() != nullptr);

    // Once A is Running (its body has begun, mid async CTag wait), B must still
    // be Pending — held behind A in the collection's FIFO queue.
    QTRY_VERIFY_WITH_TIMEOUT(fa.load()->state() == SyncOperation::Running, kOpTimeoutMs);
    QCOMPARE(fb.load()->state(), SyncOperation::Pending);

    QTRY_VERIFY_WITH_TIMEOUT(fa.load()->isFinished(), kOpTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fb.load()->isFinished(), kOpTimeoutMs);
    QCOMPARE(fa.load()->state(), SyncOperation::Succeeded);
    QCOMPARE(fb.load()->state(), SyncOperation::Succeeded);
    delete fa.load();
    delete fb.load();
}

// E5.3 (audit B7 / CP-A) — the write path (applyRecords) extends this pin
// over a create. applyRecords()'s body is entirely callback-driven (KDAV
// job signals / QNetworkReply::finished) — it returns to the event loop
// immediately after kicking off the create job, never spinning a nested
// wait — so a call marshaled onto the backend thread while the create is
// still in flight (delayed via FakeCalDavServer's per-method PUT delay)
// must observe reentrancyDepth() == 0, exactly like a fetch.
void TstBackendReentrancyPin::applyRecordsInFlight_neverRunsNested()
{
    auto *server = new FakeCalDavServer();
    server->setSeedEvents(QString::fromLatin1(kPersonalHref), {});
    server->setResponseDelayForMethod(QByteArrayLiteral("PUT"), 300);

    QThread serverThread;
    serverThread.setObjectName(QStringLiteral("apply-fake-server"));
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
        QMetaObject::invokeMethod(server, [server]() { delete server; },
                                  Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
    });

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    auto *backend = new RemoteCalendarBackend(baseUrl,
                                              QStringLiteral("testuser"),
                                              QStringLiteral("testpass"));
    backend->setCacheDir(cacheDir.path());
    backend->setDbPath(cacheDir.filePath(QStringLiteral(".kalburator-sync.db")));

    QThread ioThread;
    ioThread.setObjectName(QStringLiteral("apply-backend-io"));
    ioThread.start();
    auto ioThreadGuard = qScopeGuard([&]() {
        QMetaObject::invokeMethod(backend, [backend]() { delete backend; },
                                  Qt::BlockingQueuedConnection);
        ioThread.quit();
        ioThread.wait();
    });
    backend->moveToThread(&ioThread);

    QSignalSpy loadFinishedSpy(backend, SIGNAL(loadCalendarsFinished(QString,bool,QString)));
    QMetaObject::invokeMethod(backend, [&]() {
        backend->loadCalendars(QStringLiteral("personal-coll"));
    }, Qt::BlockingQueuedConnection);
    QTRY_COMPARE_WITH_TIMEOUT(loadFinishedSpy.count(), 1, kOpTimeoutMs);
    QVERIFY(loadFinishedSpy.first().at(1).toBool());

    WriterBatch batch;
    BackendRecord rec;
    rec.id = QStringLiteral("apply-evt-1");
    rec.data = seedIcs("apply-evt-1");
    batch.creates.append(rec);

    std::atomic<WriteOperation *> writeOp{nullptr};
    QMetaObject::invokeMethod(backend, [&]() {
        writeOp.store(backend->applyRecords(QStringLiteral("Personal"), batch));
    }, Qt::QueuedConnection);

    QTRY_VERIFY_WITH_TIMEOUT(writeOp.load() != nullptr
                             && writeOp.load()->state() == SyncOperation::Running,
                             kOpTimeoutMs);

    std::atomic<int> observedDepth{-1};
    std::atomic<bool> probeRan{false};
    QMetaObject::invokeMethod(backend, [&]() {
        observedDepth.store(backend->reentrancyDepth());
        probeRan.store(true);
    }, Qt::QueuedConnection);

    QTRY_VERIFY_WITH_TIMEOUT(probeRan.load(), kOpTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(writeOp.load()->isFinished(), kOpTimeoutMs);
    QCOMPARE(writeOp.load()->state(), SyncOperation::Succeeded);
    QVERIFY(writeOp.load()->succeededUids().contains(QStringLiteral("apply-evt-1")));
    delete writeOp.load();

    // The pin: a call marshaled onto the backend thread mid-apply must never
    // run nested inside the operation body (audit B7, extended to writes).
    QCOMPARE(observedDepth.load(), 0);
}

QTEST_MAIN(TstBackendReentrancyPin)
#include "tst_backend_reentrancy_pin.moc"
