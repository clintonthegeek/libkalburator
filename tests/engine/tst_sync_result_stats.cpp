// tst_sync_result_stats.cpp
//
// Sync-excellence campaign E1.1 (FINDINGS O30) — pins that
// SyncResult::sourceStats/targetStats are actually populated by the
// unified steady-state write path (SyncEngineWorker::
// unifiedContinueAfterConflicts's applyBatch helper), and that the
// single-mapping cancel decision (SyncEngine::onWorkerSyncCompleted)
// correctly distinguishes "cancelled after partial work" (skipped=false)
// from "cancelled before anything started" (skipped=true) using those
// stats. Before E1.1, sourceStats/targetStats were never written in the
// unified path, so statsOk (advanceQueue) was vacuously true and every
// cancelled run was misreported skipped=true regardless of partial work.
//
// Domain-neutral per campaign §0's universality rule: MockBackend pairs,
// no CalDAV.

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
constexpr auto kMappingId       = "mapping-stats";

constexpr int kSyncTimeoutMs = 30000;

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    event->setLastModified(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeMapping(SyncMode mode)
{
    SyncMapping m;
    m.id              = QString::fromLatin1(kMappingId);
    m.sourceBackend   = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar  = QString::fromLatin1(kCalendarId);
    m.targetBackend   = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar  = QString::fromLatin1(kCalendarId);
    m.mode            = mode;
    m.conflictPolicy  = ConflictResolution::SourceWins;
    m.enabled         = true;
    return m;
}

// Cancels the run the moment the FIRST record is materialized on the
// target side, then lets the write (and any subsequent side's write)
// proceed normally — simulating "cancel observed after partial work",
// as distinct from a cancel that lands before any apply (blocked fetch,
// see tst_engine_single_mapping_cancel).
class CancelOnFirstWriteBackend : public MockBackend
{
public:
    explicit CancelOnFirstWriteBackend(const QString &id) : MockBackend(id) {}

    void armCancelOnFirstWrite(QFuture<QList<SyncResult>> *future)
    {
        m_future = future;
    }

    QString createRecord(const QString &collectionId, const BackendRecord &record) override
    {
        if (m_future && !m_cancelled) {
            m_cancelled = true;
            m_future->cancel();
            // Spin the test-thread event loop so QFutureWatcher::canceled
            // -> SyncEngine::onCancelObserved marshals m_cancelled=true
            // before this write (and any later one) completes. Reentrant:
            // we are already executing inside the outer QTest::qWait's
            // event processing (this call arrived via a
            // BlockingQueuedConnection onto the backend's — test —
            // thread), and Qt event loops are reentrant.
            QTest::qWait(50);
        }
        return MockBackend::createRecord(collectionId, record);
    }

private:
    QFuture<QList<SyncResult>> *m_future = nullptr;
    bool m_cancelled = false;
};

} // namespace

class TestSyncResultStats : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // (a) A two-item create sync reports targetStats.created == 2 and
    // leaves sourceStats untouched (zero).
    void twoItemCreate_populatesTargetStats();

    // (b) A sync cancelled AFTER at least one apply landed reports
    // skipped == false (today: skipped == true, because stats are zero
    // regardless of the partial work actually done).
    void cancelledAfterPartialApply_reportsNotSkipped();

    // (c) A sync cancelled before any apply (blocked fetch, mirrors
    // tst_engine_single_mapping_cancel's pattern) keeps skipped == true.
    void cancelledBeforeAnyApply_reportsSkipped();

private:
    void setupCoordinator(const QList<SyncMapping> &mappings);

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<CancelOnFirstWriteBackend> m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_coordinator;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestSyncResultStats::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestSyncResultStats::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>(QString::fromLatin1(kSourceBackendId));
    m_target   = std::make_unique<CancelOnFirstWriteBackend>(QString::fromLatin1(kTargetBackendId));
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId), m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId), m_target.get());

    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId), hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_baselines     = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
}

void TestSyncResultStats::cleanup()
{
    m_coordinator.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_baselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TestSyncResultStats::setupCoordinator(const QList<SyncMapping> &mappings)
{
    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_baselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings(mappings);
}

void TestSyncResultStats::twoItemCreate_populatesTargetStats()
{
    // Target starts non-empty (a pre-existing, non-colliding record) so the
    // sync defers to the steady-state unified path (unifiedContinueAfter-
    // Conflicts / applyBatch) rather than dispatchFirstSync's inline
    // mirror — E1.1 only wires stats in the unified path.
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("tgt-keep"), QStringLiteral("Kept")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-1"), QStringLiteral("Alpha")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-2"), QStringLiteral("Beta")));

    setupCoordinator({ makeMapping(SyncMode::OneWayUpload) });

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    QFuture<QList<SyncResult>> future = m_coordinator->runSync(req);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(!future.isCanceled());

    const SyncResult result = future.resultAt(0).first();
    QVERIFY(result.success);
    QCOMPARE(result.targetStats.created, 2);
    QCOMPARE(result.targetStats.updated, 0);
    QCOMPARE(result.targetStats.deleted, 0);
    QCOMPARE(result.targetStats.errors, 0);
    // OneWayUpload never writes back to source.
    QCOMPARE(result.sourceStats.created, 0);
    QCOMPARE(result.sourceStats.updated, 0);
    QCOMPARE(result.sourceStats.deleted, 0);
    QCOMPARE(result.sourceStats.errors, 0);
}

void TestSyncResultStats::cancelledAfterPartialApply_reportsNotSkipped()
{
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("tgt-keep"), QStringLiteral("Kept")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-1"), QStringLiteral("Alpha")));

    setupCoordinator({ makeMapping(SyncMode::OneWayUpload) });

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    QFuture<QList<SyncResult>> future = m_coordinator->runSync(req);
    m_target->armCancelOnFirstWrite(&future);

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(future.isCanceled());

    const SyncResult result = future.resultAt(0).first();
    QVERIFY(result.cancelled);
    QVERIFY(result.targetStats.hasChanges());
    // Partial work was actually done — must NOT be reported as a never-
    // started skip.
    QCOMPARE(result.skipped, false);
}

void TestSyncResultStats::cancelledBeforeAnyApply_reportsSkipped()
{
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-1"), QStringLiteral("Alpha")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-2"), QStringLiteral("Beta")));
    m_source->setFetchBlocking(true);

    setupCoordinator({ makeMapping(SyncMode::OneWayUpload) });

    SyncRequest req;
    req.mappingIds = { QString::fromLatin1(kMappingId) };
    QFuture<QList<SyncResult>> future = m_coordinator->runSync(req);

    future.cancel();
    QTest::qWait(50);
    m_source->releaseFetchBlocker();

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(future.isCanceled());

    const SyncResult result = future.resultAt(0).first();
    QVERIFY(result.cancelled);
    QVERIFY(!result.targetStats.hasChanges());
    QVERIFY(!result.sourceStats.hasChanges());
    QCOMPARE(result.skipped, true);
}

QTEST_MAIN(TestSyncResultStats)
#include "tst_sync_result_stats.moc"
