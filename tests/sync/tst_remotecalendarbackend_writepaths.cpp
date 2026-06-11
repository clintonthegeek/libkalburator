// Plan 7 T1 — protective pins for RemoteCalendarBackend's write paths.
//
// The legacy combined write (`startSync`) is PlanStan-production-load-bearing
// (stagingcontroller.cpp:307) and the calendar CRUD trio is PlanStan's
// new-calendar flow (backenddiscoverycoordinator.cpp:248), but until this file
// both were covered only by the live-Radicale lane (tst_remotecalendarbackend
// self-skips without a server on :5232). Plan 7 T3/T4 rewrite their internals;
// these tests pin the externally observable contract in the default lane first:
//
//   - startSync signal sequence: writeStarted -> writeProgressChanged xN ->
//     itemLoaded per success / itemRemoved per delete -> syncCompleted, and
//     server-side effects over FakeCalDavServer.
//   - createCalendar MKCALENDAR 201/405 policy, URL registration, signals.
//   - updateCalendar PROPPATCH success + color-cache update + calendarUpdated.
//   - deleteCalendar DELETE 204 vs 404 policy + URL unregistration.
//
// The 412-retry fallbacks inside startSync cannot be forced through the fake
// (it ignores If-Match/If-None-Match); they remain pinned by the live lane.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "fakecaldavserver.h"
#include "remotecalendarbackend.h"
#include "syncbackend.h"

using namespace Kalburator::Sync;

namespace {

constexpr const char *kPersonalHref = "/calendars/testuser/personal/";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
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

class TstRemoteCalendarBackendWritePaths : public QObject
{
    Q_OBJECT

private slots:
    void startSync_creations_reach_server_with_signal_contract();
    void startSync_deletions_remove_from_server();
    void startSync_empty_stages_completes_immediately();
    void startSync_undiscovered_calendar_derives_url_and_writes();
    void removeItem_deletes_and_emits_itemRemoved();
    void createCalendar_201_registers_url_and_emits();
    void createCalendar_405_is_idempotent_success();
    void updateCalendar_proppatch_updates_color_cache();
    void deleteCalendar_204_unregisters_then_404_returns_false();

    // Defect 2 (thread-affinity): operations created from a worker thread must
    // still be affiliated with the backend's thread, never parented across it.
    void fetchItems_fromWorkerThread_opLivesOnBackendThread();
    void pushItems_fromWorkerThread_opLivesOnBackendThread();
    void deleteItems_fromWorkerThread_opLivesOnBackendThread();
};

void TstRemoteCalendarBackendWritePaths::startSync_creations_reach_server_with_signal_contract()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(
        QStringLiteral("Personal"),
        server.baseUrl().toString() + QString::fromLatin1(kPersonalHref).mid(1));

    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(QStringLiteral("Personal"));

    const QList<KCalendarCore::Incidence::Ptr> creations{
        makeEvent(QStringLiteral("wp-create-1"), QStringLiteral("First")),
        makeEvent(QStringLiteral("wp-create-2"), QStringLiteral("Second")),
    };

    QSignalSpy startedSpy(&backend, &RemoteCalendarBackend::writeStarted);
    QSignalSpy progressSpy(&backend, &RemoteCalendarBackend::writeProgressChanged);
    QSignalSpy loadedSpy(&backend, &RemoteCalendarBackend::itemLoaded);
    QSignalSpy completedSpy(&backend, &RemoteCalendarBackend::syncCompleted);

    backend.startSync(QStringLiteral("personal-coll"), &cal, creations, {}, {});

