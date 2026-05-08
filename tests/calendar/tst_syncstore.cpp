/**
 * @file tst_syncstore.cpp
 * @brief Tests for the three stores that replaced SyncStore (Phase D Task 9).
 *
 * SyncStore was dissolved into:
 *   - CalendarBaselineStore — iCal text baselines, property baselines, lastSyncTime
 *   - SyncConflictStore     — SQLite sync_conflicts table
 *   - BlobBaselineStore     — triple-keyed (backendId, collectionId, recordId) version hashes
 *
 * CTags and local fingerprints are now private to RemoteCalendarBackend / LocalBackend
 * respectively and are not covered here.
 */

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>

#include "calendarbaselinestore.h"
#include "syncconflictstore.h"
#include "blobbaselinestore.h"
#include "synctypes.h"

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;


class TestSyncStore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Database lifecycle tests
    void testDatabaseCreation();
    void testDatabaseReopen();
    void testInvalidPath();

    // Version hash tests (now in BlobBaselineStore triple API)
    void testSetAndGetVersionHash();
    void testRemoveVersionHash();
    void testAllVersionHashes();
    void testClearVersionHashes();
    void testClearVersionHashesForBackend();

    // Baseline tests (now in CalendarBaselineStore)
    void testSetAndGetBaseline();
    void testRemoveBaseline();
    void testAllBaselines();
    void testClearBaselines();

    // Last sync time tests (now in CalendarBaselineStore)
    void testLastSyncTime();
    void testLastSyncTimeNotSet();

    // Conflict tests (now in SyncConflictStore)
    void testRecordConflict();
    void testResolveConflict();
    void testUnresolvedConflicts();
    void testUnresolvedConflictCount();
    void testRemoveConflict();
    void testConflictSignals();

    // Maintenance tests (vacuum on SyncConflictStore; clearBackendData; clearMappingData)
    void testVacuum();
    void testClearBackendData();
    void testClearMappingData();

    // Edge cases
    void testEmptyStrings();
    void testSpecialCharacters();
    void testLargeData();

private:
    QTemporaryDir *m_tempDir = nullptr;
    CalendarBaselineStore *m_calBaselines = nullptr;
    SyncConflictStore     *m_conflictStore = nullptr;
    BlobBaselineStore     *m_blobStore = nullptr;
    QString dbPath() const;
};

void TestSyncStore::initTestCase()
{
    qRegisterMetaType<ConflictInfo>("ConflictInfo");
}

void TestSyncStore::cleanupTestCase()
{
}

void TestSyncStore::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());

    const QString path = dbPath();
    m_calBaselines  = new CalendarBaselineStore(path, this);
    m_conflictStore = new SyncConflictStore(path, this);
    m_blobStore     = new BlobBaselineStore(path);

    QVERIFY2(m_calBaselines->isValid(),  qUtf8Printable(m_calBaselines->property("lastError").toString()));
    QVERIFY2(m_conflictStore->isOpen(),  qUtf8Printable(m_conflictStore->lastError()));
    QVERIFY2(m_blobStore->isOpen(),      qUtf8Printable(m_blobStore->lastError()));
}

void TestSyncStore::cleanup()
{
    delete m_conflictStore;
    m_conflictStore = nullptr;
    delete m_calBaselines;
    m_calBaselines = nullptr;
    delete m_blobStore;
    m_blobStore = nullptr;
    delete m_tempDir;
    m_tempDir = nullptr;
}

QString TestSyncStore::dbPath() const
{
    return m_tempDir->filePath(QStringLiteral(".kalburator-sync.db"));
}

// ============================================================================
// Database lifecycle tests
// ============================================================================

void TestSyncStore::testDatabaseCreation()
{
    QVERIFY(m_calBaselines->isValid());
    QVERIFY(m_conflictStore->isOpen());
    QVERIFY(m_blobStore->isOpen());
    QVERIFY(QFile::exists(dbPath()));
    QCOMPARE(m_conflictStore->databasePath(), dbPath());
}

