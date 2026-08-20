// Parallel-sync campaign — Task 8 (the endpoint-collision scheduler) and
// Task 9 (run-level signal semantics under concurrency).
//
// The load-bearing case is testMappingsSharingAnEndpointNeverOverlap: the
// per-collection FIFO in SyncBackendBase::enqueueOperation serialises
// OPERATIONS, not diff/apply CYCLES, so only the scheduler's endpoint
// claims stop two mappings diffing and applying against the same
// (backend, calendar) concurrently.
//
// Fixture setup (RegistrySyncHost, RevisionMockBackend, makeEvent,
// makeMapping) is copied verbatim in spirit from tst_engine_fixpoint_passes.cpp
// per the task brief — do not re-derive it independently.

#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QThread>

#include <KCalendarCore/Event>

#include <memory>
#include <vector>

#include "backendregistry.h"
#include "baselinestore.h"
#include "changedetection.h"
#include "isynchost.h"
#include "lossprofile.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncbackend.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::ConflictResolution;
using Kalburator::Sync::ISyncConfigStore;
using Kalburator::Sync::ISyncHost;
using Kalburator::Sync::MockBackend;
using Kalburator::Sync::SyncBackend;
using Kalburator::Engine::SyncEngine;
using Kalburator::Engine::SyncRequest;
using Kalburator::Sync::SyncMapping;
using Kalburator::Sync::SyncMode;
using Kalburator::Sync::SyncResult;
using Kalburator::Shape::LossProfile;

namespace {

constexpr int kSyncTimeoutMs = 30000;
constexpr auto kCalendarId = "cal";
constexpr auto kSharedCalendarId = "shared";
constexpr auto kHubCalendarId = "hub";

// Minimal ISyncHost over a BackendRegistry — duplicated per-binary, same
// shape as the other engine tests (see tst_engine_fixpoint_passes.cpp).
class RegistrySyncHost final : public ISyncHost
{
public:
    explicit RegistrySyncHost(BackendRegistry *registry) : m_registry(registry) {}

    SyncBackend *backendById(const QString &id) override
    {
        return m_registry ? static_cast<SyncBackend*>(m_registry->backendInstance(id))
                          : nullptr;
    }
    QHash<QString, SyncBackend *> backends() override
    {
        QHash<QString, SyncBackend *> out;
        if (!m_registry) return out;
        for (const auto &id : m_registry->registeredInstanceIds())
            out.insert(id, static_cast<SyncBackend*>(m_registry->backendInstance(id)));
        return out;
    }
    ISyncConfigStore *configStore() override { return nullptr; }

    void syncStarted(const QString &, const LossProfile &) override {}
    void recordChanged(const QString &, const QString &, ChangeKind) override {}

private:
    BackendRegistry *m_registry = nullptr;
};

// Content-digest revision MockBackend, same shape as
// tst_engine_fixpoint_passes.cpp's RevisionMockBackend.
class RevisionMockBackend : public MockBackend, public Kalburator::Sync::ChangeDetection
{
    Q_OBJECT
public:
    using MockBackend::MockBackend;

    QString collectionRevision(const QString &calendarId) override
    {
        QStringList parts;
        const auto uids = allUids(calendarId);
        for (const auto &uid : uids)
            parts << (uid + QLatin1Char(':') + incidenceHash(calendarId, uid));
        parts.sort();
        return QString::fromLatin1(QCryptographicHash::hash(
            parts.join(QLatin1Char(',')).toUtf8(), QCryptographicHash::Sha1).toHex());
    }
    QString cachedCollectionRevision(const QString &) const override { return QString(); }
};

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeMapping(const QString &id, const QString &src, const QString &tgt,
                        const QString &srcCal = QString::fromLatin1(kCalendarId),
                        const QString &tgtCal = QString::fromLatin1(kCalendarId))
{
    SyncMapping m;
    m.id             = id;
    m.sourceBackend  = src;
    m.sourceCalendar = srcCal;
    m.targetBackend  = tgt;
    m.targetCalendar = tgtCal;
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TestEngineParallelMappings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testDisjointMappingsRunConcurrently();
    void testMappingsSharingAnEndpointNeverOverlap();
    void testHubFanInSerialises();
    void testPerResourceCapIsHonoured();
    void testMonitoredForcesSequential();
    void testCancelWithThreeInFlightFinishesOnceAfterDrain();
    void testChainStillConvergesInOneRunAtCapFour();
    void testProgressIsMonotonicUnderConcurrency();
    void testCompletePhaseOnlyAfterPoolDrains();

private:
    // Registers a fresh RevisionMockBackend named @p id and returns it.
    RevisionMockBackend *makeBackend(const QString &id);

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;

    std::unique_ptr<QTemporaryDir> m_tmpDir;
    std::unique_ptr<BackendRegistry> m_registry;
    std::unique_ptr<QThread> m_backendThread;
    std::vector<std::unique_ptr<RevisionMockBackend>> m_backends;
    std::unique_ptr<RegistrySyncHost> m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncEngine> m_engine;
};

void TestEngineParallelMappings::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestEngineParallelMappings::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_backends.clear();
    m_host = std::make_unique<RegistrySyncHost>(m_registry.get());

    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(
        m_tmpDir->filePath(QStringLiteral("baselines.db")));

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setSkipUnchangedMappings(false);
    m_engine->setBaselineStore(m_baselines.get());