    // writeStarted fires synchronously, before any job completes.
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(startedSpy.first().at(0).toString(), QStringLiteral("Personal"));
    QCOMPARE(startedSpy.first().at(1).toInt(), 2);

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 8000);
    QCOMPARE(completedSpy.first().at(0).toString(), QStringLiteral("personal-coll"));

    // One progress tick per job, terminal tick == total.
    QCOMPARE(progressSpy.count(), 2);
    QCOMPARE(progressSpy.last().at(1).toInt(), 2);
    QCOMPARE(progressSpy.last().at(2).toInt(), 2);

    // Each successful create surfaces the incidence with its new ETag.
    QCOMPARE(loadedSpy.count(), 2);

    QVERIFY(server.hasEvent(QString::fromLatin1(kPersonalHref),
                            QStringLiteral("wp-create-1")));
    QVERIFY(server.hasEvent(QString::fromLatin1(kPersonalHref),
                            QStringLiteral("wp-create-2")));
}

void TstRemoteCalendarBackendWritePaths::startSync_deletions_remove_from_server()
{
    FakeCalDavServer server;
    server.setSeedEvents(QString::fromLatin1(kPersonalHref),
                         {seedIcs("wp-del-1")});
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(
        QStringLiteral("Personal"),
        server.baseUrl().toString() + QString::fromLatin1(kPersonalHref).mid(1));

    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(QStringLiteral("Personal"));

    QSignalSpy removedSpy(&backend, &RemoteCalendarBackend::itemRemoved);
    QSignalSpy completedSpy(&backend, &RemoteCalendarBackend::syncCompleted);

    const QMap<QString, QString> deletions{{QStringLiteral("wp-del-1"), QString()}};
    backend.startSync(QStringLiteral("personal-coll"), &cal, {}, {}, deletions);

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 8000);
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(removedSpy.first().at(0).toString(), QStringLiteral("Personal"));
    QCOMPARE(removedSpy.first().at(1).toString(), QStringLiteral("wp-del-1"));
    QVERIFY(!server.hasEvent(QString::fromLatin1(kPersonalHref),
                             QStringLiteral("wp-del-1")));
}

void TstRemoteCalendarBackendWritePaths::startSync_empty_stages_completes_immediately()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerCalendarUrl(
        QStringLiteral("Personal"),
        server.baseUrl().toString() + QString::fromLatin1(kPersonalHref).mid(1));

    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(QStringLiteral("Personal"));

    QSignalSpy startedSpy(&backend, &RemoteCalendarBackend::writeStarted);
    QSignalSpy completedSpy(&backend, &RemoteCalendarBackend::syncCompleted);

    backend.startSync(QStringLiteral("personal-coll"), &cal, {}, {}, {});

    // Zero jobs: completes synchronously, with no writeStarted.
    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.first().at(0).toString(), QStringLiteral("personal-coll"));
    QCOMPARE(startedSpy.count(), 0);
}

void TstRemoteCalendarBackendWritePaths::startSync_undiscovered_calendar_derives_url_and_writes()
{
    // Regression for the first-sync DAV-URL race: on the first sync of a
    // directly-configured (non-primed) CalDAV backend, the per-calendar DAV URL
    // is not yet in the map because async loadCalendars discovery has not
    // completed. The creation must still reach the server at the URL derived
    // from base + calendarId (the same derivation createCalendar/updateCalendar/
    // deleteCalendar already use) — NOT be silently dropped.
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    // Deliberately NO registerCalendarUrl / discovery / primeCalendars: this
    // reproduces the discovery-race window the first sync runs inside.

    KCalendarCore::MemoryCalendar cal(QTimeZone::utc());
    cal.setId(QStringLiteral("Personal"));

    QSignalSpy completedSpy(&backend, &RemoteCalendarBackend::syncCompleted);

    const QList<KCalendarCore::Incidence::Ptr> creations{
        makeEvent(QStringLiteral("wp-undisc-1"), QStringLiteral("Undiscovered"))};
    backend.startSync(QStringLiteral("personal-coll"), &cal, creations, {}, {});

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 8000);
    // Derived calendar URL is base + calendarId == /testuser/Personal/.
    QVERIFY(server.hasEvent(QStringLiteral("/testuser/Personal/"),
                            QStringLiteral("wp-undisc-1")));
}