void TestSyncStore::testDatabaseReopen()
{
    // Add some data via the blob store
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("cal"),
                                      QStringLiteral("uid1"), QStringLiteral("hash1")));

    // Close and reopen
    delete m_blobStore;
    m_blobStore = new BlobBaselineStore(dbPath());
    QVERIFY(m_blobStore->isOpen());

    // Verify data persisted
    QString hash = m_blobStore->baselineHash(QStringLiteral("caldav"),
                                              QStringLiteral("cal"),
                                              QStringLiteral("uid1"));
    QCOMPARE(hash, QStringLiteral("hash1"));
}

void TestSyncStore::testInvalidPath()
{
    SyncConflictStore badStore(QStringLiteral("/nonexistent/path/db.sqlite"));
    QVERIFY(!badStore.isOpen());
    QVERIFY(!badStore.lastError().isEmpty());
}

// ============================================================================
// Version hash tests — now in BlobBaselineStore (triple API)
// ============================================================================

void TestSyncStore::testSetAndGetVersionHash()
{
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("personal"),
                                      QStringLiteral("uid1"), QStringLiteral("etag-12345")));

    QString hash = m_blobStore->baselineHash(QStringLiteral("caldav"),
                                              QStringLiteral("personal"),
                                              QStringLiteral("uid1"));
    QCOMPARE(hash, QStringLiteral("etag-12345"));

    // Non-existent returns empty
    QVERIFY(m_blobStore->baselineHash(QStringLiteral("caldav"),
                                       QStringLiteral("personal"),
                                       QStringLiteral("nonexistent")).isEmpty());
}

void TestSyncStore::testRemoveVersionHash()
{
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("personal"),
                                      QStringLiteral("uid1"), QStringLiteral("etag-12345")));

    // clearCollection removes all for (backend, collection)
    QVERIFY(m_blobStore->clearCollection(QStringLiteral("caldav"), QStringLiteral("personal")));

    QVERIFY(m_blobStore->baselineHash(QStringLiteral("caldav"),
                                       QStringLiteral("personal"),
                                       QStringLiteral("uid1")).isEmpty());
}

void TestSyncStore::testAllVersionHashes()
{
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("personal"),
                                      QStringLiteral("uid1"), QStringLiteral("etag1")));
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("personal"),
                                      QStringLiteral("uid2"), QStringLiteral("etag2")));
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("work"),
                                      QStringLiteral("uid3"), QStringLiteral("etag3")));

    QStringList personalIds = m_blobStore->baselineRecordIds(QStringLiteral("caldav"),
                                                              QStringLiteral("personal"));
    QCOMPARE(personalIds.size(), 2);
    QVERIFY(personalIds.contains(QStringLiteral("uid1")));
    QVERIFY(personalIds.contains(QStringLiteral("uid2")));
    QCOMPARE(m_blobStore->baselineHash(QStringLiteral("caldav"),
                                        QStringLiteral("personal"),
                                        QStringLiteral("uid1")),
             QStringLiteral("etag1"));
    QCOMPARE(m_blobStore->baselineHash(QStringLiteral("caldav"),
                                        QStringLiteral("personal"),
                                        QStringLiteral("uid2")),
             QStringLiteral("etag2"));
}

void TestSyncStore::testClearVersionHashes()
{
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("personal"),
                                      QStringLiteral("uid1"), QStringLiteral("etag1")));
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("work"),
                                      QStringLiteral("uid2"), QStringLiteral("etag2")));

    QVERIFY(m_blobStore->clearCollection(QStringLiteral("caldav"), QStringLiteral("personal")));

    QVERIFY(m_blobStore->baselineRecordIds(QStringLiteral("caldav"),
                                            QStringLiteral("personal")).isEmpty());

    QCOMPARE(m_blobStore->baselineRecordIds(QStringLiteral("caldav"),
                                             QStringLiteral("work")).size(), 1);
}

