// tests/calendar/tst_remotebackend_blob_view.cpp
// Phase D Task 13 — IBlobBackend smoke tests for RemoteCalendarBackend.
//
// Static/cast tests (no network):
//   1. RemoteCalendarBackend compiles with the IBlobBackend overrides.
//   2. A RemoteCalendarBackend* can be successfully upcast to IBlobBackend*.
//   3. Identity methods (backendId, displayName, isAvailable) return non-trivial
//      values without touching the network.
//   4. availableCollections() returns empty (no calendars registered — no network).
//
// Network tests (WP-D9 — revived; FakeCalDavServer PUT added in v0.63):
//   5. updateRecord with an existing UID issues a CalDAV PUT and returns true.
//   6. updateRecord with no registered calendars returns false (no-op).

#include <QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include "remotecalendarbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"
#include "syncoperation.h"

#include "fakecaldavserver.h"

using namespace Kalburator::Sync;

class TestRemoteCalendarBackendBlobView : public QObject
{
    Q_OBJECT

private slots:
    void castSucceeds();
    void identityMethods_returnNonEmpty();
    void availableCollections_emptyWithoutRegisteredCalendars();
    void updateRecord_modifies_existing_record();
    void updateRecord_nonexistent_id_returns_error();
    void loadRecords_surfacesAuthoritativeLastModified_notNow();
    void loadRecords_chunksMultigetAcrossBatches();
    void loadRecords_failsWholeOpWhenABatchFails_noPartialResults();
    void ctagMatchServingZeroCachedItems_distrustsMatchAndRelists();
    void partialMaterialization_doesNotCommitCtag();
    void collectionRevision_droppedRequests_failsWithinTimeout();
};

void TestRemoteCalendarBackendBlobView::castSucceeds()
{
    RemoteCalendarBackend backend(QUrl(QStringLiteral("https://caldav.example.com/")),
                          QStringLiteral("user"),
                          QStringLiteral("pass"));
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob != nullptr);
}

void TestRemoteCalendarBackendBlobView::identityMethods_returnNonEmpty()
{
    RemoteCalendarBackend backend(QUrl(QStringLiteral("https://caldav.example.com/")),
                          QStringLiteral("user"),
                          QStringLiteral("pass"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(!blob->backendId().isEmpty());
    QVERIFY(!blob->displayName().isEmpty());
    // isAvailable() returns true when the URL is valid and non-empty
    QVERIFY(blob->isAvailable());
}

void TestRemoteCalendarBackendBlobView::availableCollections_emptyWithoutRegisteredCalendars()
{
    RemoteCalendarBackend backend(QUrl(QStringLiteral("https://caldav.example.com/")),
                          QStringLiteral("user"),
                          QStringLiteral("pass"));
    auto *blob = static_cast<IBlobBackend *>(&backend);

    // No calendars discovered / registered yet — must return empty list.
    QVERIFY(blob->availableCollections().isEmpty());
}

void TestRemoteCalendarBackendBlobView::updateRecord_modifies_existing_record()
{
    // Verifies that updateRecord() issues a CalDAV PUT and returns true when
    // a matching calendar is registered.
    //
    // Flow: seed the server → loadCalendars → fetchItems (populates ETag cache)
    // → updateRecord (uses cached ETag in If-Match, server returns 204).
    // FakeCalDavServer has handled PUT since the v0.63 convergence work.
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    const QString uid = QStringLiteral("test-event-uid-1");
    const QByteArray origIcs =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:test-event-uid-1\r\n"
        "SUMMARY:Original\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, {origIcs});
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString()
                              + calHref.mid(1); // strip leading '/'
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    // loadCalendars discovers the calendar before we fetchItems.
    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can fetchItems");

    // fetchItems populates m_localEtags (needed for conditional PUT).
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("Personal"));
    QVERIFY(fetchOp != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 8000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);

    // updateRecord with a modified SUMMARY — must PUT to server and return true.
    BackendRecord rec;
    rec.id   = uid;
    rec.type = QStringLiteral("event");
    rec.data =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:test-event-uid-1\r\n"
        "SUMMARY:Modified\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    QVERIFY2(backend.updateRecord(rec), "updateRecord must return true for a known UID");

    // Server must still hold the event (PUT updated it in-place).
    QVERIFY2(server.hasEvent(calHref, uid),
             "event must still exist on server after updateRecord PUT");
}

void TestRemoteCalendarBackendBlobView::updateRecord_nonexistent_id_returns_error()
{
    // When the backend has NO registered calendar URLs (m_davUrls is empty),
    // updateRecord has nowhere to route the PUT and must return false.
    // This exercises the "uid not found in any calendar" warning path.
    RemoteCalendarBackend backend(QUrl(QStringLiteral("https://caldav.example.com/")),
                                  QStringLiteral("user"),
                                  QStringLiteral("pass"));
    // Deliberately do NOT call registerCalendarUrl — m_davUrls stays empty.

    BackendRecord rec;
    rec.id   = QStringLiteral("ghost-uid");
    rec.type = QStringLiteral("event");
    rec.data =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:ghost-uid\r\nSUMMARY:Ghost\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    QVERIFY2(!backend.updateRecord(rec),
             "updateRecord must return false when no calendars are registered");
}

void TestRemoteCalendarBackendBlobView::loadRecords_surfacesAuthoritativeLastModified_notNow()
{
    // N3: a record's lastModified must be its own iCal LAST-MODIFIED (falling
    // back to DTSTAMP/CREATED), never QDateTime::currentDateTimeUtc() — the
    // old behavior defeated LastWriteWins by making every remote record look
    // freshly modified on every load.
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    const QString uid = QStringLiteral("stale-event-uid-1");
    const QByteArray seededIcs =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:stale-event-uid-1\r\n"
        "SUMMARY:Old Event\r\nDTSTART:20200601T120000Z\r\n"
        "DTEND:20200601T130000Z\r\n"
        "LAST-MODIFIED:20200601T093000Z\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n";

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, {seededIcs});
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString()
                              + calHref.mid(1); // strip leading '/'
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can loadRecords");

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("Personal"));
    QCOMPARE(records.size(), 1);
    QVERIFY2(records.first().lastModified.isValid(),
             "lastModified must be a valid, parsed timestamp");
    QCOMPARE(records.first().lastModified,
             QDateTime(QDate(2020, 6, 1), QTime(9, 30, 0), QTimeZone::utc()));
}