void TstRemoteCalendarBackendWritePaths::removeItem_deletes_and_emits_itemRemoved()
{
    FakeCalDavServer server;
    server.setSeedEvents(QString::fromLatin1(kPersonalHref),
                         {seedIcs("wp-rm-1")});
    QVERIFY(server.startListening());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerCalendarUrl(
        QStringLiteral("Personal"),
        server.baseUrl().toString() + QString::fromLatin1(kPersonalHref).mid(1));

    QSignalSpy removedSpy(&backend, &RemoteCalendarBackend::itemRemoved);

    backend.removeItem(QStringLiteral("Personal"), QStringLiteral("wp-rm-1"));

    QTRY_COMPARE_WITH_TIMEOUT(removedSpy.count(), 1, 8000);
    QCOMPARE(removedSpy.first().at(0).toString(), QStringLiteral("Personal"));
    QCOMPARE(removedSpy.first().at(1).toString(), QStringLiteral("wp-rm-1"));
    QVERIFY(!server.hasEvent(QString::fromLatin1(kPersonalHref),
                             QStringLiteral("wp-rm-1")));
}

void TstRemoteCalendarBackendWritePaths::createCalendar_201_registers_url_and_emits()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));

    QSignalSpy createdSpy(&backend, &RemoteCalendarBackend::calendarCreated);
    QSignalSpy discoveredSpy(&backend, &RemoteCalendarBackend::calendarDiscovered);

    QVERIFY(backend.createCalendar(QStringLiteral("coll-1"),
                                   QStringLiteral("projects"),
                                   QStringLiteral("Projects"),
                                   CalendarType::Event));

    QCOMPARE(server.requestCount("MKCALENDAR"), 1);
    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(createdSpy.first().at(1).toString(), QStringLiteral("projects"));
    QCOMPARE(discoveredSpy.count(), 1);

    // Success registers the calendar URL and the requested content type.
    QVERIFY(backend.discoveredUrl(QStringLiteral("projects"))
                .contains(QStringLiteral("/testuser/projects/")));
    QVERIFY(backend.discoveredSupportsEvents(QStringLiteral("projects")));
    QVERIFY(!backend.discoveredSupportsTodos(QStringLiteral("projects")));
}

void TstRemoteCalendarBackendWritePaths::createCalendar_405_is_idempotent_success()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));

    QSignalSpy createdSpy(&backend, &RemoteCalendarBackend::calendarCreated);

    QVERIFY(backend.createCalendar(QStringLiteral("coll-1"),
                                   QStringLiteral("projects"),
                                   QStringLiteral("Projects")));
    // Second create: server answers 405; backend treats as already-exists
    // success and does NOT re-emit calendarCreated.
    QVERIFY(backend.createCalendar(QStringLiteral("coll-1"),
                                   QStringLiteral("projects"),
                                   QStringLiteral("Projects")));
    QCOMPARE(createdSpy.count(), 1);
    QVERIFY(!backend.discoveredUrl(QStringLiteral("projects")).isEmpty());
}

void TstRemoteCalendarBackendWritePaths::updateCalendar_proppatch_updates_color_cache()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));

    QVERIFY(backend.createCalendar(QStringLiteral("coll-1"),
                                   QStringLiteral("projects"),
                                   QStringLiteral("Projects")));

    QSignalSpy updatedSpy(&backend, &RemoteCalendarBackend::calendarUpdated);

    const QColor red(Qt::red);
    QVariantMap props;
    props.insert(QStringLiteral("displayName"), QStringLiteral("Renamed"));
    props.insert(QStringLiteral("color"), red);

    QVERIFY(backend.updateCalendar(QStringLiteral("coll-1"),
                                   QStringLiteral("projects"), props));

    QCOMPARE(server.requestCount("PROPPATCH"), 1);
    QCOMPARE(updatedSpy.count(), 1);
    // Success refreshes the local color cache.
    QCOMPARE(backend.calendarColor(QStringLiteral("projects")), red);
}

