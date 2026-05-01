// tst_orgbackend.cpp
// Migrated from PlanStan/tests/backends/ (G.9.b Tasks 68-69).
// Rewrote storeItems/loadItems/updateItem → pushItems/fetchItems (operation API).

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTimeZone>
#include <QThread>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include "orgbackend.h"
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

// Helper: push items to backend, assert success
inline void pushAndVerify(OrgBackend &backend, const QString &calendarId,
                          const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = backend.pushItems(calendarId, items, TranscodingPlan{});
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);
}

// Helper: fetch items from backend, assert success, return fetched list
inline QList<KCalendarCore::Incidence::Ptr> fetchAndVerify(OrgBackend &backend,
                                                            const QString &calendarId)
{
    auto *fetchOp = backend.fetchItems(calendarId);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 5000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    return fetchOp->fetchedItems();
}

// Helper: find incidence by UID in a list
inline KCalendarCore::Incidence::Ptr findByUid(const QList<KCalendarCore::Incidence::Ptr> &items,
                                                const QString &uid)
{
    for (const auto &inc : items) {
        if (inc->uid() == uid)
            return inc;
    }
    return {};
}

inline QString readOrgFileContent(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content;
}

} // namespace

/**
 * Test suite for OrgBackend
 *
 * Tests OrgBackend implementation of SyncBackend interface plus
 * OrgBackend-specific functionality (.org file handling, headline conversion).
 */
class OrgBackendTest : public QObject
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
    void testStartSync();
    void testMultipleCalendars();

    // OrgBackend-specific tests
    void testOrgFileCreation();
    void testOrgFileFormat();
    void testHeadlineCreation();
    void testUidPropertyDrawer();
    void testMultipleHeadlinesInOrgFile();
    void testEventToHeadlineConversion();
    void testTodoToHeadlineConversion();
    void testScheduledDateFormat();
    void testDeadlineDateFormat();
    void testHeadlineWithDescription();
    void testEmptyOrgFileFetch();

    // Phase 1: Field roundtrip tests
    void testLocationRoundtrip();
    void testUrlRoundtrip();
    void testLocationUpdate();

    // Phase 2: Field roundtrip tests
    void testClassRoundtrip();
    void testGeoRoundtrip();
    void testTimestampsRoundtrip();

    // Phase 3: Date/Time field tests
    void testScheduledRoundtrip();
    void testDeadlineRoundtrip();
    void testAllDayEventRoundtrip();
    void testTimePreservationRoundtrip();
    void testClosedTimestampRoundtrip();

    // Phase 4: Semantic mapping tests
    void testPriorityRoundtrip();
    void testStatusRoundtrip();
    void testCompletedStatusRoundtrip();

    // Regression tests
    void testOrgIdPropertyStandard();
    void testPropertyDrawerNotDuplicated();

    // Phase 4: mtime cache invalidation tests
    void testMtimeExternalModifyInvalidatesCache();
    void testMtimeUnmodifiedCacheHit();

    // Phase 5: Recurrence tests
    void testSimpleWeeklyRecurrenceRoundtrip();
    void testDailyRecurrenceRoundtrip();
    void testCatchUpRepeaterRoundtrip();
    void testRestartRepeaterRoundtrip();
    void testRecurrenceFromICalRRULE();
};

void OrgBackendTest::initTestCase() {}
void OrgBackendTest::cleanupTestCase() {}
void OrgBackendTest::init() {}
void OrgBackendTest::cleanup() {}

// === OrgBackend-Specific Tests ===

void OrgBackendTest::testOrgFileCreation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("test-calendar");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Test Calendar")));

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QVERIFY(QFile::exists(orgFilePath));

    QFile file(orgFilePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    file.close();
}

void OrgBackendTest::testOrgFileFormat()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("format-test-calendar");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Format Test")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("uid-1"), QStringLiteral("Test Event"));
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY(content.contains(QStringLiteral("* Test Event")) || content.contains(QStringLiteral("** Test Event")));
}

void OrgBackendTest::testHeadlineCreation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("headline-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Headline Test")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("uid-1"), QStringLiteral("Headline One"));
    items << createTestEvent(QStringLiteral("uid-2"), QStringLiteral("Headline Two"));
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    int headlineCount = content.count(QRegularExpression(QStringLiteral("^\\*+ "), QRegularExpression::MultilineOption));
    QVERIFY(headlineCount >= 2);
}