    // capForMapping() forces cap=1 for any backend still living on the
    // engine's own thread (Task 7's GUI-thread backstop — see its doc
    // comment). Every backend in this suite must be relocated onto its
    // own thread, exactly like PlanStan's Task 11 will do in production,
    // or none of these tests would exercise real concurrency at all.
    m_backendThread = std::make_unique<QThread>();
    m_backendThread->setObjectName(QStringLiteral("test-backend-io"));
    m_backendThread->start();
}

void TestEngineParallelMappings::cleanup()
{
    m_engine.reset();
    // Stop the backend thread's event loop before destroying the
    // backends living on it — see tst_backend_thread_relocation.cpp's
    // ioThreadGuard for the same ordering rationale.
    if (m_backendThread) {
        m_backendThread->quit();
        m_backendThread->wait();
        m_backendThread.reset();
    }
    m_baselines.reset();
    m_host.reset();
    m_backends.clear();
    m_registry.reset();
    m_tmpDir.reset();
}

RevisionMockBackend *TestEngineParallelMappings::makeBackend(const QString &id)
{
    auto backend = std::make_unique<RevisionMockBackend>(id);
    auto *ptr = backend.get();
    ptr->moveToThread(m_backendThread.get());
    m_registry->registerBackendInstance(id, ptr);
    m_backends.push_back(std::move(backend));
    return ptr;
}

void TestEngineParallelMappings::testDisjointMappingsRunConcurrently()
{
    // Two mappings sharing no ENDPOINT (different calendars) but the same
    // TARGET BACKEND. With a cap of 4 both must be in flight at once,
    // which the shared backend's high-water counter proves directly
    // rather than by timing.
    auto *a = makeBackend(QStringLiteral("A"));
    auto *b = makeBackend(QStringLiteral("B"));
    auto *remote = makeBackend(QStringLiteral("Remote"));
    a->setOperationDelay(100);
    b->setOperationDelay(100);
    remote->setOperationDelay(100);
    remote->resetConcurrencyStats();

    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"), QStringLiteral("A"), QStringLiteral("Remote"),
                   QString::fromLatin1(kCalendarId), QStringLiteral("cal1")),
        makeMapping(QStringLiteral("m2"), QStringLiteral("B"), QStringLiteral("Remote"),
                   QString::fromLatin1(kCalendarId), QStringLiteral("cal2")),
    });
    m_engine->setMaxConcurrentMappings(4);

    SyncRequest req;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    QCOMPARE(remote->maxConcurrentOps(), 2);
    QCOMPARE(future.resultAt(0).size(), 2);
    for (const auto &r : future.resultAt(0))
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
}

void TestEngineParallelMappings::testMappingsSharingAnEndpointNeverOverlap()
{
    // THE core correctness test. Two mappings whose TARGET is the same
    // (backend, calendar). The scheduler must never have both in flight,
    // because they would diff and apply against interleaved state and
    // race each other's baselines. The per-collection FIFO does NOT save
    // us here — it serialises operations, not diff/apply cycles.
    auto *a = makeBackend(QStringLiteral("A"));
    auto *b = makeBackend(QStringLiteral("B"));
    auto *shared = makeBackend(QStringLiteral("Shared"));
    a->setOperationDelay(50);
    b->setOperationDelay(50);
    shared->setOperationDelay(100);
    shared->resetConcurrencyStats();

    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"), QStringLiteral("A"), QStringLiteral("Shared")),
        makeMapping(QStringLiteral("m2"), QStringLiteral("B"), QStringLiteral("Shared")),
    });
    m_engine->setMaxConcurrentMappings(4);

    SyncRequest req;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    QCOMPARE(shared->maxConcurrentOps(), 1);
    QCOMPARE(future.resultAt(0).size(), 2);
}

