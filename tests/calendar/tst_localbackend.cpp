// tst_localbackend.cpp
// Migrated from PlanStan/tests/backends/ (G.9.b Task 71).
// Rewrote storeItems/loadItems/startSync → pushItems/fetchItems (operation API).

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>

#include "localbackend.h"
#include "syncoperation.h"

using namespace Kalburator::Sync;

namespace {

inline KCalendarCore::Incidence::Ptr createTestEvent(const QString &uid, const QString &summary)
{
    KCalendarCore::Event::Ptr event(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTime());
    event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
    return event;
}

inline KCalendarCore::Incidence::Ptr createTestTodo(const QString &uid, const QString &summary)
{
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(uid);
    todo->setSummary(summary);
    todo->setDtDue(QDateTime::currentDateTime().addDays(1));
    return todo;
}

} // namespace

class LocalBackendTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // SyncBackend interface tests
    void testBackendType();
    void testSupportsCalendarCreation();
    void testCreateCalendar();
    void testDeleteCalendar();
    void testCalendarDiscovery();
    void testFetchItems();
    void testPushItems();
    void testMultipleCalendars();

    // LocalBackend-specific tests
    void testIcsFileCreation();
    void testIcsFileFormat();
    void testMultipleIcsFilesInCalendar();
    void testCalendarDirectoryStructure();
    void testHierarchySupport();
    void testIncidenceFilePath();
    void testEmptyCalendarFetch();
    void testCorruptedIcsFile();

    // Phase-2 mapping-skip tests
    void testCalendarFingerprintDeterminism();

private:
    KCalendarCore::Incidence::Ptr createTestEventWithDetails();
};

void LocalBackendTest::initTestCase() {}
void LocalBackendTest::cleanupTestCase() {}
void LocalBackendTest::init() {}
void LocalBackendTest::cleanup() {}

KCalendarCore::Incidence::Ptr LocalBackendTest::createTestEventWithDetails()
{
    KCalendarCore::Event::Ptr event(new KCalendarCore::Event());
    event->setUid("detailed-event-uid");
    event->setSummary("Detailed Event");
    event->setDescription("This event has lots of details");
    event->setLocation("Conference Room A");
    event->setDtStart(QDateTime::currentDateTime());
    event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
    event->setCategories(QStringList() << "Work" << "Meeting");
    return event;
}

// === LocalBackend-Specific Tests ===

void LocalBackendTest::testIcsFileCreation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("test-calendar");

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Test Calendar")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("uid-1"), QStringLiteral("Test Event"));

    auto *op = backend.pushItems(calendarId, items);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    QDir calendarDir(QDir(tempDir.path()).filePath(calendarId));
    QVERIFY(calendarDir.exists());
    QStringList icsFiles = calendarDir.entryList(QStringList() << QStringLiteral("*.ics"), QDir::Files);
    QVERIFY(!icsFiles.isEmpty());
    QVERIFY(icsFiles.contains(QStringLiteral("uid-1.ics")));
}

void LocalBackendTest::testIcsFileFormat()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("test-calendar");

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Test Calendar")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEventWithDetails();

    auto *op = backend.pushItems(calendarId, items);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    const QString icsFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral("/detailed-event-uid.ics"));
    QVERIFY(QFile::exists(icsFilePath));

    QFile file(icsFilePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QVERIFY(content.contains(QStringLiteral("BEGIN:VCALENDAR")));
    QVERIFY(content.contains(QStringLiteral("END:VCALENDAR")));
    QVERIFY(content.contains(QStringLiteral("BEGIN:VEVENT")));
    QVERIFY(content.contains(QStringLiteral("END:VEVENT")));
    QVERIFY(content.contains(QStringLiteral("SUMMARY:Detailed Event")));
    QVERIFY(content.contains(QStringLiteral("LOCATION:Conference Room A")));
}

