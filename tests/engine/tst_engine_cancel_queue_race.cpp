// Sync-excellence campaign E3 (FINDINGS O33a, promoted from audit §C4).
//
// SyncEngineWorker::processSync() used to unconditionally clear
// m_cancelled at the top of every mapping dispatch. A cancel that landed
// after a mapping was already queued (posted to the worker thread) but
// before that mapping's processSync began was silently erased, and the
// queue ran one full extra mapping despite having been cancelled.
//
// This test pins the observable contract the fix restores: once
// SyncEngine::runSync()'s future is canceled, no mapping dispatched
// after that point may write anything to its destination — the mapping
// that comes AFTER the one already in flight when cancel() is called
// must never run to completion.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include "backendregistry.h"
#include "baselinestore.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncrequest.h"
#include "synctypes.h"

#include "stubsynchost.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/MemoryCalendar>

#include <memory>

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr int  kSyncTimeoutMs   = 30000;
constexpr auto kSourceBackend1  = "source-mock-1";
constexpr auto kTargetBackend1  = "target-mock-1";
constexpr auto kSourceBackend2  = "source-mock-2";
constexpr auto kTargetBackend2  = "target-mock-2";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId1     = "cal1";
constexpr auto kCalendarId2     = "cal2";
constexpr auto kMapping1        = "m1";
constexpr auto kMapping2        = "m2";

KCalendarCore::Event::Ptr makeEvent(const QString &uid, const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
    event->setUid(uid);
    event->setSummary(summary);
    event->setDtStart(QDateTime::currentDateTimeUtc());
    return event;
}

SyncMapping makeMapping(const QString &id, const QString &src, const QString &tgt,
                        const QString &calId)
{
    SyncMapping m;
    m.id             = id;
    m.sourceBackend  = src;
    m.sourceCalendar = calId;
    m.targetBackend  = tgt;
    m.targetCalendar = calId;
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::LastWriteWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TstEngineCancelQueueRace : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    /// A cancel observed while mapping 1 is still fetching must stop the
    /// queue before mapping 2 ever writes anything — even once mapping
    /// 1's blocked fetch is released and it runs to completion.
    void cancelDuringFirstMapping_secondMappingNeverWrites();

private:
    std::unique_ptr<QTemporaryDir>                      m_tmpDir;
    std::unique_ptr<BackendRegistry>                    m_registry;
    std::unique_ptr<MockBackend>                        m_src1;
    std::unique_ptr<MockBackend>                        m_dst1;
    std::unique_ptr<MockBackend>                        m_src2;
    std::unique_ptr<MockBackend>                        m_dst2;
    std::unique_ptr<StubSyncHost>                       m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncEngine>                         m_engine;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstEngineCancelQueueRace::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstEngineCancelQueueRace::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_src1 = std::make_unique<MockBackend>();
    m_dst1 = std::make_unique<MockBackend>();
    m_src2 = std::make_unique<MockBackend>();
    m_dst2 = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackend1), m_src1.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackend1), m_dst1.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackend2), m_src2.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackend2), m_dst2.get());

    m_host = std::make_unique<StubSyncHost>(m_registry.get());

    m_src1->createCalendar(QString::fromLatin1(kCollectionId), QString::fromLatin1(kCalendarId1),
                           QStringLiteral("Calendar 1"));
    m_dst1->createCalendar(QString::fromLatin1(kCollectionId), QString::fromLatin1(kCalendarId1),
                           QStringLiteral("Calendar 1"));
    m_src2->createCalendar(QString::fromLatin1(kCollectionId), QString::fromLatin1(kCalendarId2),
                           QStringLiteral("Calendar 2"));
    m_dst2->createCalendar(QString::fromLatin1(kCollectionId), QString::fromLatin1(kCalendarId2),
                           QStringLiteral("Calendar 2"));

    auto *hostCal1 = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal1->setId(QString::fromLatin1(kCalendarId1));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId1), hostCal1);
    auto *hostCal2 = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal2->setId(QString::fromLatin1(kCalendarId2));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId2), hostCal2);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setCollection(m_host->stubCollection());
    m_engine->setSyncMappings({
        makeMapping(QString::fromLatin1(kMapping1), QString::fromLatin1(kSourceBackend1),
                   QString::fromLatin1(kTargetBackend1), QString::fromLatin1(kCalendarId1)),
        makeMapping(QString::fromLatin1(kMapping2), QString::fromLatin1(kSourceBackend2),
                   QString::fromLatin1(kTargetBackend2), QString::fromLatin1(kCalendarId2)),
    });
}

void TstEngineCancelQueueRace::cleanup()
{
    m_engine.reset();
    m_baselines.reset();
    m_host.reset();
    m_dst2.reset();
    m_src2.reset();
    m_dst1.reset();
    m_src1.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TstEngineCancelQueueRace::cancelDuringFirstMapping_secondMappingNeverWrites()
{
    m_src1->addIncidence(QString::fromLatin1(kCalendarId1),
                         makeEvent(QStringLiteral("evt-1a"), QStringLiteral("Event One-A")));
    m_src2->addIncidence(QString::fromLatin1(kCalendarId2),
                         makeEvent(QStringLiteral("evt-2a"), QStringLiteral("Event Two-A")));

    // Block mapping 1's source fetch so mapping 1 cannot complete (and
    // mapping 2 cannot be dispatched) until we explicitly release it.
    m_src1->setFetchBlocking(true);

    SyncRequest req; // all-enabled (mappingIds empty) — drives both mappings.
    QFuture<QList<SyncResult>> future = m_engine->runSync(req);

    future.cancel();

    // Spin the engine-thread event loop so QFutureWatcher::canceled ->
    // onCancelObserved -> queued observeCancel reaches the worker before
    // mapping 1's fetch unblocks. (waitForFinished does NOT spin the
    // loop — CLAUDE.md / cf. tst_engine_single_mapping_cancel.)
    QTest::qWait(50);
    m_src1->releaseFetchBlocker();

    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    QVERIFY(future.isCanceled());

    // Mapping 2 must never have been dispatched to completion: nothing
    // reached its destination. Before the O33a fix, a cancel that
    // resolved to "already true" on the worker's own flag by the time
    // mapping 2's processSync started could be silently erased by the
    // unconditional clear at the top of processSync, letting the queue
    // run one full extra mapping.
    QCOMPARE(m_dst2->allUids(QString::fromLatin1(kCalendarId2)).size(), 0);
}

QTEST_MAIN(TstEngineCancelQueueRace)
#include "tst_engine_cancel_queue_race.moc"