void TestSyncStore::testClearVersionHashesForBackend()
{
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("personal"),
                                      QStringLiteral("uid1"), QStringLiteral("etag1")));
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("work"),
                                      QStringLiteral("uid2"), QStringLiteral("etag2")));
    QVERIFY(m_blobStore->setBaseline(QStringLiteral("local"), QStringLiteral("cal"),
                                      QStringLiteral("uid3"), QStringLiteral("hash3")));

    // Clear all caldav collections
    QVERIFY(m_blobStore->clearCollection(QStringLiteral("caldav"), QStringLiteral("personal")));
    QVERIFY(m_blobStore->clearCollection(QStringLiteral("caldav"), QStringLiteral("work")));

    QVERIFY(m_blobStore->baselineRecordIds(QStringLiteral("caldav"),
                                            QStringLiteral("personal")).isEmpty());
    QVERIFY(m_blobStore->baselineRecordIds(QStringLiteral("caldav"),
                                            QStringLiteral("work")).isEmpty());

    // local backend should remain
    QCOMPARE(m_blobStore->baselineRecordIds(QStringLiteral("local"),
                                             QStringLiteral("cal")).size(), 1);
}

// ============================================================================
// Baseline tests — now in CalendarBaselineStore
// ============================================================================

void TestSyncStore::testSetAndGetBaseline()
{
    QString icalData = QStringLiteral(
        "BEGIN:VCALENDAR\n"
        "BEGIN:VEVENT\n"
        "UID:test-uid\n"
        "SUMMARY:Test Event\n"
        "END:VEVENT\n"
        "END:VCALENDAR\n"
    );

    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("test-uid"), icalData));

    QString retrieved = m_calBaselines->baseline(QStringLiteral("mapping1"),
                                                  QStringLiteral("test-uid"));
    QCOMPARE(retrieved, icalData);

    // Non-existent returns empty
    QVERIFY(m_calBaselines->baseline(QStringLiteral("mapping1"),
                                      QStringLiteral("nonexistent")).isEmpty());
}

void TestSyncStore::testRemoveBaseline()
{
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("uid1"), QStringLiteral("ical-data")));

    QVERIFY(m_calBaselines->removeBaseline(QStringLiteral("mapping1"), QStringLiteral("uid1")));

    QVERIFY(m_calBaselines->baseline(QStringLiteral("mapping1"),
                                      QStringLiteral("uid1")).isEmpty());
}

void TestSyncStore::testAllBaselines()
{
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("uid1"), QStringLiteral("ical1")));
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("uid2"), QStringLiteral("ical2")));
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping2"),
                                         QStringLiteral("uid3"), QStringLiteral("ical3")));

    QHash<QString, QString> baselines = m_calBaselines->allBaselines(QStringLiteral("mapping1"));
    QCOMPARE(baselines.size(), 2);
    QCOMPARE(baselines.value(QStringLiteral("uid1")), QStringLiteral("ical1"));
    QCOMPARE(baselines.value(QStringLiteral("uid2")), QStringLiteral("ical2"));
}

void TestSyncStore::testClearBaselines()
{
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("uid1"), QStringLiteral("ical1")));
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("uid2"), QStringLiteral("ical2")));
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping2"),
                                         QStringLiteral("uid3"), QStringLiteral("ical3")));

    QVERIFY(m_calBaselines->removeBaselines(QStringLiteral("mapping1")));

    QVERIFY(m_calBaselines->allBaselines(QStringLiteral("mapping1")).isEmpty());
    QCOMPARE(m_calBaselines->allBaselines(QStringLiteral("mapping2")).size(), 1);
}

// ============================================================================
// Last sync time tests — now in CalendarBaselineStore
// ============================================================================

