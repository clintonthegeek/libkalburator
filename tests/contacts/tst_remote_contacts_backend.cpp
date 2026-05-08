// Phase Ib Task 5 — RemoteContactsBackend read-side tests.
// Phase Ib Task 6 — RemoteContactsBackend write-side tests.
// Phase Ib Task 7 — RemoteContactsBackend cancellation tests.
//
// Read-side (Task 5):
//  1. availableCollections() after registerAddressbookUrl() → 1 collection.
//  2. loadRecords() on an empty addressbook → empty list.
//  3. loadRecords() with 3 seeded records → 3 BackendRecords, bytes match.
//  4. vCard 3.0 from server → BackendRecord::type contains "vcard3".
//  5. loadRecord() for a known recordId → single record.
//  6. 401 from server → loadRecords() returns empty.
//
// Write-side (Task 6):
//  7.  createRecord() → 201 from server, returns recordId with UID, bytes intact via loadRecords.
//  8.  loadRecords() after createRecord() → 1 record, bytes match.
//  9.  updateRecord() → server bytes updated, ETag rotates, subsequent load has new bytes.
//  10. updateRecord() with stale ETag → 412, returns false, server unchanged.
//  11. deleteRecord() → gone from server, subsequent loadRecords() empty.
//  12. deleteRecord() with stale ETag → 412, returns false, server unchanged.
//
// Cancellation (Task 7):
//  13. cancel() during loadRecords() → returns empty, isCancelled() true.
//  14. cancel() during createRecord() → returns empty recordId, isCancelled() true.

#include <QtTest/QtTest>

#include "remotecontactsbackend.h"
#include "fakecarddavserver.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QTimer>
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

QByteArray makeVCard21(const QByteArray &uid, const QByteArray &fn)
{
    QByteArray v;
    v += "BEGIN:VCARD\r\n";
    v += "VERSION:2.1\r\n";
    v += "UID:" + uid + "\r\n";
    v += "FN:" + fn + "\r\n";
    v += "END:VCARD\r\n";
    return v;
}

QByteArray makeVCardNoVersion(const QByteArray &uid, const QByteArray &fn)
{
    QByteArray v;
    v += "BEGIN:VCARD\r\n";
    // Intentionally missing VERSION: line
    v += "UID:" + uid + "\r\n";
    v += "FN:" + fn + "\r\n";
    v += "END:VCARD\r\n";
    return v;
}

QByteArray makeVCard4WithLF(const QByteArray &uid, const QByteArray &fn)
{
    QByteArray v;
    v += "BEGIN:VCARD\n";  // LF only, not CRLF
    v += "VERSION:4.0\n";
    v += "UID:" + uid + "\n";
    v += "FN:" + fn + "\n";
    v += "END:VCARD\n";
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
    // --- Read-side (Task 5) -------------------------------------------------
    void availableCollections_after_register();
    void loadRecords_empty_addressbook();
    void loadRecords_three_records();
    void loadRecords_vcard3_shape();
    void loadRecord_known_recordId();
    void loadRecords_401_returns_empty();

    // --- Write-side (Task 6) ------------------------------------------------
    void createRecord_returns_recordId();
    void loadRecords_after_create();
    void updateRecord_updates_server_bytes();
    void updateRecord_stale_etag_returns_false();
    void deleteRecord_removes_from_server();
    void deleteRecord_stale_etag_returns_false();

    // --- Cancellation (Task 7) ----------------------------------------------
    void cancel_during_loadRecords_returns_empty();
    void cancel_during_createRecord_returns_empty_id();

    // --- vCard version handling (Task 12) -----------------------------------
    void loadRecords_vcard21_tagged_as_vcard3();
    void loadRecords_missing_version_tagged_as_vcard4();
    void loadRecords_vcard4_with_lf_line_endings();
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

// ---------------------------------------------------------------------------
// 7. createRecord() → server gets the data, returns a valid recordId
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::createRecord_returns_recordId()
{
    FakeCardDavServer server;
    // No seed records — addressbook starts empty.
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    BackendRecord rec;
    rec.data = makeVCard4("uid-new-eve", "Eve Adams");
    rec.id   = QString(); // not yet assigned

    const QString recordId = backend.createRecord(QStringLiteral("personal"), rec);
    QVERIFY2(!recordId.isEmpty(),
             "createRecord should return a non-empty recordId on success");
    QVERIFY2(recordId.startsWith(QStringLiteral("personal:")),
             qPrintable(QStringLiteral("recordId should be 'personal:<uid>', got: %1").arg(recordId)));
    // The UID in the recordId should match the UID in the vCard.
    QVERIFY2(recordId.contains(QStringLiteral("uid-new-eve")),
             qPrintable(QStringLiteral("recordId should contain the vCard UID, got: %1").arg(recordId)));
}

// ---------------------------------------------------------------------------
// 8. loadRecords() after createRecord() → 1 record, bytes match
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_after_create()
{
    FakeCardDavServer server;
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const QByteArray originalData = makeVCard4("uid-frank", "Frank Stone");
    BackendRecord rec;
    rec.data = originalData;

    const QString recordId = backend.createRecord(QStringLiteral("personal"), rec);
    QVERIFY(!recordId.isEmpty());

    // Now load all records — should see exactly the one we just created.
    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 1);
    QVERIFY2(records.at(0).data.contains("Frank Stone"),
             "Loaded record should contain the originally uploaded vCard data");
    QCOMPARE(records.at(0).id, recordId);
}

