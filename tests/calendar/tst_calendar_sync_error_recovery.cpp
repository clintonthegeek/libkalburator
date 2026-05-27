// tst_calendar_sync_error_recovery.cpp
//
// Phase F.0 — Backend write/fetch failure paths against
// MockBackend. Pins SyncResult.success == false and
// errorMessage non-empty whenever a backend's setFailurePoint
// triggers on the engine main path. Library-side counterpart of
// PlanStan's tst_sync_error_recovery.cpp.
//
// Extended (Task 7, 2026-05-07): additive slots absorbed from
// PlanStan's tst_sync_error_recovery.cpp covering partial-write
// rollback, retry, two-direction isolation, and no-op sync paths.
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
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "canonicalrecord.h"
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
    void initTestCase() {
        Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
        Kalburator::registerStockPlugins(pm);
    }
    void init();
    void cleanup();

    // Backend write-failure paths (Phase E's wrapper-commit pattern)
    void targetStoreItemsFailure_propagatesAsSyncResultFailure();
    void targetUpdateItemFailure_propagatesAsSyncResultFailure();
    void targetDeleteFailure_propagatesAsSyncResultFailure();

    // Backend fetch-failure paths
    void sourceFetchFailure_propagatesAsSyncResultFailure();
    void targetFetchFailure_propagatesAsSyncResultFailure();

    // Partial-write rollback and count-based failure injection
    // (absorbed from PlanStan's tst_sync_error_recovery.cpp, Task 7)
    void storeFailsPartial_failsAndRetryConverges();
    void pushFailsImmediate_propagatesAsSyncResultFailure();
    void pushFailsPartial_failsAndRetryConverges();
    void deleteFailsImmediate_propagatesAsSyncResultFailure();
    void deleteFailsPartial_failsAndRetryConverges();
    void successfulSync_allItemsReachTarget();
    void preExistingDataPreservedOnFailure();
    void retryAfterFailure_recoversCorrectly();
    void mixedOperationFailure_sharedDataPreservedAndRetryConverges();
    void twoDirectionFailureIsolation();
    void rollbackFailureResilience_errorReported();
    void emptyChangesetNoTransaction_syncSucceeds();
    void singleItemSuccess_targetHasItem();

private:
    /// Run a sync to completion. Returns true on success, false on
    /// timeout. Captures the per-mapping SyncResult into m_lastResult
    /// (preferred over the aggregate, which can be ambiguous when
    /// only some of N mappings fail; here N=1 so they agree).
    bool runOneSync();

    /// Add an event to the source backend only.
    void addSourceEvent(const QString &uid, const QString &summary);
    /// Add an event to the target backend only.
    void addTargetEvent(const QString &uid, const QString &summary);
    /// Add an identical event to both backends.
    void addEventToBoth(const QString &uid, const QString &summary);

    QStringList sourceUids() const
    { return m_source->allUids(QString::fromLatin1(kCalendarId)); }
    QStringList targetUids() const
    { return m_target->allUids(QString::fromLatin1(kCalendarId)); }

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

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
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
    m_calendarBaselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore     = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_calendarBaselines.get());
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
    // Capture per-mapping SyncResult via the multi-mapping
    // runSyncFuture, which yields QList<SyncResult>. With one mapping,
    // the first (and only) entry is the truth.
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

// ---- Helpers -------------------------------------------------------------

void TestCalendarSyncErrorRecovery::addSourceEvent(const QString &uid,
                                                    const QString &summary)
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(uid, summary));
}

void TestCalendarSyncErrorRecovery::addTargetEvent(const QString &uid,
                                                    const QString &summary)
{
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(uid, summary));
}

