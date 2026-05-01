// tst_decsyncbackend.cpp
// Migrated from PlanStan/tests/backends/ (G.9.b Task 70).
// Rewrote storeItems/loadItems/updateItem → pushItems/fetchItems (operation API).

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/ICalFormat>
#include "decsyncbackend.h"
#include "decsynclib.h"
#include "backendcapabilities.h"
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

/**
 * Test suite for DecSyncBackend
 *
 * Tests SyncBackend interface compliance, calendar/task discovery,
 * CRUD operations, interop simulation, and round-trip verification.
 */
class DecSyncBackendTest : public QObject
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
    void testCapabilities();

    // Calendar discovery
    void testCalendarDiscovery();
    void testCalendarDiscoverySkipsDeleted();
    void testTaskDiscovery();

    // Calendar CRUD
    void testCreateCalendar();
    void testCreateCalendarWithColor();
    void testDeleteCalendar();
    void testUpdateCalendarName();
    void testUpdateCalendarColor();

    // Item CRUD
    void testStoreAndLoadItems();
    void testUpdateItem();
    void testRemoveItem();
    void testStartSync();

    // Operation-based API
    void testFetchItems();
    void testPushItems();
    void testDeleteItems();

    // Tasks collection
    void testTasksCollection();
    void testDiscoveredCalendarType();

    // Interop simulation
    void testReadExternalEntries();
    void testRoundTripFormat();

    // Raw ICS access
    void testGetRawIcs();
    void testSetRawIcs();

    // Type differentiation (DecSync standard compliance)
    void testDiscoveredCalendarTypeEvent();
    void testCapabilitiesHybrid();
    void testStoreRejectsWrongType();
    void testUpdateRejectsWrongType();
    void testPushRejectsWrongType();
    void testStartSyncRejectsWrongType();
    void testLoadDetectsTypeViolation();
    void testFetchDetectsTypeViolation();
    void testCreateCalendarRoutesEvent();

    // Hybrid transparent split tests
    void testCreateHybridCalendar();
    void testHybridDiscovery();
    void testHybridStoreAndLoad();
    void testHybridPushItems();
    void testHybridStartSync();
    void testHybridRemoveItem();
    void testHybridDeleteCalendar();
    void testStandaloneTasksUnaffected();
    void testStandaloneCalendarsUnaffected();

private:
    void createDecsyncDir(const QString &path);
    void writeExternalEntry(const QString &decsyncDir, const QString &syncType,
                            const QString &collection, const QString &appId,
                            const DecSyncEntry &entry);

    QTemporaryDir *m_tempDir = nullptr;
    QString m_decsyncDir;
};

void DecSyncBackendTest::initTestCase()
{
}

void DecSyncBackendTest::cleanupTestCase()
{
}

void DecSyncBackendTest::init()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_decsyncDir = m_tempDir->path() + "/DecSync";
}

void DecSyncBackendTest::cleanup()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void DecSyncBackendTest::createDecsyncDir(const QString &path)
{
    QDir().mkpath(path);
    QFile file(path + "/.decsync-info");
    file.open(QIODevice::WriteOnly);
    file.write(R"({"version":2})");
    file.close();
}

void DecSyncBackendTest::writeExternalEntry(const QString &decsyncDir, const QString &syncType,
                                             const QString &collection, const QString &appId,
                                             const DecSyncEntry &entry)
{
    QString collDir = decsyncDir + "/" + syncType + "/" + collection;
    QString appDir = collDir + "/v2/" + appId;
    QDir().mkpath(appDir);

    QString hash = DecSyncHash::pathToHash(entry.path);
    QString filePath = appDir + "/" + hash;

    QFile file(filePath);
    file.open(QIODevice::Append | QIODevice::Text);
    file.write(entry.toLine().toUtf8());
    file.write("\n");
    file.close();

    // Update sequences
    QString seqPath = appDir + "/sequences";
    QMap<QString, int> sequences;
    QFile seqFile(seqPath);
    if (seqFile.exists() && seqFile.open(QIODevice::ReadOnly)) {
        QJsonObject obj = QJsonDocument::fromJson(seqFile.readAll()).object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            sequences[it.key()] = it.value().toInt();
        }
        seqFile.close();
    }
    sequences[hash] = sequences.value(hash, 0) + 1;

    QJsonObject seqObj;
    for (auto it = sequences.constBegin(); it != sequences.constEnd(); ++it) {
        seqObj[it.key()] = it.value();
    }
    seqFile.open(QIODevice::WriteOnly);
    seqFile.write(QJsonDocument(seqObj).toJson(QJsonDocument::Compact));
    seqFile.close();
}

// ============================================================================
// SyncBackend interface tests
// ============================================================================

void DecSyncBackendTest::testBackendType()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    QCOMPARE(backend.backendType(), QStringLiteral("decsync"));
}

void DecSyncBackendTest::testSupportsCalendarCreation()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    QVERIFY(backend.supportsCalendarCreation());
}

void DecSyncBackendTest::testCapabilities()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    auto caps = backend.capabilities();

    QCOMPARE(caps.backendType, QStringLiteral("decsync"));
    QVERIFY(caps.incidenceSupport.supportsEvents);
    QVERIFY(caps.incidenceSupport.supportsTodos);
    QVERIFY(caps.calendarCrud.supportsCreate);
    QVERIFY(caps.calendarCrud.supportsDelete);
    QVERIFY(caps.calendarCrud.supportsColor);
    QVERIFY(!caps.calendarCrud.supportsDescription);
    QVERIFY(!caps.calendarCrud.supportsOrder);
    QVERIFY(caps.syncCharacteristics.supportsDeltaSync);
    QVERIFY(!caps.syncCharacteristics.requiresFullFetch);
}

// ============================================================================
// Calendar discovery
// ============================================================================