void LocalBackendTest::testMultipleIcsFilesInCalendar()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("multi-event-calendar");

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Multi Event Calendar")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("uid-1"), QStringLiteral("Event 1"));
    items << createTestEvent(QStringLiteral("uid-2"), QStringLiteral("Event 2"));
    items << createTestEvent(QStringLiteral("uid-3"), QStringLiteral("Event 3"));

    auto *op = backend.pushItems(calendarId, items);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    QDir calendarDir(QDir(tempDir.path()).filePath(calendarId));
    QStringList icsFiles = calendarDir.entryList(QStringList() << QStringLiteral("*.ics"), QDir::Files);

    QCOMPARE(icsFiles.size(), 3);
    QVERIFY(icsFiles.contains(QStringLiteral("uid-1.ics")));
    QVERIFY(icsFiles.contains(QStringLiteral("uid-2.ics")));
    QVERIFY(icsFiles.contains(QStringLiteral("uid-3.ics")));
}

void LocalBackendTest::testCalendarDirectoryStructure()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");

    LocalBackend backend(tempDir.path());

    QVERIFY(backend.createCalendar(collectionId, QStringLiteral("calendar-1"), QStringLiteral("Calendar 1")));
    QVERIFY(backend.createCalendar(collectionId, QStringLiteral("calendar-2"), QStringLiteral("Calendar 2")));

    QDir rootDir(tempDir.path());
    QVERIFY(rootDir.exists(QStringLiteral("calendar-1")));
    QVERIFY(rootDir.exists(QStringLiteral("calendar-2")));
    QVERIFY(QFileInfo(rootDir.filePath(QStringLiteral("calendar-1"))).isDir());
    QVERIFY(QFileInfo(rootDir.filePath(QStringLiteral("calendar-2"))).isDir());
}

void LocalBackendTest::testHierarchySupport()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("hierarchy-calendar");

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Hierarchy Test")));

    auto parentEvent = createTestEvent(QStringLiteral("parent-uid"), QStringLiteral("Parent Event"));
    auto childEvent = createTestEvent(QStringLiteral("child-uid"), QStringLiteral("Child Event"));
    childEvent->setRelatedTo(QStringLiteral("parent-uid"), KCalendarCore::Incidence::RelTypeParent);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << parentEvent << childEvent;

    auto *op = backend.pushItems(calendarId, items);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    QDir calendarDir(QDir(tempDir.path()).filePath(calendarId));
    QVERIFY(calendarDir.exists(QStringLiteral("parent-uid.ics")));
    QVERIFY(calendarDir.exists(QStringLiteral("child-uid.ics")));

    QFile childFile(calendarDir.filePath(QStringLiteral("child-uid.ics")));
    QVERIFY(childFile.open(QIODevice::ReadOnly));
    const QString childContent = QString::fromUtf8(childFile.readAll());
    childFile.close();
    QVERIFY(childContent.contains(QStringLiteral("RELATED-TO")));
}

void LocalBackendTest::testIncidenceFilePath()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("path-test-calendar");

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Path Test")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("special-uid-123"), QStringLiteral("Event"));

    auto *op = backend.pushItems(calendarId, items);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    const QString expectedPath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral("/special-uid-123.ics"));
    QVERIFY(QFile::exists(expectedPath));
}

void LocalBackendTest::testEmptyCalendarFetch()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("empty-calendar");

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Empty Calendar")));

    auto *fetchOp = backend.fetchItems(calendarId);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 5000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    QVERIFY(fetchOp->fetchedItems().isEmpty());
}

void LocalBackendTest::testCorruptedIcsFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("corrupted-calendar");

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Corrupted Test")));

    QDir calendarDir(QDir(tempDir.path()).filePath(calendarId));
    QFile badFile(calendarDir.filePath(QStringLiteral("corrupted.ics")));
    QVERIFY(badFile.open(QIODevice::WriteOnly));
    badFile.write("THIS IS NOT VALID ICAL DATA");
    badFile.close();

    // fetchItems should handle corruption gracefully (not crash).
    auto *fetchOp = backend.fetchItems(calendarId);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 5000);
    // Either succeeds with zero items or fails — either is acceptable; must not crash.
    QVERIFY(fetchOp->state() == SyncOperation::Succeeded || fetchOp->state() == SyncOperation::Failed);
}