void TstRemoteCalendarBackendWritePaths::deleteCalendar_204_unregisters_then_404_returns_false()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));

    QVERIFY(backend.createCalendar(QStringLiteral("coll-1"),
                                   QStringLiteral("projects"),
                                   QStringLiteral("Projects")));

    QSignalSpy deletedSpy(&backend, &RemoteCalendarBackend::calendarDeleted);

    QVERIFY(backend.deleteCalendar(QStringLiteral("coll-1"),
                                   QStringLiteral("projects")));
    QCOMPARE(deletedSpy.count(), 1);
    QVERIFY(backend.discoveredUrl(QStringLiteral("projects")).isEmpty());

    // Second delete: the collection is gone server-side -> 404 -> false.
    QVERIFY(!backend.deleteCalendar(QStringLiteral("coll-1"),
                                    QStringLiteral("projects")));
}

// ---------------------------------------------------------------------------
// Defect 2 — thread-affinity of the operation-based API.
//
// The SyncEngine drives a backend's BlobBackend CRUD (loadRecords/createRecord/
// ...) from a worker thread, which in turn calls the op-based API (fetchItems/
// pushItems/deleteItems). The backend object itself lives on the owning (main)
// thread. The operation must end up affiliated with the backend's thread — its
// completion lambda (QMetaObject::invokeMethod(this, ...)) runs there and emits
// `finished` from there — so creating it with `this` as parent across the
// thread boundary is illegal ("QObject: Cannot create children for a parent
// that is in a different thread") and strands the op on the caller's thread.
//
// Each test constructs the op-API call on a worker thread and asserts the
// returned operation lives on the backend's thread. An empty base URL makes
// davUrlFor() return nullopt, so the early-fail path is taken (no network) —
// but the operation object is still constructed first, which is the site under
// test. The base destructor does not touch m_pendingOperations, so deleting the
// op (or leaking it on the RED early-return) is safe.

namespace {

template <typename Call>
QThread *callerThreadFor(Call &&call)
{
    QThread *worker = QThread::create(std::forward<Call>(call));
    worker->start();
    return worker;
}

} // namespace

void TstRemoteCalendarBackendWritePaths::fetchItems_fromWorkerThread_opLivesOnBackendThread()
{
    RemoteCalendarBackend backend{QUrl(), QString(), QString()};

    FetchOperation *op = nullptr;
    QThread *worker = callerThreadFor([&]() {
        op = backend.fetchItems(QStringLiteral("worker-cal"));
    });
    QVERIFY(worker->wait(5000));
    delete worker;

    QVERIFY(op != nullptr);
    QCOMPARE(op->thread(), backend.thread());

    delete op;
}

void TstRemoteCalendarBackendWritePaths::pushItems_fromWorkerThread_opLivesOnBackendThread()
{
    RemoteCalendarBackend backend{QUrl(), QString(), QString()};

    const QList<KCalendarCore::Incidence::Ptr> items{
        makeEvent(QStringLiteral("worker-push-1"), QStringLiteral("Pushed"))};

    PushOperation *op = nullptr;
    QThread *worker = callerThreadFor([&]() {
        op = backend.pushItems(QStringLiteral("worker-cal"), items);
    });
    QVERIFY(worker->wait(5000));
    delete worker;

    QVERIFY(op != nullptr);
    QCOMPARE(op->thread(), backend.thread());

    delete op;
}

void TstRemoteCalendarBackendWritePaths::deleteItems_fromWorkerThread_opLivesOnBackendThread()
{
    RemoteCalendarBackend backend{QUrl(), QString(), QString()};

    DeleteOperation *op = nullptr;
    QThread *worker = callerThreadFor([&]() {
        op = backend.deleteItems(QStringLiteral("worker-cal"),
                                 QStringList{QStringLiteral("worker-del-1")});
    });
    QVERIFY(worker->wait(5000));
    delete worker;

    QVERIFY(op != nullptr);
    QCOMPARE(op->thread(), backend.thread());

    delete op;
}

QTEST_GUILESS_MAIN(TstRemoteCalendarBackendWritePaths)
#include "tst_remotecalendarbackend_writepaths.moc"