void DecSyncBackendTest::testCalendarDiscovery()
{
    createDecsyncDir(m_decsyncDir);

    // Create calendar collections manually
    DecSyncCollection coll1(m_decsyncDir + "/calendars/personal", QStringLiteral("other-app"));
    coll1.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                   QJsonValue(QStringLiteral("Personal")));

    DecSyncCollection coll2(m_decsyncDir + "/calendars/work", QStringLiteral("other-app"));
    coll2.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                   QJsonValue(QStringLiteral("Work")));

    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    QSignalSpy finishedSpy(&backend, &SyncBackend::loadCalendarsFinished);

    backend.loadCalendars(QStringLiteral("test-collection"));

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy[0][1].toBool());  // success=true
    QCOMPARE(spy.count(), 2);

    QStringList discovered;
    for (int i = 0; i < spy.count(); ++i) {
        discovered.append(spy[i][1].toString());
    }
    QVERIFY(discovered.contains(QStringLiteral("personal")));
    QVERIFY(discovered.contains(QStringLiteral("work")));
}

void DecSyncBackendTest::testCalendarDiscoverySkipsDeleted()
{
    createDecsyncDir(m_decsyncDir);

    // Create a deleted calendar
    DecSyncCollection coll(m_decsyncDir + "/calendars/deleted-cal", QStringLiteral("other-app"));
    coll.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                   QJsonValue(QStringLiteral("Deleted Cal")));
    coll.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("deleted")),
                   QJsonValue(true));

    // And a non-deleted one
    DecSyncCollection coll2(m_decsyncDir + "/calendars/active-cal", QStringLiteral("other-app"));
    coll2.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                   QJsonValue(QStringLiteral("Active Cal")));

    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);

    backend.loadCalendars(QStringLiteral("test-collection"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][1].toString(), QStringLiteral("active-cal"));
}

void DecSyncBackendTest::testTaskDiscovery()
{
    createDecsyncDir(m_decsyncDir);

    DecSyncCollection coll(m_decsyncDir + "/tasks/shopping", QStringLiteral("other-app"));
    coll.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                   QJsonValue(QStringLiteral("Shopping")));

    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);

    backend.loadCalendars(QStringLiteral("test-collection"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][1].toString(), QStringLiteral("tasks/shopping"));
}

// ============================================================================
// Calendar CRUD
// ============================================================================

void DecSyncBackendTest::testCreateCalendar()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    QVERIFY(backend.createCalendar(QStringLiteral("coll"), QStringLiteral("my-cal"),
                                    QStringLiteral("My Calendar"), CalendarType::Event));

    // Verify directory was created
    QVERIFY(QDir(m_decsyncDir + "/calendars/my-cal/v2/test-app").exists());

    // Verify name was set
    auto name = backend.discoveredDisplayName(QStringLiteral("my-cal"));
    QCOMPARE(name, QStringLiteral("My Calendar"));
}

void DecSyncBackendTest::testCreateCalendarWithColor()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    QVERIFY(backend.createCalendar(QStringLiteral("coll"), QStringLiteral("colored-cal"),
                                    QStringLiteral("Colored"), CalendarType::Event));
    QVERIFY(backend.updateCalendar(QStringLiteral("coll"), QStringLiteral("colored-cal"),
                                    {{QStringLiteral("color"), QStringLiteral("#ff0000")}}));

    QColor color = backend.calendarColor(QStringLiteral("colored-cal"));
    QVERIFY(color.isValid());
    QCOMPARE(color.name(), QStringLiteral("#ff0000"));
}

void DecSyncBackendTest::testDeleteCalendar()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("to-delete"),
                           QStringLiteral("To Delete"), CalendarType::Event);

    QVERIFY(backend.deleteCalendar(QStringLiteral("coll"), QStringLiteral("to-delete")));

    // Verify soft-delete: directory still exists but deleted=true
    QVERIFY(QDir(m_decsyncDir + "/calendars/to-delete").exists());

    // Should not appear in discovery
    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    backend.loadCalendars(QStringLiteral("coll"));
    QCOMPARE(spy.count(), 0);
}

void DecSyncBackendTest::testUpdateCalendarName()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("rename-me"),
                           QStringLiteral("Old Name"), CalendarType::Event);

    QVERIFY(backend.updateCalendar(QStringLiteral("coll"), QStringLiteral("rename-me"),
                                    {{QStringLiteral("displayName"), QStringLiteral("New Name")}}));

    QCOMPARE(backend.discoveredDisplayName(QStringLiteral("rename-me")),
             QStringLiteral("New Name"));
}

void DecSyncBackendTest::testUpdateCalendarColor()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("color-cal"),
                           QStringLiteral("Color Cal"), CalendarType::Event);

    QVERIFY(backend.updateCalendar(QStringLiteral("coll"), QStringLiteral("color-cal"),
                                    {{QStringLiteral("color"), QStringLiteral("#00ff00")}}));

    QColor color = backend.calendarColor(QStringLiteral("color-cal"));
    QCOMPARE(color.name(), QStringLiteral("#00ff00"));
}

// ============================================================================
// Item CRUD
// ============================================================================

void DecSyncBackendTest::testStoreAndLoadItems()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("store-cal"),
                           QStringLiteral("Store Cal"), CalendarType::Event);

    auto event = createTestEvent(QStringLiteral("event-1"), QStringLiteral("Test Event"));

    // Push items
    PushOperation *pushOp = backend.pushItems(QStringLiteral("store-cal"), {event}, TranscodingPlan{});
    QVERIFY(pushOp);
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Fetch items back
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("store-cal"));
    QVERIFY(fetchOp);
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchOp->state(), SyncOperation::Succeeded);

    auto loaded = fetchOp->fetchedItems();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded[0]->uid(), QStringLiteral("event-1"));
    QCOMPARE(loaded[0]->summary(), QStringLiteral("Test Event"));

    delete fetchOp;
}

void DecSyncBackendTest::testUpdateItem()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("update-cal"),
                           QStringLiteral("Update Cal"), CalendarType::Event);

    auto event = createTestEvent(QStringLiteral("update-uid"), QStringLiteral("Original"));

    // Push original
    PushOperation *pushOp = backend.pushItems(QStringLiteral("update-cal"), {event}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Update: change summary and push again (replaces by uid)
    event->setSummary(QStringLiteral("Updated"));
    PushOperation *updateOp = backend.pushItems(QStringLiteral("update-cal"), {event}, TranscodingPlan{});
    QSignalSpy updateSpy(updateOp, &SyncOperation::finished);
    QTRY_COMPARE(updateSpy.count(), 1);
    QCOMPARE(updateOp->state(), SyncOperation::Succeeded);
    delete updateOp;

    // Reload and verify
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("update-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);

    auto items = fetchOp->fetchedItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0]->summary(), QStringLiteral("Updated"));

    delete fetchOp;
}

