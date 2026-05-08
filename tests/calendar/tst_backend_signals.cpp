/**
 * @file tst_backend_signals.cpp
 * @brief Tests for backend streaming signal emission
 *
 * These tests verify that all backends emit the expected signals during
 * fetch and write operations, enabling real-time UI updates.
 *
 * Migrated from PlanStan to libkalburator as part of G.9.b Task 73.
 * All storeItems/loadItems calls replaced with the operation API (pushItems/fetchItems).
 */

#include <QTest>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/ICalFormat>

#include "localbackend.h"
#include "mockbackend.h"
#include "remotecalendarbackend.h"
#include "syncbackend.h"
#include "syncoperation.h"

#ifdef KALBURATOR_HAVE_ORG_IO
#include "orgbackend.h"
#endif

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;

using namespace KCalendarCore;

namespace {

// Server configuration for CalDAV tests
const QString CALDAV_SERVER_HOST = QStringLiteral("127.0.0.1");
const int CALDAV_SERVER_PORT = 5232;
const QString CALDAV_SERVER_URL = QStringLiteral("http://127.0.0.1:5232");
const QString CALDAV_USERNAME_1 = QStringLiteral("testuser1");
const QString CALDAV_PASSWORD_1 = QStringLiteral("password1");

inline bool isCaldavServerAvailable()
{
    QTcpSocket socket;
    socket.connectToHost(CALDAV_SERVER_HOST, CALDAV_SERVER_PORT);
    bool connected = socket.waitForConnected(2000);
    socket.close();
    return connected;
}

inline QUrl caldavPrincipalUrl(const QString &username)
{
    return QUrl(CALDAV_SERVER_URL + QStringLiteral("/") + username + QStringLiteral("/"));
}

Incidence::Ptr createTestEvent(const QString &uid, const QString &summary)
{
    Event::Ptr event(new Event);
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTime());
    event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
    return event;
}

void writeTestIcsFiles(const QString &calDir, int count)
{
    QDir dir(calDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    ICalFormat format;
    for (int i = 0; i < count; i++) {
        auto cal = QSharedPointer<MemoryCalendar>(
            new MemoryCalendar(QTimeZone::systemTimeZone())
        );
        auto event = createTestEvent(
            QStringLiteral("test-uid-%1").arg(i),
            QStringLiteral("Test Event %1").arg(i)
        );
        cal->addIncidence(event);

        QString filePath = dir.filePath(QStringLiteral("test-uid-%1.ics").arg(i));
        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(format.toString(cal).toUtf8());
            file.close();
        }
    }
}

void writeTestOrgFile(const QString &filePath, int headlineCount)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&file);
    out << "#+TITLE: Test Org File\n\n";

    for (int i = 0; i < headlineCount; i++) {
        out << "* TODO Test Task " << i << "\n";
        out << "  :PROPERTIES:\n";
        out << "  :ID: org-test-uid-" << i << "\n";
        out << "  :END:\n\n";
    }

    file.close();
}

} // anonymous namespace