void TestSyncStore::testLastSyncTime()
{
    QDateTime syncTime = QDateTime::currentDateTimeUtc();
    QVERIFY(m_calBaselines->setLastSyncTime(QStringLiteral("mapping1"), syncTime));

    QDateTime retrieved = m_calBaselines->lastSyncTime(QStringLiteral("mapping1"));

    // Compare with some tolerance for database round-trip
    QVERIFY(qAbs(retrieved.secsTo(syncTime)) < 2);
}

void TestSyncStore::testLastSyncTimeNotSet()
{
    QDateTime retrieved = m_calBaselines->lastSyncTime(QStringLiteral("nonexistent"));
    QVERIFY(!retrieved.isValid());
}

// ============================================================================
// Conflict tests — now in SyncConflictStore
// ============================================================================

void TestSyncStore::testRecordConflict()
{
    ConflictInfo conflict;
    conflict.sourceBackendId = QStringLiteral("caldav");
    conflict.calendarId = QStringLiteral("personal");
    conflict.sourceId = QStringLiteral("uid1");
    conflict.targetId = QStringLiteral("remote1");
    conflict.sourceDescription = QStringLiteral("local ical data");
    conflict.targetDescription = QStringLiteral("remote ical data");
    conflict.sourceModified = QDateTime::currentDateTimeUtc().addSecs(-3600);
    conflict.targetModified = QDateTime::currentDateTimeUtc();

    QString conflictId = m_conflictStore->recordConflict(conflict);
    QVERIFY(!conflictId.isEmpty());

    // Retrieve and verify
    ConflictInfo retrieved = m_conflictStore->conflict(conflictId);
    QCOMPARE(retrieved.sourceBackendId, conflict.sourceBackendId);
    QCOMPARE(retrieved.calendarId, conflict.calendarId);
    QCOMPARE(retrieved.sourceId, conflict.sourceId);
    QCOMPARE(retrieved.targetId, conflict.targetId);
    QCOMPARE(retrieved.sourceDescription, conflict.sourceDescription);
    QCOMPARE(retrieved.targetDescription, conflict.targetDescription);
}

void TestSyncStore::testResolveConflict()
{
    ConflictInfo conflict;
    conflict.sourceBackendId = QStringLiteral("caldav");
    conflict.calendarId = QStringLiteral("personal");
    conflict.sourceId = QStringLiteral("uid1");

    QString conflictId = m_conflictStore->recordConflict(conflict);

    QCOMPARE(m_conflictStore->unresolvedConflictCount(), 1);

    m_conflictStore->resolveConflict(conflictId, ConflictResolution::SourceWins);

    QCOMPARE(m_conflictStore->unresolvedConflictCount(), 0);
}

void TestSyncStore::testUnresolvedConflicts()
{
    ConflictInfo conflict1;
    conflict1.sourceBackendId = QStringLiteral("caldav");
    conflict1.calendarId = QStringLiteral("personal");
    conflict1.sourceId = QStringLiteral("uid1");

    ConflictInfo conflict2;
    conflict2.sourceBackendId = QStringLiteral("caldav");
    conflict2.calendarId = QStringLiteral("personal");
    conflict2.sourceId = QStringLiteral("uid2");

    ConflictInfo conflict3;
    conflict3.sourceBackendId = QStringLiteral("local");
    conflict3.calendarId = QStringLiteral("work");
    conflict3.sourceId = QStringLiteral("uid3");

    m_conflictStore->recordConflict(conflict1);
    QString id2 = m_conflictStore->recordConflict(conflict2);
    m_conflictStore->recordConflict(conflict3);

    m_conflictStore->resolveConflict(id2, ConflictResolution::TargetWins);

    QList<ConflictInfo> allConflicts = m_conflictStore->unresolvedConflicts();
    QCOMPARE(allConflicts.size(), 2);
}

void TestSyncStore::testUnresolvedConflictCount()
{
    ConflictInfo conflict;
    conflict.sourceBackendId = QStringLiteral("caldav");
    conflict.calendarId = QStringLiteral("personal");
    conflict.sourceId = QStringLiteral("uid1");

    QCOMPARE(m_conflictStore->unresolvedConflictCount(), 0);

    m_conflictStore->recordConflict(conflict);
    QCOMPARE(m_conflictStore->unresolvedConflictCount(), 1);
}