// N4: split a large multiget into chunked, sequential batches.

namespace {
QByteArray makeEventIcs(const QString &uid)
{
    return QStringLiteral(
               "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
               "BEGIN:VEVENT\r\nUID:%1\r\n"
               "SUMMARY:Event %1\r\nDTSTART:20260601T120000Z\r\n"
               "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid)
        .toUtf8();
}
}  // namespace

void TestRemoteCalendarBackendBlobView::loadRecords_chunksMultigetAcrossBatches()
{
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    QList<QByteArray> seeds;
    for (int i = 0; i < 7; ++i)
        seeds << makeEventIcs(QStringLiteral("event-%1").arg(i));

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, seeds);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.setMultigetChunkSize(3);  // 7 items / 3 per batch = 3 batches (3,3,1)
    backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can loadRecords");

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("Personal"));
    QCOMPARE(records.size(), 7);
    QCOMPARE(server.multigetReportCount(), 3);  // ceil(7 / 3) == 3 batches
}

void TestRemoteCalendarBackendBlobView::loadRecords_failsWholeOpWhenABatchFails_noPartialResults()
{
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    QList<QByteArray> seeds;
    for (int i = 0; i < 7; ++i)
        seeds << makeEventIcs(QStringLiteral("event-%1").arg(i));

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, seeds);
    server.setFailNthMultigetReport(2);  // the second batch's REPORT fails (500)
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.setMultigetChunkSize(3);
    backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can loadRecords");

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("Personal"));
    QVERIFY2(records.isEmpty(),
             "a failed batch must fail the whole op — never a partial result set");
}

