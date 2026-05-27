// tst_calendar_sync_oneway.cpp
//
// Phase D.0 — One-way upload sync. Verifies that SyncMode::OneWayUpload
// only propagates source→target and does not pull target-only events
// back to source.
//
// See: docs/phase0/04l-phase-d0-test-harness-design.md

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
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

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

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

class TestCalendarSyncOneway : public QObject
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

    void oneWayUpload_sourceToTarget();
    void oneWayUpload_ignoresTargetOnlyEvents();

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

void TestCalendarSyncOneway::init()
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

void TestCalendarSyncOneway::cleanup()
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

bool TestCalendarSyncOneway::runOneSync()
{
    auto future = m_coordinator->runSyncFuture(
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

void TestCalendarSyncOneway::oneWayUpload_sourceToTarget()
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Source One")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-2"),
                                     QStringLiteral("Source Two")));

    QCOMPARE(sourceUids().size(), 2);
    QVERIFY(targetUids().isEmpty());

    QVERIFY(runOneSync());

    QCOMPARE(sourceUids().size(), 2);
    QCOMPARE(targetUids().size(), 2);
    QVERIFY(targetUids().contains(QStringLiteral("evt-1")));
    QVERIFY(targetUids().contains(QStringLiteral("evt-2")));
}

void TestCalendarSyncOneway::oneWayUpload_ignoresTargetOnlyEvents()
{
    // Target has events that are NOT on source. One-way upload mode
    // should leave source untouched (no pull from target).
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-X"),
                                     QStringLiteral("Target Only X")));
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-Y"),
                                     QStringLiteral("Target Only Y")));

    QVERIFY(sourceUids().isEmpty());
    QCOMPARE(targetUids().size(), 2);

    QVERIFY(runOneSync());

    // Source must remain empty — one-way upload doesn't pull target.
    QVERIFY(sourceUids().isEmpty());
}

QTEST_MAIN(TestCalendarSyncOneway)
#include "tst_calendar_sync_oneway.moc"