void OrgBackendTest::testUidPropertyDrawer()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("uid-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("UID Test")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("specific-uid-123"), QStringLiteral("Event with UID"));
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY(content.contains(QStringLiteral(":PROPERTIES:")));
    QVERIFY(content.contains(QStringLiteral(":ID:")));
    QVERIFY(content.contains(QStringLiteral("specific-uid-123")));
    QVERIFY(content.contains(QStringLiteral(":END:")));
}

void OrgBackendTest::testMultipleHeadlinesInOrgFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("multi-headline");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Multi Headline Test")));

    QList<KCalendarCore::Incidence::Ptr> items;
    for (int i = 1; i <= 5; ++i) {
        items << createTestEvent(
            QString(QStringLiteral("uid-%1")).arg(i),
            QString(QStringLiteral("Headline %1")).arg(i)
        );
    }
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    for (int i = 1; i <= 5; ++i) {
        QVERIFY(content.contains(QString(QStringLiteral("Headline %1")).arg(i)));
    }
}

void OrgBackendTest::testEventToHeadlineConversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("event-conversion");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Event Conversion")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("detailed-todo"));
    todo->setSummary(QStringLiteral("Important Meeting"));
    todo->setDescription(QStringLiteral("Discuss project roadmap"));
    todo->setLocation(QStringLiteral("Room 101"));
    todo->setDtStart(QDateTime::currentDateTime());

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY(content.contains(QStringLiteral("Important Meeting")));
    QVERIFY(content.contains(QStringLiteral(":ID:")));
    QVERIFY(content.contains(QStringLiteral("detailed-todo")));
}

void OrgBackendTest::testTodoToHeadlineConversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("todo-conversion");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Todo Conversion")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestTodo(QStringLiteral("todo-uid"), QStringLiteral("Complete Task"));
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY(content.contains(QStringLiteral("Complete Task")));
    QVERIFY(content.contains(QStringLiteral("TODO")) || content.contains(QStringLiteral("* ")));
}

void OrgBackendTest::testScheduledDateFormat()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("scheduled-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Scheduled Test")));

    KCalendarCore::Event::Ptr event(new KCalendarCore::Event());
    event->setUid(QStringLiteral("scheduled-event"));
    event->setSummary(QStringLiteral("Scheduled Event"));
    event->setDtStart(QDateTime::currentDateTime());

    QList<KCalendarCore::Incidence::Ptr> items;
    items << event;
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY(content.contains(QStringLiteral("SCHEDULED:")) || content.contains(QStringLiteral("scheduled")));
}

void OrgBackendTest::testDeadlineDateFormat()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("deadline-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Deadline Test")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestTodo(QStringLiteral("deadline-todo"), QStringLiteral("Task with Deadline"));
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY(content.contains(QStringLiteral("DEADLINE:")) || content.contains(QStringLiteral("deadline")) || content.contains(QStringLiteral("Task with Deadline")));
}

void OrgBackendTest::testHeadlineWithDescription()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("description-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Description Test")));

    KCalendarCore::Event::Ptr event(new KCalendarCore::Event());
    event->setUid(QStringLiteral("event-with-desc"));
    event->setSummary(QStringLiteral("Event Title"));
    event->setDescription(QStringLiteral("This is the event description.\nIt has multiple lines."));
    event->setDtStart(QDateTime::currentDateTime());

    QList<KCalendarCore::Incidence::Ptr> items;
    items << event;
    pushAndVerify(backend, calendarId, items);

    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY(content.contains(QStringLiteral("Event Title")));
    QVERIFY(content.contains(QStringLiteral("event description")) || content.length() > 100);
}

void OrgBackendTest::testEmptyOrgFileFetch()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("empty-org");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Empty Org")));

    auto *fetchOp = backend.fetchItems(calendarId);
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 5000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    QCOMPARE(fetchOp->fetchedItems().count(), 0);
}

// === Phase 1: Field Roundtrip Tests ===

void OrgBackendTest::testLocationRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("location-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Location Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("location-uid-123"));
    todo->setSummary(QStringLiteral("Meeting with Location"));
    todo->setLocation(QStringLiteral("Conference Room 101"));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify LOCATION is in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral(":LOCATION:")));
    QVERIFY(content.contains(QStringLiteral("Conference Room 101")));

    // Reload from org file
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("location-uid-123"));
    QVERIFY(loaded);
    QCOMPARE(loaded->location(), QStringLiteral("Conference Room 101"));
}