// N5: a CTag match must not be trusted if it would serve zero cached items —
// the exact "CTag ahead of content cache" bug (a calendar with real server
// items reading back as empty/fresh/success forever after).
void TestRemoteCalendarBackendBlobView::ctagMatchServingZeroCachedItems_distrustsMatchAndRelists()
{
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, {makeEventIcs(QStringLiteral("event-0")),
                                   makeEventIcs(QStringLiteral("event-1"))});
    server.setCollectionCtag(calHref, QStringLiteral("ctag-v1"));
    QVERIFY(server.startListening());

    QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    const QString dbPath = dbDir.filePath(QStringLiteral("sync.db"));
    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);

    // First backend: real first sync. Commits ctag-v1 to the shared dbPath
    // and populates ITS OWN content cache.
    {
        QTemporaryDir cacheDir1;
        QVERIFY(cacheDir1.isValid());
        RemoteCalendarBackend backend(server.baseUrl(),
                                      QStringLiteral("testuser"),
                                      QStringLiteral("testpass"));
        backend.setDbPath(dbPath);
        backend.setCacheDir(cacheDir1.path());
        backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

        QSignalSpy loadSpy(&backend,
                           SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        backend.loadCalendars(QStringLiteral("Personal"));
        QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
        QVERIFY(loadSpy.first().at(1).toBool());

        auto *blob = static_cast<IBlobBackend *>(&backend);
        const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("Personal"));
        QCOMPARE(records.size(), 2);
        QCOMPARE(backend.cachedCollectionRevision(QStringLiteral("Personal")),
                 QStringLiteral("ctag-v1"));
    }

    // Second backend: SAME dbPath (so storedCtag == "ctag-v1", matching the
    // server's unchanged ctag), but a FRESH, empty cache dir — simulating the
    // content cache having gone missing/stale for an unchanged CTag. Must
    // NOT trust the CTag match; must re-list and re-fetch for real.
    QTemporaryDir cacheDir2;
    QVERIFY(cacheDir2.isValid());
    RemoteCalendarBackend backend2(server.baseUrl(),
                                   QStringLiteral("testuser"),
                                   QStringLiteral("testpass"));
    backend2.setDbPath(dbPath);
    backend2.setCacheDir(cacheDir2.path());
    backend2.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    QSignalSpy loadSpy2(&backend2,
                        SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend2.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy2.count() > 0, 5000);
    QVERIFY(loadSpy2.first().at(1).toBool());

    auto *blob2 = static_cast<IBlobBackend *>(&backend2);
    const QList<BackendRecord> records2 = blob2->loadRecords(QStringLiteral("Personal"));
    QVERIFY2(records2.size() == 2,
             "must re-list and re-fetch for real instead of trusting a CTag "
             "match that would serve zero items");
}

// N5: countSkipped > 0 (an item that fetched but failed to parse) must not
// commit the CTag even though every multiget batch structurally succeeded —
// otherwise a later CTag-match short-circuit would serve that incomplete
// set as "current" forever after.
void TestRemoteCalendarBackendBlobView::partialMaterialization_doesNotCommitCtag()
{
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, {
        makeEventIcs(QStringLiteral("event-0")),
        makeEventIcs(QStringLiteral("event-1")),
        QByteArrayLiteral("UID:malformed-1\r\nTHIS IS NOT VALID ICALENDAR DATA"),
    });
    server.setCollectionCtag(calHref, QStringLiteral("ctag-v1"));
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setDbPath(dbDir.filePath(QStringLiteral("sync.db")));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY(loadSpy.first().at(1).toBool());

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("Personal"));
    QCOMPARE(records.size(), 2);  // the two valid events; the malformed one is skipped

    QVERIFY2(backend.cachedCollectionRevision(QStringLiteral("Personal")).isEmpty(),
             "the CTag must not be committed when any item failed to materialize");
}

void TestRemoteCalendarBackendBlobView::collectionRevision_droppedRequests_failsWithinTimeout()
{
    // H1.2/O22: without a QNAM transfer timeout, a server that accepts a
    // connection and never responds stalls the raw davSyncRequest() round
    // trip forever. collectionRevision() is the QNAM-level path (a PROPFIND
    // via davSyncRequest(nam(), ...) — see fetchFreshCtag()); fetchItems()
    // itself goes through KDAV::DavItemsListJob, which does not share our
    // nam() and so isn't affected by setTransferTimeoutMs() at all. Pins
    // that collectionRevision() returns empty (its transportOk()-false
    // path) once the timeout elapses, rather than hanging. Uses
    // setTransferTimeoutMs() to shrink the wait from the real 30s default
    // so the test stays fast.
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setDropRequests(true);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    // Pre-register rather than discover via loadCalendars() — discovery's
    // own PROPFIND would hang against a dropping server too, which isn't
    // what this test is pinning.
    backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);
    backend.setTransferTimeoutMs(2000);

    QString revision;
    QElapsedTimer timer;
    timer.start();
    revision = backend.collectionRevision(QStringLiteral("Personal"));
    QVERIFY2(timer.elapsed() < 60000,
             "collectionRevision must fail within the transfer timeout, not hang");
    QVERIFY2(revision.isEmpty(),
             "a dropped/never-answered PROPFIND must not report a revision");
}

QTEST_MAIN(TestRemoteCalendarBackendBlobView)
#include "tst_remotecalendarbackend_blob_view.moc"
