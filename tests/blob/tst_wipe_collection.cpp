// tst_wipe_collection.cpp
//
// WP-D2 (architectural-redress campaign) — concrete-backend wipeCollection tests.
//
// Pins the IBlobBackend::wipeCollection contract on two concrete backends:
//
//   1. LocalBlobBackend (default IBlobBackend per-record-delete path)
//      - emptiesCollection: records gone, collection dir survives
//      - survivorIsolation: sibling collection untouched
//
//   2. RemoteCalendarBackend over FakeCalDavServer (network path)
//      - emptiesCollection: records removed from server via CalDAV DELETE
//      - survivorIsolation: sibling collection untouched on server
//
// The FakeCalDavServer DELETE support was added in WP-D2 (fakecaldavserver.cpp).

#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>

#include "localblobbackend.h"
#include "remotecalendarbackend.h"
#include "fakecaldavserver.h"

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::LocalBlobBackend;
using Kalburator::Sync::RemoteCalendarBackend;

namespace {

BackendRecord makeRecord(const QString &id)
{
    BackendRecord r;
    r.id          = id;
    r.displayName = id;
    r.type        = QStringLiteral("memo");
    r.data        = ("content-" + id).toUtf8();
    return r;
}

CollectionInfo makeCollection(const QString &id,
                              const QString &type = QStringLiteral("memos"))
{
    CollectionInfo c;
    c.id   = id;
    c.name = id;
    c.type = type;
    return c;
}

QByteArray makeIcs(const QString &uid, const QString &summary)
{
    return (QStringLiteral("BEGIN:VCALENDAR\r\n"
                           "VERSION:2.0\r\n"
                           "BEGIN:VEVENT\r\n"
                           "UID:%1\r\n"
                           "SUMMARY:%2\r\n"
                           "DTSTART:20260601T120000Z\r\n"
                           "END:VEVENT\r\n"
                           "END:VCALENDAR\r\n")
                .arg(uid, summary))
        .toUtf8();
}

} // namespace

class TstWipeCollection : public QObject
{
    Q_OBJECT
private slots:
    // LocalBlobBackend
    void local_wipe_emptiesCollection();
    void local_wipe_survivorIsolation();

    // RemoteCalendarBackend
    void remote_wipe_emptiesCollection();
    void remote_wipe_survivorIsolation();
};

// ---------------------------------------------------------------------------
// LocalBlobBackend
// ---------------------------------------------------------------------------

void TstWipeCollection::local_wipe_emptiesCollection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LocalBlobBackend b(dir.path());

    b.createCollection(makeCollection(QStringLiteral("personal")));
    const QString id1 = b.createRecord(QStringLiteral("personal"),
                                        makeRecord(QStringLiteral("rec-1")));
    const QString id2 = b.createRecord(QStringLiteral("personal"),
                                        makeRecord(QStringLiteral("rec-2")));
    QVERIFY(!id1.isEmpty());
    QVERIFY(!id2.isEmpty());
    QCOMPARE(b.loadRecords(QStringLiteral("personal")).size(), 2);

    QVERIFY(b.wipeCollection(QStringLiteral("personal")));
    QCOMPARE(b.loadRecords(QStringLiteral("personal")).size(), 0);

    // The collection directory must still exist after wipe.
    QVERIFY(QDir(dir.path() + QStringLiteral("/personal")).exists());
}

void TstWipeCollection::local_wipe_survivorIsolation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    LocalBlobBackend b(dir.path());

    b.createCollection(makeCollection(QStringLiteral("personal")));
    b.createCollection(makeCollection(QStringLiteral("work")));

    b.createRecord(QStringLiteral("personal"), makeRecord(QStringLiteral("p-1")));
    b.createRecord(QStringLiteral("personal"), makeRecord(QStringLiteral("p-2")));
    b.createRecord(QStringLiteral("work"),     makeRecord(QStringLiteral("w-1")));
    b.createRecord(QStringLiteral("work"),     makeRecord(QStringLiteral("w-2")));

    QVERIFY(b.wipeCollection(QStringLiteral("personal")));

    QCOMPARE(b.loadRecords(QStringLiteral("personal")).size(), 0);
    QCOMPARE(b.loadRecords(QStringLiteral("work")).size(), 2);
}

// ---------------------------------------------------------------------------
// RemoteCalendarBackend
// ---------------------------------------------------------------------------

void TstWipeCollection::remote_wipe_emptiesCollection()
{
    FakeCalDavServer server;
    QVERIFY(server.startListening());

    server.setSeedEvents(QStringLiteral("/calendars/testuser/personal/"), {
        makeIcs(QStringLiteral("uid-p1"), QStringLiteral("Event P1")),
        makeIcs(QStringLiteral("uid-p2"), QStringLiteral("Event P2")),
    });
    QCOMPARE(server.storedEvents(QStringLiteral("/calendars/testuser/personal/")).size(), 2);

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerCalendarUrl(
        QStringLiteral("personal"),
        server.baseUrl().toString() + QStringLiteral("calendars/testuser/personal/"));

    QVERIFY(backend.wipeCollection(QStringLiteral("personal")));

    QCOMPARE(server.storedEvents(QStringLiteral("/calendars/testuser/personal/")).size(), 0);
}

void TstWipeCollection::remote_wipe_survivorIsolation()
{
    FakeCalDavServer server;
    server.setCalendars({
        { QStringLiteral("Personal"), QStringLiteral("/calendars/testuser/personal/") },
        { QStringLiteral("Work"),     QStringLiteral("/calendars/testuser/work/")     },
    });
    QVERIFY(server.startListening());

    server.setSeedEvents(QStringLiteral("/calendars/testuser/personal/"), {
        makeIcs(QStringLiteral("uid-p1"), QStringLiteral("Event P1")),
        makeIcs(QStringLiteral("uid-p2"), QStringLiteral("Event P2")),
    });
    server.setSeedEvents(QStringLiteral("/calendars/testuser/work/"), {
        makeIcs(QStringLiteral("uid-w1"), QStringLiteral("Event W1")),
        makeIcs(QStringLiteral("uid-w2"), QStringLiteral("Event W2")),
    });

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerCalendarUrl(
        QStringLiteral("personal"),
        server.baseUrl().toString() + QStringLiteral("calendars/testuser/personal/"));
    backend.registerCalendarUrl(
        QStringLiteral("work"),
        server.baseUrl().toString() + QStringLiteral("calendars/testuser/work/"));

    QVERIFY(backend.wipeCollection(QStringLiteral("personal")));

    QCOMPARE(server.storedEvents(QStringLiteral("/calendars/testuser/personal/")).size(), 0);
    QCOMPARE(server.storedEvents(QStringLiteral("/calendars/testuser/work/")).size(), 2);
}

QTEST_MAIN(TstWipeCollection)
#include "tst_wipe_collection.moc"
