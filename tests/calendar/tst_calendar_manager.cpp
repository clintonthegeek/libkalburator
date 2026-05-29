#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "calendarmanager.h"
#include "logicalcalendar.h"
#include "mockbackend.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {
constexpr auto kBackendA   = "backend-a";
constexpr auto kBackendB   = "backend-b";
constexpr auto kCalId      = "cal-1";
constexpr auto kCalIdB     = "cal-1-b";
constexpr auto kLogicalId  = "logical-1";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    e->setUid(uid);
    e->setSummary(summary);
    e->setDtStart(QDateTime(QDate(2026, 5, 29), QTime(9, 0), QTimeZone::utc()));
    return e;
}

// A single-primary LogicalCalendar bound to one backend.
LogicalCalendar makeLogical(const QString &logicalId,
                            const QString &backendId,
                            const QString &calId,
                            bool needsCreation)
{
    LogicalCalendar lc;
    lc.id          = logicalId;
    lc.displayName = QStringLiteral("Test ") + logicalId;
    lc.type        = CalendarType::Hybrid;
    CalendarBackendBinding b;
    b.backendId     = backendId;
    b.calendarId    = calId;
    b.role          = BackendRole::Primary;
    b.enabled       = true;
    b.needsCreation = needsCreation;
    lc.bindings.append(b);
    return lc;
}

// Task 3: helper — two-backend logical with both needing creation.
LogicalCalendar makeTwoBackendLogical()
{
    LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                     QString::fromLatin1(kBackendA),
                                     QString::fromLatin1(kCalId), true);
    CalendarBackendBinding spoke;
    spoke.backendId     = QString::fromLatin1(kBackendB);
    spoke.calendarId    = QString::fromLatin1(kCalIdB);
    spoke.role          = BackendRole::Sync1;
    spoke.enabled       = true;
    spoke.needsCreation = true;
    lc.bindings.append(spoke);
    return lc;
}
} // namespace

class TestCalendarManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void construct_doesNotCrash_exposesHostAndConfig();

    // Task 2
    void createCalendar_needsCreation_createsOnBackend_emitsSignal();
    void createCalendar_registersInConfig_clearsNeedsCreation();

    // Task 3
    void createCalendar_oneBackendFails_otherStillCreated_noRollback();

private:
    std::unique_ptr<BackendRegistry> m_registry;
    std::unique_ptr<MockBackend>     m_backendA;
    std::unique_ptr<MockBackend>     m_backendB;
    std::unique_ptr<StubSyncHost>    m_host;
    std::unique_ptr<CalendarManager> m_mgr;
};

void TestCalendarManager::init()
{
    m_registry = std::make_unique<BackendRegistry>();
    m_backendA = std::make_unique<MockBackend>(QString::fromLatin1(kBackendA));
    m_backendB = std::make_unique<MockBackend>(QString::fromLatin1(kBackendB));
    m_registry->registerBackendInstance(QString::fromLatin1(kBackendA), m_backendA.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kBackendB), m_backendB.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    m_mgr  = std::make_unique<CalendarManager>(m_host.get(), m_host->stubCollection());
}

void TestCalendarManager::cleanup()
{
    m_mgr.reset();
    m_host.reset();
    m_backendB.reset();
    m_backendA.reset();
    m_registry.reset();
}

void TestCalendarManager::construct_doesNotCrash_exposesHostAndConfig()
{
    QVERIFY(m_mgr->host() == m_host.get());
    QVERIFY(m_mgr->configManager() == m_host->configStore());
}

// ============================================================
// Task 2 — createCalendar success path
// ============================================================

void TestCalendarManager::createCalendar_needsCreation_createsOnBackend_emitsSignal()
{
    QSignalSpy created(m_mgr.get(), &CalendarManager::calendarCreated);
    QVERIFY(created.isValid());
    const LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                           QString::fromLatin1(kBackendA),
                                           QString::fromLatin1(kCalId), true);
    const CreationResult r = m_mgr->createCalendar(lc);
    QVERIFY(r.success);
    QCOMPARE(r.logicalCalendarId, QString::fromLatin1(kLogicalId));
    QVERIFY(r.errors.isEmpty());
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.at(0).at(0).toString(), QString::fromLatin1(kLogicalId));
}

void TestCalendarManager::createCalendar_registersInConfig_clearsNeedsCreation()
{
    const LogicalCalendar lc = makeLogical(QString::fromLatin1(kLogicalId),
                                           QString::fromLatin1(kBackendA),
                                           QString::fromLatin1(kCalId), true);
    m_mgr->createCalendar(lc);
    const LogicalCalendar stored = m_host->configStore()->logicalCalendar(QString::fromLatin1(kLogicalId));
    QCOMPARE(stored.id, QString::fromLatin1(kLogicalId));
    QCOMPARE(stored.primaryBinding().needsCreation, false);
    QVERIFY(m_host->stubConfig()->saveCount() >= 1);
}

// ============================================================
// Task 3 — createCalendar partial failure is non-atomic
// ============================================================

void TestCalendarManager::createCalendar_oneBackendFails_otherStillCreated_noRollback()
{
    m_backendB->setFailurePoint(MockBackend::FailurePoint::OnCreateCalendar, 0,
                                QStringLiteral("injected create failure"));
    QSignalSpy failed(m_mgr.get(), &CalendarManager::operationFailed);
    const CreationResult r = m_mgr->createCalendar(makeTwoBackendLogical());
    QVERIFY(!r.success);
    QVERIFY(!r.errors.isEmpty());
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendA)), true);
    QCOMPARE(r.backendResults.value(QString::fromLatin1(kBackendB)), false);
    QVERIFY(failed.count() >= 1);
    QVERIFY(m_backendA->calendarIds().contains(QString::fromLatin1(kCalId)));
    QVERIFY(!m_backendB->calendarIds().contains(QString::fromLatin1(kCalIdB)));
}

QTEST_MAIN(TestCalendarManager)
#include "tst_calendar_manager.moc"