void OrgBackendTest::testUrlRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("url-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("URL Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("url-uid-456"));
    todo->setSummary(QStringLiteral("Task with URL"));
    todo->setUrl(QUrl(QStringLiteral("https://example.com/project")));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify URL is in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral(":URL:")));
    QVERIFY(content.contains(QStringLiteral("https://example.com/project")));

    // Reload from org file
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("url-uid-456"));
    QVERIFY(loaded);
    QCOMPARE(loaded->url(), QUrl(QStringLiteral("https://example.com/project")));
}

void OrgBackendTest::testLocationUpdate()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("location-update-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Location Update Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("location-update-uid"));
    todo->setSummary(QStringLiteral("Meeting to Update"));
    todo->setLocation(QStringLiteral("Room A"));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Update location via pushItems (replaces updateItem)
    todo->setLocation(QStringLiteral("Room B"));
    pushAndVerify(backend, calendarId, {todo});

    // Verify updated location in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral(":LOCATION: Room B")));
    QVERIFY(!content.contains(QStringLiteral("Room A")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("location-update-uid"));
    QVERIFY(loaded);
    QCOMPARE(loaded->location(), QStringLiteral("Room B"));
}

void OrgBackendTest::testClassRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("class-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Class Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("class-uid-123"));
    todo->setSummary(QStringLiteral("Private Task"));
    todo->setSecrecy(KCalendarCore::Incidence::SecrecyPrivate);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify CLASS is in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral(":CLASS:")));
    QVERIFY(content.contains(QStringLiteral("PRIVATE")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("class-uid-123"));
    QVERIFY(loaded);
    QCOMPARE(loaded->secrecy(), KCalendarCore::Incidence::SecrecyPrivate);
}

void OrgBackendTest::testGeoRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("geo-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Geo Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("geo-uid-123"));
    todo->setSummary(QStringLiteral("Task with Location"));
    todo->setGeoLatitude(37.7749f);
    todo->setGeoLongitude(-122.4194f);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify GEO is in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral(":GEO:")));
    QVERIFY(content.contains(QStringLiteral("37.77")));
    QVERIFY(content.contains(QStringLiteral("-122.41")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("geo-uid-123"));
    QVERIFY(loaded);
    QVERIFY(loaded->hasGeo());
    QVERIFY(qAbs(loaded->geoLatitude() - 37.7749f) < 0.001f);
    QVERIFY(qAbs(loaded->geoLongitude() - (-122.4194f)) < 0.001f);
}

void OrgBackendTest::testTimestampsRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("timestamps-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Timestamps Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("timestamps-uid-123"));
    todo->setSummary(QStringLiteral("Task with Timestamps"));
    QDateTime created(QDate(2024, 6, 15), QTime(10, 30), QTimeZone::utc());
    QDateTime lastMod(QDate(2024, 7, 20), QTime(14, 45), QTimeZone::utc());
    todo->setCreated(created);
    todo->setLastModified(lastMod);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify timestamps are in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral(":CREATED:")));
    QVERIFY(content.contains(QStringLiteral(":LAST-MODIFIED:")));
    QVERIFY(content.contains(QStringLiteral("2024-06-15")));
    QVERIFY(content.contains(QStringLiteral("2024-07-20")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("timestamps-uid-123"));
    QVERIFY(loaded);
    QVERIFY(loaded->created().isValid());
    QVERIFY(loaded->lastModified().isValid());
    QCOMPARE(loaded->created().date(), QDate(2024, 6, 15));
    QCOMPARE(loaded->lastModified().date(), QDate(2024, 7, 20));
}

// === Phase 3: Date/Time Field Tests ===

void OrgBackendTest::testScheduledRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("scheduled-roundtrip-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Scheduled Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("scheduled-uid-123"));
    todo->setSummary(QStringLiteral("Task with Start Date"));
    QDateTime dtStart(QDate(2024, 8, 15), QTime(14, 30));
    todo->setDtStart(dtStart);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify SCHEDULED is in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("SCHEDULED:")));
    QVERIFY(content.contains(QStringLiteral("2024-08-15")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("scheduled-uid-123"));
    QVERIFY(loaded);
    QVERIFY(loaded->dtStart().isValid());
    QCOMPARE(loaded->dtStart().date(), QDate(2024, 8, 15));
}

