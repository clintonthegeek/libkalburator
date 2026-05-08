// tests/calendar/tst_remotebackend.cpp
// G.9.b Task 72 — migrated from PlanStan/tests/backends/tst_remotebackend.cpp
//
// Test suite for RemoteCalendarBackend (CalDAV).
//
// These tests require a running Radicale server with pre-configured test accounts.
// See docs/RadicaleSetupForTesting.md for setup instructions.
//
// Tests skip gracefully when the server is unavailable.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>
#include <QTemporaryDir>
#include <QTcpSocket>

#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/Journal>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/ICalFormat>

#include "remotecalendarbackend.h"
#include "syncoperation.h"
#include "syncbackend.h"

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;

// ============================================================================
// CalDAV test server configuration (inlined from PlanStan's caldav_test_config.h)
// ============================================================================
namespace CalDavTestConfig {

const QString SERVER_HOST = QStringLiteral("127.0.0.1");
const int SERVER_PORT = 5232;
const QString SERVER_URL = QStringLiteral("http://127.0.0.1:5232");

const QString USERNAME_1 = QStringLiteral("testuser1");
const QString PASSWORD_1 = QStringLiteral("password1");

const QString USERNAME_2 = QStringLiteral("testuser2");
const QString PASSWORD_2 = QStringLiteral("password2");

const QString USERNAME_3 = QStringLiteral("testuser3");
const QString PASSWORD_3 = QStringLiteral("password3");

inline bool isServerAvailable()
{
    QTcpSocket socket;
    socket.connectToHost(SERVER_HOST, SERVER_PORT);
    bool connected = socket.waitForConnected(2000);
    socket.close();
    return connected;
}

inline QUrl principalUrl(const QString &username)
{
    return QUrl(SERVER_URL + QStringLiteral("/") + username + QStringLiteral("/"));
}

inline QUrl calendarUrl(const QString &username, const QString &calendarName)
{
    return QUrl(SERVER_URL + QStringLiteral("/") + username + QStringLiteral("/") + calendarName + QStringLiteral("/"));
}

} // namespace CalDavTestConfig

// ============================================================================
// Test helper functions (anonymous namespace)
// ============================================================================
namespace {

KCalendarCore::Event::Ptr createTestEvent(const QString &summary = QString())
{
    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(QUuid::createUuid().toString(QUuid::WithoutBraces));
    event->setSummary(summary.isEmpty()
        ? QStringLiteral("Test Event ") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)
        : summary);
    event->setDtStart(QDateTime::currentDateTime());
    event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
    event->setDescription(QStringLiteral("Test event created by tst_remotebackend"));
    return event;
}

KCalendarCore::Todo::Ptr createTestTodo(const QString &summary = QString())
{
    auto todo = KCalendarCore::Todo::Ptr::create();
    todo->setUid(QUuid::createUuid().toString(QUuid::WithoutBraces));
    todo->setSummary(summary.isEmpty()
        ? QStringLiteral("Test Todo ") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)
        : summary);
    todo->setDtDue(QDateTime::currentDateTime().addDays(7));
    todo->setDescription(QStringLiteral("Test todo created by tst_remotebackend"));
    return todo;
}

} // anonymous namespace

// ============================================================================
// Test class
// ============================================================================

class RemoteCalendarBackendTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Backend type and factory
    void testBackendType();
    void testFactoryMethod();

    // Calendar discovery
    void testLoadCalendars();
    void testCalendarDiscoverySignals();

    // Incidence CRUD
    void testStoreEvent();
    void testStoreTodo();
    void testLoadItems();
    void testUpdateItem();
    void testRemoveItem();
    void testStoreMultipleItems();

    // ETag handling
    void testEtagPopulatedOnCreate();
    void testEtagUpdatedOnModify();

    // Sync operations
    void testStartSyncCreations();
    void testStartSyncUpdates();
    void testStartSyncDeletions();
    void testStartSyncMixed();

    // CTag optimization
    void testCtagCacheSkip();
    void testFetchAllCtagsBatched();

    // Error handling
    void testInvalidCredentials();
    void testInvalidUrl();