void DecSyncBackendTest::testRemoveItem()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("remove-cal"),
                           QStringLiteral("Remove Cal"), CalendarType::Event);

    auto event = createTestEvent(QStringLiteral("remove-uid"), QStringLiteral("To Remove"));

    // Push item
    PushOperation *pushOp = backend.pushItems(QStringLiteral("remove-cal"), {event}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Remove it
    backend.removeItem(QStringLiteral("remove-cal"), QStringLiteral("remove-uid"));

    // Fetch -- should be empty
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("remove-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);

    QCOMPARE(fetchOp->fetchedItems().size(), 0);

    delete fetchOp;
}

void DecSyncBackendTest::testStartSync()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("sync-cal"),
                           QStringLiteral("Sync Cal"), CalendarType::Event);

    auto creation = createTestEvent(QStringLiteral("created-uid"), QStringLiteral("Created"));
    auto update = createTestEvent(QStringLiteral("updated-uid"), QStringLiteral("Updated"));

    // Pre-store the item that will be "updated"
    PushOperation *prePushOp = backend.pushItems(QStringLiteral("sync-cal"), {update}, TranscodingPlan{});
    QSignalSpy prePushSpy(prePushOp, &SyncOperation::finished);
    QTRY_COMPARE(prePushSpy.count(), 1);
    QCOMPARE(prePushOp->state(), SyncOperation::Succeeded);
    delete prePushOp;

    // Push both (creation + updated version of update)
    PushOperation *pushOp = backend.pushItems(QStringLiteral("sync-cal"), {creation, update}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Verify both items exist
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("sync-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchOp->fetchedItems().size(), 2);

    delete fetchOp;
}

// ============================================================================
// Operation-based API
// ============================================================================

void DecSyncBackendTest::testFetchItems()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("fetch-cal"),
                           QStringLiteral("Fetch Cal"), CalendarType::Event);

    // Push items first
    PushOperation *prePushOp = backend.pushItems(QStringLiteral("fetch-cal"), {
        createTestEvent(QStringLiteral("f1"), QStringLiteral("Fetch 1")),
        createTestEvent(QStringLiteral("f2"), QStringLiteral("Fetch 2"))
    }, TranscodingPlan{});
    QSignalSpy prePushSpy(prePushOp, &SyncOperation::finished);
    QTRY_COMPARE(prePushSpy.count(), 1);
    QCOMPARE(prePushOp->state(), SyncOperation::Succeeded);
    delete prePushOp;

    FetchOperation *op = backend.fetchItems(QStringLiteral("fetch-cal"));
    QVERIFY(op);

    QSignalSpy finishedSpy(op, &SyncOperation::finished);
    QTRY_COMPARE(finishedSpy.count(), 1);

    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->fetchedItems().size(), 2);

    delete op;
}

void DecSyncBackendTest::testPushItems()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("push-cal"),
                           QStringLiteral("Push Cal"), CalendarType::Event);

    auto event1 = createTestEvent(QStringLiteral("p1"), QStringLiteral("Push 1"));
    auto event2 = createTestEvent(QStringLiteral("p2"), QStringLiteral("Push 2"));

    PushOperation *op = backend.pushItems(QStringLiteral("push-cal"), {event1, event2}, TranscodingPlan{});
    QVERIFY(op);

    QSignalSpy finishedSpy(op, &SyncOperation::finished);
    QTRY_COMPARE(finishedSpy.count(), 1);

    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 2);

    // Verify items were written
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("push-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchOp->fetchedItems().size(), 2);

    delete fetchOp;
    delete op;
}

void DecSyncBackendTest::testDeleteItems()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("del-cal"),
                           QStringLiteral("Del Cal"), CalendarType::Event);

    // Push items first
    PushOperation *prePushOp = backend.pushItems(QStringLiteral("del-cal"), {
        createTestEvent(QStringLiteral("d1"), QStringLiteral("Del 1")),
        createTestEvent(QStringLiteral("d2"), QStringLiteral("Del 2"))
    }, TranscodingPlan{});
    QSignalSpy prePushSpy(prePushOp, &SyncOperation::finished);
    QTRY_COMPARE(prePushSpy.count(), 1);
    QCOMPARE(prePushOp->state(), SyncOperation::Succeeded);
    delete prePushOp;

    DeleteOperation *op = backend.deleteItems(QStringLiteral("del-cal"), {QStringLiteral("d1")});
    QVERIFY(op);

    QSignalSpy finishedSpy(op, &SyncOperation::finished);
    QTRY_COMPARE(finishedSpy.count(), 1);

    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 1);

    // Verify only d2 remains
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("del-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchOp->fetchedItems().size(), 1);
    QCOMPARE(fetchOp->fetchedItems()[0]->uid(), QStringLiteral("d2"));

    delete fetchOp;
    delete op;
}

// ============================================================================
// Tasks collection
// ============================================================================

void DecSyncBackendTest::testTasksCollection()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("tasks/groceries"),
                           QStringLiteral("Groceries"), CalendarType::Todo);

    // Verify directory created under tasks/
    QVERIFY(QDir(m_decsyncDir + "/tasks/groceries").exists());

    // Push a todo item
    auto todo = createTestTodo(QStringLiteral("todo-1"), QStringLiteral("Buy milk"));
    PushOperation *pushOp = backend.pushItems(QStringLiteral("tasks/groceries"), {todo}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Fetch back
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("tasks/groceries"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);

    auto items = fetchOp->fetchedItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0]->uid(), QStringLiteral("todo-1"));

    delete fetchOp;
}

void DecSyncBackendTest::testDiscoveredCalendarType()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    // calendars/ collections are Event type per DecSync standard
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("my-cal")), CalendarType::Event);
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("tasks/my-tasks")), CalendarType::Todo);
}

// ============================================================================
// Interop simulation
// ============================================================================