void TestSyncStore::testRemoveConflict()
{
    ConflictInfo conflict;
    conflict.sourceBackendId = QStringLiteral("caldav");
    conflict.calendarId = QStringLiteral("personal");
    conflict.sourceId = QStringLiteral("uid1");

    QString conflictId = m_conflictStore->recordConflict(conflict);
    QCOMPARE(m_conflictStore->unresolvedConflictCount(), 1);

    m_conflictStore->removeConflict(conflictId);
    QCOMPARE(m_conflictStore->unresolvedConflictCount(), 0);

    ConflictInfo retrieved = m_conflictStore->conflict(conflictId);
    QVERIFY(retrieved.sourceId.isEmpty());
}

void TestSyncStore::testConflictSignals()
{
    QSignalSpy recordedSpy(m_conflictStore, &SyncConflictStore::conflictRecorded);
    QSignalSpy resolvedSpy(m_conflictStore, &SyncConflictStore::conflictResolved);

    ConflictInfo conflict;
    conflict.sourceBackendId = QStringLiteral("caldav");
    conflict.calendarId = QStringLiteral("personal");
    conflict.sourceId = QStringLiteral("uid1");

    QString conflictId = m_conflictStore->recordConflict(conflict);

    QCOMPARE(recordedSpy.count(), 1);
    ConflictInfo signalConflict = recordedSpy.first().first().value<ConflictInfo>();
    QCOMPARE(signalConflict.sourceId, QStringLiteral("uid1"));

    m_conflictStore->resolveConflict(conflictId, ConflictResolution::SourceWins);

    QCOMPARE(resolvedSpy.count(), 1);
    QCOMPARE(resolvedSpy.first().first().toString(), conflictId);
}

// ============================================================================
// Maintenance tests
// ============================================================================

void TestSyncStore::testVacuum()
{
    // Add and remove data to create fragmentation
    for (int i = 0; i < 100; ++i) {
        m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                    QStringLiteral("uid%1").arg(i),
                                    QStringLiteral("ical-data-%1").arg(i));
    }
    for (int i = 0; i < 100; ++i) {
        m_calBaselines->removeBaseline(QStringLiteral("mapping1"),
                                       QStringLiteral("uid%1").arg(i));
    }

    // Vacuum should not crash
    m_conflictStore->vacuum();

    // Database should still work
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"), QStringLiteral("new-uid"),
                                         QStringLiteral("new-ical")));
    QCOMPARE(m_calBaselines->baseline(QStringLiteral("mapping1"), QStringLiteral("new-uid")),
             QStringLiteral("new-ical"));
}

void TestSyncStore::testClearBackendData()
{
    // clearBackendData in SyncConflictStore clears conflicts for that backend
    ConflictInfo conflict1;
    conflict1.sourceBackendId = QStringLiteral("caldav");
    conflict1.calendarId = QStringLiteral("cal");
    conflict1.sourceId = QStringLiteral("uid1");

    ConflictInfo conflict2;
    conflict2.sourceBackendId = QStringLiteral("local");
    conflict2.calendarId = QStringLiteral("cal");
    conflict2.sourceId = QStringLiteral("uid2");

    m_conflictStore->recordConflict(conflict1);
    m_conflictStore->recordConflict(conflict2);

    m_conflictStore->clearBackendData(QStringLiteral("caldav"));

    // caldav conflicts gone
    for (const ConflictInfo &c : m_conflictStore->unresolvedConflicts()) {
        QVERIFY(c.sourceBackendId != QStringLiteral("caldav"));
    }

    // local conflicts remain
    bool foundLocal = false;
    for (const ConflictInfo &c : m_conflictStore->unresolvedConflicts()) {
        if (c.sourceBackendId == QStringLiteral("local"))
            foundLocal = true;
    }
    QVERIFY(foundLocal);
}

