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

#include <KCalendarCore/Event>

#include "remotecalendarbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"
#include "syncoperation.h"
#include "recordidentity.h"
#include "writeoperation.h"
#include "writerbatch.h"

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
    void updateRecord_multiCalendar_ownershipMiss_doesNotGuessWrite();
    void updateRecord_concurrentServerEdit_surfaces412_noSilentOverwrite();
    void updateRecord_after412_nextFetch_detectsConcurrentChange();
    void loadRecords_surfacesAuthoritativeLastModified_notNow();
    void loadRecords_chunksMultigetAcrossBatches();
    void loadRecords_failsWholeOpWhenABatchFails_noPartialResults();
    void ctagMatchServingZeroCachedItems_distrustsMatchAndRelists();
    void partialMaterialization_doesNotCommitCtag();
    void collectionRevision_droppedRequests_failsWithinTimeout();
    void fetchItems_droppedRequests_failsWithinTimeout();
    void pushItems_droppedRequests_failsWithinTimeout();

    // VP.c-step-1b — detached exceptions as distinct blob records.
    void detachedException_masterAndExceptionAreTwoRecords();
    void detachedException_applyRecordsUpdate_targetsExceptionHref();
    void detachedException_deleteRecord_removesOnlyExceptionHref();
    void detachedException_refetchAfterWrite_keepsIdsStable();
    void detachedException_loadRecord_bareUidServesMaster();
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
    server.setCalendars({{QStringLiteral("personal"), calHref}});
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
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    // loadCalendars discovers the calendar before we fetchItems.
    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can fetchItems");

    // fetchItems populates m_localEtags (needed for conditional PUT).
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("personal"));
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

void TestRemoteCalendarBackendBlobView::updateRecord_multiCalendar_ownershipMiss_doesNotGuessWrite()
{
    // O32: with multiple registered calendars and a uid that lives in NONE of
    // them (no ETag-map hit, no content-cache hit), updateRecord must FAIL
    // rather than guess by PUTting into the first registered calendar. The
    // deleted try-all fallback used to "succeed" here by writing the record
    // into a calendar that never owned it.
    const QString personalHref = QStringLiteral("/calendars/testuser/personal/");
    const QString workHref = QStringLiteral("/calendars/testuser/work/");

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("personal"), personalHref},
                         {QStringLiteral("work"), workHref}});
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"),
                               server.baseUrl().toString() + personalHref.mid(1));
    backend.registerCalendarUrl(QStringLiteral("work"),
                               server.baseUrl().toString() + workHref.mid(1));

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(), "loadCalendars must succeed");

    BackendRecord rec;
    rec.id   = QStringLiteral("nobody-owns-me");
    rec.type = QStringLiteral("event");
    rec.data =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:nobody-owns-me\r\nSUMMARY:Guess\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    QVERIFY2(!backend.updateRecord(rec),
             "updateRecord must fail when no registered calendar owns the uid");
    QCOMPARE(server.requestCount("PUT"), 0);
    QVERIFY2(!server.hasEvent(personalHref, rec.id),
             "must not have guess-written into Personal");
    QVERIFY2(!server.hasEvent(workHref, rec.id),
             "must not have guess-written into Work");
}