void DecSyncBackendTest::testReadExternalEntries()
{
    createDecsyncDir(m_decsyncDir);

    // Simulate another DecSync app writing entries
    KCalendarCore::ICalFormat icalFormat;
    auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
    );
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(QStringLiteral("ext-uid-1"));
    event->setSummary(QStringLiteral("External Event"));
    event->setDtStart(QDateTime(QDate(2024, 6, 15), QTime(10, 0), QTimeZone::utc()));
    event->setDtEnd(QDateTime(QDate(2024, 6, 15), QTime(11, 0), QTimeZone::utc()));
    tempCal->addIncidence(event);
    QString icalStr = icalFormat.toString(tempCal);

    DecSyncEntry entry;
    entry.path = {QStringLiteral("resources"), QStringLiteral("ext-uid-1")};
    entry.datetime = QStringLiteral("2024-06-15T10:00:00");
    entry.key = QJsonValue();
    entry.value = QJsonValue(icalStr);

    writeExternalEntry(m_decsyncDir, QStringLiteral("calendars"),
                       QStringLiteral("external-cal"), QStringLiteral("android-app"), entry);

    // Also write an info entry for the collection name
    DecSyncEntry nameEntry;
    nameEntry.path = {QStringLiteral("info")};
    nameEntry.datetime = QStringLiteral("2024-06-15T09:00:00");
    nameEntry.key = QJsonValue(QStringLiteral("name"));
    nameEntry.value = QJsonValue(QStringLiteral("External Cal"));
    writeExternalEntry(m_decsyncDir, QStringLiteral("calendars"),
                       QStringLiteral("external-cal"), QStringLiteral("android-app"), nameEntry);

    // Read with test backend
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("planstan-test"));
    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    backend.loadCalendars(QStringLiteral("coll"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][1].toString(), QStringLiteral("external-cal"));

    // Fetch items
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("external-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);

    auto items = fetchOp->fetchedItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0]->uid(), QStringLiteral("ext-uid-1"));
    QCOMPARE(items[0]->summary(), QStringLiteral("External Event"));

    delete fetchOp;
}

void DecSyncBackendTest::testRoundTripFormat()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("roundtrip"),
                           QStringLiteral("Roundtrip"), CalendarType::Event);

    // Write an event with full details
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(QStringLiteral("rt-uid-1"));
    event->setSummary(QStringLiteral("Roundtrip Event"));
    event->setDescription(QStringLiteral("A detailed description"));
    event->setLocation(QStringLiteral("Conference Room"));
    event->setDtStart(QDateTime(QDate(2024, 6, 15), QTime(10, 0), QTimeZone::utc()));
    event->setDtEnd(QDateTime(QDate(2024, 6, 15), QTime(11, 0), QTimeZone::utc()));
    event->setCategories({QStringLiteral("Work"), QStringLiteral("Meeting")});

    PushOperation *pushOp = backend.pushItems(QStringLiteral("roundtrip"), {event}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Verify the on-disk format is valid DecSync
    DecSyncCollection coll(m_decsyncDir + "/calendars/roundtrip", QStringLiteral("test-app"));
    QMap<QString, DecSyncEntry> resources = coll.readAllResources();

    QCOMPARE(resources.size(), 1);
    QVERIFY(resources.contains(QStringLiteral("rt-uid-1")));

    // Value should be parseable iCalendar
    QString icalData = resources[QStringLiteral("rt-uid-1")].value.toString();
    QVERIFY(icalData.contains(QStringLiteral("VCALENDAR")));
    QVERIFY(icalData.contains(QStringLiteral("VEVENT")));
    QVERIFY(icalData.contains(QStringLiteral("Roundtrip Event")));

    // Verify the entry path format
    QCOMPARE(resources[QStringLiteral("rt-uid-1")].path,
             (QStringList{QStringLiteral("resources"), QStringLiteral("rt-uid-1")}));

    // Verify null key (per DecSync contacts-calendars spec)
    QVERIFY(resources[QStringLiteral("rt-uid-1")].key.isNull());
}

// ============================================================================
// Raw ICS access
// ============================================================================

void DecSyncBackendTest::testGetRawIcs()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("raw-cal"),
                           QStringLiteral("Raw Cal"), CalendarType::Event);

    PushOperation *pushOp = backend.pushItems(QStringLiteral("raw-cal"),
                                              {createTestEvent(QStringLiteral("raw-uid"), QStringLiteral("Raw Event"))},
                                              TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    QString raw = backend.getRawIcs(QStringLiteral("raw-cal"), QStringLiteral("raw-uid"));
    QVERIFY(!raw.isEmpty());
    QVERIFY(raw.contains(QStringLiteral("VCALENDAR")));
    QVERIFY(raw.contains(QStringLiteral("raw-uid")));
}

void DecSyncBackendTest::testSetRawIcs()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("set-raw-cal"),
                           QStringLiteral("Set Raw Cal"), CalendarType::Event);

    QString icsContent = QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//Test//Test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:set-raw-uid\r\n"
        "SUMMARY:Set Raw Event\r\n"
        "DTSTART:20240615T100000Z\r\n"
        "DTEND:20240615T110000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n");

    QVERIFY(backend.setRawIcs(QStringLiteral("set-raw-cal"), QStringLiteral("set-raw-uid"), icsContent));

    QString retrieved = backend.getRawIcs(QStringLiteral("set-raw-cal"), QStringLiteral("set-raw-uid"));
    QVERIFY(retrieved.contains(QStringLiteral("Set Raw Event")));
}

// ============================================================================
// Type differentiation tests
// ============================================================================

void DecSyncBackendTest::testDiscoveredCalendarTypeEvent()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    // Bare ID maps to calendars/ -> Event type
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("personal")), CalendarType::Event);
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("work")), CalendarType::Event);

    // tasks/ prefix -> Todo type
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("tasks/shopping")), CalendarType::Todo);
}

void DecSyncBackendTest::testCapabilitiesHybrid()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    auto caps = backend.capabilities();

    // DecSync supports hybrid via transparent split
    QVERIFY(caps.incidenceSupport.supportsHybrid);
    QVERIFY(!caps.incidenceSupport.perCalendarRestrictions);
    QVERIFY(!caps.incidenceSupport.supportsJournals);

    // Supports events and todos
    QVERIFY(caps.incidenceSupport.supportsEvents);
    QVERIFY(caps.incidenceSupport.supportsTodos);

    // supportsCalendarType should reflect this
    QVERIFY(caps.supportsCalendarType(CalendarType::Event));
    QVERIFY(caps.supportsCalendarType(CalendarType::Todo));
    QVERIFY(caps.supportsCalendarType(CalendarType::Hybrid));
}

