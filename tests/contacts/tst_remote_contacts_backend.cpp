// Phase Ib Task 5 — RemoteContactsBackend read-side tests.
//
// Tests:
//  1. availableCollections() after registerAddressbookUrl() → 1 collection.
//  2. loadRecords() on an empty addressbook → empty list.
//  3. loadRecords() with 3 seeded records → 3 BackendRecords, bytes match.
//  4. vCard 3.0 from server → BackendRecord::type contains "vcard3".
//  5. loadRecord() for a known recordId → single record.
//  6. 401 from server → loadRecords() returns empty.

#include <QtTest/QtTest>

#include "remotecontactsbackend.h"
#include "fakecarddavserver.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QUrl>

using namespace Kalburator::Sync;

// ---------------------------------------------------------------------------
// Sample vCard data
// ---------------------------------------------------------------------------

namespace {

QByteArray makeVCard4(const QByteArray &uid, const QByteArray &fn)
{
    QByteArray v;
    v += "BEGIN:VCARD\r\n";
    v += "VERSION:4.0\r\n";
    v += "UID:" + uid + "\r\n";
    v += "FN:" + fn + "\r\n";
    v += "END:VCARD\r\n";
    return v;
}

QByteArray makeVCard3(const QByteArray &uid, const QByteArray &fn)
{
    QByteArray v;
    v += "BEGIN:VCARD\r\n";
    v += "VERSION:3.0\r\n";
    v += "UID:" + uid + "\r\n";
    v += "FN:" + fn + "\r\n";
    v += "END:VCARD\r\n";
    return v;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TstRemoteContactsBackend : public QObject
{
    Q_OBJECT

private slots:
    void availableCollections_after_register();
    void loadRecords_empty_addressbook();
    void loadRecords_three_records();
    void loadRecords_vcard3_shape();
    void loadRecord_known_recordId();
    void loadRecords_401_returns_empty();
};

// ---------------------------------------------------------------------------
// 1. availableCollections() after registerAddressbookUrl()
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::availableCollections_after_register()
{
    RemoteContactsBackend backend(QUrl(QStringLiteral("http://example.com/")),
                                  QStringLiteral("u"),
                                  QStringLiteral("p"));

    QVERIFY(backend.availableCollections().isEmpty());

    backend.registerAddressbookUrl(
        QStringLiteral("personal"),
        QUrl(QStringLiteral("http://example.com/addressbooks/testuser/personal/")));

    const QList<CollectionInfo> cols = backend.availableCollections();
    QCOMPARE(cols.size(), 1);
    QCOMPARE(cols.at(0).id,   QStringLiteral("personal"));
    QCOMPARE(cols.at(0).type, QStringLiteral("contacts"));
    QVERIFY(!cols.at(0).path.isEmpty());
}

// ---------------------------------------------------------------------------
// 2. loadRecords() on empty addressbook
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_empty_addressbook()
{
    FakeCardDavServer server;
    // Default addressbook "personal" exists but has no seeded records.
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));
    QVERIFY(records.isEmpty());
}

// ---------------------------------------------------------------------------
// 3. loadRecords() with 3 seeded records
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_three_records()
{
    FakeCardDavServer server;
    const QList<QByteArray> vcards = {
        makeVCard4("uid-alice", "Alice Smith"),
        makeVCard4("uid-bob",   "Bob Jones"),
        makeVCard4("uid-carol", "Carol White"),
    };
    server.setSeedRecords(QStringLiteral("personal"), vcards);
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 3);

    // Collect loaded record IDs and verify format "<collectionId>:<uid>".
    QStringList loadedIds;
    for (const auto &rec : records) {
        QVERIFY(rec.id.startsWith(QStringLiteral("personal:")));
        QVERIFY(!rec.data.isEmpty());
        loadedIds << rec.id;
    }

    // Verify the bytes actually contain the original vCard data.
    // Build a uid → data map from the loaded records.
    QHash<QString, QByteArray> byUid;
    for (const auto &rec : records) {
        const QString uid = rec.id.mid(QStringLiteral("personal:").size());
        byUid.insert(uid, rec.data);
    }

    QVERIFY(byUid.contains(QStringLiteral("uid-alice")));
    QVERIFY(byUid.value(QStringLiteral("uid-alice")).contains("Alice Smith"));
    QVERIFY(byUid.contains(QStringLiteral("uid-bob")));
    QVERIFY(byUid.value(QStringLiteral("uid-bob")).contains("Bob Jones"));
    QVERIFY(byUid.contains(QStringLiteral("uid-carol")));
    QVERIFY(byUid.value(QStringLiteral("uid-carol")).contains("Carol White"));
}

// ---------------------------------------------------------------------------
// 4. vCard 3.0 → type contains "vcard3"
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_vcard3_shape()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCard3("uid-v3", "Old Format Person") });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 1);
    QVERIFY2(records.at(0).type.contains(QStringLiteral("vcard3")),
             qPrintable(QStringLiteral("type was: %1").arg(records.at(0).type)));
}

// ---------------------------------------------------------------------------
// 5. loadRecord() for a known recordId
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecord_known_recordId()
{
    FakeCardDavServer server;
    const QByteArray originalData = makeVCard4("uid-dave", "Dave Brown");
    server.setSeedRecords(QStringLiteral("personal"), { originalData });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const auto opt = backend.loadRecord(QStringLiteral("personal:uid-dave"));
    QVERIFY(opt.has_value());
    QCOMPARE(opt->id, QStringLiteral("personal:uid-dave"));
    QVERIFY(opt->data.contains("Dave Brown"));
}

// ---------------------------------------------------------------------------
// 6. 401 from server → loadRecords() returns empty
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_401_returns_empty()
{
    FakeCardDavServer server;
    server.setReturn401(true);
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCard4("uid-x", "Should Not Appear") });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("baduser"),
                                  QStringLiteral("badpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));
    QVERIFY(records.isEmpty());
}

QTEST_GUILESS_MAIN(TstRemoteContactsBackend)
#include "tst_remote_contacts_backend.moc"