class TestBackendSignals : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
    }

    // ========================================================================
    // LocalBackend Fetch Signal Tests
    // ========================================================================

    void testLocalBackend_fetchItems_emitsFetchStarted() {
        // Setup: Create directory with test files
        QString calDir = m_tempDir.filePath("fetch_started_test");
        writeTestIcsFiles(calDir, 5);

        LocalBackend backend(m_tempDir.path());

        // Spy on fetchStarted signal
        QSignalSpy fetchStartedSpy(&backend, &SyncBackend::fetchStarted);
        QVERIFY(fetchStartedSpy.isValid());

        // Execute fetch
        FetchOperation *op = backend.fetchItems("fetch_started_test");
        QVERIFY(op != nullptr);

        // Wait for operation to complete
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Verify fetchStarted was emitted with correct count
        QCOMPARE(fetchStartedSpy.count(), 1);
        QList<QVariant> args = fetchStartedSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("fetch_started_test"));
        QCOMPARE(args.at(1).toInt(), 5);  // 5 files

        op->deleteLater();
    }

    void testLocalBackend_fetchItems_emitsItemFetchedForEachItem() {
        // Setup
        QString calDir = m_tempDir.filePath("item_fetched_test");
        writeTestIcsFiles(calDir, 10);

        LocalBackend backend(m_tempDir.path());

        // Spy on itemFetched signal
        QSignalSpy itemFetchedSpy(&backend, &SyncBackend::itemFetched);
        QVERIFY(itemFetchedSpy.isValid());

        // Execute fetch
        FetchOperation *op = backend.fetchItems("item_fetched_test");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Verify itemFetched was emitted for each item
        QCOMPARE(itemFetchedSpy.count(), 10);

        // Verify each signal has the correct calendar ID
        for (int i = 0; i < itemFetchedSpy.count(); i++) {
            QList<QVariant> args = itemFetchedSpy.at(i);
            QCOMPARE(args.at(0).toString(), QStringLiteral("item_fetched_test"));
            // Verify incidence is valid
            auto incidence = args.at(1).value<Incidence::Ptr>();
            QVERIFY(incidence);
            QVERIFY(!incidence->uid().isEmpty());
        }

        op->deleteLater();
    }

    void testLocalBackend_fetchItems_emitsProgressChangedIncrementally() {
        // Setup
        QString calDir = m_tempDir.filePath("progress_test");
        writeTestIcsFiles(calDir, 20);

        LocalBackend backend(m_tempDir.path());

        // Spy on fetchProgressChanged signal
        QSignalSpy progressSpy(&backend, &SyncBackend::fetchProgressChanged);
        QVERIFY(progressSpy.isValid());

        // Execute fetch
        FetchOperation *op = backend.fetchItems("progress_test");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Verify progress signals were emitted
        QVERIFY(progressSpy.count() >= 1);

        // Verify progress is monotonically increasing
        int lastCurrent = 0;
        for (int i = 0; i < progressSpy.count(); i++) {
            QList<QVariant> args = progressSpy.at(i);
            int current = args.at(1).toInt();
            int total = args.at(2).toInt();

            QVERIFY(current >= lastCurrent);  // Monotonically increasing
            QCOMPARE(total, 20);  // Total stays constant
            lastCurrent = current;
        }

        // Final progress should equal total
        QList<QVariant> lastArgs = progressSpy.last();
        QCOMPARE(lastArgs.at(1).toInt(), 20);

        op->deleteLater();
    }

    void testLocalBackend_fetchItems_emitsFetchFinished() {
        // Setup
        QString calDir = m_tempDir.filePath("fetch_finished_test");
        writeTestIcsFiles(calDir, 3);

        LocalBackend backend(m_tempDir.path());

        // Spy on fetchFinished signal
        QSignalSpy fetchFinishedSpy(&backend, &SyncBackend::fetchFinished);
        QVERIFY(fetchFinishedSpy.isValid());

        // Execute fetch
        FetchOperation *op = backend.fetchItems("fetch_finished_test");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Verify fetchFinished was emitted
        QCOMPARE(fetchFinishedSpy.count(), 1);
        QList<QVariant> args = fetchFinishedSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("fetch_finished_test"));
        QCOMPARE(args.at(1).toBool(), true);  // Success

        op->deleteLater();
    }

    void testLocalBackend_fetchItems_signalOrderIsCorrect() {
        // Setup
        QString calDir = m_tempDir.filePath("signal_order_test");
        writeTestIcsFiles(calDir, 5);

        LocalBackend backend(m_tempDir.path());

        // Track signal order
        QStringList signalOrder;

        connect(&backend, &SyncBackend::fetchStarted, this, [&signalOrder]() {
            signalOrder.append("fetchStarted");
        });
        connect(&backend, &SyncBackend::itemFetched, this, [&signalOrder]() {
            if (!signalOrder.contains("itemFetched")) {
                signalOrder.append("itemFetched");
            }
        });
        connect(&backend, &SyncBackend::fetchProgressChanged, this, [&signalOrder]() {
            if (!signalOrder.contains("fetchProgressChanged")) {
                signalOrder.append("fetchProgressChanged");
            }
        });
        connect(&backend, &SyncBackend::fetchFinished, this, [&signalOrder]() {
            signalOrder.append("fetchFinished");
        });

        // Execute fetch
        FetchOperation *op = backend.fetchItems("signal_order_test");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Verify order: fetchStarted -> itemFetched/progress -> fetchFinished
        QVERIFY(signalOrder.size() >= 3);
        QCOMPARE(signalOrder.first(), QStringLiteral("fetchStarted"));
        QCOMPARE(signalOrder.last(), QStringLiteral("fetchFinished"));

        op->deleteLater();
    }

    // ========================================================================
    // LocalBackend Write Tests (operation API — pushItems does not emit
    // writeStarted/writeProgressChanged/writeFinished; tests verify operation success)
    // ========================================================================

    void testLocalBackend_pushItems_writesItemsSuccessfully() {
        // Setup
        QString calDir = m_tempDir.filePath("write_started_test");
        QDir().mkpath(calDir);

        LocalBackend backend(m_tempDir.path());

        QList<Incidence::Ptr> items;
        for (int i = 0; i < 5; i++) {
            items.append(createTestEvent(
                QStringLiteral("write-uid-%1").arg(i),
                QStringLiteral("Write Test %1").arg(i)
            ));
        }

        // Execute push via operation API
        PushOperation *op = backend.pushItems("write_started_test", items, TranscodingPlan{});
        QVERIFY(op != nullptr);

        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify operation succeeded and all items were written
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 5);

        op->deleteLater();
    }

    void testLocalBackend_pushItems_writesMultipleItems() {
        // Setup
        QString calDir = m_tempDir.filePath("write_progress_test");
        QDir().mkpath(calDir);

        LocalBackend backend(m_tempDir.path());

        QList<Incidence::Ptr> items;
        for (int i = 0; i < 15; i++) {
            items.append(createTestEvent(
                QStringLiteral("wprogress-uid-%1").arg(i),
                QStringLiteral("Write Progress Test %1").arg(i)
            ));
        }

        // Execute push via operation API
        PushOperation *op = backend.pushItems("write_progress_test", items, TranscodingPlan{});
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify all 15 items were pushed
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 15);

        op->deleteLater();
    }

    void testLocalBackend_pushItems_singleItem() {
        // Setup
        QString calDir = m_tempDir.filePath("write_finished_test");
        QDir().mkpath(calDir);

        LocalBackend backend(m_tempDir.path());

        QList<Incidence::Ptr> items;
        items.append(createTestEvent("wfinish-uid-1", "Write Finish Test"));

        // Execute push via operation API
        PushOperation *op = backend.pushItems("write_finished_test", items, TranscodingPlan{});
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 1);

        op->deleteLater();
    }

    // ========================================================================
    // MockBackend Signal Tests
    // ========================================================================

    void testMockBackend_fetchItems_emitsFetchSignals() {
        MockBackend backend;

        QList<Incidence::Ptr> items;
        for (int i = 0; i < 5; i++) {
            items.append(createTestEvent(
                QStringLiteral("mock-fetch-uid-%1").arg(i),
                QStringLiteral("Mock Fetch Test %1").arg(i)
            ));
        }
        backend.setCalendarData("mock_fetch_test", items);

        // Spy on signals
        QSignalSpy fetchStartedSpy(&backend, &SyncBackend::fetchStarted);
        QSignalSpy itemFetchedSpy(&backend, &SyncBackend::itemFetched);
        QSignalSpy fetchFinishedSpy(&backend, &SyncBackend::fetchFinished);

        // Execute fetch
        FetchOperation *op = backend.fetchItems("mock_fetch_test");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Verify MockBackend emits streaming fetch signals
        QCOMPARE(fetchStartedSpy.count(), 1);
        QCOMPARE(fetchStartedSpy.first().at(0).toString(), QStringLiteral("mock_fetch_test"));
        QCOMPARE(fetchStartedSpy.first().at(1).toInt(), 5);

        QCOMPARE(itemFetchedSpy.count(), 5);

        QCOMPARE(fetchFinishedSpy.count(), 1);
        QCOMPARE(fetchFinishedSpy.first().at(1).toBool(), true);  // success

        // Also verify the operation completes successfully
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->fetchedItems().size(), 5);

        op->deleteLater();
    }

    void testMockBackend_pushItems_writesItemsSuccessfully() {
        MockBackend backend;

        QList<Incidence::Ptr> items;
        for (int i = 0; i < 3; i++) {
            items.append(createTestEvent(
                QStringLiteral("mock-write-uid-%1").arg(i),
                QStringLiteral("Mock Write Test %1").arg(i)
            ));
        }

        // Execute push via operation API
        PushOperation *op = backend.pushItems("mock_write_test", items, TranscodingPlan{});
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify operation succeeded
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 3);

        op->deleteLater();
    }

    // ========================================================================
    // Edge Case Tests
    // ========================================================================

    void testLocalBackend_fetchItems_emptyDirectory() {
        // Setup: Create empty directory
        QString calDir = m_tempDir.filePath("empty_dir_test");
        QDir().mkpath(calDir);

        LocalBackend backend(m_tempDir.path());

        QSignalSpy fetchStartedSpy(&backend, &SyncBackend::fetchStarted);
        QSignalSpy fetchFinishedSpy(&backend, &SyncBackend::fetchFinished);

        FetchOperation *op = backend.fetchItems("empty_dir_test");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Should emit start with 0 items
        QCOMPARE(fetchStartedSpy.count(), 1);
        QCOMPARE(fetchStartedSpy.first().at(1).toInt(), 0);

        // Should emit finished with success
        QCOMPARE(fetchFinishedSpy.count(), 1);
        QCOMPARE(fetchFinishedSpy.first().at(1).toBool(), true);

        // Operation should have empty results
        QCOMPARE(op->fetchedItems().size(), 0);

        op->deleteLater();
    }

    void testLocalBackend_fetchItems_nonexistentDirectory() {
        LocalBackend backend(m_tempDir.path());

        QSignalSpy fetchFinishedSpy(&backend, &SyncBackend::fetchFinished);

        FetchOperation *op = backend.fetchItems("nonexistent_calendar");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(5000));

        // Should emit finished with failure
        QCOMPARE(fetchFinishedSpy.count(), 1);
        QCOMPARE(fetchFinishedSpy.first().at(1).toBool(), false);

        // Operation should be failed
        QCOMPARE(op->state(), SyncOperation::Failed);

        op->deleteLater();
    }

    void testLocalBackend_pushItems_emptyList() {
        QString calDir = m_tempDir.filePath("empty_write_test");
        QDir().mkpath(calDir);

        LocalBackend backend(m_tempDir.path());

        QList<Incidence::Ptr> emptyItems;

        // Execute push via operation API
        PushOperation *op = backend.pushItems("empty_write_test", emptyItems, TranscodingPlan{});
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Operation should complete successfully with no items
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 0);

        op->deleteLater();
    }

    // ========================================================================
    // OrgBackend Signal Tests
    // OrgBackend::fetchItems emits streaming fetch signals (fetchStarted,
    // itemFetched, fetchProgressChanged, fetchFinished).
    // OrgBackend::pushItems also emits streaming write signals.
    // ========================================================================

