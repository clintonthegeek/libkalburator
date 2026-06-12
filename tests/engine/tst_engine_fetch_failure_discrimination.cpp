// tst_engine_fetch_failure_discrimination.cpp
//
// Fix B (2026-06-12 Akonadi scoped-backend handoff): the engine must
// distinguish a *genuine* fetchItems failure from a backend that doesn't
// implement fetchItems at all.
//
// Background: SyncEngineWorker uses fetchItems() as a cancellable gating
// step and only awaits the op when it is Running. A backend that overrides
// fetchItems and fails fast (state Failed at return) — e.g. the scoped
// AkonadiBackend whose collection never resolved — was skipped exactly like
// a backend that returns the base-class "not implemented" default. The
// worker then read an empty/stale cache via loadRecordsOrError (whose default
// reports no error) and declared the mapping a SUCCESS with 0 records. Under
// clobber the target was already wiped, so a failed source fetch silently
// destroyed the target and still "succeeded".
//
// These tests pin the discrimination:
//   1. genuineFetchFailure_failsMappingNotSilentSuccess — an overriding
//      backend whose fetch genuinely fails (but whose read path is silent)
//      must FAIL the mapping, not succeed with 0 records.
//   3. clobberWithGenuineSourceFetchFailure_failsAndDoesNotWipeTarget —
//      a clobber whose source fetch genuinely fails must fail the mapping
//      and must NOT wipe the target (the wipe sits after the source fetch).
//
// (Test 2 — base-class "not implemented" default must still sync via
//  loadRecords — lives alongside once the NotSupported discriminator exists.)

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "syncconflictstore.h"
#include "synctypes.h"

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

class TstEngineFetchFailureDiscrimination : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void init();
    void cleanup();

    void genuineFetchFailure_failsMappingNotSilentSuccess();
    void notImplementedFetch_stillSyncsViaLoadRecords();
    void clobberWithGenuineSourceFetchFailure_failsAndDoesNotWipeTarget();

private:
    bool runRequest(const SyncRequest &request);
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

void TstEngineFetchFailureDiscrimination::init()
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

void TstEngineFetchFailureDiscrimination::cleanup()
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

bool TstEngineFetchFailureDiscrimination::runRequest(const SyncRequest &request)
{
    auto future = m_coordinator->runSync(request);
    if (!QTest::qWaitFor([&] { return future.isFinished(); }, kSyncTimeoutMs)) {
        qWarning() << "runSync did not finish within" << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSync was canceled unexpectedly";
        return false;
    }
    const auto results = future.resultAt(0);
    if (results.isEmpty()) {
        qWarning() << "runSync produced no per-mapping result";
        return false;
    }
    m_lastResult = results.last();
    return true;
}

bool TstEngineFetchFailureDiscrimination::runOneSync()
{
    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    return runRequest(req);
}

// ---------------------------------------------------------------------------

void TstEngineFetchFailureDiscrimination::genuineFetchFailure_failsMappingNotSilentSuccess()
{
    // Source's fetchItems fails fast (Failed at return), but its read path is
    // silent (loadRecordsOrError reports no error, returns 0 records) — exactly
    // the scoped AkonadiBackend's shape. With the buggy engine the mapping
    // "succeeds" with 0 records. It must instead FAIL.
    m_source->setFetchOpFailsSilently(
        true, QStringLiteral("simulated genuine source fetch failure"));

    // TwoWay routes through the unified dispatch path (the fetch-gated path) —
    // this is the mode WildPalms' real Akonadi routes use ("mode: TwoWay" in
    // the HotSync log). OneWayUpload first-syncs take a separate fast path
    // (dispatchFirstSync) that this gate does not cover; see FINDINGS.
    SyncMapping mapping = makeOneWayUploadMapping();
    mapping.mode = SyncMode::TwoWay;
    m_coordinator->setSyncMappings({mapping});

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success,
             "engine reported success despite a genuine fetchItems failure");
    QVERIFY2(!m_lastResult.errorMessage.isEmpty(),
             "errorMessage was empty after a genuine fetchItems failure");
}

void TstEngineFetchFailureDiscrimination::notImplementedFetch_stillSyncsViaLoadRecords()
{
    // The loadRecords-only contract: a backend that does NOT implement
    // fetchItems (returns the NotSupported "not implemented" default — the
    // shape of Sinks::FilteredCollectionBackend and every WP palm↔hub leg)
    // must keep syncing via loadRecords. The fetch gate must NOT mistake
    // NotSupported for a genuine failure. Source holds 3 events and uses the
    // base-default fetchItems; the records must reach the target.
    for (int i = 1; i <= 3; ++i) {
        auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        e->setUid(QStringLiteral("uid-%1").arg(i));
        e->setSummary(QStringLiteral("Event %1").arg(i));
        e->setDtStart(QDateTime::currentDateTimeUtc());
        m_source->addIncidence(QString::fromLatin1(kCalendarId), e);
    }
    m_source->setUseBaseFetchItems(true);

    // TwoWay routes through the unified (fetch-gated) path, so this exercises
    // the gate's NotSupported handling rather than the first-sync fast path.
    SyncMapping mapping = makeOneWayUploadMapping();
    mapping.mode = SyncMode::TwoWay;
    m_coordinator->setSyncMappings({mapping});

    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success,
             "a not-implemented fetch must not fail the mapping");
    QCOMPARE(m_target->allUids(QString::fromLatin1(kCalendarId)).size(), 3);
}

void TstEngineFetchFailureDiscrimination::clobberWithGenuineSourceFetchFailure_failsAndDoesNotWipeTarget()
{
    // Target holds 3 real events. Source's fetchItems fails fast (silent read).
    // A clobber wipes the target only AFTER the source fetch — so a genuine
    // source-fetch failure must fail the mapping BEFORE the wipe, leaving the
    // target intact.
    for (int i = 1; i <= 3; ++i) {
        auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        e->setUid(QStringLiteral("uid-%1").arg(i));
        e->setSummary(QStringLiteral("Event %1").arg(i));
        e->setDtStart(QDateTime::currentDateTimeUtc());
        m_target->addIncidence(QString::fromLatin1(kCalendarId), e);
    }

    m_source->setFetchOpFailsSilently(
        true, QStringLiteral("simulated genuine source fetch failure (clobber)"));

    SyncMapping mapping = makeOneWayUploadMapping();
    m_coordinator->setSyncMappings({mapping});

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    ExecutionOverride ov;
    ov.clobber = true;
    req.executionOverride = ov;

    QVERIFY(runRequest(req));
    QVERIFY2(!m_lastResult.success,
             "clobber reported success despite a genuine source fetch failure");

    // The target must NOT have been wiped.
    m_source->setFetchOpFailsSilently(false);
    QCOMPARE(m_target->allUids(QString::fromLatin1(kCalendarId)).size(), 3);
}

QTEST_GUILESS_MAIN(TstEngineFetchFailureDiscrimination)
#include "tst_engine_fetch_failure_discrimination.moc"
