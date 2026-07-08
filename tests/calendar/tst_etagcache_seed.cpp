// E6 (FINDINGS O35) — seed KDAV's per-session EtagCache from the persistent
// CalDavContentCache on a fresh backend instance, so a restart's first
// changed-CTag re-diff re-downloads only the items that actually changed
// instead of the whole collection.

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
}  // namespace

class TestEtagCacheSeed : public QObject
{
    Q_OBJECT

private slots:
    void restart_onlyChangedItemRefetched_ctagChanged();
    void restart_fullyServedFromCache_ctagUnchanged();
};

// RED: today, RemoteCalendarBackend's KDAV::EtagCache is constructed fresh
// (in-memory, per-instance) in the constructor and never seeded from the
// persistent CalDavContentCache. A fresh backend instance pointed at the
// SAME cache dir as a prior sync, after the server's CTag changes because
// exactly ONE of three items was edited, must re-fetch only that ONE item's
// body — not all three — because the other two are already in the content
// cache with matching ETags.
void TestEtagCacheSeed::restart_onlyChangedItemRefetched_ctagChanged()
{
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, {
        makeEventIcs(QStringLiteral("event-0"), QStringLiteral("Event 0")),
        makeEventIcs(QStringLiteral("event-1"), QStringLiteral("Event 1")),
        makeEventIcs(QStringLiteral("event-2"), QStringLiteral("Event 2")),
    });
    server.setCollectionCtag(calHref, QStringLiteral("ctag-v1"));
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    const QString dbPath = dbDir.filePath(QStringLiteral("sync.db"));
    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);

    // First backend instance: real first sync, populates the persistent
    // content cache at cacheDir with all three items and commits ctag-v1.
    {
        RemoteCalendarBackend backend(server.baseUrl(),
                                      QStringLiteral("testuser"),
                                      QStringLiteral("testpass"));
        backend.setDbPath(dbPath);
        backend.setCacheDir(cacheDir.path());
        backend.setMultigetChunkSize(1);
        backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

        QSignalSpy loadSpy(&backend,
                           SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        backend.loadCalendars(QStringLiteral("Personal"));
        QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
        QVERIFY(loadSpy.first().at(1).toBool());

        auto *blob = static_cast<IBlobBackend *>(&backend);
        QCOMPARE(blob->loadRecords(QStringLiteral("Personal")).size(), 3);
        QCOMPARE(backend.cachedCollectionRevision(QStringLiteral("Personal")),
                 QStringLiteral("ctag-v1"));
    }
    // Backend destroyed — its in-memory KDAV::EtagCache is gone with it.
    // The persistent content cache at cacheDir survives.

    // Simulate a restart: bump ONE item + the collection CTag.
    server.setSeedEvents(calHref, {
        makeEventIcs(QStringLiteral("event-0"), QStringLiteral("Event 0 EDITED")),
    });
    server.setCollectionCtag(calHref, QStringLiteral("ctag-v2"));

    // Second backend instance, same dbPath (so storedCtag == "ctag-v1", which
    // now differs from the server's "ctag-v2" — a real re-diff), SAME cache
    // dir (the persisted content survives the restart).
    RemoteCalendarBackend backend2(server.baseUrl(),
                                   QStringLiteral("testuser"),
                                   QStringLiteral("testpass"));
    backend2.setDbPath(dbPath);
    backend2.setCacheDir(cacheDir.path());
    backend2.setMultigetChunkSize(1);
    backend2.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    QSignalSpy loadSpy2(&backend2,
                        SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend2.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy2.count() > 0, 5000);
    QVERIFY(loadSpy2.first().at(1).toBool());

    auto *blob2 = static_cast<IBlobBackend *>(&backend2);
    QCOMPARE(blob2->loadRecords(QStringLiteral("Personal")).size(), 3);

    // With setMultigetChunkSize(1), multigetReportCount() is exactly the
    // number of item bodies fetched from the server: 3 for backend1's
    // initial sync, plus only event-0 (the one that actually changed) for
    // backend2's restart sync — the other two must be served from the
    // persistent content cache via a seeded EtagCache, not re-downloaded.
    QCOMPARE(server.multigetReportCount(), 4);
}

// Companion: a fresh instance whose CTag is UNCHANGED must still be served
// fully from cache (pins no regression of the existing short-circuit).
void TestEtagCacheSeed::restart_fullyServedFromCache_ctagUnchanged()
{
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    FakeCalDavServer server;
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSeedEvents(calHref, {
        makeEventIcs(QStringLiteral("event-0"), QStringLiteral("Event 0")),
        makeEventIcs(QStringLiteral("event-1"), QStringLiteral("Event 1")),
    });
    server.setCollectionCtag(calHref, QStringLiteral("ctag-v1"));
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());
    QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    const QString dbPath = dbDir.filePath(QStringLiteral("sync.db"));
    const QString calDavUrl = server.baseUrl().toString() + calHref.mid(1);

    {
        RemoteCalendarBackend backend(server.baseUrl(),
                                      QStringLiteral("testuser"),
                                      QStringLiteral("testpass"));
        backend.setDbPath(dbPath);
        backend.setCacheDir(cacheDir.path());
        backend.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

        QSignalSpy loadSpy(&backend,
                           SIGNAL(loadCalendarsFinished(QString, bool, QString)));
        backend.loadCalendars(QStringLiteral("Personal"));
        QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() > 0, 5000);
        QVERIFY(loadSpy.first().at(1).toBool());

        auto *blob = static_cast<IBlobBackend *>(&backend);
        QCOMPARE(blob->loadRecords(QStringLiteral("Personal")).size(), 2);
        QCOMPARE(backend.cachedCollectionRevision(QStringLiteral("Personal")),
                 QStringLiteral("ctag-v1"));
    }

    // No server-side change at all — CTag stays "ctag-v1".
    RemoteCalendarBackend backend2(server.baseUrl(),
                                   QStringLiteral("testuser"),
                                   QStringLiteral("testpass"));
    backend2.setDbPath(dbPath);
    backend2.setCacheDir(cacheDir.path());
    backend2.registerCalendarUrl(QStringLiteral("Personal"), calDavUrl);

    QSignalSpy loadSpy2(&backend2,
                        SIGNAL(loadCalendarsFinished(QString, bool, QString)));
    backend2.loadCalendars(QStringLiteral("Personal"));
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy2.count() > 0, 5000);
    QVERIFY(loadSpy2.first().at(1).toBool());

    auto *blob2 = static_cast<IBlobBackend *>(&backend2);
    QCOMPARE(blob2->loadRecords(QStringLiteral("Personal")).size(), 2);
    // Only backend1's initial sync issues a multiget REPORT; backend2's
    // CTag-unchanged resync must short-circuit before ever creating a
    // DavItemsListJob (no regression of the existing cache-hit path).
    QCOMPARE(server.multigetReportCount(), 1);
}

QTEST_GUILESS_MAIN(TestEtagCacheSeed)
#include "tst_etagcache_seed.moc"