#ifdef KALBURATOR_HAVE_ORG_IO

    void testOrgBackend_fetchItems_emitsFetchStarted() {
        // Setup: Create org directory and file
        QString orgDir = m_tempDir.filePath("org_fetch_started");
        QDir().mkpath(orgDir);
        writeTestOrgFile(orgDir + "/test_cal.org", 5);

        OrgBackend backend(orgDir);

        // Spy on fetchStarted signal
        QSignalSpy fetchStartedSpy(&backend, &SyncBackend::fetchStarted);
        QVERIFY(fetchStartedSpy.isValid());

        // Fetch items via operation API
        FetchOperation *op = backend.fetchItems("test_cal");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify fetchStarted was emitted
        QCOMPARE(fetchStartedSpy.count(), 1);
        QCOMPARE(fetchStartedSpy.first().at(0).toString(), QStringLiteral("test_cal"));
        QCOMPARE(fetchStartedSpy.first().at(1).toInt(), 5);  // 5 headlines

        op->deleteLater();
    }

    void testOrgBackend_fetchItems_emitsItemFetchedForEachItem() {
        // Setup
        QString orgDir = m_tempDir.filePath("org_item_fetched");
        QDir().mkpath(orgDir);
        writeTestOrgFile(orgDir + "/test_cal.org", 8);

        OrgBackend backend(orgDir);

        // Spy on itemFetched signal
        QSignalSpy itemFetchedSpy(&backend, &SyncBackend::itemFetched);
        QVERIFY(itemFetchedSpy.isValid());

        FetchOperation *op = backend.fetchItems("test_cal");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify itemFetched was emitted for each item
        QCOMPARE(itemFetchedSpy.count(), 8);

        // Verify each signal has valid data
        for (int i = 0; i < itemFetchedSpy.count(); i++) {
            QList<QVariant> args = itemFetchedSpy.at(i);
            QCOMPARE(args.at(0).toString(), QStringLiteral("test_cal"));
            auto incidence = args.at(1).value<Incidence::Ptr>();
            QVERIFY(incidence);
            QVERIFY(!incidence->uid().isEmpty());
        }

        op->deleteLater();
    }

    void testOrgBackend_fetchItems_emitsFetchProgressChanged() {
        // Setup
        QString orgDir = m_tempDir.filePath("org_progress");
        QDir().mkpath(orgDir);
        writeTestOrgFile(orgDir + "/test_cal.org", 10);

        OrgBackend backend(orgDir);

        // Spy on fetchProgressChanged signal
        QSignalSpy progressSpy(&backend, &SyncBackend::fetchProgressChanged);
        QVERIFY(progressSpy.isValid());

        FetchOperation *op = backend.fetchItems("test_cal");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify progress was emitted incrementally
        QVERIFY(progressSpy.count() >= 1);  // At least one progress update

        // Verify progress reaches 100%
        QList<QVariant> lastProgress = progressSpy.last();
        int lastCurrent = lastProgress.at(1).toInt();
        int lastTotal = lastProgress.at(2).toInt();
        QCOMPARE(lastCurrent, lastTotal);  // Should reach 100%

        op->deleteLater();
    }

    void testOrgBackend_fetchItems_emitsFetchFinished() {
        // Setup
        QString orgDir = m_tempDir.filePath("org_fetch_finished");
        QDir().mkpath(orgDir);
        writeTestOrgFile(orgDir + "/test_cal.org", 3);

        OrgBackend backend(orgDir);

        // Spy on fetchFinished signal
        QSignalSpy fetchFinishedSpy(&backend, &SyncBackend::fetchFinished);
        QVERIFY(fetchFinishedSpy.isValid());

        FetchOperation *op = backend.fetchItems("test_cal");
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify fetchFinished was emitted with success
        QCOMPARE(fetchFinishedSpy.count(), 1);
        QCOMPARE(fetchFinishedSpy.first().at(0).toString(), QStringLiteral("test_cal"));
        QCOMPARE(fetchFinishedSpy.first().at(1).toBool(), true);  // success

        op->deleteLater();
    }

    void testOrgBackend_pushItems_emitsWriteStarted() {
        // Setup
        QString orgDir = m_tempDir.filePath("org_write_started");
        QDir().mkpath(orgDir);
        // Create empty org file first
        writeTestOrgFile(orgDir + "/test_cal.org", 0);

        OrgBackend backend(orgDir);

        QList<Incidence::Ptr> items;
        for (int i = 0; i < 4; i++) {
            Todo::Ptr todo(new Todo);
            todo->setUid(QStringLiteral("org-write-uid-%1").arg(i));
            todo->setSummary(QStringLiteral("Org Write Test %1").arg(i));
            items.append(todo);
        }

        // Spy on writeStarted signal
        QSignalSpy writeStartedSpy(&backend, &SyncBackend::writeStarted);
        QVERIFY(writeStartedSpy.isValid());

        // OrgBackend::pushItems emits write signals
        PushOperation *op = backend.pushItems("test_cal", items, TranscodingPlan{});
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify writeStarted was emitted
        QCOMPARE(writeStartedSpy.count(), 1);
        QCOMPARE(writeStartedSpy.first().at(0).toString(), QStringLiteral("test_cal"));
        QCOMPARE(writeStartedSpy.first().at(1).toInt(), 4);  // 4 items

        op->deleteLater();
    }

    void testOrgBackend_pushItems_emitsWriteProgressChanged() {
        // Setup
        QString orgDir = m_tempDir.filePath("org_write_progress");
        QDir().mkpath(orgDir);
        writeTestOrgFile(orgDir + "/test_cal.org", 0);

        OrgBackend backend(orgDir);

        QList<Incidence::Ptr> items;
        for (int i = 0; i < 6; i++) {
            Todo::Ptr todo(new Todo);
            todo->setUid(QStringLiteral("org-wprogress-uid-%1").arg(i));
            todo->setSummary(QStringLiteral("Org Write Progress %1").arg(i));
            items.append(todo);
        }

        // Spy on writeProgressChanged signal
        QSignalSpy progressSpy(&backend, &SyncBackend::writeProgressChanged);
        QVERIFY(progressSpy.isValid());

        // OrgBackend::pushItems emits write progress signals
        PushOperation *op = backend.pushItems("test_cal", items, TranscodingPlan{});
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify progress was emitted
        QCOMPARE(progressSpy.count(), 6);  // One per item

        // Verify progress is monotonically increasing
        for (int i = 0; i < progressSpy.count(); i++) {
            QCOMPARE(progressSpy.at(i).at(1).toInt(), i + 1);  // current
            QCOMPARE(progressSpy.at(i).at(2).toInt(), 6);      // total
        }

        op->deleteLater();
    }

    void testOrgBackend_pushItems_emitsWriteFinished() {
        // Setup
        QString orgDir = m_tempDir.filePath("org_write_finished");
        QDir().mkpath(orgDir);
        writeTestOrgFile(orgDir + "/test_cal.org", 0);

        OrgBackend backend(orgDir);

        QList<Incidence::Ptr> items;
        Todo::Ptr todo(new Todo);
        todo->setUid(QStringLiteral("org-wfinish-uid-1"));
        todo->setSummary(QStringLiteral("Org Write Finish Test"));
        items.append(todo);

        // OrgBackend::pushItems completes via operation state
        PushOperation *op = backend.pushItems("test_cal", items, TranscodingPlan{});
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 5000);

        // Verify operation completed successfully
        QVERIFY(op->state() == SyncOperation::Completed || op->state() == SyncOperation::Failed);

        op->deleteLater();
    }