void DecSyncBackendTest::testStoreRejectsWrongType()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("event-cal"),
                           QStringLiteral("Events Only"), CalendarType::Event);

    // Push a todo in an event calendar — should auto-promote to hybrid
    auto todo = createTestTodo(QStringLiteral("bad-todo"), QStringLiteral("Was Wrong Type"));
    auto event = createTestEvent(QStringLiteral("good-event"), QStringLiteral("Right Type"));

    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    PushOperation *pushOp = backend.pushItems(QStringLiteral("event-cal"), {todo, event}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);

    // Auto-promotion: no error, both items stored
    QCOMPARE(errorSpy.count(), 0);

    // Both items should have been stored (calendar auto-promoted to hybrid)
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("event-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchOp->fetchedItems().size(), 2);

    // Calendar should now be hybrid
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("event-cal")), CalendarType::Hybrid);

    delete fetchOp;
    delete pushOp;
}

void DecSyncBackendTest::testUpdateRejectsWrongType()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("event-cal-upd"),
                           QStringLiteral("Events Only"), CalendarType::Event);

    // Push a todo in an event calendar — should auto-promote to hybrid
    auto todo = createTestTodo(QStringLiteral("bad-todo-upd"), QStringLiteral("Was Wrong Type"));

    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    PushOperation *pushOp = backend.pushItems(QStringLiteral("event-cal-upd"), {todo}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);

    // Auto-promotion: no error
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("event-cal-upd")), CalendarType::Hybrid);

    delete pushOp;
}

void DecSyncBackendTest::testPushRejectsWrongType()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("tasks/push-tasks"),
                           QStringLiteral("Tasks Only"), CalendarType::Todo);

    // Try to push an event to a task collection
    auto event = createTestEvent(QStringLiteral("bad-event"), QStringLiteral("Wrong Type"));
    auto todo = createTestTodo(QStringLiteral("good-todo"), QStringLiteral("Right Type"));

    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    PushOperation *op = backend.pushItems(QStringLiteral("tasks/push-tasks"), {event, todo}, TranscodingPlan{});

    QSignalSpy finishedSpy(op, &SyncOperation::finished);
    QTRY_COMPARE(finishedSpy.count(), 1);

    // Event should be rejected, todo should succeed
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(op->succeededUids().size(), 1);
    QCOMPARE(op->succeededUids()[0], QStringLiteral("good-todo"));

    delete op;
}

void DecSyncBackendTest::testStartSyncRejectsWrongType()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("sync-event-cal"),
                           QStringLiteral("Events Only"), CalendarType::Event);

    auto event = createTestEvent(QStringLiteral("sync-event"), QStringLiteral("Good Event"));
    auto todo = createTestTodo(QStringLiteral("sync-todo"), QStringLiteral("Was Bad Todo"));

    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    // Push both: auto-promotion means no error, both accepted
    PushOperation *pushOp = backend.pushItems(QStringLiteral("sync-event-cal"), {event, todo}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);

    QCOMPARE(errorSpy.count(), 0);

    // Both items should have been written (calendar auto-promoted to hybrid)
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("sync-event-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchOp->fetchedItems().size(), 2);

    delete fetchOp;
    delete pushOp;
}

void DecSyncBackendTest::testLoadDetectsTypeViolation()
{
    createDecsyncDir(m_decsyncDir);

    // Simulate a 3rd-party app putting a VTODO into a calendars/ collection
    KCalendarCore::ICalFormat icalFormat;
    auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
    );
    auto todo = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("misplaced-todo"));
    todo->setSummary(QStringLiteral("Misplaced Todo"));
    todo->setDtDue(QDateTime(QDate(2024, 6, 15), QTime(10, 0), QTimeZone::utc()));
    tempCal->addIncidence(todo);
    QString icalStr = icalFormat.toString(tempCal);

    DecSyncEntry entry;
    entry.path = {QStringLiteral("resources"), QStringLiteral("misplaced-todo")};
    entry.datetime = QStringLiteral("2024-06-15T10:00:00");
    entry.key = QJsonValue();
    entry.value = QJsonValue(icalStr);

    writeExternalEntry(m_decsyncDir, QStringLiteral("calendars"),
                       QStringLiteral("violation-cal"), QStringLiteral("android-app"), entry);

    // Also write name info so the collection is discoverable
    DecSyncEntry nameEntry;
    nameEntry.path = {QStringLiteral("info")};
    nameEntry.datetime = QStringLiteral("2024-06-15T09:00:00");
    nameEntry.key = QJsonValue(QStringLiteral("name"));
    nameEntry.value = QJsonValue(QStringLiteral("Violation Cal"));
    writeExternalEntry(m_decsyncDir, QStringLiteral("calendars"),
                       QStringLiteral("violation-cal"), QStringLiteral("android-app"), nameEntry);

    DecSyncBackend backend(m_decsyncDir, QStringLiteral("planstan-test"));

    QSignalSpy violationSpy(&backend, &SyncBackend::typeViolationDetected);

    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("violation-cal"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);

    // Item should be loaded (data preservation) — auto-promoted to hybrid
    auto items = fetchOp->fetchedItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0]->uid(), QStringLiteral("misplaced-todo"));

    // Auto-promotion: no violation signal, calendar silently became hybrid
    QCOMPARE(violationSpy.count(), 0);
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("violation-cal")), CalendarType::Hybrid);

    delete fetchOp;
}