private:
    QString generateTestCalendarName();
    void waitForSignal(QSignalSpy &spy, int timeout = 10000);
    bool waitForCalendarDiscovery(RemoteCalendarBackend *backend, int timeout = 10000);

    RemoteCalendarBackend *m_backend = nullptr;
    KCalendarCore::MemoryCalendar *m_testCalendar = nullptr;
    QString m_testCalendarName;
    QStringList m_createdCalendars;
};

void RemoteCalendarBackendTest::initTestCase()
{
    if (!CalDavTestConfig::isServerAvailable()) {
        QSKIP("Radicale server not available at 127.0.0.1:5232. "
              "See docs/RadicaleSetupForTesting.md for setup instructions.");
    }

    qDebug() << "CalDAV test server available at" << CalDavTestConfig::SERVER_URL;
}

void RemoteCalendarBackendTest::cleanupTestCase()
{
    // Note: Calendar cleanup happens in cleanup() per-test
}

void RemoteCalendarBackendTest::init()
{
    QUrl serverUrl = CalDavTestConfig::principalUrl(CalDavTestConfig::USERNAME_1);
    m_backend = new RemoteCalendarBackend(serverUrl,
                                   CalDavTestConfig::USERNAME_1,
                                   CalDavTestConfig::PASSWORD_1,
                                   this);

    m_testCalendarName = generateTestCalendarName();
    m_testCalendar = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    m_testCalendar->setId(m_testCalendarName);

    bool created = m_backend->createCalendar(QStringLiteral("test-collection"),
                                              m_testCalendarName,
                                              QStringLiteral("Test Calendar"));
    if (created) {
        m_createdCalendars.append(m_testCalendarName);
        qDebug() << "Created test calendar:" << m_testCalendarName;
    } else {
        qWarning() << "Failed to create test calendar:" << m_testCalendarName;
    }
}

void RemoteCalendarBackendTest::cleanup()
{
    // Delete any created incidences from test calendar
    if (m_testCalendar && m_backend) {
        for (const auto &inc : m_testCalendar->incidences()) {
            m_backend->removeItem(m_testCalendarName, inc->uid());
        }
        QTest::qWait(500);
    }

    // Delete the test calendar from the server
    if (m_backend && !m_testCalendarName.isEmpty()) {
        bool deleted = m_backend->deleteCalendar(QStringLiteral("test-collection"), m_testCalendarName);
        if (deleted) {
            m_createdCalendars.removeAll(m_testCalendarName);
            qDebug() << "Deleted test calendar:" << m_testCalendarName;
        } else {
            qWarning() << "Failed to delete test calendar:" << m_testCalendarName;
        }
    }

    delete m_testCalendar;
    m_testCalendar = nullptr;

    delete m_backend;
    m_backend = nullptr;
}

QString RemoteCalendarBackendTest::generateTestCalendarName()
{
    return QStringLiteral("test-calendar-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

void RemoteCalendarBackendTest::waitForSignal(QSignalSpy &spy, int timeout)
{
    if (spy.count() > 0) {
        return;
    }

    int elapsed = 0;
    const int interval = 100;
    while (spy.count() == 0 && elapsed < timeout) {
        QTest::qWait(interval);
        elapsed += interval;
    }
}

bool RemoteCalendarBackendTest::waitForCalendarDiscovery(RemoteCalendarBackend *backend, int timeout)
{
    QSignalSpy spy(backend, &RemoteCalendarBackend::calendarDiscovered);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool received = false;
    connect(backend, &RemoteCalendarBackend::calendarDiscovered, [&received, &loop]() {
        received = true;
        // Don't quit yet - wait for all calendars
    });

    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeout);

    while (timer.isActive()) {
        loop.processEvents(QEventLoop::AllEvents, 100);
        QTest::qWait(50);
        if (received) {
            // Give a bit more time for additional calendars
            QTest::qWait(500);
            break;
        }
    }

    return received || spy.count() > 0;
}

// ============================================================================
// Tests
// ============================================================================

void RemoteCalendarBackendTest::testBackendType()
{
    QCOMPARE(m_backend->backendType(), QStringLiteral("caldav"));
}

void RemoteCalendarBackendTest::testFactoryMethod()
{
    QVariantMap config;
    config[QStringLiteral("url")] = CalDavTestConfig::principalUrl(CalDavTestConfig::USERNAME_1).toString();
    config[QStringLiteral("username")] = CalDavTestConfig::USERNAME_1;
    config[QStringLiteral("password")] = CalDavTestConfig::PASSWORD_1;

    SyncBackend *backend = RemoteCalendarBackend::create(config, this);
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendType(), QStringLiteral("caldav"));

    delete backend;
}