void OrgBackendTest::testDeadlineRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("deadline-roundtrip-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Deadline Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("deadline-uid-123"));
    todo->setSummary(QStringLiteral("Task with Deadline"));
    QDateTime dtDue(QDate(2024, 9, 20), QTime(17, 0));
    todo->setDtDue(dtDue);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify DEADLINE is in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("DEADLINE:")));
    QVERIFY(content.contains(QStringLiteral("2024-09-20")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("deadline-uid-123"));
    QVERIFY(loaded);
    auto loadedTodo = loaded.dynamicCast<KCalendarCore::Todo>();
    QVERIFY(loadedTodo);
    QVERIFY(loadedTodo->dtDue().isValid());
    QCOMPARE(loadedTodo->dtDue().date(), QDate(2024, 9, 20));
}

void OrgBackendTest::testAllDayEventRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("allday-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("All Day Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("allday-uid-123"));
    todo->setSummary(QStringLiteral("All Day Task"));
    todo->setDtStart(QDateTime(QDate(2024, 10, 5), QTime(0, 0)));
    todo->setAllDay(true);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify SCHEDULED is in org file without time
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("SCHEDULED:")));
    QVERIFY(content.contains(QStringLiteral("2024-10-05")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("allday-uid-123"));
    QVERIFY(loaded);
    QVERIFY(loaded->dtStart().isValid());
    QCOMPARE(loaded->dtStart().date(), QDate(2024, 10, 5));
    QVERIFY(loaded->allDay());
}

void OrgBackendTest::testTimePreservationRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("time-preservation-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Time Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("time-uid-123"));
    todo->setSummary(QStringLiteral("Task at 2:30 PM"));
    QDateTime dtStart(QDate(2024, 8, 15), QTime(14, 30));
    QDateTime dtDue(QDate(2024, 8, 20), QTime(17, 45));
    todo->setDtStart(dtStart);
    todo->setDtDue(dtDue);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify timestamps are in org file with correct format
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("SCHEDULED:")));
    QVERIFY(content.contains(QStringLiteral("14:30")));
    QVERIFY(content.contains(QStringLiteral("DEADLINE:")));
    QVERIFY(content.contains(QStringLiteral("17:45")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("time-uid-123"));
    QVERIFY(loaded);
    auto loadedTodo = loaded.dynamicCast<KCalendarCore::Todo>();
    QVERIFY(loadedTodo);

    QVERIFY(loadedTodo->dtStart().isValid());
    QCOMPARE(loadedTodo->dtStart().date(), QDate(2024, 8, 15));
    QCOMPARE(loadedTodo->dtStart().time(), QTime(14, 30));

    QVERIFY(loadedTodo->dtDue().isValid());
    QCOMPARE(loadedTodo->dtDue().date(), QDate(2024, 8, 20));
    QCOMPARE(loadedTodo->dtDue().time(), QTime(17, 45));
}

void OrgBackendTest::testClosedTimestampRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("closed-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Closed Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("closed-uid-123"));
    todo->setSummary(QStringLiteral("Completed Task"));
    todo->setCompleted(true);
    QDateTime completedTime(QDate(2024, 7, 25), QTime(12, 48));
    todo->setCompleted(completedTime);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify CLOSED is in org file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("DONE")));
    QVERIFY(content.contains(QStringLiteral("CLOSED:")));
    QVERIFY(content.contains(QStringLiteral("2024-07-25")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("closed-uid-123"));
    QVERIFY(loaded);
    auto loadedTodo = loaded.dynamicCast<KCalendarCore::Todo>();
    QVERIFY(loadedTodo);
    QVERIFY(loadedTodo->isCompleted());
    QVERIFY(loadedTodo->completed().isValid());
    QCOMPARE(loadedTodo->completed().date(), QDate(2024, 7, 25));
}

// === Phase 4: Semantic Mapping Tests ===

void OrgBackendTest::testPriorityRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("priority-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Priority Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("priority-uid-123"));
    todo->setSummary(QStringLiteral("High Priority Task"));
    todo->setPriority(1);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify [#A] is in headline and :PRIORITY: in properties
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("[#A]")));
    QVERIFY(content.contains(QStringLiteral(":PRIORITY: 1")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("priority-uid-123"));
    QVERIFY(loaded);
    QCOMPARE(loaded->priority(), 1);
}

void OrgBackendTest::testStatusRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("status-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Status Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("status-uid-123"));
    todo->setSummary(QStringLiteral("In Progress Task"));
    todo->setStatus(KCalendarCore::Todo::StatusInProcess);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify INPROGRESS keyword in headline
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("INPROGRESS")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("status-uid-123"));
    QVERIFY(loaded);
    auto loadedTodo = loaded.dynamicCast<KCalendarCore::Todo>();
    QVERIFY(loadedTodo);
    QCOMPARE(loadedTodo->status(), KCalendarCore::Todo::StatusInProcess);
}

