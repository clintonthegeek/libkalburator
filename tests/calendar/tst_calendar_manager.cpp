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
} // namespace

class TestCalendarManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void construct_doesNotCrash_exposesHostAndConfig();

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

QTEST_MAIN(TestCalendarManager)
#include "tst_calendar_manager.moc"