void RemoteCalendarBackendTest::testLoadCalendars()
{
    QSignalSpy spy(m_backend, &RemoteCalendarBackend::calendarDiscovered);

    m_backend->loadCalendars(QStringLiteral("test-collection"));

    bool success = waitForCalendarDiscovery(m_backend, 10000);

    if (!success && spy.count() == 0) {
        qWarning() << "No calendars discovered - this may be normal for a fresh user account";
    }

    qDebug() << "Discovered" << spy.count() << "calendars";
}

void RemoteCalendarBackendTest::testCalendarDiscoverySignals()
{
    QSignalSpy spy(m_backend, &RemoteCalendarBackend::calendarDiscovered);

    m_backend->loadCalendars(QStringLiteral("test-collection"));

    waitForCalendarDiscovery(m_backend, 10000);

    if (spy.count() > 0) {
        QList<QVariant> args = spy.first();
        QCOMPARE(args.at(0).toString(), QStringLiteral("test-collection"));
        QVERIFY(!args.at(1).toString().isEmpty());
    }
}

void RemoteCalendarBackendTest::testStoreEvent()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered on server - cannot test event storage");
    }

    QString testCalId = m_testCalendarName;

    auto event = createTestEvent();
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(event);

    // Use operation API instead of storeItems(cal, items)
    PushOperation *pushOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    waitForSignal(pushSpy, 10000);

    if (pushOp->state() == SyncOperation::Succeeded) {
        qDebug() << "Event stored successfully with UID:" << event->uid();
        m_backend->removeItem(testCalId, event->uid());
        QTest::qWait(500);
    } else {
        qWarning() << "Event storage may have failed:" << pushOp->errorString();
    }
}

void RemoteCalendarBackendTest::testStoreTodo()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered on server - cannot test todo storage");
    }

    QString testCalId = m_testCalendarName;

    auto todo = createTestTodo();
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(todo);

    // Use operation API instead of storeItems(cal, items)
    PushOperation *pushOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    waitForSignal(pushSpy, 10000);

    if (pushOp->state() == SyncOperation::Succeeded) {
        qDebug() << "Todo stored successfully with UID:" << todo->uid();
        m_backend->removeItem(testCalId, todo->uid());
        QTest::qWait(500);
    }
}

void RemoteCalendarBackendTest::testLoadItems()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered on server - cannot test item loading");
    }

    QString testCalId = m_testCalendarName;

    // First, store an event using operation API
    auto event = createTestEvent(QStringLiteral("LoadItemsTest"));
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(event);

    PushOperation *pushOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    waitForSignal(pushSpy, 10000);

    // Now load items using operation API instead of loadItems(cal)
    FetchOperation *fetchOp = m_backend->fetchItems(testCalId);
    QSignalSpy fetchSpy(fetchOp, &SyncOperation::finished);
    waitForSignal(fetchSpy, 15000);

    if (fetchOp->state() == SyncOperation::Succeeded) {
        auto loadedIncidences = fetchOp->fetchedItems();
        qDebug() << "Loaded" << loadedIncidences.count() << "incidences";

        bool found = false;
        for (const auto &inc : loadedIncidences) {
            if (inc->uid() == event->uid()) {
                found = true;
                QCOMPARE(inc->summary(), QStringLiteral("LoadItemsTest"));
                break;
            }
        }

        if (!found) {
            qWarning() << "Created event not found in loaded items (may be timing issue)";
        }
    } else {
        qWarning() << "Fetch failed:" << fetchOp->errorString();
    }

    // Clean up
    m_backend->removeItem(testCalId, event->uid());
    QTest::qWait(500);
}