// ---------------------------------------------------------------------------
// 9. updateRecord() → server bytes updated, subsequent load has new bytes
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::updateRecord_updates_server_bytes()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCard4("uid-grace", "Grace Old Name") });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    // Load so the backend caches the ETag in its handle.
    const QList<BackendRecord> before = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(before.size(), 1);
    QVERIFY(before.at(0).data.contains("Grace Old Name"));

    // Build an updated record with the same id but new bytes.
    BackendRecord updated = before.at(0);
    updated.data = makeVCard4("uid-grace", "Grace New Name");

    const bool ok = backend.updateRecord(updated);
    QVERIFY2(ok, "updateRecord should return true on success");

    // Reload from server — new bytes should be present.
    const QList<BackendRecord> after = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(after.size(), 1);
    QVERIFY2(after.at(0).data.contains("Grace New Name"),
             "After update, server should have the new vCard bytes");
    QVERIFY2(!after.at(0).data.contains("Grace Old Name"),
             "After update, old bytes should be gone");
}

// ---------------------------------------------------------------------------
// 10. updateRecord() with stale ETag → returns false, server unchanged
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::updateRecord_stale_etag_returns_false()
{
    FakeCardDavServer server;
    const QByteArray originalData = makeVCard4("uid-henry", "Henry Unchanged");
    server.setSeedRecords(QStringLiteral("personal"), { originalData });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    // Populate the backend's handle cache.
    const QList<BackendRecord> loaded = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(loaded.size(), 1);

    // Externally bump the server ETag so the backend's cached ETag goes stale.
    QVERIFY(server.bumpEtag(QStringLiteral("personal"), QStringLiteral("uid-henry")));

    // Try to update — should fail with 412.
    BackendRecord staleRec = loaded.at(0);
    staleRec.data = makeVCard4("uid-henry", "Henry Changed");
    const bool ok = backend.updateRecord(staleRec);
    QVERIFY2(!ok, "updateRecord with stale ETag should return false (412)");

    // Server should still have the original data.
    const QList<BackendRecord> after = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(after.size(), 1);
    QVERIFY2(after.at(0).data.contains("Henry Unchanged"),
             "Server data should be unchanged after a stale-ETag update failure");
}

// ---------------------------------------------------------------------------
// 11. deleteRecord() → gone from server, subsequent loadRecords() empty
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::deleteRecord_removes_from_server()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCard4("uid-ivan", "Ivan Gone") });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    // Populate handle cache.
    const QList<BackendRecord> before = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(before.size(), 1);

    const bool ok = backend.deleteRecord(before.at(0).id);
    QVERIFY2(ok, "deleteRecord should return true on success");

    // Server should now report empty.
    const QList<BackendRecord> after = backend.loadRecords(QStringLiteral("personal"));
    QVERIFY2(after.isEmpty(),
             "After deleteRecord the addressbook should be empty");
}

// ---------------------------------------------------------------------------
// 12. deleteRecord() with stale ETag → returns false, server unchanged
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::deleteRecord_stale_etag_returns_false()
{
    FakeCardDavServer server;
    const QByteArray originalData = makeVCard4("uid-julia", "Julia Stays");
    server.setSeedRecords(QStringLiteral("personal"), { originalData });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    // Populate handle cache.
    const QList<BackendRecord> loaded = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(loaded.size(), 1);
    const QString recordId = loaded.at(0).id;

    // Externally bump ETag so backend's cached one goes stale.
    QVERIFY(server.bumpEtag(QStringLiteral("personal"), QStringLiteral("uid-julia")));

    // Delete should fail (412).
    const bool ok = backend.deleteRecord(recordId);
    QVERIFY2(!ok, "deleteRecord with stale ETag should return false (412)");

    // Server record should still be there.
    const QList<BackendRecord> after = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(after.size(), 1);
    QVERIFY2(after.at(0).data.contains("Julia Stays"),
             "Server record should be unchanged after a stale-ETag delete failure");
}