void DecSyncBackendTest::testFetchDetectsTypeViolation()
{
    createDecsyncDir(m_decsyncDir);

    // Simulate a 3rd-party app putting a VTODO into a calendars/ collection
    KCalendarCore::ICalFormat icalFormat;
    auto tempCal = QSharedPointer<KCalendarCore::MemoryCalendar>(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone())
    );
    auto todo = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("fetch-misplaced"));
    todo->setSummary(QStringLiteral("Misplaced"));
    todo->setDtDue(QDateTime(QDate(2024, 6, 15), QTime(10, 0), QTimeZone::utc()));
    tempCal->addIncidence(todo);
    QString icalStr = icalFormat.toString(tempCal);

    DecSyncEntry entry;
    entry.path = {QStringLiteral("resources"), QStringLiteral("fetch-misplaced")};
    entry.datetime = QStringLiteral("2024-06-15T10:00:00");
    entry.key = QJsonValue();
    entry.value = QJsonValue(icalStr);

    writeExternalEntry(m_decsyncDir, QStringLiteral("calendars"),
                       QStringLiteral("fetch-violation-cal"), QStringLiteral("android-app"), entry);

    DecSyncEntry nameEntry;
    nameEntry.path = {QStringLiteral("info")};
    nameEntry.datetime = QStringLiteral("2024-06-15T09:00:00");
    nameEntry.key = QJsonValue(QStringLiteral("name"));
    nameEntry.value = QJsonValue(QStringLiteral("Fetch Violation Cal"));
    writeExternalEntry(m_decsyncDir, QStringLiteral("calendars"),
                       QStringLiteral("fetch-violation-cal"), QStringLiteral("android-app"), nameEntry);

    DecSyncBackend backend(m_decsyncDir, QStringLiteral("planstan-test"));

    QSignalSpy violationSpy(&backend, &SyncBackend::typeViolationDetected);

    FetchOperation *op = backend.fetchItems(QStringLiteral("fetch-violation-cal"));
    QSignalSpy finishedSpy(op, &SyncOperation::finished);
    QTRY_COMPARE(finishedSpy.count(), 1);

    // Item should be fetched — auto-promoted to hybrid
    QCOMPARE(op->fetchedItems().size(), 1);
    QCOMPARE(op->fetchedItems()[0]->uid(), QStringLiteral("fetch-misplaced"));

    // Auto-promotion: no violation signal
    QCOMPARE(violationSpy.count(), 0);
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("fetch-violation-cal")), CalendarType::Hybrid);

    delete op;
}

void DecSyncBackendTest::testCreateCalendarRoutesEvent()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    // Explicitly create as Event type
    QVERIFY(backend.createCalendar(QStringLiteral("coll"), QStringLiteral("explicit-event"),
                                    QStringLiteral("Explicit Event"), CalendarType::Event));

    // Should be in calendars/ directory
    QVERIFY(QDir(m_decsyncDir + "/calendars/explicit-event/v2/test-app").exists());

    // Type should be Event
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("explicit-event")), CalendarType::Event);
}

// ============================================================================
// Hybrid transparent split tests
// ============================================================================

void DecSyncBackendTest::testCreateHybridCalendar()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    QVERIFY(backend.createCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-cal"),
                                    QStringLiteral("Hybrid Cal"), CalendarType::Hybrid));

    // tasks/ should be created eagerly (for discoverability)
    // calendars/ should NOT exist yet (deferred until VEVENTs arrive)
    QVERIFY(!QDir(m_decsyncDir + "/calendars/hybrid-cal").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-cal").exists());

    // The hybrid flag should be set on the tasks/ collection
    DecSyncDir dir(m_decsyncDir);
    QMap<QString, QJsonValue> taskInfo = dir.getStaticInfo(QStringLiteral("tasks"), QStringLiteral("hybrid-cal"));
    QVERIFY(taskInfo.value(QStringLiteral("hybrid")).toBool(false));

    // Type should be Hybrid
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("hybrid-cal")), CalendarType::Hybrid);

    // Name should be readable from the tasks/ side
    QCOMPARE(backend.discoveredDisplayName(QStringLiteral("hybrid-cal")),
             QStringLiteral("Hybrid Cal"));
}

void DecSyncBackendTest::testHybridDiscovery()
{
    createDecsyncDir(m_decsyncDir);

    // Create a paired set: calendars/paired and tasks/paired
    DecSyncCollection calColl(m_decsyncDir + "/calendars/paired", QStringLiteral("other-app"));
    calColl.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                     QJsonValue(QStringLiteral("Paired Cal")));

    DecSyncCollection taskColl(m_decsyncDir + "/tasks/paired", QStringLiteral("other-app"));
    taskColl.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                      QJsonValue(QStringLiteral("Paired Cal")));

    // Create a solo tasks/ with hybrid flag (tasks-only hybrid, e.g. "Waiting For")
    DecSyncCollection hybridTasks(m_decsyncDir + "/tasks/waiting-for", QStringLiteral("other-app"));
    hybridTasks.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                         QJsonValue(QStringLiteral("Waiting For")));
    hybridTasks.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("hybrid")),
                         QJsonValue(true));

    // Also create a standalone tasks/ collection (no hybrid flag)
    DecSyncCollection standaloneTasks(m_decsyncDir + "/tasks/standalone-tasks", QStringLiteral("other-app"));
    standaloneTasks.setEntry({QStringLiteral("info")}, QJsonValue(QStringLiteral("name")),
                             QJsonValue(QStringLiteral("Standalone Tasks")));

    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);

    backend.loadCalendars(QStringLiteral("test-coll"));

    // Should discover:
    //   "paired" (hybrid, merged from both dirs)
    //   "waiting-for" (hybrid, solo tasks/ with hybrid flag — bare ID)
    //   "tasks/standalone-tasks" (standalone task collection)
    // but NOT "tasks/paired" or "tasks/waiting-for"
    QCOMPARE(spy.count(), 3);

    QStringList discovered;
    for (int i = 0; i < spy.count(); ++i) {
        discovered.append(spy[i][1].toString());
    }
    QVERIFY(discovered.contains(QStringLiteral("paired")));
    QVERIFY(discovered.contains(QStringLiteral("waiting-for")));
    QVERIFY(discovered.contains(QStringLiteral("tasks/standalone-tasks")));
    QVERIFY(!discovered.contains(QStringLiteral("tasks/paired")));
    QVERIFY(!discovered.contains(QStringLiteral("tasks/waiting-for")));

    // Both should be detected as hybrid
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("paired")), CalendarType::Hybrid);
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("waiting-for")), CalendarType::Hybrid);
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("tasks/standalone-tasks")), CalendarType::Todo);

    // "waiting-for" display name should be readable from tasks/ side
    QCOMPARE(backend.discoveredDisplayName(QStringLiteral("waiting-for")),
             QStringLiteral("Waiting For"));
}