void TestRemoteCalendarBackendBlobView::updateRecord_concurrentServerEdit_surfaces412_noSilentOverwrite()
{
    // O32: setRawIcs already sends If-Match with the cached ETag — this pins
    // that a real precondition failure (someone else edited the item on the
    // server since our last fetch) surfaces as a FAILED updateRecord, never
    // a silent overwrite or an auto-force retry (that auto-force is confined
    // to the user-resolved-conflict startSync path, not this steady-state
    // blob path).
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    const QString uid = QStringLiteral("contested-uid");
    const QByteArray origIcs =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:contested-uid\r\n"
        "SUMMARY:Original\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("personal"), calHref}});
    server.setSeedEvents(calHref, {origIcs});
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(), "loadCalendars must succeed");

    // fetchItems populates the backend's cached ETag for the seeded item.
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("personal"));
    QVERIFY(fetchOp != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 8000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);

    // Someone else edits the item directly on the server — bumps its ETag
    // out from under our cached copy (out-of-band, no PUT from this backend).
    const QByteArray concurrentIcs =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:contested-uid\r\n"
        "SUMMARY:Edited By Someone Else\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    server.setSeedEvents(calHref, {concurrentIcs});

    // Our stale-ETag update must fail — the server rejects the PUT (412).
    BackendRecord rec;
    rec.id   = uid;
    rec.type = QStringLiteral("event");
    rec.data =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:contested-uid\r\n"
        "SUMMARY:My Local Change\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    QVERIFY2(!backend.updateRecord(rec),
             "a stale-ETag update must fail on 412, never auto-force");

    // The concurrent edit must survive untouched — no silent overwrite.
    const QList<QByteArray> stored = server.storedEvents(calHref);
    QCOMPARE(stored.size(), 1);
    QVERIFY2(stored.first().contains("Edited By Someone Else"),
             "the concurrent server edit must not be clobbered by our stale PUT");
}

void TestRemoteCalendarBackendBlobView::updateRecord_after412_nextFetch_detectsConcurrentChange()
{
    // Companion to the 412 test above: after our push loses the race, the
    // NEXT fetch must surface the concurrent edit (the engine's next sync
    // cycle re-diffs against it) rather than silently keeping our stale
    // local view.
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    const QString uid = QStringLiteral("contested-uid-2");
    const QByteArray origIcs =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:contested-uid-2\r\n"
        "SUMMARY:Original\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("personal"), calHref}});
    server.setSeedEvents(calHref, {origIcs});
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(), "loadCalendars must succeed");

    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("personal"));
    QVERIFY(fetchOp != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 8000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);

    const QByteArray concurrentIcs =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:contested-uid-2\r\n"
        "SUMMARY:Edited By Someone Else\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    server.setSeedEvents(calHref, {concurrentIcs});

    BackendRecord rec;
    rec.id   = uid;
    rec.type = QStringLiteral("event");
    rec.data =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
        "BEGIN:VEVENT\r\nUID:contested-uid-2\r\n"
        "SUMMARY:My Local Change\r\nDTSTART:20260601T120000Z\r\n"
        "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    QVERIFY2(!backend.updateRecord(rec), "the stale-ETag update must fail on 412");

    // Next fetch cycle: must pick up the concurrent server-side content.
    FetchOperation *refetchOp = backend.fetchItems(QStringLiteral("personal"));
    QVERIFY(refetchOp != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(refetchOp->isFinished(), 8000);
    QCOMPARE(refetchOp->state(), SyncOperation::Succeeded);

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 1);
    QVERIFY2(records.first().data.contains("Edited By Someone Else"),
             "the next fetch must surface the concurrent edit, not our stale local view");
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
    server.setCalendars({{QStringLiteral("personal"), calHref}});
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
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can loadRecords");

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
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