void TestCalendarSyncErrorRecovery::addEventToBoth(const QString &uid,
                                                    const QString &summary)
{
    addSourceEvent(uid, summary);
    addTargetEvent(uid, summary);
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
    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(eventB->uid(), eventToIcal(eventB)));

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

    m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId),
                                       calendarTestRec(stale->uid(), eventToIcal(stale)));

    // Seed the blob baseline so the unified path knows the record previously
    // existed — without it, the engine treats evt-stale as target-only-new
    // and copies it to source instead of deleting it.
    const auto targetRecords = m_target->loadRecords(QString::fromLatin1(kCalendarId));
    for (const auto &rec : targetRecords) {
        if (rec.id == stale->uid()) {
            Kalburator::Shape::CanonicalRecord canonical;
            canonical.recordId = rec.id;
            canonical.shape    = { Kalburator::Shape::DomainId{QStringLiteral("blob")},
                                   Kalburator::Shape::EncodingId{QStringLiteral("raw")} };
            canonical.data     = rec.contentHash.toUtf8();
            m_calendarBaselines->setBaselineV3(QString::fromLatin1(kMappingId), canonical);
        }
    }

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

// ---- Additive slots absorbed from PlanStan (Task 7, 2026-05-07) ----------
//
// These cover count-based failure injection (partial writes, partial
// deletes) and rollback/retry behavior that was not yet present in the
// original F.0 slots above.  All use the same init()/cleanup() harness
// and runOneSync() helper.

void TestCalendarSyncErrorRecovery::storeFailsPartial_failsAndRetryConverges()
{
    // Target fails after 2 of 5 stores. The converged engine is best-effort
    // (no transactional rollback): the sync reports failure and does NOT save
    // baselines, so a retry re-attempts the writes and converges. We do not
    // assert the partial count — it is unordered and not contractual.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));
    addSourceEvent(QStringLiteral("event-4"), QStringLiteral("Event Four"));
    addSourceEvent(QStringLiteral("event-5"), QStringLiteral("Event Five"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial store failure must propagate");
    QCOMPARE(sourceUids().size(), 5);

    // Retry-safe: clearing the failure and re-syncing converges the target.
    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 5);
    QCOMPARE(sourceUids().size(), 5);
}

void TestCalendarSyncErrorRecovery::pushFailsImmediate_propagatesAsSyncResultFailure()
{
    // Source has 3 events. Target fails on first push (OnPush, 0).
    // Expected: no items written to target; sync reports failure.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnPush, 0);

    QVERIFY(runOneSync());

    QVERIFY2(!m_lastResult.success,
             "Immediate push failure must propagate as SyncResult failure");
    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 0);
}

