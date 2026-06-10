// tst_engine_write_gate.cpp
//
// Sync-topology promotion (2026-05-27) — engine write-gate.
//
// The engine must never write to a backend that reports read-only for a
// collection (discoveredWritable() == false). There are TWO write paths and
// both are gated:
//   - Task 5: first-sync inline blob mirror (SyncEngineWorker::dispatchFirstSync,
//     OneWayUpload + empty target) — createRecord/updateRecord/deleteRecord.
//   - Task 6: steady-state sink writer (unifiedContinueAfterConflicts' applyBatch,
//     reached by TwoWay over a non-empty target) — RecordWriter::apply, which the
//     DefaultBlobWriter routes back through createRecord/updateRecord/deleteRecord.
//
// A read-only target must receive ZERO writes in both cases, and the sync must
// still complete successfully (a skip is a no-op, not an error).

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
constexpr auto kMappingId       = "mapping-write-gate";

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

// A MockBackend that reports read-only for every collection and counts the
// three mutating record calls. Both engine write paths funnel through these,
// so a single counter proves "no write reached the read-only backend".
class ReadOnlyMockBackend : public MockBackend
{
public:
    explicit ReadOnlyMockBackend(const QString &id) : MockBackend(id) {}

    bool discoveredWritable(const QString &) const override { return false; }

    QString createRecord(const QString &collectionId, const BackendRecord &record) override
    {
        ++createCalls;
        return MockBackend::createRecord(collectionId, record);
    }
    bool updateRecord(const BackendRecord &record) override
    {
        ++updateCalls;
        return MockBackend::updateRecord(record);
    }
    bool deleteRecord(const QString &recordId) override
    {
        ++deleteCalls;
        return MockBackend::deleteRecord(recordId);
    }

    int writeCalls() const { return createCalls + updateCalls + deleteCalls; }

    int createCalls = 0;
    int updateCalls = 0;
    int deleteCalls = 0;
};

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

} // namespace

class TestEngineWriteGate : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // Task 5: OneWayUpload first sync over an empty read-only target.
    void firstSyncMirrorSkipsReadOnlyTarget();

    // Task 6: TwoWay sync over a NON-empty read-only target (steady-state path).
    void steadyStateSinkWriterSkipsReadOnlyTarget();

private:
    bool runOneSync();
    void setupCoordinator(const QList<SyncMapping> &mappings);

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<ReadOnlyMockBackend>   m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_coordinator;

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TestEngineWriteGate::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestEngineWriteGate::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>(QString::fromLatin1(kSourceBackendId));
    m_target   = std::make_unique<ReadOnlyMockBackend>(QString::fromLatin1(kTargetBackendId));
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

void TestEngineWriteGate::cleanup()
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

void TestEngineWriteGate::setupCoordinator(const QList<SyncMapping> &mappings)
{
    m_coordinator = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_coordinator->setBaselineStore(m_baselines.get());
    m_coordinator->setSyncConflictStore(m_conflictStore.get());
    m_coordinator->setConflictManager(m_conflictManager.get());
    m_coordinator->setCollection(m_host->stubCollection());
    m_coordinator->setSyncMappings(mappings);
}

bool TestEngineWriteGate::runOneSync()
{
    SyncRequest req;
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto future = m_coordinator->runSync(req);
    int waited = 0;
    while (!future.isFinished() && waited < kSyncTimeoutMs) {
        QTest::qWait(10);
        waited += 10;
    }
    if (!future.isFinished()) {
        qWarning() << "runSync did not finish within" << kSyncTimeoutMs << "ms";
        return false;
    }
    if (future.isCanceled()) {
        qWarning() << "runSync was canceled unexpectedly";
        return false;
    }
    const QList<SyncResult> results = future.resultAt(0);
    // Skipping a read-only target is success, not an error.
    for (const auto &r : results) {
        if (!r.success) {
            qWarning() << "sync reported failure:" << r.errorMessage;
            return false;
        }
    }
    return true;
}

// ---- Task 5: first-sync mirror -------------------------------------------

void TestEngineWriteGate::firstSyncMirrorSkipsReadOnlyTarget()
{
    // Source has 2 events; target is empty + read-only. OneWayUpload routes
    // through dispatchFirstSync's inline blob mirror.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-1"), QStringLiteral("Alpha")));
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("evt-2"), QStringLiteral("Beta")));

    setupCoordinator({ makeMapping(SyncMode::OneWayUpload) });

    QVERIFY(runOneSync());                 // skip is not an error
    QCOMPARE(m_target->createCalls, 0);
    QCOMPARE(m_target->updateCalls, 0);
    QCOMPARE(m_target->deleteCalls, 0);
}

// ---- Task 6: steady-state sink writer ------------------------------------

void TestEngineWriteGate::steadyStateSinkWriterSkipsReadOnlyTarget()
{
    // Target is NON-empty (so dispatchFirstSync defers) and read-only. A TwoWay
    // sync never enters dispatchFirstSync (it requires OneWayUpload), so this
    // exercises the unifiedContinueAfterConflicts applyBatch sink writer.
    // Non-overlapping UIDs => no conflict => no AskUser pause.
    m_source->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("src-1"), QStringLiteral("From Source")));
    m_target->addIncidence(QString::fromLatin1(kCalendarId),
                           makeEvent(QStringLiteral("tgt-1"), QStringLiteral("From Target")));

    setupCoordinator({ makeMapping(SyncMode::TwoWay) });

    QVERIFY(runOneSync());                 // skip is not an error
    // No mutating record call reached the read-only target via the sink writer.
    QCOMPARE(m_target->writeCalls(), 0);
}

QTEST_MAIN(TestEngineWriteGate)
#include "tst_engine_write_gate.moc"