// VP.c-step-1b fixtures: a recurring series' MASTER and one DETACHED
// EXCEPTION, delivered the way a real CalDAV server stores them — as two
// SEPARATE resources (hrefs) that share the RFC 5545 UID. The exception's
// own resource carries the RECURRENCE-ID line.
QByteArray makeSeriesMasterIcs(const QString &uid)
{
    return QStringLiteral(
               "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
               "BEGIN:VEVENT\r\nUID:%1\r\n"
               "SUMMARY:Series master %1\r\n"
               "DTSTART:20260601T120000Z\r\n"
               "RRULE:FREQ=DAILY\r\n"
               "END:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid)
        .toUtf8();
}

QByteArray makeSeriesExceptionIcs(const QString &uid, const QString &summarySuffix)
{
    return QStringLiteral(
               "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
               "BEGIN:VEVENT\r\nUID:%1\r\n"
               "RECURRENCE-ID:20260602T090000Z\r\n"
               "SUMMARY:Series override %1%2\r\n"
               "DTSTART:20260602T110000Z\r\n"
               "END:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid, summarySuffix)
        .toUtf8();
}

// The UTC recurrence instant the exception fixture expresses — the composite
// record id normalizes to this regardless of the parse-time zone form.
QDateTime seriesExceptionRecurrenceId()
{
    return QDateTime(QDate(2026, 6, 2), QTime(9, 0, 0), QTimeZone::utc());
}

// Seed a master + detached-exception pair at their own hrefs in one
// collection. Returns the collection-relative paths (for request-path
// assertions) and the expected composite exception record id.
struct DetachedExceptionFixture {
    QString calHref;
    QString masterPath;
    QString excPath;
    QString excRecordId;
};
DetachedExceptionFixture seedMasterAndException(FakeCalDavServer &server,
                                                const QString &uid)
{
    DetachedExceptionFixture fx;
    fx.calHref = QStringLiteral("/calendars/testuser/personal/");
    fx.masterPath = fx.calHref + QStringLiteral("series-master.ics");
    fx.excPath = fx.calHref + QStringLiteral("series-exc.ics");
    fx.excRecordId = composeRecordIdentity(uid, seriesExceptionRecurrenceId());
    server.setCalendars({{QStringLiteral("personal"), fx.calHref}});
    server.setSeedEventAt(fx.calHref, QStringLiteral("series-master"),
                          makeSeriesMasterIcs(uid));
    server.setSeedEventAt(fx.calHref, QStringLiteral("series-exc"),
                          makeSeriesExceptionIcs(uid, QString()));
    return fx;
}
}  // namespace

void TestRemoteCalendarBackendBlobView::loadRecords_chunksMultigetAcrossBatches()
{
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    QList<QByteArray> seeds;
    for (int i = 0; i < 7; ++i)
        seeds << makeEventIcs(QStringLiteral("event-%1").arg(i));

    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("personal"), calHref}});
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
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can loadRecords");

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
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
    server.setCalendars({{QStringLiteral("personal"), calHref}});
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
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY2(loadSpy.first().at(1).toBool(),
             "loadCalendars must succeed before we can loadRecords");

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
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
    server.setCalendars({{QStringLiteral("personal"), calHref}});
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
        backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

        QSignalSpy loadSpy(&backend,
                           SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        backend.loadCalendars(QStringLiteral("personal"));
        QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
        QVERIFY(loadSpy.first().at(1).toBool());

        auto *blob = static_cast<IBlobBackend *>(&backend);
        const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
        QCOMPARE(records.size(), 2);
        QCOMPARE(backend.cachedCollectionRevision(QStringLiteral("personal")),
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
    backend2.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy2(&backend2,
                        SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend2.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy2.count() > 0, 5000);
    QVERIFY(loadSpy2.first().at(1).toBool());

    auto *blob2 = static_cast<IBlobBackend *>(&backend2);
    const QList<BackendRecord> records2 = blob2->loadRecords(QStringLiteral("personal"));
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
    server.setCalendars({{QStringLiteral("personal"), calHref}});
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
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY(loadSpy.first().at(1).toBool());

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 2);  // the two valid events; the malformed one is skipped

    QVERIFY2(backend.cachedCollectionRevision(QStringLiteral("personal")).isEmpty(),
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
    server.setCalendars({{QStringLiteral("personal"), calHref}});
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
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);
    backend.setTransferTimeoutMs(2000);

    QString revision;
    QElapsedTimer timer;
    timer.start();
    revision = backend.collectionRevision(QStringLiteral("personal"));
    QVERIFY2(timer.elapsed() < 60000,
             "collectionRevision must fail within the transfer timeout, not hang");
    QVERIFY2(revision.isEmpty(),
             "a dropped/never-answered PROPFIND must not report a revision");
}