void TestEngineParallelMappings::testHubFanInSerialises()
{
    // Five mappings all targeting one logical calendar. Every pair
    // collides on the shared target, so the whole set must run one at a
    // time even at a cap of 4.
    auto *hub = makeBackend(QStringLiteral("Hub"));
    hub->setOperationDelay(30);
    hub->resetConcurrencyStats();

    QList<SyncMapping> mappings;
    for (int i = 0; i < 5; ++i) {
        auto *spoke = makeBackend(QStringLiteral("Spoke%1").arg(i));
        spoke->setOperationDelay(30);
        mappings.append(makeMapping(QStringLiteral("m%1").arg(i),
                                    spoke->backendId(), QStringLiteral("Hub")));
    }
    m_engine->setSyncMappings(mappings);
    m_engine->setMaxConcurrentMappings(4);

    SyncRequest req;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    QCOMPARE(hub->maxConcurrentOps(), 1);
    QCOMPARE(future.resultAt(0).size(), 5);
}

void TestEngineParallelMappings::testPerResourceCapIsHonoured()
{
    // Six disjoint mappings, host asks for 6, but the remote backend
    // declares maxConcurrentOperations() == 4. The engine must cap at 4
    // for mappings using that resource.
    auto *remote = makeBackend(QStringLiteral("Remote"));
    remote->setDeclaredMaxConcurrentOps(4);
    remote->setOperationDelay(80);
    remote->resetConcurrencyStats();

    QList<SyncMapping> mappings;
    for (int i = 0; i < 6; ++i) {
        auto *src = makeBackend(QStringLiteral("Src%1").arg(i));
        src->setOperationDelay(80);
        mappings.append(makeMapping(QStringLiteral("m%1").arg(i),
                                    src->backendId(), QStringLiteral("Remote"),
                                    QString::fromLatin1(kCalendarId),
                                    QStringLiteral("cal%1").arg(i)));
    }
    m_engine->setSyncMappings(mappings);
    m_engine->setMaxConcurrentMappings(6);

    SyncRequest req;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    QVERIFY2(remote->maxConcurrentOps() <= 4,
             "the backend's declared cap must override the host's");
}

void TestEngineParallelMappings::testMonitoredForcesSequential()
{
    auto *remote = makeBackend(QStringLiteral("Remote"));
    remote->setOperationDelay(80);
    remote->resetConcurrencyStats();

    QList<SyncMapping> mappings;
    for (int i = 0; i < 4; ++i) {
        auto *src = makeBackend(QStringLiteral("Src%1").arg(i));
        src->setOperationDelay(80);
        mappings.append(makeMapping(QStringLiteral("m%1").arg(i),
                                    src->backendId(), QStringLiteral("Remote"),
                                    QString::fromLatin1(kCalendarId),
                                    QStringLiteral("cal%1").arg(i)));
    }
    m_engine->setSyncMappings(mappings);
    m_engine->setMaxConcurrentMappings(4);

    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Monitored;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    QCOMPARE(remote->maxConcurrentOps(), 1);
}

void TestEngineParallelMappings::testCancelWithThreeInFlightFinishesOnceAfterDrain()
{
    // The sharpest new bug risk in the design: the pre-pool cancel path
    // finished m_currentIface immediately, which under a pool would
    // finish the future while workers are still writing into m_queue.
    auto *remote = makeBackend(QStringLiteral("Remote"));
    remote->setOperationDelay(200);

    QList<SyncMapping> mappings;
    for (int i = 0; i < 6; ++i) {
        auto *src = makeBackend(QStringLiteral("Src%1").arg(i));
        src->setOperationDelay(200);
        mappings.append(makeMapping(QStringLiteral("m%1").arg(i),
                                    src->backendId(), QStringLiteral("Remote"),
                                    QString::fromLatin1(kCalendarId),
                                    QStringLiteral("cal%1").arg(i)));
    }
    m_engine->setSyncMappings(mappings);
    m_engine->setMaxConcurrentMappings(3);

    SyncRequest req;
    auto future = m_engine->runSync(req);

    QTest::qWait(100); // let some mappings get in flight
    future.cancel();

    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));
    QVERIFY(future.isCanceled());
    QCOMPARE(future.resultCount(), 1);
    QVERIFY(!m_engine->isSyncing());
}