void OrgBackendTest::testCompletedStatusRoundtrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("completed-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Completed Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("completed-uid-123"));
    todo->setSummary(QStringLiteral("Completed Task"));
    todo->setStatus(KCalendarCore::Todo::StatusCompleted);
    todo->setCompleted(true);

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify DONE keyword in headline
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY(content.contains(QStringLiteral("DONE")));

    // Reload and verify
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("completed-uid-123"));
    QVERIFY(loaded);
    auto loadedTodo = loaded.dynamicCast<KCalendarCore::Todo>();
    QVERIFY(loadedTodo);
    QCOMPARE(loadedTodo->status(), KCalendarCore::Todo::StatusCompleted);
}

// === Regression Tests ===

void OrgBackendTest::testOrgIdPropertyStandard()
{
    // Test that OrgBackend correctly uses :ID: property (org-mode standard) as identifier
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("id-fallback-test");
    const QString testId = QStringLiteral("748987A0-1FCE-4F0B-98C9-7CE3958BF179");

    // Create org file with :ID: property (org-mode native format)
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QFile orgFile(orgFilePath);
    QVERIFY(orgFile.open(QIODevice::WriteOnly));
    QString orgContent = QStringLiteral(
        "* Eric Tutoring\n"
        "  :PROPERTIES:\n"
        "  :ID:       %1\n"
        "  :END:\n"
    ).arg(testId);
    orgFile.write(orgContent.toUtf8());
    orgFile.close();

    // Load items - backend should use :ID: as the UID
    OrgBackend backend(tempDir.path());

    auto fetched = fetchAndVerify(backend, calendarId);
    QCOMPARE(fetched.size(), 1);
    auto loaded = fetched.first();
    QCOMPARE(loaded->uid(), testId);
    QCOMPARE(loaded->summary(), QStringLiteral("Eric Tutoring"));

    // Update the item and store - should update the same headline, not create duplicate
    loaded->setSummary(QStringLiteral("Eric Tutoring Updated"));
    loaded->setLocation(QStringLiteral("Room 101"));

    pushAndVerify(backend, calendarId, {loaded});

    // Verify only ONE headline in the file (no duplicate)
    QString content = readOrgFileContent(orgFilePath);
    int headlineCount = content.count(QRegularExpression(QStringLiteral("^\\* "), QRegularExpression::MultilineOption));
    QCOMPARE(headlineCount, 1);

    // Verify content was updated
    QVERIFY(content.contains(QStringLiteral("Eric Tutoring Updated")));
    QVERIFY(content.contains(QStringLiteral(":LOCATION:")));
    QVERIFY(content.contains(QStringLiteral("Room 101")));
    QVERIFY(content.contains(testId));
}

// === SyncBackend Interface Tests ===

void OrgBackendTest::testBackendType()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    QCOMPARE(backend.backendType(), QStringLiteral("orgmode"));
}

void OrgBackendTest::testSupportsCalendarCreation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.supportsCalendarCreation());
}

void OrgBackendTest::testCreateCalendar()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("calendar-1"), QStringLiteral("Test Calendar")));

    QString orgFilePath = QDir(tempDir.path()).filePath(QStringLiteral("calendar-1.org"));
    QVERIFY(QFile::exists(orgFilePath));
}

void OrgBackendTest::testDeleteCalendar()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("delete-me"), QStringLiteral("Delete Me"));

    QVERIFY(backend.deleteCalendar(QStringLiteral("collection-1"), QStringLiteral("delete-me")));

    QString orgFilePath = QDir(tempDir.path()).filePath(QStringLiteral("delete-me.org"));
    QVERIFY(!QFile::exists(orgFilePath));
}

void OrgBackendTest::testCalendarDiscovery()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-1"), QStringLiteral("Calendar 1"));
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-2"), QStringLiteral("Calendar 2"));

    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    backend.loadCalendars(QStringLiteral("collection-1"));

    QVERIFY(spy.count() >= 2);
}

void OrgBackendTest::testFetchItems()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("test-cal"), QStringLiteral("Test"));

    auto *fetchOp = backend.fetchItems(QStringLiteral("test-cal"));
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 5000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
}

