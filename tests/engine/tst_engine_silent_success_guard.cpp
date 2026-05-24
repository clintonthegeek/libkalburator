// tst_engine_silent_success_guard.cpp
//
// Layer B — silent-success guard (failing test).
//
// The bug: five SyncEngineWorker call sites call
// IBlobBackend::loadRecords() instead of loadRecordsOrError().
// When a backend's read fails, loadRecords() returns {} silently
// and the engine mistakes the empty result for "the source/target
// is genuinely empty." This causes it to route through
// dispatchFirstSync's blob mirror, which can wipe real target
// records, or to harvest poisoned baselines.
//
// These three sub-tests demonstrate the bug:
//   1. targetReadFailure_doesNotMirrorEmptySource   — target has 3
//      real events; an OnFetch failure makes the engine think target
//      is empty and takes the blob-mirror fast path. Engine must
//      report failure, not success.
//   2. sourceReadFailure_doesNotPushDeletesToTarget — second sync
//      with source OnFetch failure: engine sees "0 source records"
//      and would push 5 spurious deletes to target. Must report
//      failure; target records must survive.
//   3. baselineHarvest_skippedOnFailedRead          — first-sync
//      source OnFetch failure causes harvestBaselinesAfterFirstSync
//      to store 0 baselines and still report success. Must report
//      failure; no baselines must be written.
//
// Currently all three FAIL because the engine's five bare
// loadRecords() call sites treat the silent {} as "backend is
// empty." Subsequent commits migrate them to loadRecordsOrError().

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncconflictstore.h"
#include "synctypes.h"
#include "transformationregistry.h"

#include "stubsynchost.h"

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
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::OneWayUpload;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TstEngineSilentSuccessGuard : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        // Populate the injected bundle once; per-slot init() builds a
        // SyncEngine reading from this same m_shape.
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void init();
    void cleanup();

    // Sub-test 1: OnFetch on TARGET — engine must not mirror empty source
    void targetReadFailure_doesNotMirrorEmptySource();

    // Sub-test 2: OnFetch on SOURCE during incremental sync — must not push deletions
    void sourceReadFailure_doesNotPushDeletesToTarget();

    // Sub-test 3: OnFetch on SOURCE during first sync — must not harvest baselines
    void baselineHarvest_skippedOnFailedRead();

private:
    bool runOneSync();

    Kalburator::Shape::ShapeRegistries     m_shape;
    Kalburator::Sync::BackendRegistry      m_pmRegistry;
    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calendarBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_coordinator;

    SyncResult m_lastResult;
};

// ---- Lifecycle ------------------------------------------------------------

void TstEngineSilentSuccessGuard::init()
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

    m_lastResult = SyncResult{};
}

void TstEngineSilentSuccessGuard::cleanup()
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

bool TstEngineSilentSuccessGuard::runOneSync()
{
    auto future = m_coordinator->runSyncFuture(
        SyncEngine::SyncBehavior::Unmonitored);
    if (!QTest::qWaitFor([&] { return future.isFinished(); }, kSyncTimeoutMs)) {
        qWarning() << "runSyncFuture did not finish within"
                   << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSyncFuture was canceled unexpectedly";
        return false;
    }
    const auto results = future.resultAt(0);
    if (results.isEmpty()) {
        qWarning() << "runSyncFuture produced no per-mapping result "
                      "(engine never reached per-mapping completion)";
        return false;
    }
    m_lastResult = results.last();
    return true;
}

// ---- Tests ---------------------------------------------------------------

void TstEngineSilentSuccessGuard::targetReadFailure_doesNotMirrorEmptySource()
{
    // Target has 3 real events. Source is empty. No baseline.
    // Inject OnFetch failure on TARGET.
    //
    // With the BUGGY engine:
    //   targetEmpty check calls loadRecords(target) -> OnFetch -> {} ->
    //   targetEmpty = true -> routes to blob mirror -> mirror reads source
    //   (0 events) and target (OnFetch -> {}) -> mirror does nothing ->
    //   engine reports SUCCESS (bug!).
    //
    // After the fix:
    //   loadRecordsOrError(target) -> returns false with error ->
    //   engine reports failure. PASS.
    for (int i = 1; i <= 3; ++i) {
        auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        e->setUid(QStringLiteral("uid-%1").arg(i));
        e->setSummary(QStringLiteral("Event %1").arg(i));
        e->setDtStart(QDateTime::currentDateTimeUtc());
        m_target->addIncidence(QString::fromLatin1(kCalendarId), e);
    }

    m_target->setFailurePoint(MockBackend::FailurePoint::OnFetch, 0,
                              QStringLiteral("simulated target read failure"));

    SyncMapping mapping = makeOneWayUploadMapping();
    m_coordinator->setSyncMappings({mapping});

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success,
             "engine reported success despite target read failure");
    QVERIFY2(!m_lastResult.errorMessage.isEmpty(), "errorMessage was empty");

    m_target->clearFailurePoint();
    QCOMPARE(m_target->allUids(QString::fromLatin1(kCalendarId)).size(), 3);
}