// ---------------------------------------------------------------------------
// 13. cancel() during loadRecords() → empty result, isCancelled() true
//
// Strategy: configure the fake server with a 200ms response delay, schedule
// cancel() to fire 50ms in (while the backend is blocked in QEventLoop::exec()
// waiting for the server's PROPFIND reply). The abort() causes the reply's
// finished signal to fire immediately, the helper returns an empty map, and
// loadRecords() returns {}.
//
// Because loadRecords() blocks synchronously on the current thread, we use
// QTimer::singleShot to inject the cancel() into the running event loop.
// No QTRY_VERIFY_WITH_TIMEOUT is needed; once loadRecords() returns we can
// check results directly.
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::cancel_during_loadRecords_returns_empty()
{
    FakeCardDavServer server;
    // Seed one record so the server would normally return non-empty.
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCard4("uid-cancel-lr", "Cancel Load") });
    server.setResponseDelayMs(200); // respond 200ms after receiving the request
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    // Fire cancel() 50ms after entering the blocking loadRecords() call.
    // The timer fires inside the backend's inner QEventLoop::exec().
    QTimer::singleShot(50, &backend, &RemoteContactsBackend::cancel);

    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));

    QVERIFY2(records.isEmpty(),
             "loadRecords() after cancel() should return an empty list");
    QVERIFY2(backend.isCancelled(),
             "isCancelled() should be true after cancel() was called");
}

// ---------------------------------------------------------------------------
// 14. cancel() during createRecord() → empty recordId, isCancelled() true
//
// Same strategy: 200ms server delay, 50ms cancel() timer. The PUT reply is
// aborted, putVCard() returns status 0, createRecord() returns "".
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::cancel_during_createRecord_returns_empty_id()
{
    FakeCardDavServer server;
    server.setResponseDelayMs(200);
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    BackendRecord rec;
    rec.data = makeVCard4("uid-cancel-cr", "Cancel Create");

    // Fire cancel() 50ms after entering the blocking createRecord() call.
    QTimer::singleShot(50, &backend, &RemoteContactsBackend::cancel);

    const QString recordId = backend.createRecord(QStringLiteral("personal"), rec);

    QVERIFY2(recordId.isEmpty(),
             "createRecord() after cancel() should return an empty recordId");
    QVERIFY2(backend.isCancelled(),
             "isCancelled() should be true after cancel() was called");
}

// ---------------------------------------------------------------------------
// 15. vCard 2.1 → tagged as vcard3 (with log warning)
//
// Task 12: vCard 2.1 is detected and best-effort transcoded as vcard3.
// The engine Pipeline transcodes vcard3 to vcard4 for the calendar domain.
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_vcard21_tagged_as_vcard3()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCard21("uid-v21", "vCard 2.1 Contact") });
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
             qPrintable(QStringLiteral("vCard 2.1 should be tagged as vcard3, got: %1")
                        .arg(records.at(0).type)));
}

// ---------------------------------------------------------------------------
// 16. Missing VERSION: line → tagged as vcard4 (assume latest)
//
// Task 12: vCard without explicit VERSION is assumed to be vcard4 (latest).
// This follows the principle of assuming the most recent version when
// unspecified.
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_missing_version_tagged_as_vcard4()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCardNoVersion("uid-no-ver", "No Version Contact") });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 1);
    QVERIFY2(records.at(0).type.contains(QStringLiteral("vcard4")),
             qPrintable(QStringLiteral("Missing VERSION should default to vcard4, got: %1")
                        .arg(records.at(0).type)));
}

// ---------------------------------------------------------------------------
// 17. vCard 4.0 with LF line endings (not CRLF) → tagged as vcard4
//
// Task 12: Version detection is robust against both LF (\n) and CRLF (\r\n)
// line endings. Some servers may send pure LF; the detection should work
// either way.
// ---------------------------------------------------------------------------

void TstRemoteContactsBackend::loadRecords_vcard4_with_lf_line_endings()
{
    FakeCardDavServer server;
    server.setSeedRecords(QStringLiteral("personal"),
                          { makeVCard4WithLF("uid-lf", "LF Line Endings") });
    QVERIFY(server.startListening());

    const QUrl addressbookUrl = server.baseUrl().resolved(
        QUrl(QStringLiteral("/addressbooks/testuser/personal/")));

    RemoteContactsBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.registerAddressbookUrl(QStringLiteral("personal"), addressbookUrl);

    const QList<BackendRecord> records = backend.loadRecords(QStringLiteral("personal"));
    QCOMPARE(records.size(), 1);
    QVERIFY2(records.at(0).type.contains(QStringLiteral("vcard4")),
             qPrintable(QStringLiteral("vCard 4.0 with LF line endings should be tagged as vcard4, got: %1")
                        .arg(records.at(0).type)));
}

QTEST_GUILESS_MAIN(TstRemoteContactsBackend)
#include "tst_remote_contacts_backend.moc"