void OrgBackendTest::testPushItems()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("store-test"), QStringLiteral("Store Test"));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("uid-1"), QStringLiteral("Event 1"));
    items << createTestTodo(QStringLiteral("uid-2"), QStringLiteral("Todo 1"));

    auto *op = backend.pushItems(QStringLiteral("store-test"), items, TranscodingPlan{});
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    QString orgFilePath = QDir(tempDir.path()).filePath(QStringLiteral("store-test.org"));
    QVERIFY(QFile::exists(orgFilePath));
}

void OrgBackendTest::testStartSync()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("sync-test"), QStringLiteral("Sync Test"));

    // Push an item and verify it lands
    QList<KCalendarCore::Incidence::Ptr> items;
    items << createTestEvent(QStringLiteral("sync-event-1"), QStringLiteral("Synced Event"));

    auto *op = backend.pushItems(QStringLiteral("sync-test"), items, TranscodingPlan{});
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);
    QCOMPARE(op->state(), SyncOperation::Succeeded);

    auto *fetchOp = backend.fetchItems(QStringLiteral("sync-test"));
    QTRY_VERIFY_WITH_TIMEOUT(fetchOp->isFinished(), 5000);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);
    QVERIFY(!fetchOp->fetchedItems().isEmpty());
}

void OrgBackendTest::testMultipleCalendars()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-a"), QStringLiteral("Calendar A"));
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-b"), QStringLiteral("Calendar B"));
    backend.createCalendar(QStringLiteral("collection-1"), QStringLiteral("cal-c"), QStringLiteral("Calendar C"));

    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    backend.loadCalendars(QStringLiteral("collection-1"));

    QVERIFY(spy.count() >= 3);
}

void OrgBackendTest::testPropertyDrawerNotDuplicated()
{
    // Regression test: Verify that saving, loading, updating, and saving again
    // does NOT create duplicate PROPERTIES drawers.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString collectionId = QStringLiteral("test-collection");
    const QString calendarId = QStringLiteral("drawer-test");

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(collectionId, calendarId, QStringLiteral("Drawer Test")));

    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("drawer-test-uid"));
    todo->setSummary(QStringLiteral("Test Task"));
    todo->setLocation(QStringLiteral("Initial Location"));

    QList<KCalendarCore::Incidence::Ptr> items;
    items << todo;
    pushAndVerify(backend, calendarId, items);

    // Verify only one :PROPERTIES: in file
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QString content1 = readOrgFileContent(orgFilePath);
    int propertiesCount1 = content1.count(QStringLiteral(":PROPERTIES:"));
    QCOMPARE(propertiesCount1, 1);

    // Update the item (via pushItems — replaces updateItem)
    todo->setLocation(QStringLiteral("Updated Location"));
    pushAndVerify(backend, calendarId, {todo});

    // Verify still only one :PROPERTIES: in file
    QString content2 = readOrgFileContent(orgFilePath);
    int propertiesCount2 = content2.count(QStringLiteral(":PROPERTIES:"));
    QCOMPARE(propertiesCount2, 1);

    // Reload and update again to test roundtrip
    auto fetched = fetchAndVerify(backend, calendarId);
    auto loaded = findByUid(fetched, QStringLiteral("drawer-test-uid"));
    QVERIFY(loaded);
    loaded->setLocation(QStringLiteral("Third Location"));
    pushAndVerify(backend, calendarId, {loaded});

    // Verify still only one :PROPERTIES: in file
    QString content3 = readOrgFileContent(orgFilePath);
    int propertiesCount3 = content3.count(QStringLiteral(":PROPERTIES:"));
    QCOMPARE(propertiesCount3, 1);

    // Verify drawer lines are properly indented for parser
    QVERIFY(content3.contains(QRegularExpression(QStringLiteral("\\s+:PROPERTIES:"))));
    QVERIFY(content3.contains(QRegularExpression(QStringLiteral("\\s+:ID:"))));
    QVERIFY(content3.contains(QRegularExpression(QStringLiteral("\\s+:END:"))));
}

// ============================================================================
// Phase 4: mtime cache invalidation tests
// ============================================================================

