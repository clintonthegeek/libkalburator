/// G.6 Task 43 — runSyncFuture(QList<QString>) subset dispatch.
///
/// Verifies that when a list of mapping IDs is passed to the overload,
/// only those mappings run and the future returns exactly that many results.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncconflictstore.h"
#include "synctypes.h"
#include "transformationregistry.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr int kSyncTimeoutMs = 30000;

SyncMapping makeMapping(const QString &id,
                        const QString &src, const QString &tgt,
                        const QString &col)
{
    SyncMapping m;
    m.id             = id;
    m.sourceBackend  = src;
    m.sourceCalendar = col;
    m.targetBackend  = tgt;
    m.targetCalendar = col;
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TestEngineSubsetDispatch : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        Kalburator::Sync::BackendRegistry pmRegistry;
        Kalburator::PluginManager pm(&pmRegistry);
        Kalburator::registerStockPlugins(pm);
    }
    void cleanupTestCase() {
        Kalburator::Shape::TransformationRegistry::instance().clear();
        Kalburator::Shape::DomainRegistry::instance().clear();
        Kalburator::Shape::DomainOperationsRegistry::instance().clear();
    }
    void init();
    void cleanup();

    /// With a filter of 2 out of 3 mappings, the future contains exactly
    /// 2 SyncResult entries and both succeed (empty backends → no-op sync).
    void subsetOf2Runs_returnsExactly2Results();

    /// An empty ID list → zero mappings dispatched → future resolves with
    /// an empty result list.
    void emptySubset_returnsEmptyResults();

    /// All-mappings overload (no filter) still runs all 3 enabled mappings.
    void noFilter_runsAll3();

private:
    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_src1, m_tgt1;
    std::unique_ptr<MockBackend>           m_src2, m_tgt2;
    std::unique_ptr<MockBackend>           m_src3, m_tgt3;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_calBaselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_engine;
};

void TestEngineSubsetDispatch::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();

    auto makeAndRegisterBackend = [&](const QString &id) -> std::unique_ptr<MockBackend> {
        auto b = std::make_unique<MockBackend>(id);
        b->createCalendar(QStringLiteral("col"), QStringLiteral("col"), id);
        m_registry->registerBackendInstance(id, b.get());
        return b;
    };

    m_src1 = makeAndRegisterBackend(QStringLiteral("src1"));
    m_tgt1 = makeAndRegisterBackend(QStringLiteral("tgt1"));
    m_src2 = makeAndRegisterBackend(QStringLiteral("src2"));
    m_tgt2 = makeAndRegisterBackend(QStringLiteral("tgt2"));
    m_src3 = makeAndRegisterBackend(QStringLiteral("src3"));
    m_tgt3 = makeAndRegisterBackend(QStringLiteral("tgt3"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QStringLiteral("col"));
    m_host->stubCollection()->addCalendarWithId(QStringLiteral("col"), hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));
    m_calBaselines    = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore   = std::make_unique<SyncConflictStore>(dbPath);
    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setBaselineStore(m_calBaselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setCollection(m_host->stubCollection());
    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"), QStringLiteral("src1"), QStringLiteral("tgt1"), QStringLiteral("col")),
        makeMapping(QStringLiteral("m2"), QStringLiteral("src2"), QStringLiteral("tgt2"), QStringLiteral("col")),
        makeMapping(QStringLiteral("m3"), QStringLiteral("src3"), QStringLiteral("tgt3"), QStringLiteral("col")),
    });
}

void TestEngineSubsetDispatch::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_calBaselines.reset();
    m_host.reset();
    m_tgt3.reset(); m_src3.reset();
    m_tgt2.reset(); m_src2.reset();
    m_tgt1.reset(); m_src1.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TestEngineSubsetDispatch::subsetOf2Runs_returnsExactly2Results()
{
    auto future = m_engine->runSyncFuture(
        QList<QString>{ QStringLiteral("m1"), QStringLiteral("m3") });
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    const auto results = future.resultAt(0);
    QCOMPARE(results.size(), 2);
    for (const auto &r : results)
        QVERIFY(r.success);
}

void TestEngineSubsetDispatch::emptySubset_returnsEmptyResults()
{
    auto future = m_engine->runSyncFuture(QList<QString>{});
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    const auto results = future.resultAt(0);
    QCOMPARE(results.size(), 0);
}

void TestEngineSubsetDispatch::noFilter_runsAll3()
{
    auto future = m_engine->runSyncFuture();
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeoutMs);
    const auto results = future.resultAt(0);
    QCOMPARE(results.size(), 3);
}

QTEST_MAIN(TestEngineSubsetDispatch)
#include "tst_engine_subset_dispatch.moc"
