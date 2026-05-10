/// F1 Task 7 — engine boundary integration test
///
/// Pins SyncEngine as the unified entry point for the calendar domain.
/// One test method remains after Task 12 pruning:
///
///  1. `runSync_calendarMapping_propagatesEvents` — drives a calendar
///     mapping through `SyncEngine::runSync()` (CalendarDomainAdapter
///     under the hood); confirms the per-mapping flow still works
///     after Tasks 4-5 collapsed `SyncCoordinator` and routed
///     through the adapter.
///
/// The blob-facade cases (`runBlobTwoWay_*`, `mixedDomains_*`) were
/// removed in Task 12; their coverage moved to
/// `tst_engine_mirror_direction` (Task 11). The facade methods they
/// exercised are deleted in Task 13.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "blobbaselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-cal";

constexpr int kSyncTimeoutMs = 5000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeCalendarMapping()
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = SyncMode::TwoWay;
    m.conflictPolicy  = ConflictResolution::LastWriteWins;
    m.enabled         = true;
    return m;
}

} // namespace

class TestEngineUnifiedBoundary : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void runSync_calendarMapping_propagatesEvents();

private:
    bool runOneCalendarSync();
    void seedCalendarFixtures();

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_calSource;
    std::unique_ptr<MockBackend>           m_calTarget;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_engine;
};

void TestEngineUnifiedBoundary::seedCalendarFixtures()
{
    m_calSource->createCalendar(QString::fromLatin1(kCollectionId),
                                QString::fromLatin1(kCalendarId),
                                QStringLiteral("Calendar 1"));
    m_calTarget->createCalendar(QString::fromLatin1(kCollectionId),
                                QString::fromLatin1(kCalendarId),
                                QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);
}

void TestEngineUnifiedBoundary::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_calSource = std::make_unique<MockBackend>();
    m_calTarget = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        m_calSource.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        m_calTarget.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    seedCalendarFixtures();

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setBaselineStore(m_calendarBaselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setCollection(m_host->stubCollection());
    m_engine->setSyncMappings({ makeCalendarMapping() });
}

void TestEngineUnifiedBoundary::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_calendarBaselines.reset();
    m_host.reset();
    m_calTarget.reset();
    m_calSource.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

bool TestEngineUnifiedBoundary::runOneCalendarSync()
{
    auto future = m_engine->runSyncFuture(
        SyncEngine::SyncBehavior::Unmonitored);
    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    if (!future.isFinished()) {
        qWarning() << "runSyncFuture did not finish within"
                   << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSyncFuture was canceled unexpectedly";
        return false;
    }
    return true;
}

// ---- Tests ---------------------------------------------------------------

void TestEngineUnifiedBoundary::runSync_calendarMapping_propagatesEvents()
{
    // Source has two events, target empty. After runSync, both
    // sides should converge — proving the calendar adapter is wired
    // and exercised end-to-end through the SyncEngine boundary.
    m_calSource->addIncidence(QString::fromLatin1(kCalendarId),
                              makeEvent(QStringLiteral("evt-1"),
                                        QStringLiteral("Event One")));
    m_calSource->addIncidence(QString::fromLatin1(kCalendarId),
                              makeEvent(QStringLiteral("evt-2"),
                                        QStringLiteral("Event Two")));

    QSignalSpy startedSpy(m_engine.get(), &SyncEngine::syncStarted);

    QVERIFY(runOneCalendarSync());

    QVERIFY(startedSpy.size() >= 1);
    // Completion is observed via QFuture::isFinished inside
    // runOneCalendarSync (per-mapping syncCompleted signal retired
    // in F2 Task 42).

    QCOMPARE(m_calSource->allUids(QString::fromLatin1(kCalendarId)).size(), 2);
    QCOMPARE(m_calTarget->allUids(QString::fromLatin1(kCalendarId)).size(), 2);
}


QTEST_MAIN(TestEngineUnifiedBoundary)
#include "tst_engine_unified_boundary.moc"