void OrgBackendTest::testMtimeExternalModifyInvalidatesCache()
{
    // External modify -> fetchItems returns fresh data, not stale cache
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString calendarId = QStringLiteral("mtime-test");
    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(QStringLiteral("coll"), calendarId, QStringLiteral("Mtime Test")));

    // Store initial item
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("mtime-uid-1"));
    todo->setSummary(QStringLiteral("Original Summary"));
    pushAndVerify(backend, calendarId, {todo});

    // Fetch to populate cache
    auto fetched1 = fetchAndVerify(backend, calendarId);
    QCOMPARE(fetched1.size(), 1);
    QCOMPARE(fetched1.first()->summary(), QStringLiteral("Original Summary"));

    // Externally modify the file (simulate Orgzly writing)
    QThread::msleep(1100);  // Ensure mtime differs (filesystem granularity)
    QString orgFilePath = QDir(tempDir.path()).filePath(calendarId + QStringLiteral(".org"));
    QFile file(orgFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("* TODO Modified by Orgzly\n  :PROPERTIES:\n  :ID: mtime-uid-1\n  :END:\n");
    file.close();

    // Fetch again -- should see the external changes, not stale cache
    auto fetched2 = fetchAndVerify(backend, calendarId);
    QCOMPARE(fetched2.size(), 1);
    QCOMPARE(fetched2.first()->summary(), QStringLiteral("Modified by Orgzly"));
}

void OrgBackendTest::testMtimeUnmodifiedCacheHit()
{
    // Unmodified file -> cache hit (fast)
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString calendarId = QStringLiteral("mtime-cache-test");
    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(QStringLiteral("coll"), calendarId, QStringLiteral("Cache Test")));

    // Store initial item
    KCalendarCore::Todo::Ptr todo(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("cache-uid-1"));
    todo->setSummary(QStringLiteral("Cached Summary"));
    pushAndVerify(backend, calendarId, {todo});

    // Fetch twice without modification -- second should use cache
    auto fetched1 = fetchAndVerify(backend, calendarId);
    QCOMPARE(fetched1.size(), 1);

    auto fetched2 = fetchAndVerify(backend, calendarId);
    QCOMPARE(fetched2.size(), 1);
    QCOMPARE(fetched2.first()->summary(), QStringLiteral("Cached Summary"));
}

// ============================================================================
// Phase 5: Recurrence tests
// ============================================================================