void DecSyncBackendTest::testHybridStoreAndLoad()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-sl"),
                           QStringLiteral("Hybrid SL"), CalendarType::Hybrid);

    // tasks/ exists, calendars/ deferred
    QVERIFY(!QDir(m_decsyncDir + "/calendars/hybrid-sl").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-sl").exists());

    auto event = createTestEvent(QStringLiteral("h-event"), QStringLiteral("Hybrid Event"));
    auto todo = createTestTodo(QStringLiteral("h-todo"), QStringLiteral("Hybrid Todo"));

    // Push mixed items — should succeed without errors and lazily create dirs
    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    PushOperation *pushOp = backend.pushItems(QStringLiteral("hybrid-sl"), {event, todo}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    delete pushOp;

    // Now both dirs should exist (lazily created)
    QVERIFY(QDir(m_decsyncDir + "/calendars/hybrid-sl").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-sl").exists());

    // Verify items went to correct directories
    DecSyncCollection eventColl(m_decsyncDir + "/calendars/hybrid-sl", QStringLiteral("test-app"));
    QMap<QString, DecSyncEntry> eventResources = eventColl.readAllResources();
    QVERIFY(eventResources.contains(QStringLiteral("h-event")));
    QVERIFY(!eventResources.contains(QStringLiteral("h-todo")));

    DecSyncCollection taskColl(m_decsyncDir + "/tasks/hybrid-sl", QStringLiteral("test-app"));
    QMap<QString, DecSyncEntry> taskResources = taskColl.readAllResources();
    QVERIFY(taskResources.contains(QStringLiteral("h-todo")));
    QVERIFY(!taskResources.contains(QStringLiteral("h-event")));

    // Verify the hybrid flag was written to both collections
    DecSyncDir dir(m_decsyncDir);
    QMap<QString, QJsonValue> eventInfo = dir.getStaticInfo(QStringLiteral("calendars"), QStringLiteral("hybrid-sl"));
    QVERIFY(eventInfo.value(QStringLiteral("hybrid")).toBool(false));
    QMap<QString, QJsonValue> taskInfo = dir.getStaticInfo(QStringLiteral("tasks"), QStringLiteral("hybrid-sl"));
    QVERIFY(taskInfo.value(QStringLiteral("hybrid")).toBool(false));

    // Verify name was written from pending metadata
    QCOMPARE(eventInfo.value(QStringLiteral("name")).toString(), QStringLiteral("Hybrid SL"));
    QCOMPARE(taskInfo.value(QStringLiteral("name")).toString(), QStringLiteral("Hybrid SL"));

    // Fetch items back — should get both
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("hybrid-sl"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);

    auto items = fetchOp->fetchedItems();
    QCOMPARE(items.size(), 2);
    QStringList loadedUids;
    for (const auto &inc : items) {
        loadedUids.append(inc->uid());
    }
    QVERIFY(loadedUids.contains(QStringLiteral("h-event")));
    QVERIFY(loadedUids.contains(QStringLiteral("h-todo")));

    delete fetchOp;
}

void DecSyncBackendTest::testHybridPushItems()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-push"),
                           QStringLiteral("Hybrid Push"), CalendarType::Hybrid);

    // tasks/ exists, calendars/ deferred
    QVERIFY(!QDir(m_decsyncDir + "/calendars/hybrid-push").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-push").exists());

    auto event = createTestEvent(QStringLiteral("hp-event"), QStringLiteral("Push Event"));
    auto todo = createTestTodo(QStringLiteral("hp-todo"), QStringLiteral("Push Todo"));

    PushOperation *op = backend.pushItems(QStringLiteral("hybrid-push"), {event, todo}, TranscodingPlan{});
    QVERIFY(op);

    QSignalSpy finishedSpy(op, &SyncOperation::finished);
    QTRY_COMPARE(finishedSpy.count(), 1);

    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids().size(), 2);
    QVERIFY(op->succeededUids().contains(QStringLiteral("hp-event")));
    QVERIFY(op->succeededUids().contains(QStringLiteral("hp-todo")));

    // Dirs should now exist (lazily created)
    QVERIFY(QDir(m_decsyncDir + "/calendars/hybrid-push").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-push").exists());

    // Verify fetch returns both
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("hybrid-push"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchOp->fetchedItems().size(), 2);

    delete fetchOp;
    delete op;
}

void DecSyncBackendTest::testHybridStartSync()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-sync"),
                           QStringLiteral("Hybrid Sync"), CalendarType::Hybrid);

    // tasks/ exists, calendars/ deferred
    QVERIFY(!QDir(m_decsyncDir + "/calendars/hybrid-sync").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-sync").exists());

    auto event = createTestEvent(QStringLiteral("hs-event"), QStringLiteral("Sync Event"));
    auto todo = createTestTodo(QStringLiteral("hs-todo"), QStringLiteral("Sync Todo"));

    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    PushOperation *pushOp = backend.pushItems(QStringLiteral("hybrid-sync"), {event, todo}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);  // No type errors for hybrid
    delete pushOp;

    // Dirs should now exist (lazily created)
    QVERIFY(QDir(m_decsyncDir + "/calendars/hybrid-sync").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-sync").exists());

    // Verify both items were written
    FetchOperation *fetchOp = backend.fetchItems(QStringLiteral("hybrid-sync"));
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy.count(), 1);

    auto items = fetchOp->fetchedItems();
    QCOMPARE(items.size(), 2);
    QStringList loadedUids;
    for (const auto &inc : items) {
        loadedUids.append(inc->uid());
    }
    QVERIFY(loadedUids.contains(QStringLiteral("hs-event")));
    QVERIFY(loadedUids.contains(QStringLiteral("hs-todo")));

    delete fetchOp;
}