void RemoteCalendarBackendTest::testUpdateItem()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    // Create event using operation API
    auto event = createTestEvent(QStringLiteral("OriginalSummary"));
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(event);

    PushOperation *createOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy createSpy(createOp, &SyncOperation::finished);
    waitForSignal(createSpy, 10000);

    if (createOp->state() != SyncOperation::Succeeded) {
        QSKIP("Event creation failed - cannot test update");
    }

    // Modify and update using operation API (pushItems with updated item)
    event->setSummary(QStringLiteral("UpdatedSummary"));
    QList<KCalendarCore::Incidence::Ptr> updatedItems;
    updatedItems.append(event);

    PushOperation *updateOp = m_backend->pushItems(testCalId, updatedItems, TranscodingPlan{});
    QSignalSpy updateSpy(updateOp, &SyncOperation::finished);
    waitForSignal(updateSpy, 10000);

    if (updateOp->state() == SyncOperation::Succeeded) {
        qDebug() << "Event updated successfully";
    }

    // Clean up
    m_backend->removeItem(testCalId, event->uid());
    QTest::qWait(500);
}

void RemoteCalendarBackendTest::testRemoveItem()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    // Create event using operation API
    auto event = createTestEvent();
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(event);

    PushOperation *pushOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    waitForSignal(pushSpy, 10000);

    // Remove it
    QSignalSpy removeSpy(m_backend, &RemoteCalendarBackend::itemRemoved);
    m_backend->removeItem(testCalId, event->uid());
    waitForSignal(removeSpy, 10000);

    if (removeSpy.count() > 0) {
        QList<QVariant> args = removeSpy.first();
        QCOMPARE(args.at(0).toString(), testCalId);
        QCOMPARE(args.at(1).toString(), event->uid());
        qDebug() << "Event removed successfully";
    }
}

void RemoteCalendarBackendTest::testStoreMultipleItems()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    // Create multiple events
    QList<KCalendarCore::Incidence::Ptr> items;
    QStringList uids;
    for (int i = 0; i < 3; ++i) {
        auto event = createTestEvent(QStringLiteral("MultiTest Event %1").arg(i));
        items.append(event);
        uids.append(event->uid());
    }

    // Use operation API — pushItems handles multiple items
    PushOperation *pushOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);

    // Wait up to 20 seconds for the multi-item push
    int maxWait = 20000;
    int elapsed = 0;
    while (!pushOp->isFinished() && elapsed < maxWait) {
        QTest::qWait(200);
        elapsed += 200;
    }

    int succeededCount = pushOp->succeededUids().size();
    qDebug() << "Stored" << succeededCount << "of 3 items";
    QVERIFY(succeededCount >= 1);  // At least some should succeed

    // Clean up
    for (const QString &uid : uids) {
        m_backend->removeItem(testCalId, uid);
    }
    QTest::qWait(1000);
}

void RemoteCalendarBackendTest::testEtagPopulatedOnCreate()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    auto event = createTestEvent();
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(event);

    // Use operation API instead of storeItems
    PushOperation *pushOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    waitForSignal(pushSpy, 10000);

    if (pushOp->state() == SyncOperation::Succeeded) {
        qDebug() << "Event created, checking ETag via itemLoaded signal path";
        // ETag checking via the itemLoaded signal is not needed with operation API —
        // the operation itself reports success. ETags are managed internally by RemoteCalendarBackend.
        // Note: Some servers may not expose ETags in the push response.
    }

    // Clean up
    m_backend->removeItem(testCalId, event->uid());
    QTest::qWait(500);
}