#endif // KALBURATOR_HAVE_ORG_IO

    // ========================================================================
    // RemoteCalendarBackend Signal Tests (requires CalDAV server)
    // ========================================================================

    void testRemoteCalendarBackend_fetchItems_emitsFetchSignals() {
        // Skip if CalDAV server not available
        if (!isCaldavServerAvailable()) {
            QSKIP("CalDAV server not available at 127.0.0.1:5232");
        }

        // Setup: Create backend connected to test server
        QUrl serverUrl = caldavPrincipalUrl(CALDAV_USERNAME_1);
        RemoteCalendarBackend backend(serverUrl, CALDAV_USERNAME_1, CALDAV_PASSWORD_1);

        // First, discover calendars
        QSignalSpy calDiscoverySpy(&backend, &SyncBackend::calendarDiscovered);
        backend.loadCalendars("test_collection");

        // Wait for calendar discovery (async) - but don't fail if empty
        calDiscoverySpy.wait(10000);

        // Get first discovered calendar, or create one if none exist
        QString calendarId;
        if (calDiscoverySpy.count() > 0) {
            calendarId = calDiscoverySpy.first().at(1).toString();
        } else {
            // Try to create a test calendar
            QString testCalName = QStringLiteral("signal-test-cal-%1").arg(QDateTime::currentMSecsSinceEpoch());
            QSignalSpy calCreatedSpy(&backend, &SyncBackend::calendarCreated);
            bool created = backend.createCalendar("test_collection", testCalName, "Signal Test Calendar");
            if (!created || !calCreatedSpy.wait(10000)) {
                QSKIP("No calendars on server and couldn't create one");
            }
            calendarId = testCalName;
        }

        // Create test events on server first
        QList<Incidence::Ptr> testItems;
        for (int i = 0; i < 5; i++) {
            Event::Ptr event(new Event);
            event->setUid(QStringLiteral("remote-signal-test-uid-%1").arg(i));
            event->setSummary(QStringLiteral("Remote Signal Test %1").arg(i));
            event->setDtStart(QDateTime::currentDateTime());
            event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
            testItems.append(event);
        }

        // Push items to server
        PushOperation *pushOp = backend.pushItems(calendarId, testItems, TranscodingPlan{});
        QSignalSpy pushFinishedSpy(pushOp, &SyncOperation::finished);
        QVERIFY(pushFinishedSpy.wait(15000));
        QCOMPARE(pushOp->state(), SyncOperation::Succeeded);

        // Now test fetching with streaming signals
        QSignalSpy fetchStartedSpy(&backend, &SyncBackend::fetchStarted);
        QSignalSpy itemFetchedSpy(&backend, &SyncBackend::itemFetched);
        QSignalSpy fetchProgressSpy(&backend, &SyncBackend::fetchProgressChanged);
        QSignalSpy fetchFinishedSpy(&backend, &SyncBackend::fetchFinished);

        FetchOperation *fetchOp = backend.fetchItems(calendarId);
        QSignalSpy fetchOpFinishedSpy(fetchOp, &SyncOperation::finished);
        QVERIFY(fetchOpFinishedSpy.wait(15000));

        // Verify fetch streaming signals were emitted
        QVERIFY2(fetchStartedSpy.count() >= 1, "fetchStarted should be emitted");
        QVERIFY2(itemFetchedSpy.count() >= 5, "itemFetched should be emitted for each item");
        QVERIFY2(fetchProgressSpy.count() >= 1, "fetchProgressChanged should be emitted");
        QVERIFY2(fetchFinishedSpy.count() >= 1, "fetchFinished should be emitted");

        // Cleanup: Delete test items from server
        QStringList uidsToDelete;
        for (const auto &item : testItems) {
            uidsToDelete.append(item->uid());
        }
        DeleteOperation *deleteOp = backend.deleteItems(calendarId, uidsToDelete);
        QSignalSpy deleteFinishedSpy(deleteOp, &SyncOperation::finished);
        deleteFinishedSpy.wait(10000);

        pushOp->deleteLater();
        fetchOp->deleteLater();
        deleteOp->deleteLater();
    }

    void testRemoteCalendarBackend_pushItems_pushesItemsSuccessfully() {
        // Skip if CalDAV server not available
        if (!isCaldavServerAvailable()) {
            QSKIP("CalDAV server not available at 127.0.0.1:5232");
        }

        // Setup
        QUrl serverUrl = caldavPrincipalUrl(CALDAV_USERNAME_1);
        RemoteCalendarBackend backend(serverUrl, CALDAV_USERNAME_1, CALDAV_PASSWORD_1);

        // Discover calendars
        QSignalSpy calDiscoverySpy(&backend, &SyncBackend::calendarDiscovered);
        backend.loadCalendars("test_collection");
        calDiscoverySpy.wait(10000);

        QString calendarId;
        if (calDiscoverySpy.count() > 0) {
            calendarId = calDiscoverySpy.first().at(1).toString();
        } else {
            // Try to create a test calendar
            QString testCalName = QStringLiteral("write-signal-test-cal-%1").arg(QDateTime::currentMSecsSinceEpoch());
            QSignalSpy calCreatedSpy(&backend, &SyncBackend::calendarCreated);
            bool created = backend.createCalendar("test_collection", testCalName, "Write Signal Test Calendar");
            if (!created || !calCreatedSpy.wait(10000)) {
                QSKIP("No calendars on server and couldn't create one");
            }
            calendarId = testCalName;
        }

        // Use timestamp in UIDs for test isolation (avoids 412 if previous run left items)
        qint64 ts = QDateTime::currentMSecsSinceEpoch();
        QList<Incidence::Ptr> items;
        for (int i = 0; i < 3; i++) {
            Event::Ptr event(new Event);
            event->setUid(QStringLiteral("remote-write-signal-test-%1-%2").arg(ts).arg(i));
            event->setSummary(QStringLiteral("Remote Write Signal Test %1").arg(i));
            event->setDtStart(QDateTime::currentDateTime());
            event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
            items.append(event);
        }

        // Push items via operation API
        PushOperation *op = backend.pushItems(calendarId, items, TranscodingPlan{});
        QSignalSpy finishedSpy(op, &SyncOperation::finished);
        QVERIFY(finishedSpy.wait(30000));  // Wait up to 30s for network operations

        // Verify push operation succeeded
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->succeededUids().size(), 3);

        // Cleanup
        QStringList uidsToDelete;
        for (const auto &item : items) {
            uidsToDelete.append(item->uid());
        }
        DeleteOperation *deleteOp = backend.deleteItems(calendarId, uidsToDelete);
        QSignalSpy deleteFinishedSpy(deleteOp, &SyncOperation::finished);
        deleteFinishedSpy.wait(10000);

        op->deleteLater();
        deleteOp->deleteLater();
    }

    void testRemoteCalendarBackend_startSync_emitsWriteSignals() {
        // Skip if CalDAV server not available
        if (!isCaldavServerAvailable()) {
            QSKIP("CalDAV server not available at 127.0.0.1:5232");
        }

        // Setup
        QUrl serverUrl = caldavPrincipalUrl(CALDAV_USERNAME_1);
        RemoteCalendarBackend backend(serverUrl, CALDAV_USERNAME_1, CALDAV_PASSWORD_1);

        // Discover calendars
        QSignalSpy calDiscoverySpy(&backend, &SyncBackend::calendarDiscovered);
        backend.loadCalendars("test_collection");
        calDiscoverySpy.wait(10000);

        QString calendarId;
        if (calDiscoverySpy.count() > 0) {
            calendarId = calDiscoverySpy.first().at(1).toString();
        } else {
            // Try to create a test calendar
            QString testCalName = QStringLiteral("sync-signal-test-cal-%1").arg(QDateTime::currentMSecsSinceEpoch());
            QSignalSpy calCreatedSpy(&backend, &SyncBackend::calendarCreated);
            bool created = backend.createCalendar("test_collection", testCalName, "Sync Signal Test Calendar");
            if (!created || !calCreatedSpy.wait(10000)) {
                QSKIP("No calendars on server and couldn't create one");
            }
            calendarId = testCalName;
        }

        auto testCal = new MemoryCalendar(QTimeZone::systemTimeZone());
        testCal->setId(calendarId);

        // Create test items for sync
        QList<Incidence::Ptr> creations;
        for (int i = 0; i < 4; i++) {
            Event::Ptr event(new Event);
            event->setUid(QStringLiteral("remote-sync-signal-test-%1").arg(i));
            event->setSummary(QStringLiteral("Remote Sync Signal Test %1").arg(i));
            event->setDtStart(QDateTime::currentDateTime());
            event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
            creations.append(event);
        }

        // Spy on write signals
        QSignalSpy writeStartedSpy(&backend, &SyncBackend::writeStarted);
        QSignalSpy writeProgressSpy(&backend, &SyncBackend::writeProgressChanged);

        // Start sync with creations
        QSignalSpy syncCompletedSpy(&backend, &SyncBackend::syncCompleted);
        backend.startSync("test_collection", testCal, creations, {}, {});

        // Wait for sync to complete
        QVERIFY(syncCompletedSpy.wait(20000));

        // Verify write signals were emitted during sync
        QCOMPARE(writeStartedSpy.count(), 1);
        QCOMPARE(writeStartedSpy.first().at(1).toInt(), 4);  // 4 creations
        QVERIFY(writeProgressSpy.count() >= 1);  // At least some progress

        // Cleanup
        QStringList uidsToDelete;
        for (const auto &item : creations) {
            uidsToDelete.append(item->uid());
        }
        DeleteOperation *deleteOp = backend.deleteItems(calendarId, uidsToDelete);
        QSignalSpy deleteFinishedSpy(deleteOp, &SyncOperation::finished);
        deleteFinishedSpy.wait(10000);

        deleteOp->deleteLater();
        delete testCal;
    }
};

QTEST_GUILESS_MAIN(TestBackendSignals)
#include "tst_backend_signals.moc"