void DecSyncBackendTest::testHybridRemoveItem()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-rm"),
                           QStringLiteral("Hybrid Remove"), CalendarType::Hybrid);

    auto event = createTestEvent(QStringLiteral("hr-event"), QStringLiteral("Remove Event"));
    auto todo = createTestTodo(QStringLiteral("hr-todo"), QStringLiteral("Remove Todo"));

    PushOperation *pushOp = backend.pushItems(QStringLiteral("hybrid-rm"), {event, todo}, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Remove the todo
    backend.removeItem(QStringLiteral("hybrid-rm"), QStringLiteral("hr-todo"));

    // Fetch — should only have the event
    FetchOperation *fetchOp1 = backend.fetchItems(QStringLiteral("hybrid-rm"));
    QSignalSpy fetchSpy1(fetchOp1, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy1.count(), 1);
    QCOMPARE(fetchOp1->fetchedItems().size(), 1);
    QCOMPARE(fetchOp1->fetchedItems()[0]->uid(), QStringLiteral("hr-event"));
    delete fetchOp1;

    // Remove the event
    backend.removeItem(QStringLiteral("hybrid-rm"), QStringLiteral("hr-event"));

    FetchOperation *fetchOp2 = backend.fetchItems(QStringLiteral("hybrid-rm"));
    QSignalSpy fetchSpy2(fetchOp2, &SyncOperation::finished);
    QTRY_COMPARE(fetchSpy2.count(), 1);
    QCOMPARE(fetchOp2->fetchedItems().size(), 0);
    delete fetchOp2;
}

void DecSyncBackendTest::testHybridDeleteCalendar()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-del"),
                           QStringLiteral("Hybrid Delete"), CalendarType::Hybrid);

    // Test 1: Delete before any VEVENTs (only tasks/ exists) — soft-deletes tasks/
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-del").exists());
    QVERIFY(!QDir(m_decsyncDir + "/calendars/hybrid-del").exists());
    QVERIFY(backend.deleteCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-del")));
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("hybrid-del")), CalendarType::Event); // no longer hybrid

    // Test 2: Create hybrid, store items to create dirs, then delete
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-del2"),
                           QStringLiteral("Hybrid Delete 2"), CalendarType::Hybrid);

    PushOperation *pushOp = backend.pushItems(QStringLiteral("hybrid-del2"), {
        createTestEvent(QStringLiteral("hd-event"), QStringLiteral("Del Event")),
        createTestTodo(QStringLiteral("hd-todo"), QStringLiteral("Del Todo"))
    }, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy.count(), 1);
    QCOMPARE(pushOp->state(), SyncOperation::Succeeded);
    delete pushOp;

    // Dirs should exist now
    QVERIFY(QDir(m_decsyncDir + "/calendars/hybrid-del2").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-del2").exists());

    QVERIFY(backend.deleteCalendar(QStringLiteral("coll"), QStringLiteral("hybrid-del2")));

    // Both directories should still exist (soft delete)
    QVERIFY(QDir(m_decsyncDir + "/calendars/hybrid-del2").exists());
    QVERIFY(QDir(m_decsyncDir + "/tasks/hybrid-del2").exists());

    // Neither should appear in discovery
    QSignalSpy spy(&backend, &SyncBackend::calendarDiscovered);
    backend.loadCalendars(QStringLiteral("coll"));
    QCOMPARE(spy.count(), 0);
}

void DecSyncBackendTest::testStandaloneTasksUnaffected()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    // Create a standalone tasks/ collection (no matching calendars/)
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("tasks/standalone"),
                           QStringLiteral("Standalone Tasks"), CalendarType::Todo);

    // Verify it's in tasks/ only
    QVERIFY(QDir(m_decsyncDir + "/tasks/standalone").exists());
    QVERIFY(!QDir(m_decsyncDir + "/calendars/standalone").exists());

    // Type should be Todo, not Hybrid
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("tasks/standalone")), CalendarType::Todo);

    // Push a todo — should work
    auto todo = createTestTodo(QStringLiteral("sa-todo"), QStringLiteral("Standalone Todo"));

    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    PushOperation *pushOp1 = backend.pushItems(QStringLiteral("tasks/standalone"), {todo}, TranscodingPlan{});
    QSignalSpy pushSpy1(pushOp1, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy1.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    delete pushOp1;

    // Push an event — should be rejected
    auto event = createTestEvent(QStringLiteral("sa-event"), QStringLiteral("Bad Event"));
    PushOperation *pushOp2 = backend.pushItems(QStringLiteral("tasks/standalone"), {event}, TranscodingPlan{});
    QSignalSpy pushSpy2(pushOp2, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy2.count(), 1);
    QCOMPARE(errorSpy.count(), 1);
    delete pushOp2;
}

void DecSyncBackendTest::testStandaloneCalendarsUnaffected()
{
    createDecsyncDir(m_decsyncDir);
    DecSyncBackend backend(m_decsyncDir, QStringLiteral("test-app"));

    // Create a standalone calendars/ collection (no matching tasks/)
    backend.createCalendar(QStringLiteral("coll"), QStringLiteral("standalone-events"),
                           QStringLiteral("Standalone Events"), CalendarType::Event);

    // Verify it's in calendars/ only
    QVERIFY(QDir(m_decsyncDir + "/calendars/standalone-events").exists());
    QVERIFY(!QDir(m_decsyncDir + "/tasks/standalone-events").exists());

    // Type should be Event, not Hybrid
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("standalone-events")), CalendarType::Event);

    // Push an event — should work
    auto event = createTestEvent(QStringLiteral("se-event"), QStringLiteral("Standalone Event"));

    QSignalSpy errorSpy(&backend, &SyncBackend::calendarError);
    PushOperation *pushOp1 = backend.pushItems(QStringLiteral("standalone-events"), {event}, TranscodingPlan{});
    QSignalSpy pushSpy1(pushOp1, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy1.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    delete pushOp1;

    // Push a todo — should auto-promote to hybrid
    auto todo = createTestTodo(QStringLiteral("se-todo"), QStringLiteral("Was Bad Todo"));
    PushOperation *pushOp2 = backend.pushItems(QStringLiteral("standalone-events"), {todo}, TranscodingPlan{});
    QSignalSpy pushSpy2(pushOp2, &SyncOperation::finished);
    QTRY_COMPARE(pushSpy2.count(), 1);
    QCOMPARE(errorSpy.count(), 0);  // No error, auto-promoted
    delete pushOp2;

    // Calendar should now be hybrid
    QCOMPARE(backend.discoveredCalendarType(QStringLiteral("standalone-events")), CalendarType::Hybrid);
    // tasks/ directory should now exist
    QVERIFY(QDir(m_decsyncDir + "/tasks/standalone-events").exists());
}

QTEST_GUILESS_MAIN(DecSyncBackendTest)
#include "tst_decsyncbackend.moc"
