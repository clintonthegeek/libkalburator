// tst_calendar_sync_full.cpp
//
// Phase D.0 — Full bidirectional sync against MockBackend pair through
// StubSyncHost. Pins SyncEngine/SyncEngine behavior before any
// engine refactor lands.
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

constexpr int kSyncTimeoutMs = 5000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeTwoWayMapping()
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

class TestCalendarSyncFull : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void fullSync_bothEmpty_doesNothing();
    void fullSync_sourceHasEvents_propagatesToTarget();
    void fullSync_targetHasEvents_propagatesToSource();
    void fullSync_disjointEvents_bothConverge();

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
};

// ---- Lifecycle ------------------------------------------------------------

void TestCalendarSyncFull::initTestCase() {}
void TestCalendarSyncFull::cleanupTestCase() {}

void TestCalendarSyncFull::init()
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

    // Both backends must have the calendar present before sync runs.
    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    // SyncEngine::applyChangesToBackend looks up the calendar via
    // host->collection()->calendar(calendarId). The collection must
    // have a MemoryCalendar registered under the same id used in the
    // SyncMapping for the engine to write changes back to either side.
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                 hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_coordinator->setBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeTwoWayMapping() });
}

void TestCalendarSyncFull::cleanup()
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

bool TestCalendarSyncFull::runOneSync()
{
    // Use the multi-mapping form (runSyncFuture() with no mappingId).
    // The single-mapping form does not cleanly exit the post-sync
    // processNextMapping loop in SyncEngine, leading to a second
    // queued sync that interferes with cleanup.
    auto future = m_coordinator->runSyncFuture(
        SyncEngine::SyncBehavior::Unmonitored);
    // QFuture::waitForFinished() does not spin the event loop; poll
    // with QTest::qWait() until the future finishes or we time out.
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

void TestCalendarSyncFull::fullSync_bothEmpty_doesNothing()
{
    QVERIFY(sourceUids().isEmpty());
    QVERIFY(targetUids().isEmpty());

    QVERIFY(runOneSync());

    QVERIFY(sourceUids().isEmpty());
    QVERIFY(targetUids().isEmpty());
}

void TestCalendarSyncFull::fullSync_sourceHasEvents_propagatesToTarget()
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Event One")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-2"),
                                     QStringLiteral("Event Two")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-3"),
                                     QStringLiteral("Event Three")));

    QCOMPARE(sourceUids().size(), 3);
    QVERIFY(targetUids().isEmpty());

    QVERIFY(runOneSync());

    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 3);
}

void TestCalendarSyncFull::fullSync_targetHasEvents_propagatesToSource()
{
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-A"),
                                     QStringLiteral("Event A")));
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-B"),
                                     QStringLiteral("Event B")));
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-C"),
                                     QStringLiteral("Event C")));

    QVERIFY(sourceUids().isEmpty());
    QCOMPARE(targetUids().size(), 3);

    QVERIFY(runOneSync());

    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 3);
}

void TestCalendarSyncFull::fullSync_disjointEvents_bothConverge()
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-A"),
                                     QStringLiteral("From Source")));
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-B"),
                                     QStringLiteral("From Target")));

    QVERIFY(runOneSync());

    QCOMPARE(sourceUids().size(), 2);
    QCOMPARE(targetUids().size(), 2);
    QVERIFY(sourceUids().contains(QStringLiteral("evt-A")));
    QVERIFY(sourceUids().contains(QStringLiteral("evt-B")));
    QVERIFY(targetUids().contains(QStringLiteral("evt-A")));
    QVERIFY(targetUids().contains(QStringLiteral("evt-B")));
}

QTEST_MAIN(TestCalendarSyncFull)
#include "tst_calendar_sync_full.moc"