// === SyncBackend Interface Tests ===

void LocalBackendTest::testBackendType()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());
    QCOMPARE(backend.backendType(), QStringLiteral("local"));
}

void LocalBackendTest::testSupportsCalendarCreation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.supportsCalendarCreation());
}

void LocalBackendTest::testCreateCalendar()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("calendar-1"), QStringLiteral("Test Calendar")));

    QDir calendarDir(QDir(tempDir.path()).filePath(QStringLiteral("calendar-1")));
    QVERIFY(calendarDir.exists());
}

void LocalBackendTest::testDeleteCalendar()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("delete-me"), QStringLiteral("Delete Me"));

    QVERIFY(backend.deleteCalendar(QStringLiteral("collection-1"), QStringLiteral("delete-me")));

    QDir calendarDir(QDir(tempDir.path()).filePath(QStringLiteral("delete-me")));
    QVERIFY(!calendarDir.exists());
}

void LocalBackendTest::testCalendarDiscovery()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-1"), QStringLiteral("Calendar 1"));
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-2"), QStringLiteral("Calendar 2"));

    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    backend.loadCalendars(QStringLiteral("collection-1"));

    QVERIFY(spy.count() >= 2);
}

void LocalBackendTest::testFetchItems()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("test-cal"), QStringLiteral("Test"));

    auto *fetchOp = backend.fetchItems(QStringLiteral("test-cal"));
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 5000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
}

void LocalBackendTest::testPushItems()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("store-test"), QStringLiteral("Store Test"));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("uid-1"), QStringLiteral("Event 1"));
    items << createTestTodo(QStringLiteral("uid-2"), QStringLiteral("Todo 1"));

    auto *op = backend.pushItems(QStringLiteral("store-test"), items);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    QDir calendarDir(QDir(tempDir.path()).filePath(QStringLiteral("store-test")));
    QVERIFY(calendarDir.exists(QStringLiteral("uid-1.ics")));
}

void LocalBackendTest::testMultipleCalendars()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    LocalBackend backend(tempDir.path());

    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-a"), QStringLiteral("Calendar A"));
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-b"), QStringLiteral("Calendar B"));
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-c"), QStringLiteral("Calendar C"));

    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    backend.loadCalendars(QStringLiteral("collection-1"));

    QVERIFY(spy.count() >= 3);
}

void LocalBackendTest::testCalendarFingerprintDeterminism()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    LocalBackend backend(tmpDir.path());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("test-cal");
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Test")));

    // Through the production surface: the engine consumes the fingerprint as
    // ChangeDetection::collectionRevision (calendarFingerprint went private, P7b.T3).
    const QString fp1 = backend.collectionRevision(calendarId);
    const QString fp2 = backend.collectionRevision(calendarId);
    QCOMPARE(fp1, fp2);
    QVERIFY(!fp1.isEmpty());

    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(QStringLiteral("event-1"));
    event->setSummary(QStringLiteral("evt1"));
    event->setDtStart(QDateTime::currentDateTime());
    QList<KCalendarCore::Incidence::Ptr> items{ event };

    auto *push = backend.pushItems(calendarId, items);
    QTRY_VERIFY_WITH_TIMEOUT(push->isFinished(), 5000);
    QCOMPARE(push->state(), SyncOperation::Succeeded);

    const QString fp3 = backend.collectionRevision(calendarId);
    QVERIFY(fp3 != fp1);
    QCOMPARE(fp3, backend.collectionRevision(calendarId));
}

QTEST_GUILESS_MAIN(LocalBackendTest)
#include "tst_localbackend.moc"