void TestEngineParallelMappings::testChainStillConvergesInOneRunAtCapFour()
{
    // L2's fixpoint re-pass must still settle a two-hop chain within a
    // single user-visible Sync when mappings run concurrently. Parallel
    // counterpart of tst_engine_fixpoint_passes' testChainConvergesInOneRun.
    auto *a = makeBackend(QStringLiteral("A"));
    makeBackend(QStringLiteral("B"));
    auto *c = makeBackend(QStringLiteral("C"));

    // Hostile order: downstream mapping (B<->C) listed before the
    // upstream mapping (A<->B) that will dirty its source.
    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m2"), QStringLiteral("B"), QStringLiteral("C")),
        makeMapping(QStringLiteral("m1"), QStringLiteral("A"), QStringLiteral("B")),
    });
    m_engine->setMaxConcurrentMappings(4);

    // Prime: settle sync-progress tokens at zero cost.
    {
        SyncRequest req;
        auto f = m_engine->runSync(req);
        QVERIFY(QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs));
    }

    a->addIncidence(QString::fromLatin1(kCalendarId),
                    makeEvent(QStringLiteral("rec-new"), QStringLiteral("New Record")));

    SyncRequest req;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    QVERIFY2(c->allUids(QString::fromLatin1(kCalendarId)).contains(QStringLiteral("rec-new")),
             "L2 must still converge a hostile-order chain when mappings run concurrently");
}

void TestEngineParallelMappings::testProgressIsMonotonicUnderConcurrency()
{
    auto *remote = makeBackend(QStringLiteral("Remote"));
    remote->setOperationDelay(50);

    QList<SyncMapping> mappings;
    for (int i = 0; i < 6; ++i) {
        auto *src = makeBackend(QStringLiteral("Src%1").arg(i));
        src->setOperationDelay(50);
        mappings.append(makeMapping(QStringLiteral("m%1").arg(i),
                                    src->backendId(), QStringLiteral("Remote"),
                                    QString::fromLatin1(kCalendarId),
                                    QStringLiteral("cal%1").arg(i)));
    }
    m_engine->setSyncMappings(mappings);
    m_engine->setMaxConcurrentMappings(4);

    QList<int> seen;
    connect(m_engine.get(), &SyncEngine::progressUpdated,
            this, [&seen](int current, int, const QString &) { seen.append(current); });

    SyncRequest req;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    for (int i = 1; i < seen.size(); ++i) {
        QVERIFY2(seen.at(i) >= seen.at(i - 1),
                 "progressUpdated's current must never go backwards");
    }
}

void TestEngineParallelMappings::testCompletePhaseOnlyAfterPoolDrains()
{
    // WildPalms drives shouldPauseTickle() off this signal, so Complete
    // must not be announced while mappings are still running.
    auto *remote = makeBackend(QStringLiteral("Remote"));
    remote->setOperationDelay(100);

    QList<SyncMapping> mappings;
    for (int i = 0; i < 4; ++i) {
        auto *src = makeBackend(QStringLiteral("Src%1").arg(i));
        src->setOperationDelay(100);
        mappings.append(makeMapping(QStringLiteral("m%1").arg(i),
                                    src->backendId(), QStringLiteral("Remote"),
                                    QString::fromLatin1(kCalendarId),
                                    QStringLiteral("cal%1").arg(i)));
    }
    m_engine->setSyncMappings(mappings);
    m_engine->setMaxConcurrentMappings(4);

    bool sawCompleteWhileBusy = false;
    connect(m_engine.get(), &SyncEngine::phaseChanged,
            this, [&](SyncEngine::SyncPhase phase) {
        if (phase == SyncEngine::SyncPhase::Complete && m_engine->isSyncing())
            sawCompleteWhileBusy = true;
    });

    SyncRequest req;
    auto future = m_engine->runSync(req);
    QVERIFY(QTest::qWaitFor([&]{ return future.isFinished(); }, kSyncTimeoutMs));

    QVERIFY(!sawCompleteWhileBusy);
}

QTEST_MAIN(TestEngineParallelMappings)
#include "tst_engine_parallel_mappings.moc"