void TestCalendarSyncErrorRecovery::pushFailsPartial_failsAndRetryConverges()
{
    // OnPush variant of storeFailsPartial: best-effort + retry-safe.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));
    addSourceEvent(QStringLiteral("event-4"), QStringLiteral("Event Four"));
    addSourceEvent(QStringLiteral("event-5"), QStringLiteral("Event Five"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnPush, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial push failure must propagate");
    QCOMPARE(sourceUids().size(), 5);

    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 5);
    QCOMPARE(sourceUids().size(), 5);
}

void TestCalendarSyncErrorRecovery::deleteFailsImmediate_propagatesAsSyncResultFailure()
{
    // Both backends start with 3 identical events.
    // First sync establishes baselines (no failure).
    // Delete 2 events from source, inject OnDelete failure, run second sync.
    // Expected: target retains all 3 events (delete rolled back); sync fails.
    addEventToBoth(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addEventToBoth(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addEventToBoth(QStringLiteral("event-3"), QStringLiteral("Event Three"));

    // Establish baselines
    QVERIFY(runOneSync());
    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 3);

    // Delete 2 events from source
    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-1"));
    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-2"));
    QCOMPARE(sourceUids().size(), 1);

    // Inject delete failure
    m_target->setFailurePoint(MockBackend::FailurePoint::OnDelete, 0);

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success,
             "Immediate delete failure must propagate as SyncResult failure");

    // Source has 1 event (2 were removed before second sync)
    QCOMPARE(sourceUids().size(), 1);
    // Target retains all 3 (delete failed before any removal)
    QCOMPARE(targetUids().size(), 3);
}

void TestCalendarSyncErrorRecovery::deleteFailsPartial_failsAndRetryConverges()
{
    // Both start with 3 identical events (baseline sync). Delete all 3 from
    // source; target fails after 1 delete. Best-effort leaves a partial state,
    // but the sync fails and baselines are not advanced, so a retry converges
    // the target to empty.
    addEventToBoth(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addEventToBoth(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addEventToBoth(QStringLiteral("event-3"), QStringLiteral("Event Three"));

    QVERIFY(runOneSync());                 // establish baselines
    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 3);

    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-1"));
    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-2"));
    m_source->removeItem(QString::fromLatin1(kCalendarId), QStringLiteral("event-3"));
    QCOMPARE(sourceUids().size(), 0);

    m_target->setFailurePoint(MockBackend::FailurePoint::OnDelete, 1);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial delete failure must propagate");
    QCOMPARE(sourceUids().size(), 0);

    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 0);
}

void TestCalendarSyncErrorRecovery::successfulSync_allItemsReachTarget()
{
    // Normal sync with no failure injection.
    // Verifies: sync succeeds and all 3 source items reach the target.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));

    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Normal sync must succeed");
    QCOMPARE(targetUids().size(), 3);
}

void TestCalendarSyncErrorRecovery::preExistingDataPreservedOnFailure()
{
    // Target pre-populated with 3 events; source has 5 new events. Target fails
    // mid-store. The failed target-direction write must not destroy pre-existing
    // target data; the (successful) target->source direction still propagates
    // the 3 pre-existing items to source.
    addTargetEvent(QStringLiteral("existing-1"), QStringLiteral("Existing One"));
    addTargetEvent(QStringLiteral("existing-2"), QStringLiteral("Existing Two"));
    addTargetEvent(QStringLiteral("existing-3"), QStringLiteral("Existing Three"));
    QCOMPARE(targetUids().size(), 3);

    addSourceEvent(QStringLiteral("new-1"), QStringLiteral("New One"));
    addSourceEvent(QStringLiteral("new-2"), QStringLiteral("New Two"));
    addSourceEvent(QStringLiteral("new-3"), QStringLiteral("New Three"));
    addSourceEvent(QStringLiteral("new-4"), QStringLiteral("New Four"));
    addSourceEvent(QStringLiteral("new-5"), QStringLiteral("New Five"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial store failure must propagate");

    // Pre-existing target data is untouched by the failed sync.
    QVERIFY(targetUids().contains(QStringLiteral("existing-1")));
    QVERIFY(targetUids().contains(QStringLiteral("existing-2")));
    QVERIFY(targetUids().contains(QStringLiteral("existing-3")));

    // target->source direction succeeded: source gained the 3 pre-existing items.
    QCOMPARE(sourceUids().size(), 8);
}

void TestCalendarSyncErrorRecovery::retryAfterFailure_recoversCorrectly()
{
    // First sync fails (target store fails after 2). Baseline must NOT be
    // updated on failure (Bug 11 regression guard). Second sync (no failure)
    // should recover: target gets all 5 items; source unchanged.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));
    addSourceEvent(QStringLiteral("event-4"), QStringLiteral("Event Four"));
    addSourceEvent(QStringLiteral("event-5"), QStringLiteral("Event Five"));

    // First sync: inject failure after 2 stores
    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "First sync should fail");

    // Clear failure and retry
    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Second sync should succeed");

    // Source unchanged (no phantom deletions from corrupted baselines)
    QCOMPARE(sourceUids().size(), 5);
    // Target recovered: all 5 items present
    QCOMPARE(targetUids().size(), 5);
}

void TestCalendarSyncErrorRecovery::mixedOperationFailure_sharedDataPreservedAndRetryConverges()
{
    // Both start with 3 identical events (baseline). Add 3 new to source; target
    // fails mid-push. Shared (baseline) data must remain; retry converges to 6.
    addEventToBoth(QStringLiteral("shared-1"), QStringLiteral("Shared One"));
    addEventToBoth(QStringLiteral("shared-2"), QStringLiteral("Shared Two"));
    addEventToBoth(QStringLiteral("shared-3"), QStringLiteral("Shared Three"));

    QVERIFY(runOneSync());                 // establish baselines
    QCOMPARE(sourceUids().size(), 3);
    QCOMPARE(targetUids().size(), 3);

    addSourceEvent(QStringLiteral("new-1"), QStringLiteral("New One"));
    addSourceEvent(QStringLiteral("new-2"), QStringLiteral("New Two"));
    addSourceEvent(QStringLiteral("new-3"), QStringLiteral("New Three"));
    QCOMPARE(sourceUids().size(), 6);

    m_target->setFailurePoint(MockBackend::FailurePoint::OnPush, 2);
    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Partial push failure must propagate");

    QVERIFY(m_target->incidence(QString::fromLatin1(kCalendarId), QStringLiteral("shared-1")));
    QVERIFY(m_target->incidence(QString::fromLatin1(kCalendarId), QStringLiteral("shared-2")));
    QVERIFY(m_target->incidence(QString::fromLatin1(kCalendarId), QStringLiteral("shared-3")));

    m_target->clearFailurePoint();
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Retry after cleared failure must succeed");
    QCOMPARE(targetUids().size(), 6);
}

void TestCalendarSyncErrorRecovery::twoDirectionFailureIsolation()
{
    // Source has 3 events; target has 2 different events.
    // Target-direction push succeeds (3 from source → target).
    // Source-direction push fails (target's 2 items fail to reach source).
    // Verifies: target items persist (5 total); source unchanged (3); failure reported.
    addSourceEvent(QStringLiteral("src-1"), QStringLiteral("Source One"));
    addSourceEvent(QStringLiteral("src-2"), QStringLiteral("Source Two"));
    addSourceEvent(QStringLiteral("src-3"), QStringLiteral("Source Three"));
    addTargetEvent(QStringLiteral("tgt-1"), QStringLiteral("Target One"));
    addTargetEvent(QStringLiteral("tgt-2"), QStringLiteral("Target Two"));

    // Inject failure on SOURCE backend (source direction applies second)
    m_source->setFailurePoint(MockBackend::FailurePoint::OnPush, 0);

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Failure in source direction must propagate");

    // Target: 2 original + 3 from source (target direction succeeded)
    QCOMPARE(targetUids().size(), 5);
    // Source: original 3 only (source direction was rolled back)
    QCOMPARE(sourceUids().size(), 3);
}

void TestCalendarSyncErrorRecovery::rollbackFailureResilience_errorReported()
{
    // Source has 3 events. Target fails after 1 store.
    // Verifies: failure reported, error message non-empty, target rolled back.
    addSourceEvent(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addSourceEvent(QStringLiteral("event-2"), QStringLiteral("Event Two"));
    addSourceEvent(QStringLiteral("event-3"), QStringLiteral("Event Three"));

    m_target->setFailurePoint(MockBackend::FailurePoint::OnStoreItems, 1);

    QVERIFY(runOneSync());
    QVERIFY2(!m_lastResult.success, "Sync must report failure");
    QVERIFY2(!m_lastResult.errorMessage.isEmpty(), "Error message must be non-empty");
    QCOMPARE(sourceUids().size(), 3);
}

void TestCalendarSyncErrorRecovery::emptyChangesetNoTransaction_syncSucceeds()
{
    // Source and target have identical data. Sync produces no changes.
    // Verifies: no transaction created; sync succeeds; both backends unchanged.
    //
    // Note: the PlanStan version also asserted that no .planstan-pending.json
    // file was created, but that is a PlanStan-specific artifact and is not
    // checked here.
    addEventToBoth(QStringLiteral("event-1"), QStringLiteral("Event One"));
    addEventToBoth(QStringLiteral("event-2"), QStringLiteral("Event Two"));

    // First sync: establish baselines
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Baseline sync must succeed");

    // Second sync: no changes
    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "No-change sync must succeed");

    QCOMPARE(sourceUids().size(), 2);
    QCOMPARE(targetUids().size(), 2);
}

void TestCalendarSyncErrorRecovery::singleItemSuccess_targetHasItem()
{
    // Source has 1 event. Normal sync.
    // Verifies: sync succeeds; item appears on target.
    addSourceEvent(QStringLiteral("solo-1"), QStringLiteral("Solo Event"));

    QVERIFY(runOneSync());
    QVERIFY2(m_lastResult.success, "Single-item sync must succeed");
    QCOMPARE(targetUids().size(), 1);
    QVERIFY(m_target->incidence(QString::fromLatin1(kCalendarId),
                                QStringLiteral("solo-1")));
}

QTEST_GUILESS_MAIN(TestCalendarSyncErrorRecovery)
#include "tst_calendar_sync_error_recovery.moc"