void TstEngineSilentSuccessGuard::sourceReadFailure_doesNotPushDeletesToTarget()
{
    // Seed the same 5 events on both source and target.
    // First sync: succeeds, seeds the baseline.
    // Then inject OnFetch on SOURCE. Run a second sync.
    //
    // With the BUGGY engine (second sync):
    //   loadRecords(source) -> OnFetch -> {} -> engine sees "0 source records"
    //   Baseline says "5 records were synced before"
    //   OneWayUpload diff: 5 records deleted from source -> push 5 deletes to
    //   target -> result.success = true (bug!).
    //
    // After the fix:
    //   loadRecordsOrError(source) -> returns false with error ->
    //   engine reports failure; target records survive. PASS.
    for (int i = 1; i <= 5; ++i) {
        const auto uid = QStringLiteral("uid-%1").arg(i);
        const auto summary = QStringLiteral("Event %1").arg(i);
        const auto dtStart = QDateTime::currentDateTimeUtc();

        auto eSrc = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        eSrc->setUid(uid);
        eSrc->setSummary(summary);
        eSrc->setDtStart(dtStart);
        m_source->addIncidence(QString::fromLatin1(kCalendarId), eSrc);

        auto eTgt = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        eTgt->setUid(uid);
        eTgt->setSummary(summary);
        eTgt->setDtStart(dtStart);
        m_target->addIncidence(QString::fromLatin1(kCalendarId), eTgt);
    }

    SyncMapping mapping = makeOneWayUploadMapping();
    m_coordinator->setSyncMappings({mapping});

    // First sync: succeeds, seeds the baseline
    QVERIFY(runOneSync());
    QVERIFY(m_lastResult.success);

    // Now inject failure on source
    m_source->setFailurePoint(MockBackend::FailurePoint::OnFetch, 0,
                              QStringLiteral("simulated source read failure"));

    // Second sync: source read fails
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success,
             "engine reported success despite source read failure");

    // Target records must be untouched
    m_source->clearFailurePoint();
    QCOMPARE(m_target->allUids(QString::fromLatin1(kCalendarId)).size(), 5);
}

void TstEngineSilentSuccessGuard::baselineHarvest_skippedOnFailedRead()
{
    // Target is empty. Source has 4 events.
    // Inject OnFetch on SOURCE. Run sync.
    //
    // With the BUGGY engine:
    //   targetEmpty check: target genuinely empty -> targetEmpty = true
    //   Mirror reads source (OnFetch -> {}) and target (empty)
    //   Mirror does nothing (nothing to copy)
    //   harvestBaselinesAfterFirstSync reads source again (OnFetch -> {})
    //   -> stores 0 baselines -> result.success = true (bug!).
    //
    // After the fix:
    //   loadRecordsOrError(source) returns false with error ->
    //   engine reports failure; no baselines written. PASS.
    for (int i = 1; i <= 4; ++i) {
        auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        e->setUid(QStringLiteral("uid-%1").arg(i));
        e->setSummary(QStringLiteral("Event %1").arg(i));
        e->setDtStart(QDateTime::currentDateTimeUtc());
        m_source->addIncidence(QString::fromLatin1(kCalendarId), e);
    }

    m_source->setFailurePoint(MockBackend::FailurePoint::OnFetch, 0,
                              QStringLiteral("simulated first-sync source read failure"));

    SyncMapping mapping = makeOneWayUploadMapping();
    m_coordinator->setSyncMappings({mapping});

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success,
             "engine reported success despite source read failure");

    // No baselines should have been harvested
    QVERIFY2(m_calendarBaselines->baselinesForMappingV3(
                 QString::fromLatin1(kMappingId)).isEmpty(),
             "baselines were harvested despite failed source read");
}

QTEST_GUILESS_MAIN(TstEngineSilentSuccessGuard)
#include "tst_engine_silent_success_guard.moc"
