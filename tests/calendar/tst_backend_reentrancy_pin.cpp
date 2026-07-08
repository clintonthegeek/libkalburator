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

using namespace Kalburator::Sync;

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

QTEST_MAIN(TstBackendReentrancyPin)
#include "tst_backend_reentrancy_pin.moc"