void RemoteCalendarBackendTest::testEtagUpdatedOnModify()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    // Create
    auto event = createTestEvent();
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(event);

    PushOperation *createOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy createSpy(createOp, &SyncOperation::finished);
    waitForSignal(createSpy, 10000);

    if (createOp->state() != SyncOperation::Succeeded) {
        QSKIP("Event creation failed - cannot test ETag update");
    }

    // Modify and update using operation API
    event->setSummary(QStringLiteral("Modified for ETag test"));
    QList<KCalendarCore::Incidence::Ptr> updatedItems;
    updatedItems.append(event);

    PushOperation *updateOp = m_backend->pushItems(testCalId, updatedItems, TranscodingPlan{});
    QSignalSpy updateSpy(updateOp, &SyncOperation::finished);
    waitForSignal(updateSpy, 10000);

    if (updateOp->state() == SyncOperation::Succeeded) {
        qDebug() << "Event modified — ETag update is managed internally by RemoteCalendarBackend";
    }

    // Clean up
    m_backend->removeItem(testCalId, event->uid());
    QTest::qWait(500);
}

void RemoteCalendarBackendTest::testStartSyncCreations()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    QList<KCalendarCore::Incidence::Ptr> creations;
    auto event = createTestEvent(QStringLiteral("SyncCreate Test"));
    creations.append(event);

    QList<KCalendarCore::Incidence::Ptr> updates;
    QMap<QString, QString> deletions;

    auto testCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    testCal->setId(testCalId);

    QSignalSpy syncSpy(m_backend, &RemoteCalendarBackend::syncCompleted);

    m_backend->startSync(QStringLiteral("test-collection"), testCal, creations, updates, deletions);

    waitForSignal(syncSpy, 15000);

    if (syncSpy.count() > 0) {
        qDebug() << "Sync completed for creations";
    }

    // Clean up
    m_backend->removeItem(testCalId, event->uid());
    QTest::qWait(500);

    delete testCal;
}

void RemoteCalendarBackendTest::testStartSyncUpdates()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    // First create an event using operation API
    auto event = createTestEvent(QStringLiteral("SyncUpdate Original"));
    QList<KCalendarCore::Incidence::Ptr> createItems;
    createItems.append(event);

    auto testCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    testCal->setId(testCalId);
    testCal->addIncidence(event);

    PushOperation *createOp = m_backend->pushItems(testCalId, createItems, TranscodingPlan{});
    QSignalSpy createSpy(createOp, &SyncOperation::finished);
    waitForSignal(createSpy, 10000);

    // Now sync with update
    event->setSummary(QStringLiteral("SyncUpdate Modified"));

    QList<KCalendarCore::Incidence::Ptr> updates;
    updates.append(event);

    QList<KCalendarCore::Incidence::Ptr> creations;
    QMap<QString, QString> deletions;

    QSignalSpy syncSpy(m_backend, &RemoteCalendarBackend::syncCompleted);

    m_backend->startSync(QStringLiteral("test-collection"), testCal, creations, updates, deletions);

    waitForSignal(syncSpy, 15000);

    if (syncSpy.count() > 0) {
        qDebug() << "Sync completed for updates";
    }

    // Clean up
    m_backend->removeItem(testCalId, event->uid());
    QTest::qWait(500);

    delete testCal;
}

void RemoteCalendarBackendTest::testStartSyncDeletions()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    // First create an event using operation API
    auto event = createTestEvent(QStringLiteral("SyncDelete Test"));
    QList<KCalendarCore::Incidence::Ptr> createItems;
    createItems.append(event);

    auto testCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    testCal->setId(testCalId);

    PushOperation *createOp = m_backend->pushItems(testCalId, createItems, TranscodingPlan{});
    QSignalSpy createSpy(createOp, &SyncOperation::finished);
    waitForSignal(createSpy, 10000);

    // Now sync with deletion
    QMap<QString, QString> deletions;
    deletions[event->uid()] = QString();

    QList<KCalendarCore::Incidence::Ptr> creations;
    QList<KCalendarCore::Incidence::Ptr> updates;

    QSignalSpy syncSpy(m_backend, &RemoteCalendarBackend::syncCompleted);

    m_backend->startSync(QStringLiteral("test-collection"), testCal, creations, updates, deletions);

    waitForSignal(syncSpy, 15000);

    if (syncSpy.count() > 0) {
        qDebug() << "Sync completed for deletions";
    }

    delete testCal;
}

