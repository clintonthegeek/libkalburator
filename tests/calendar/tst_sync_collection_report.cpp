// E7 (FINDINGS O36) — RFC 6578 sync-collection REPORT support in the CalDAV
// backend, replacing the O(collection-size) Depth:1 PROPFIND/listing delta
// enumeration with a server-computed delta (changed + deleted/tombstoned
// hrefs since a stored sync-token), while keeping the existing CTag+PROPFIND
// path as a permanent fallback for servers that don't advertise the
// capability. Covers §10's five RED scenarios (a)-(e).

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "remotecalendarbackend.h"
#include "iblobbackend.h"
#include "backendrecord.h"
#include "syncoperation.h"

#include "fakecaldavserver.h"

using namespace Kalburator::Sync;

namespace {
QByteArray makeEventIcs(const QString &uid, const QString &summary)
{
    return QStringLiteral(
               "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
               "BEGIN:VEVENT\r\nUID:%1\r\n"
               "SUMMARY:%2\r\nDTSTART:20260601T120000Z\r\n"
               "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid, summary)
        .toUtf8();
}

// Shared fixture: a capability-advertising server seeded with three events,
// wired to a backend that has already completed its first full sync (and,
// once E7 lands, bootstrapped a sync-token). Returns the backend by pointer
// (caller owns via unique_ptr) so tests can drive further cycles against it.
struct Fixture {
    FakeCalDavServer server;
    QTemporaryDir cacheDir;
    QTemporaryDir dbDir;
    QString calHref = QStringLiteral("/calendars/testuser/personal/");
    QString dbPath;
    QString calDavUrl;
    std::unique_ptr<RemoteCalendarBackend> backend;

    bool setUp(bool supportsSyncCollection = true)
    {
        server.setCalendars({{QStringLiteral("Personal"), calHref}});
        server.setSupportsSyncCollection(supportsSyncCollection);
        server.setSeedEvents(calHref, {
            makeEventIcs(QStringLiteral("event-0"), QStringLiteral("Event 0")),
            makeEventIcs(QStringLiteral("event-1"), QStringLiteral("Event 1")),
            makeEventIcs(QStringLiteral("event-2"), QStringLiteral("Event 2")),
        });
        server.setCollectionCtag(calHref, QStringLiteral("ctag-v1"));
        if (!server.startListening()) return false;
        if (!cacheDir.isValid() || !dbDir.isValid()) return false;

        dbPath = dbDir.filePath(QStringLiteral("sync.db"));
        calDavUrl = server.baseUrl().toString() + calHref.mid(1);

        backend = std::make_unique<RemoteCalendarBackend>(
            server.baseUrl(), QStringLiteral("testuser"), QStringLiteral("testpass"));
        backend->setDbPath(dbPath);
        backend->setCacheDir(cacheDir.path());
        backend->registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

        QSignalSpy loadSpy(backend.get(),
                           SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        backend->loadCalendars(QStringLiteral("Personal"));
        if (!QTest::qWaitFor([&]() { return loadSpy.count() > 0; }, 5000)) return false;
        if (!loadSpy.first().at(1).toBool()) return false;

        auto *blob = static_cast<IBlobBackend *>(backend.get());
        return blob->loadRecords(QStringLiteral("Personal")).size() == 3;
    }
};
}  // namespace

class TestSyncCollectionReport : public QObject
{
    Q_OBJECT

private slots:
    void steadyState_singleChangedItem_usesReportNotListing();
    void deletion_arrivesAsTombstone_noFullListing();
    void tokenInvalidation_fallsBackAndReacquires();
    void unsupportedServer_behavesLikePreE7();
    void restart_storedToken_oneReportOneItem();
};

// (a) RED: today, every changed-CTag cycle pays a full Depth:1 listing no
// matter how small the actual delta is. Once a sync-collection-capable
// collection has a stored token, a single changed item must cost exactly
// ONE sync-collection REPORT + ONE multiget of ONE href — no listing REPORT.
void TestSyncCollectionReport::steadyState_singleChangedItem_usesReportNotListing()
{
    Fixture fx;
    QVERIFY(fx.setUp());
    auto *blob = static_cast<IBlobBackend *>(fx.backend.get());

    fx.server.setSeedEvents(fx.calHref, {
        makeEventIcs(QStringLiteral("event-0"), QStringLiteral("Event 0 EDITED")),
    });
    fx.server.setCollectionCtag(fx.calHref, QStringLiteral("ctag-v2"));

    const int reportBefore = fx.server.requestCount("REPORT");
    const int multigetBefore = fx.server.multigetReportCount();
    const int syncCollBefore = fx.server.syncCollectionReportCount();

    // Full snapshot must still be 3 — sync-collection returns a delta, but
    // recordsFromLastFetch()'s contract is the full current collection.
    QCOMPARE(blob->loadRecords(QStringLiteral("Personal")).size(), 3);

    QCOMPARE(fx.server.syncCollectionReportCount() - syncCollBefore, 1);
    QCOMPARE(fx.server.multigetReportCount() - multigetBefore, 1);
    // Exactly those two REPORTs this cycle — no calendar-query listing REPORT.
    QCOMPARE(fx.server.requestCount("REPORT") - reportBefore, 2);
}

// (b) RED: a remote deletion must arrive as a tombstone in the
// sync-collection delta — no full listing needed to detect it (today's
// deletion detection depends entirely on the Depth:1 listing).
void TestSyncCollectionReport::deletion_arrivesAsTombstone_noFullListing()
{
    Fixture fx;
    QVERIFY(fx.setUp());
    auto *blob = static_cast<IBlobBackend *>(fx.backend.get());

    fx.server.removeEvent(fx.calHref, QStringLiteral("event-1"));
    fx.server.setCollectionCtag(fx.calHref, QStringLiteral("ctag-v2"));

    const int reportBefore = fx.server.requestCount("REPORT");
    const int multigetBefore = fx.server.multigetReportCount();
    const int syncCollBefore = fx.server.syncCollectionReportCount();

    QCOMPARE(blob->loadRecords(QStringLiteral("Personal")).size(), 2);

    QCOMPARE(fx.server.syncCollectionReportCount() - syncCollBefore, 1);
    QCOMPARE(fx.server.multigetReportCount() - multigetBefore, 0);
    QCOMPARE(fx.server.requestCount("REPORT") - reportBefore, 1);
}

// (c) RED: RFC 6578 §3.3 token invalidation. The fake returns 410 for any
// sync-collection REPORT carrying a token; the cycle must still complete
// correctly via the CTag+listing fallback, AND re-acquire a fresh token so
// the NEXT cycle is back on the REPORT path.
void TestSyncCollectionReport::tokenInvalidation_fallsBackAndReacquires()
{
    Fixture fx;
    QVERIFY(fx.setUp());
    auto *blob = static_cast<IBlobBackend *>(fx.backend.get());

    fx.server.setInvalidateSyncTokens(true);
    fx.server.setSeedEvents(fx.calHref, {
        makeEventIcs(QStringLiteral("event-0"), QStringLiteral("Event 0 EDITED")),
    });
    fx.server.setCollectionCtag(fx.calHref, QStringLiteral("ctag-v2"));

    // Falls back to the listing path and still converges correctly.
    QCOMPARE(blob->loadRecords(QStringLiteral("Personal")).size(), 3);
    QVERIFY(fx.server.multigetReportCount() > 0);

    // Re-enable the server and make one more change: the fallback cycle
    // above must have re-acquired a token, so THIS cycle is back on the
    // REPORT path (one sync-collection REPORT, no listing).
    fx.server.setInvalidateSyncTokens(false);
    fx.server.setSeedEvents(fx.calHref, {
        makeEventIcs(QStringLiteral("event-1"), QStringLiteral("Event 1 EDITED")),
    });
    fx.server.setCollectionCtag(fx.calHref, QStringLiteral("ctag-v3"));

    const int reportBefore = fx.server.requestCount("REPORT");
    const int multigetBefore = fx.server.multigetReportCount();
    const int syncCollBefore = fx.server.syncCollectionReportCount();

    QCOMPARE(blob->loadRecords(QStringLiteral("Personal")).size(), 3);

    QCOMPARE(fx.server.syncCollectionReportCount() - syncCollBefore, 1);
    QCOMPARE(fx.server.multigetReportCount() - multigetBefore, 1);
    QCOMPARE(fx.server.requestCount("REPORT") - reportBefore, 2);
}

// (d) Regression pin: a server that does NOT advertise sync-collection must
// behave byte-identically to pre-E7 — the CTag+listing fallback is
// permanent, never weakened by this phase.
void TestSyncCollectionReport::unsupportedServer_behavesLikePreE7()
{
    Fixture fx;
    QVERIFY(fx.setUp(/*supportsSyncCollection=*/false));
    auto *blob = static_cast<IBlobBackend *>(fx.backend.get());

    fx.server.setSeedEvents(fx.calHref, {
        makeEventIcs(QStringLiteral("event-0"), QStringLiteral("Event 0 EDITED")),
    });
    fx.server.setCollectionCtag(fx.calHref, QStringLiteral("ctag-v2"));

    QCOMPARE(blob->loadRecords(QStringLiteral("Personal")).size(), 3);

    // Never a sync-collection REPORT — the capability was never advertised.
    QCOMPARE(fx.server.syncCollectionReportCount(), 0);
    QCOMPARE(fx.server.multigetReportCount(), 2);  // initial sync + this cycle
}

// (e) RED: restart-shaped — a FRESH backend instance (same dbPath, so it
// reads the persisted sync-token) plus one server-side change must cost
// exactly one sync-collection REPORT + one multiget, composing with E6's
// EtagCache-seeding restart pin.
void TestSyncCollectionReport::restart_storedToken_oneReportOneItem()
{
    Fixture fx;
    QVERIFY(fx.setUp());
    fx.backend.reset();  // destroy the first instance; its in-memory EtagCache is gone

    fx.server.setSeedEvents(fx.calHref, {
        makeEventIcs(QStringLiteral("event-2"), QStringLiteral("Event 2 EDITED")),
    });
    fx.server.setCollectionCtag(fx.calHref, QStringLiteral("ctag-v2"));

    RemoteCalendarBackend backend2(fx.server.baseUrl(),
                                   QStringLiteral("testuser"),
                                   QStringLiteral("testpass"));
    backend2.setDbPath(fx.dbPath);
    backend2.setCacheDir(fx.cacheDir.path());
    backend2.registerCalendarUrl(QStringLiteral("Personal"), fx.calDavUrl);

    QSignalSpy loadSpy2(&backend2,
                        SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend2.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy2.count() > 0, 5000);
    QVERIFY(loadSpy2.first().at(1).toBool());

    const int reportBefore = fx.server.requestCount("REPORT");
    const int multigetBefore = fx.server.multigetReportCount();
    const int syncCollBefore = fx.server.syncCollectionReportCount();

    auto *blob2 = static_cast<IBlobBackend *>(&backend2);
    QCOMPARE(blob2->loadRecords(QStringLiteral("Personal")).size(), 3);

    QCOMPARE(fx.server.syncCollectionReportCount() - syncCollBefore, 1);
    QCOMPARE(fx.server.multigetReportCount() - multigetBefore, 1);
    QCOMPARE(fx.server.requestCount("REPORT") - reportBefore, 2);
}

QTEST_GUILESS_MAIN(TestSyncCollectionReport)
#include "tst_sync_collection_report.moc"