void TestRemoteCalendarBackendBlobView::fetchItems_droppedRequests_failsWithinTimeout()
{
    // H5.5/O25: fetchItems() runs its item-listing traffic through
    // KDAV::DavItemsListJob on KDAV's own internal network stack, which
    // H1.2's setTransferTimeout() (on our nam()) never touches. Against a
    // server that accepts the connection and never answers, the list job
    // hangs forever, the FetchOperation never settles, and the engine's
    // fetch gate wedges (O22). The per-job watchdog must fail the op within
    // the transfer-timeout window. Pre-H5.5 this HANGS (capped by the QTRY
    // bound → isFinished() stays false → RED).
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("personal"), calHref}});
    server.setDropRequests(true);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    // Pre-register (no discovery) — a fresh backend has no stored CTag, so
    // fetchItems() skips the nam()-level CTag PROPFIND and goes straight to
    // the KDAV list job, which is exactly the surface O25 is about.
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);
    backend.setTransferTimeoutMs(2000);

    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("personal"));
    QVERIFY(fetchOp != nullptr);
    // ~3x the 2000ms watchdog window: comfortably covers a single killed
    // list job while still failing fast if the op wedges.
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 7000);
    QCOMPARE(fetchOp->state(), SyncOperation::Failed);
    QVERIFY2(!fetchOp->errorString().isEmpty(),
             "a timed-out KDAV list job must surface a non-empty error");
}

void TestRemoteCalendarBackendBlobView::pushItems_droppedRequests_failsWithinTimeout()
{
    // H5.5/O25: the write path (DavItemCreateJob) shares the same KDAV
    // network stack and the same wedge. A push against a dropping server
    // must settle Failed within the watchdog window rather than hang.
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("personal"), calHref}});
    server.setDropRequests(true);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);
    backend.setTransferTimeoutMs(2000);

    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
    event->setUid(QStringLiteral("h55-push-uid-1"));
    event->setSummary(QStringLiteral("Watchdog push"));
    event->setDtStart(QDateTime(QDate(2026, 7, 5), QTime(12, 0), QTimeZone::utc()));

    PushOperation *pushOp =
        backend.pushItems(QStringLiteral("personal"),
                          {event.staticCast<KCalendarCore::Incidence>()});
    QVERIFY(pushOp != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(pushOp->isFinished(), 7000);
    QCOMPARE(pushOp->state(), SyncOperation::Failed);
}

// ============================================================================
// VP.c-step-1b — detached exceptions as distinct blob records.
//
// A real CalDAV server stores a recurring series' master and each detached
// exception as SEPARATE resources (separate hrefs/etags) sharing one RFC
// 5545 UID. The blob pipeline keys records by the COMPOSITE identity
// (uid\x01<UTC-ISO recurrenceId>, src/sync/recordidentity.h) so the second
// resource no longer clobbers the first (the old last-block-wins behavior).
// ============================================================================

void TestRemoteCalendarBackendBlobView::detachedException_masterAndExceptionAreTwoRecords()
{
    const QString uid = QStringLiteral("series-1");
    FakeCalDavServer server;
    const DetachedExceptionFixture fx = seedMasterAndException(server, uid);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + fx.calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY(loadSpy.first().at(1).toBool());

    // fetchItems itself must surface both incidences — one per delivered href.
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("personal"));
    QVERIFY(fetchOp != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 8000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    int masterIncidences = 0;
    int exceptionIncidences = 0;
    for (const auto &inc : fetchOp->fetchedItems()) {
        if (!inc || inc->uid() != uid) continue;
        if (inc->hasRecurrenceId()) ++exceptionIncidences;
        else ++masterIncidences;
    }
    QCOMPARE(masterIncidences, 1);
    QCOMPARE(exceptionIncidences, 1);

    // The blob-record view must mint TWO records: master id == bare uid,
    // exception id == composed identity — no last-block-wins collision.
    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 2);

    const BackendRecord *masterRec = nullptr;
    const BackendRecord *excRec = nullptr;
    for (const auto &rec : records) {
        if (rec.id == uid) {
            masterRec = &rec;
        } else if (isExceptionRecordId(rec.id)) {
            excRec = &rec;
        }
    }
    QVERIFY2(masterRec, "the master record must keep the bare-uid record id");
    QVERIFY2(excRec, "the exception must mint a composite record id");
    QCOMPARE(excRec->id, fx.excRecordId);
    QVERIFY2(masterRec->id != excRec->id,
             "master and exception must be distinct records");

    // Both payloads preserved, each from its own resource.
    QVERIFY2(masterRec->data.contains("SUMMARY:Series master series-1"),
             "the master record must carry the master resource's bytes");
    QVERIFY2(!masterRec->data.contains("RECURRENCE-ID"),
             "the master record's payload must not carry a RECURRENCE-ID");
    QVERIFY2(excRec->data.contains("RECURRENCE-ID:20260602T090000Z"),
             "the exception record must retain its RECURRENCE-ID line");
    QVERIFY2(excRec->data.contains("SUMMARY:Series override series-1"),
             "the exception record must carry the exception resource's bytes");
    QVERIFY2(masterRec->contentHash != excRec->contentHash,
             "two distinct resources must not hash to identical record content");
}