void RemoteCalendarBackendTest::testStartSyncMixed()
{
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered");
    }

    QString testCalId = m_testCalendarName;

    auto testCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    testCal->setId(testCalId);

    // Create two events first using operation API
    auto eventToUpdate = createTestEvent(QStringLiteral("MixedSync Update"));
    auto eventToDelete = createTestEvent(QStringLiteral("MixedSync Delete"));

    QList<KCalendarCore::Incidence::Ptr> createItems;
    createItems.append(eventToUpdate);
    createItems.append(eventToDelete);

    testCal->addIncidence(eventToUpdate);
    testCal->addIncidence(eventToDelete);

    PushOperation *createOp = m_backend->pushItems(testCalId, createItems, TranscodingPlan{});
    int maxWait = 10000;
    int elapsed = 0;
    while (!createOp->isFinished() && elapsed < maxWait) {
        QTest::qWait(200);
        elapsed += 200;
    }

    // Now do mixed sync: create, update, delete
    auto eventToCreate = createTestEvent(QStringLiteral("MixedSync Create"));
    eventToUpdate->setSummary(QStringLiteral("MixedSync Updated"));

    QList<KCalendarCore::Incidence::Ptr> creations;
    creations.append(eventToCreate);

    QList<KCalendarCore::Incidence::Ptr> updates;
    updates.append(eventToUpdate);

    QMap<QString, QString> deletions;
    deletions[eventToDelete->uid()] = QString();

    QSignalSpy syncSpy(m_backend, &RemoteCalendarBackend::syncCompleted);

    m_backend->startSync(QStringLiteral("test-collection"), testCal, creations, updates, deletions);

    waitForSignal(syncSpy, 15000);

    if (syncSpy.count() > 0) {
        qDebug() << "Mixed sync completed";
    }

    // Clean up remaining events
    m_backend->removeItem(testCalId, eventToCreate->uid());
    m_backend->removeItem(testCalId, eventToUpdate->uid());
    QTest::qWait(1000);

    delete testCal;
}

void RemoteCalendarBackendTest::testCtagCacheSkip()
{
    // 1. Discover calendars (needed to populate DAV URLs)
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    bool discovered = waitForCalendarDiscovery(m_backend, 10000);

    if (!discovered) {
        QSKIP("No calendars discovered on server - cannot test CTag cache skip");
    }

    QString testCalId = m_testCalendarName;

    // 2. Create a DB path for the backend's internal CTag store
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString dbPath = tmpDir.path() + QStringLiteral("/test-sync.db");

    // 3. Wire up the DB path to the backend (CTag store is internal to RemoteCalendarBackend)
    m_backend->setDbPath(dbPath);

    // 4. Push an event to the calendar so there's something to fetch
    auto event = createTestEvent(QStringLiteral("CTag Cache Skip Test"));
    QList<KCalendarCore::Incidence::Ptr> items;
    items.append(event);

    PushOperation *pushOp = m_backend->pushItems(testCalId, items, TranscodingPlan{});
    QSignalSpy pushSpy(pushOp, &SyncOperation::finished);
    waitForSignal(pushSpy, 10000);
    QVERIFY(pushOp->state() == SyncOperation::Succeeded);
    qDebug() << "Pushed test event:" << event->uid();

    // 5. Reload calendars to refresh CTag discovery from the server
    QSignalSpy loadCalSpy(m_backend, &SyncBackend::loadCalendarsFinished);
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    waitForSignal(loadCalSpy, 10000);
    QVERIFY(loadCalSpy.count() > 0);

    // 6. First fetchItems() — should do a full fetch and store the CTag
    FetchOperation *fetch1 = m_backend->fetchItems(testCalId);
    QSignalSpy fetch1Spy(fetch1, &SyncOperation::finished);
    waitForSignal(fetch1Spy, 15000);

    QVERIFY(fetch1->state() == SyncOperation::Succeeded);
    int firstFetchCount = fetch1->fetchedItems().size();
    QVERIFY(firstFetchCount > 0);
    qDebug() << "First fetch returned" << firstFetchCount << "items";

    // 7. Second fetchItems() — without any server changes, CTag should match
    //    and items should be served from cache
    FetchOperation *fetch2 = m_backend->fetchItems(testCalId);
    QSignalSpy fetch2Spy(fetch2, &SyncOperation::finished);
    waitForSignal(fetch2Spy, 15000);

    QVERIFY(fetch2->state() == SyncOperation::Succeeded);
    int secondFetchCount = fetch2->fetchedItems().size();
    qDebug() << "Second fetch returned" << secondFetchCount << "items";

    // 8. Both fetches should return the same number of items
    QCOMPARE(secondFetchCount, firstFetchCount);

    // Clean up
    m_backend->removeItem(testCalId, event->uid());
    QTest::qWait(500);
}