void TestSyncStore::testClearMappingData()
{
    // Add baselines and lastSyncTime for two mappings
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("uid1"), QStringLiteral("ical1")));
    QVERIFY(m_calBaselines->setLastSyncTime(QStringLiteral("mapping1"),
                                             QDateTime::currentDateTimeUtc()));

    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping2"),
                                         QStringLiteral("uid2"), QStringLiteral("ical2")));

    // Clear mapping1
    QVERIFY(m_calBaselines->removeBaselines(QStringLiteral("mapping1")));

    // mapping1 baselines should be gone
    QVERIFY(m_calBaselines->allBaselines(QStringLiteral("mapping1")).isEmpty());

    // mapping2 data should remain
    QCOMPARE(m_calBaselines->baseline(QStringLiteral("mapping2"), QStringLiteral("uid2")),
             QStringLiteral("ical2"));
}

// ============================================================================
// Edge cases
// ============================================================================

void TestSyncStore::testEmptyStrings()
{
    // Empty args should not crash
    m_blobStore->setBaseline(QString(), QStringLiteral("cal"),
                              QStringLiteral("uid"), QStringLiteral("hash"));
    m_blobStore->setBaseline(QStringLiteral("caldav"), QString(),
                              QStringLiteral("uid"), QStringLiteral("hash"));
    m_blobStore->setBaseline(QStringLiteral("caldav"), QStringLiteral("cal"),
                              QString(), QStringLiteral("hash"));
    m_calBaselines->setBaseline(QString(), QStringLiteral("uid"), QStringLiteral("ical"));
    m_calBaselines->setBaseline(QStringLiteral("mapping"), QString(), QStringLiteral("ical"));
}

void TestSyncStore::testSpecialCharacters()
{
    QString specialUid = QStringLiteral("uid-with-'quotes'-and-\"doublequotes\"");
    QString specialIcal = QStringLiteral(
        "BEGIN:VCALENDAR\nDESCRIPTION:value with 'quotes' & \"doubles\"\nEND:VCALENDAR\n");

    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"), specialUid, specialIcal));
    QCOMPARE(m_calBaselines->baseline(QStringLiteral("mapping1"), specialUid), specialIcal);

    // Unicode characters
    QString unicodeUid = QStringLiteral("uid-with-日本語-and-émojis-\U0001f389");
    QString unicodeIcal = QStringLiteral("BEGIN:VCALENDAR\nSUMMARY:日本語 event\nEND:VCALENDAR\n");
    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"), unicodeUid, unicodeIcal));
    QCOMPARE(m_calBaselines->baseline(QStringLiteral("mapping1"), unicodeUid), unicodeIcal);
}

void TestSyncStore::testLargeData()
{
    QString largeIcal;
    largeIcal.reserve(100000);
    largeIcal = QStringLiteral("BEGIN:VCALENDAR\n");
    for (int i = 0; i < 1000; ++i) {
        largeIcal += QStringLiteral("BEGIN:VEVENT\n");
        largeIcal += QStringLiteral("UID:event-%1\n").arg(i);
        largeIcal += QStringLiteral("SUMMARY:Event number %1 with a longer description\n").arg(i);
        largeIcal += QStringLiteral("DESCRIPTION:This is a very long description that ");
        largeIcal += QStringLiteral("contains lots of text to simulate real-world calendar data.\n");
        largeIcal += QStringLiteral("END:VEVENT\n");
    }
    largeIcal += QStringLiteral("END:VCALENDAR\n");

    QVERIFY(m_calBaselines->setBaseline(QStringLiteral("mapping1"),
                                         QStringLiteral("large-uid"), largeIcal));

    QString retrieved = m_calBaselines->baseline(QStringLiteral("mapping1"),
                                                  QStringLiteral("large-uid"));
    QCOMPARE(retrieved, largeIcal);
}

QTEST_MAIN(TestSyncStore)
#include "tst_syncstore.moc"