void TestRemoteCalendarBackendBlobView::detachedException_applyRecordsUpdate_targetsExceptionHref()
{
    const QString uid = QStringLiteral("series-2");
    FakeCalDavServer server;
    const DetachedExceptionFixture fx = seedMasterAndException(server, uid);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + fx.calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY(loadSpy.first().at(1).toBool());

    // First fetch populates the composite-id->URL map and per-href etags.
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QCOMPARE(blob->loadRecords(QStringLiteral("personal")).size(), 2);

    // Edit the EXCEPTION through the blob write path (the engine's
    // steady-state route). Addressing the exception's COMPOSITE id must PUT
    // to the exception's own href, not the master's.
    BackendRecord rec;
    rec.id = fx.excRecordId;
    rec.type = QStringLiteral("calendar");
    rec.data = makeSeriesExceptionIcs(uid, QStringLiteral(" (edited)"));

    WriterBatch batch;
    batch.updates.append(rec);
    WriteOperation *op = backend.applyRecords(QStringLiteral("personal"), batch);
    QVERIFY(op != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 8000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QVERIFY2(op->succeededUids().contains(fx.excRecordId),
             "the exception record id must be reported as succeeded");

    const QStringList putPaths = server.requestPaths(QByteArrayLiteral("PUT"));
    QCOMPARE(putPaths.size(), 1);
    QCOMPARE(putPaths.first(), fx.excPath);

    // Master resource untouched; exception updated in place.
    QVERIFY2(server.hasEvent(fx.calHref, uid), "the series must still exist");
    const QList<QByteArray> stored = server.storedEvents(fx.calHref);
    QCOMPARE(stored.size(), 2);
    bool masterUnchanged = false;
    bool exceptionEdited = false;
    for (const QByteArray &data : stored) {
        if (data.contains("SUMMARY:Series master series-2")) masterUnchanged = true;
        if (data.contains("SUMMARY:Series override series-2 (edited)")) exceptionEdited = true;
    }
    QVERIFY2(masterUnchanged, "the master's bytes must be untouched by the exception PUT");
    QVERIFY2(exceptionEdited, "the exception's bytes must reflect the update");
}

void TestRemoteCalendarBackendBlobView::detachedException_deleteRecord_removesOnlyExceptionHref()
{
    const QString uid = QStringLiteral("series-3");
    FakeCalDavServer server;
    const DetachedExceptionFixture fx = seedMasterAndException(server, uid);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + fx.calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY(loadSpy.first().at(1).toBool());

    auto *blob = static_cast<IBlobBackend *>(&backend);
    QCOMPARE(blob->loadRecords(QStringLiteral("personal")).size(), 2);

    // Delete ONLY the exception record — must DELETE the exception's href.
    QVERIFY2(backend.deleteRecord(fx.excRecordId),
             "deleteRecord must succeed for the exception record id");
    const QStringList delPaths = server.requestPaths(QByteArrayLiteral("DELETE"));
    QCOMPARE(delPaths.size(), 1);
    QCOMPARE(delPaths.first(), fx.excPath);

    // The master survives and still owns the UID; the exception href is gone.
    QVERIFY2(server.hasEvent(fx.calHref, uid),
             "the master must still own the UID after the exception is deleted");
    const QList<QByteArray> stored = server.storedEvents(fx.calHref);
    QCOMPARE(stored.size(), 1);
    QVERIFY2(stored.first().contains("SUMMARY:Series master series-3"),
             "only the master resource must remain on the server");

    // Refetch now reports exactly one record — the bare-uid master.
    const QList<BackendRecord> records = blob->loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().id, uid);
}

