// tst_calendar_sync_error_recovery.cpp
//
// Phase F.0 — Backend write/fetch failure paths against
// MockBackend. Pins SyncResult.success == false and
// errorMessage non-empty whenever a backend's setFailurePoint
// triggers on the engine main path. Library-side counterpart of
// PlanStan's tst_sync_error_recovery.cpp.
//
// See: docs/phase0/04o-phase-f0-test-gaps-design.md

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "calendarbaselinestore.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "synccoordinator.h"
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

QString eventToIcal(const KCalendarCore::Incidence::Ptr &inc)
{
    KCalendarCore::ICalFormat fmt;
    return fmt.toICalString(inc);
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

class TestCalendarSyncErrorRecovery : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}
    void init();
    void cleanup();

    // Backend write-failure paths (Phase E's wrapper-commit pattern)
    void targetStoreItemsFailure_propagatesAsSyncResultFailure();
    void targetUpdateItemFailure_propagatesAsSyncResultFailure();
    void targetDeleteFailure_propagatesAsSyncResultFailure();

    // Backend fetch-failure paths
    void sourceFetchFailure_propagatesAsSyncResultFailure();
    void targetFetchFailure_propagatesAsSyncResultFailure();

private:
    /// Run a sync to completion. Returns true on success, false on
    /// timeout. Captures the per-mapping SyncResult into m_lastResult
    /// (preferred over the aggregate, which can be ambiguous when
    /// only some of N mappings fail; here N=1 so they agree).
    bool runOneSync();

    QStringList sourceUids() const
    { return m_source->allUids(QString::fromLatin1(kCalendarId)); }
    QStringList targetUids() const
    { return m_target->allUids(QString::fromLatin1(kCalendarId)); }

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<CalendarBaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncCoordinator>       m_coordinator;

    SyncResult m_lastResult;
};

// ---- Lifecycle ------------------------------------------------------------

void TestCalendarSyncErrorRecovery::init()
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
    m_calendarBaselines = std::make_unique<CalendarBaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_coordinator = std::make_unique<SyncCoordinator>(m_registry.get(), m_host.get());
    m_coordinator->setCalendarBaselineStore(m_calendarBaselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings({ makeTwoWayMapping() });

    m_lastResult = SyncResult{};
}

void TestCalendarSyncErrorRecovery::cleanup()
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

bool TestCalendarSyncErrorRecovery::runOneSync()
{
    // Capture per-mapping SyncResult — fires once per mapping just
    // before allSyncsCompleted aggregates. With one mapping, the
    // per-mapping signal is the truth.
    QSignalSpy completedSpy(m_coordinator.get(),
                            &SyncCoordinator::syncCompleted);
    QSignalSpy allDoneSpy(m_coordinator.get(),
                          &SyncCoordinator::allSyncsCompleted);

    m_coordinator->runSync(SyncCoordinator::SyncBehavior::Unmonitored);
    if (!allDoneSpy.wait(kSyncTimeoutMs)) {
        qWarning() << "allSyncsCompleted signal did not fire within"
                   << kSyncTimeoutMs << "ms";
        return false;
    }

    if (completedSpy.isEmpty()) {
        qWarning() << "syncCompleted did not fire (engine never reached "
                      "per-mapping completion)";
        return false;
    }
    const auto args = completedSpy.takeLast();
    if (args.size() < 2) {
        qWarning() << "syncCompleted args size" << args.size()
                   << "(expected 2: mappingId, SyncResult)";
        return false;
    }
    m_lastResult = args.at(1).value<SyncResult>();
    return true;
}

// ---- Tests ---------------------------------------------------------------

void TestCalendarSyncErrorRecovery::targetStoreItemsFailure_propagatesAsSyncResultFailure()
{
    // Source has a new event; target is empty. The sync will try to
    // create the event on the target via storeItems / startSync.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Event One")));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems);

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success,
             "Expected SyncResult.success == false on storeItems failure");
    QVERIFY2(!m_lastResult.errorMessage.isEmpty(),
             "Expected non-empty errorMessage on storeItems failure");
}

void TestCalendarSyncErrorRecovery::targetUpdateItemFailure_propagatesAsSyncResultFailure()
{
    // Both sides have the event with same uid; target has the older
    // copy. With a baseline seeded, the engine takes the diff/merge
    // path rather than the first-sync quick path, and issues an
    // updateItem on target.
    auto eventA = makeEvent(QStringLiteral("evt-1"), QStringLiteral("New Summary"));
    auto eventB = makeEvent(QStringLiteral("evt-1"), QStringLiteral("Old Summary"));
    m_source->addIncidence(QString::fromLatin1(kCalendarId), eventA);
    m_target->addIncidence(QString::fromLatin1(kCalendarId), eventB);

    // Seed the baseline as the "agreed-upon prior state" (same content
    // as eventB so target is unmodified-relative-to-baseline; source is
    // modified). Real iCal text is required — the diff logic parses it.
    m_calendarBaselines->setBaseline(QString::fromLatin1(kMappingId),
                                     eventB->uid(),
                                     eventToIcal(eventB));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

void TestCalendarSyncErrorRecovery::targetDeleteFailure_propagatesAsSyncResultFailure()
{
    // Target has an event the source no longer has; sync will issue a
    // delete on target. Requires a baseline so the engine knows it's
    // a delete, not a "first-sync, mirror everything" case.
    auto stale = makeEvent(QStringLiteral("evt-stale"),
                           QStringLiteral("To Be Deleted"));
    m_target->addIncidence(QString::fromLatin1(kCalendarId), stale);

    m_calendarBaselines->setBaseline(QString::fromLatin1(kMappingId),
                                     stale->uid(),
                                     eventToIcal(stale));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnDelete);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

void TestCalendarSyncErrorRecovery::sourceFetchFailure_propagatesAsSyncResultFailure()
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Event One")));

    m_source->setFailurePoint(MockBackend::FailurePoint::OnFetch);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

void TestCalendarSyncErrorRecovery::targetFetchFailure_propagatesAsSyncResultFailure()
{
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"),
                                     QStringLiteral("Event One")));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnFetch);

    QVERIFY(runOneSync());
    QVERIFY(!m_lastResult.success);
    QVERIFY(!m_lastResult.errorMessage.isEmpty());
}

QTEST_GUILESS_MAIN(TestCalendarSyncErrorRecovery)
#include "tst_calendar_sync_error_recovery.moc"