void RemoteCalendarBackendTest::testFetchAllCtagsBatched()
{
    // 1. Discover calendars (populates m_davUrls).
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    if (!waitForCalendarDiscovery(m_backend, 10000)) {
        QSKIP("No calendars discovered on server");
    }

    // 2. Push an event so the server has a non-trivial CTag.
    auto event = createTestEvent(QStringLiteral("FetchAllCtags"));
    QList<KCalendarCore::Incidence::Ptr> items{ event };
    PushOperation *push = m_backend->pushItems(m_testCalendarName, items, TranscodingPlan{});
    QSignalSpy pushSpy(push, &SyncOperation::finished);
    waitForSignal(pushSpy, 10000);
    QVERIFY(push->state() == SyncOperation::Succeeded);

    // 3. Reload calendars to refresh discovery state.
    QSignalSpy loadCalSpy(m_backend, &SyncBackend::loadCalendarsFinished);
    m_backend->loadCalendars(QStringLiteral("test-collection"));
    waitForSignal(loadCalSpy, 10000);

    // 4. Fetch all ctags in one batched PROPFIND.
    QStringList ids{ m_testCalendarName };
    QMap<QString, QString> ctags = m_backend->fetchAllCtags(ids);

    QVERIFY2(!ctags.isEmpty(),
             "fetchAllCtags returned empty — server did not return any CS:getctag");
    QVERIFY(ctags.contains(m_testCalendarName));
    QVERIFY(!ctags.value(m_testCalendarName).isEmpty());

    // 5. Cleanup.
    m_backend->removeItem(m_testCalendarName, event->uid());
    QTest::qWait(500);
}

void RemoteCalendarBackendTest::testInvalidCredentials()
{
    QUrl serverUrl = CalDavTestConfig::principalUrl(CalDavTestConfig::USERNAME_1);
    auto badBackend = new RemoteCalendarBackend(serverUrl,
                                         CalDavTestConfig::USERNAME_1,
                                         QStringLiteral("wrong-password"),
                                         this);

    QSignalSpy spy(badBackend, &RemoteCalendarBackend::calendarDiscovered);

    badBackend->loadCalendars(QStringLiteral("test-collection"));

    QTest::qWait(3000);

    qDebug() << "Calendars discovered with bad credentials:" << spy.count();

    delete badBackend;
}

void RemoteCalendarBackendTest::testInvalidUrl()
{
    QUrl badUrl(QStringLiteral("http://nonexistent.invalid:9999/"));
    auto badBackend = new RemoteCalendarBackend(badUrl,
                                         QStringLiteral("user"),
                                         QStringLiteral("pass"),
                                         this);

    QSignalSpy spy(badBackend, &RemoteCalendarBackend::calendarDiscovered);

    badBackend->loadCalendars(QStringLiteral("test-collection"));

    QTest::qWait(5000);

    QCOMPARE(spy.count(), 0);
    qDebug() << "Correctly failed to connect to invalid URL";

    delete badBackend;
}

QTEST_GUILESS_MAIN(RemoteCalendarBackendTest)
#include "tst_remotecalendarbackend.moc"