void TestRemoteCalendarBackendBlobView::detachedException_refetchAfterWrite_keepsIdsStable()
{
    const QString uid = QStringLiteral("series-4");
    FakeCalDavServer server;
    const DetachedExceptionFixture fx = seedMasterAndException(server, uid);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + fx.calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY(loadSpy.first().at(1).toBool());

    auto *blob = static_cast<IBlobBackend *>(&backend);
    const QList<BackendRecord> first = blob->loadRecords(QStringLiteral("personal"));
    QCOMPARE(first.size(), 2);
    QString firstMasterHash;
    for (const auto &rec : first) {
        if (rec.id == uid) firstMasterHash = rec.contentHash;
    }
    QVERIFY2(!firstMasterHash.isEmpty(), "the master record must be present");

    // Push an edit to the exception through the blob write path.
    BackendRecord rec;
    rec.id = fx.excRecordId;
    rec.data = makeSeriesExceptionIcs(uid, QStringLiteral(" (edited)"));
    WriterBatch batch;
    batch.updates.append(rec);
    WriteOperation *op = backend.applyRecords(QStringLiteral("personal"), batch);
    QVERIFY(op != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 8000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    // Refetch: both ids must be byte-identical to the first pass — a record
    // id depends only on uid + recurrence-id, never on write order.
    const QList<BackendRecord> second = blob->loadRecords(QStringLiteral("personal"));
    QCOMPARE(second.size(), 2);
    const BackendRecord *masterRec = nullptr;
    const BackendRecord *excRec = nullptr;
    for (const auto &rec : second) {
        if (rec.id == uid) {
            masterRec = &rec;
        } else if (isExceptionRecordId(rec.id)) {
            excRec = &rec;
        }
    }
    QVERIFY2(masterRec && excRec, "both records must survive the refetch");
    QCOMPARE(excRec->id, fx.excRecordId);
    QVERIFY2(excRec->data.contains("SUMMARY:Series override series-4 (edited)"),
             "the refetch must surface the edited exception bytes");
    QVERIFY2(masterRec->data.contains("SUMMARY:Series master series-4"),
             "the master record's payload must be unchanged");
    QCOMPARE(masterRec->contentHash, firstMasterHash);
}

void TestRemoteCalendarBackendBlobView::detachedException_loadRecord_bareUidServesMaster()
{
    const QString uid = QStringLiteral("series-5");
    FakeCalDavServer server;
    const DetachedExceptionFixture fx = seedMasterAndException(server, uid);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    const QString calDavUrl = server.baseUrl().toString() + fx.calHref.mid(1);
    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.registerCalendarUrl(QStringLiteral("personal"), calDavUrl);

    QSignalSpy loadSpy(&backend,
                       SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend.loadCalendars(QStringLiteral("personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
    QVERIFY(loadSpy.first().at(1).toBool());

    auto *blob = static_cast<IBlobBackend *>(&backend);
    QCOMPARE(blob->loadRecords(QStringLiteral("personal")).size(), 2);

    // Bare master uid still resolves to the master record.
    const auto master = backend.loadRecord(uid);
    QVERIFY(master.has_value());
    QCOMPARE(master->id, uid);
    QVERIFY2(master->data.contains("SUMMARY:Series master series-5"),
             "loadRecord(bare uid) must serve the master's own bytes");

    // Composite exception id resolves to the exception record.
    const auto exc = backend.loadRecord(fx.excRecordId);
    QVERIFY(exc.has_value());
    QCOMPARE(exc->id, fx.excRecordId);
    QVERIFY2(exc->data.contains("RECURRENCE-ID:20260602T090000Z"),
             "loadRecord(composite id) must serve the exception's own bytes");
}

QTEST_MAIN(TestRemoteCalendarBackendBlobView)
#include "tst_remotecalendarbackend_blob_view.moc"