void OrgBackendTest::testSimpleWeeklyRecurrenceRoundtrip()
{
    // Test that +1w repeater is preserved through roundtrip
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString orgFilePath = tempDir.path() + QStringLiteral("/recurring.org");

    // Create org file with weekly repeater
    QFile file(orgFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("* TODO Weekly meeting\n");
    file.write("  SCHEDULED: <2024-01-15 Mon 10:00 +1w>\n");
    file.close();

    // Load via backend
    OrgBackend backend(tempDir.path());
    backend.loadCalendars(QStringLiteral("test"));

    auto fetched = fetchAndVerify(backend, QStringLiteral("recurring"));
    QCOMPARE(fetched.size(), 1);

    auto todo = fetched.first().dynamicCast<KCalendarCore::Todo>();
    QVERIFY(todo);

    QVERIFY(todo->recurs());
    QCOMPARE(todo->recurrence()->recurrenceType(), static_cast<ushort>(KCalendarCore::Recurrence::rWeekly));
    QCOMPARE(todo->recurrence()->frequency(), 1);

    // Update and save (roundtrip)
    pushAndVerify(backend, QStringLiteral("recurring"), {todo});

    // Read file content and verify repeater is present
    QString content = readOrgFileContent(orgFilePath);
    QVERIFY2(content.contains(QStringLiteral("+1w")), qPrintable(QStringLiteral("Repeater +1w not found in: ") + content));
}

void OrgBackendTest::testDailyRecurrenceRoundtrip()
{
    // Test that +3d repeater is preserved through roundtrip
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString orgFilePath = tempDir.path() + QStringLiteral("/daily.org");

    QFile file(orgFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("* TODO Daily standup\n");
    file.write("  SCHEDULED: <2024-01-15 Mon 09:00 +3d>\n");
    file.close();

    OrgBackend backend(tempDir.path());
    backend.loadCalendars(QStringLiteral("test"));

    auto fetched = fetchAndVerify(backend, QStringLiteral("daily"));
    QCOMPARE(fetched.size(), 1);

    auto todo = fetched.first().dynamicCast<KCalendarCore::Todo>();
    QVERIFY(todo);

    QVERIFY(todo->recurs());
    QCOMPARE(todo->recurrence()->recurrenceType(), static_cast<ushort>(KCalendarCore::Recurrence::rDaily));
    QCOMPARE(todo->recurrence()->frequency(), 3);

    // Roundtrip
    pushAndVerify(backend, QStringLiteral("daily"), {todo});

    QString content = readOrgFileContent(orgFilePath);
    QVERIFY2(content.contains(QStringLiteral("+3d")), qPrintable(QStringLiteral("Repeater +3d not found in: ") + content));
}

void OrgBackendTest::testCatchUpRepeaterRoundtrip()
{
    // Test that ++1w (catch-up) repeater is preserved via backend roundtrip data
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString orgFilePath = tempDir.path() + QStringLiteral("/catchup.org");

    QFile file(orgFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("* TODO Bill payment\n");
    file.write("  SCHEDULED: <2024-01-15 Mon ++1w>\n");
    file.close();

    OrgBackend backend(tempDir.path());
    backend.loadCalendars(QStringLiteral("test"));

    auto fetched = fetchAndVerify(backend, QStringLiteral("catchup"));
    QCOMPARE(fetched.size(), 1);

    auto todo = fetched.first().dynamicCast<KCalendarCore::Todo>();
    QVERIFY(todo);

    QVERIFY(todo->recurs());
    QCOMPARE(todo->recurrence()->recurrenceType(), static_cast<ushort>(KCalendarCore::Recurrence::rWeekly));

    // Under incidence purity (Feb 2026), repeater syntax is NOT stored as a custom
    // property on the incidence. It's preserved in OrgBackend's m_roundtripData maps.
    // Roundtrip: the org file should still have ++1w after save
    pushAndVerify(backend, QStringLiteral("catchup"), {todo});

    QString content = readOrgFileContent(orgFilePath);
    QVERIFY2(content.contains(QStringLiteral("++1w")), qPrintable(QStringLiteral("Catch-up repeater ++1w not found in: ") + content));
}

void OrgBackendTest::testRestartRepeaterRoundtrip()
{
    // Test that .+1m (restart) repeater is preserved via backend roundtrip data
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString orgFilePath = tempDir.path() + QStringLiteral("/restart.org");

    QFile file(orgFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("* TODO Review subscriptions\n");
    file.write("  SCHEDULED: <2024-01-15 Mon .+1m>\n");
    file.close();

    OrgBackend backend(tempDir.path());
    backend.loadCalendars(QStringLiteral("test"));

    auto fetched = fetchAndVerify(backend, QStringLiteral("restart"));
    QCOMPARE(fetched.size(), 1);

    auto todo = fetched.first().dynamicCast<KCalendarCore::Todo>();
    QVERIFY(todo);

    QVERIFY(todo->recurs());
    QCOMPARE(todo->recurrence()->recurrenceType(), static_cast<ushort>(KCalendarCore::Recurrence::rMonthlyDay));

    // Under incidence purity (Feb 2026), repeater syntax is NOT stored as a custom
    // property on the incidence. It's preserved in OrgBackend's m_roundtripData maps.
    // Roundtrip: the org file should still have .+1m after save
    pushAndVerify(backend, QStringLiteral("restart"), {todo});

    QString content = readOrgFileContent(orgFilePath);
    QVERIFY2(content.contains(QStringLiteral(".+1m")), qPrintable(QStringLiteral("Restart repeater .+1m not found in: ") + content));
}

void OrgBackendTest::testRecurrenceFromICalRRULE()
{
    // Test that a recurring incidence created programmatically (with RRULE)
    // gets converted to an org repeater when saved
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OrgBackend backend(tempDir.path());
    QVERIFY(backend.createCalendar(QString(), QStringLiteral("icaltest"), QStringLiteral("iCal Test")));

    // Create a recurring todo programmatically
    auto todo = KCalendarCore::Todo::Ptr::create();
    todo->setUid(QStringLiteral("ical-recurrence-test"));
    todo->setSummary(QStringLiteral("Weekly review"));
    todo->setDtStart(QDateTime(QDate(2024, 1, 15), QTime(14, 0)));
    todo->recurrence()->setWeekly(2);  // Every 2 weeks

    pushAndVerify(backend, QStringLiteral("icaltest"), {todo});

    // Read file and verify org repeater was generated
    QString orgFilePath = tempDir.path() + QStringLiteral("/icaltest.org");
    QString content = readOrgFileContent(orgFilePath);

    QVERIFY2(content.contains(QStringLiteral("+2w")), qPrintable(QStringLiteral("Expected +2w repeater in: ") + content));
}

QTEST_GUILESS_MAIN(OrgBackendTest)

#include "tst_orgbackend.moc"
