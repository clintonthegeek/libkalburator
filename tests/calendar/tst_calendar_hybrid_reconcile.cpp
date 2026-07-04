// tst_calendar_hybrid_reconcile.cpp
//
// Per-kind canon dispatch Task 8 — engine-level acceptance test. A one-way
// upload of a calendar holding BOTH a VEVENT and a VTODO must propagate
// both to the target. Prior to Task 5's kind dispatch, the VTODO silently
// demoted to empty bytes and never appeared on the target (the F4 bug).

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Todo>

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "syncconflictstore.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";

constexpr int kSyncTimeoutMs = 30000;

SyncMapping makeOneWayUploadMapping()
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = SyncMode::OneWayUpload;
    m.conflictPolicy  = ConflictResolution::SourceWins;
    m.enabled         = true;
    return m;
}

} // namespace

class TestCalendarHybridReconcile : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void cleanupTestCase() {}
    void init();
    void cleanup();

    void hybridCalendarPropagatesEventsAndTodos();

private:
    bool runOneSync();
    QStringList sourceUids() const { return m_source->allUids(QString::fromLatin1(kCalendarId)); }
    QStringList targetUids() const { return m_target->allUids(QString::fromLatin1(kCalendarId)); }

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>       m_coordinator;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestCalendarHybridReconcile::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>();
    m_target   = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId),
                                        m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId),
                                        m_target.get());

    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeOneWayUploadMapping() });
}

void TestCalendarHybridReconcile::cleanup()
{
    m_coordinator.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_calendarBaselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

bool TestCalendarHybridReconcile::runOneSync()
{
    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_coordinator->runSync(req);
    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    if (!future.isFinished()) {
        qWarning() << "runSync did not finish within"
                   << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSync was canceled unexpectedly";
        return false;
    }
    return true;
}

// ---- Tests ---------------------------------------------------------------

void TestCalendarHybridReconcile::hybridCalendarPropagatesEventsAndTodos()
{
    // Source holds one VEVENT and one VTODO in the same calendar.
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(QStringLiteral("evt-1"));
    event->setSummary(QStringLiteral("Standup"));
    event->setDtStart(QDateTime::currentDateTimeUtc());
    m_source->addIncidence(QString::fromLatin1(kCalendarId), event);

    auto todo = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo());
    todo->setUid(QStringLiteral("todo-1"));
    todo->setSummary(QStringLiteral("Buy milk"));
    todo->setStatus(KCalendarCore::Incidence::StatusNeedsAction);
    m_source->addIncidence(QString::fromLatin1(kCalendarId), todo);

    QCOMPARE(sourceUids().size(), 2);
    QVERIFY(targetUids().isEmpty());

    QVERIFY(runOneSync());

    // BOTH must land on the target — the VTODO must not vanish (the bug).
    QCOMPARE(targetUids().size(), 2);
    QVERIFY2(targetUids().contains(QStringLiteral("evt-1")),  "event must propagate");
    QVERIFY2(targetUids().contains(QStringLiteral("todo-1")), "todo must propagate (was lost)");
}

QTEST_MAIN(TestCalendarHybridReconcile)
#include "tst_calendar_hybrid_reconcile.moc"
