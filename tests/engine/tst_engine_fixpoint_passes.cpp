// Sync-graph redesign campaign — Task 2 (spec §5.9 L2, mechanism §2.3).
//
// L1 (Task 1) un-freezes a pending mapping's fast-path skip verdict when an
// earlier mapping in the SAME run writes one of its endpoints. That only
// helps when the writer runs BEFORE the mapping it dirties. If the queue
// order is hostile — the downstream mapping already ran (and skipped,
// finding nothing changed yet) before the upstream mapping writes its
// shared endpoint — L1 alone cannot help: there is no "later mapping in
// this run" left to un-freeze.
//
// L2 closes that gap: once the queue exhausts, if any mapping in the pass
// actually wrote an endpoint that a DIFFERENT mapping also touches, the
// engine re-primes the queue with just the mappings dirtied by someone
// else and runs another pass — up to kMaxSyncPasses (3) total — until
// quiescent. One user-visible Sync converges regardless of mapping order.
//
// Fixture: three RevisionMockBackend instances A, B, C. Same content-digest
// revision shape as Task 1's fixture (order-independent hash over each
// calendar's (uid, item hash) pairs), so idempotent re-writes never defeat
// settling.
//
// testChainConvergesInOneRun: mappings are DELIBERATELY ORDERED
// [M2 = B<->C, M1 = A<->B] — the hostile order named in the task brief.
// A new record lands in A only. M2 runs first and finds B/C empty →
// skip-eligible or no-op. M1 then runs and writes B. Post-L1 that would
// leave C stale after this run (M2 already ran). L2 must re-pass: M2 is
// re-primed (dirtied by M1's write to the shared B endpoint) and pushes
// the record on to C, all within one syncPassStarted-observed run.
//
// testQuiescentRunIsSinglePass: run the same fixture again immediately
// with no new changes — nothing is dirtied, so no re-pass fires and the
// future's result list is exactly one entry per mapping (all skips/no-ops).
//
// testPassCapRespected: drives the chain case and inspects every
// syncPassStarted emission — none may report a pass number above
// kMaxSyncPasses (3).
//
// testSelfDirtyDoesNotRepass: a single mapping M1 = A<->B; a change in A
// makes M1 write B. M1 is the ONLY mapping touching either of its own
// endpoints, so per the "already converged with its own writes" rule, no
// second pass is warranted — spy count stays 0.

#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include <KCalendarCore/Event>

#include <memory>

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

// Minimal ISyncHost over a BackendRegistry (same shape as the other engine
// tests; duplicated because the engine-test target links no shared stub
// carrying it).
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

// MockBackend + ChangeDetection whose collectionRevision() is a real
// content digest (order-independent hash over (uid, item-hash) pairs), not
// a static/incrementing token — so it actually settles when content stops
// changing and actually moves when a record is added. Mirrors
// GenericSqliteBackend::collectionRevision()'s contract.
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

SyncMapping makeMapping(const QString &id, const QString &src, const QString &tgt)
{
    SyncMapping m;
    m.id             = id;
    m.sourceBackend  = src;
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = tgt;
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

} // namespace

class TestEngineFixpointPasses : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testChainConvergesInOneRun();
    void testQuiescentRunIsSinglePass();
    void testPassCapRespected();
    void testSelfDirtyDoesNotRepass();

private:
    QFuture<QList<SyncResult>> runOnce();
    void setupChainFixture(); // A<->B<->C, hostile order [M2, M1]
    void setupSingleMappingFixture(); // A<->B only

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;

    std::unique_ptr<QTemporaryDir> m_tmpDir;
    std::unique_ptr<BackendRegistry> m_registry;
    std::unique_ptr<RevisionMockBackend> m_a, m_b, m_c;
    std::unique_ptr<RegistrySyncHost> m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncEngine> m_engine;
};

void TestEngineFixpointPasses::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestEngineFixpointPasses::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_a = std::make_unique<RevisionMockBackend>(QStringLiteral("A"));
    m_b = std::make_unique<RevisionMockBackend>(QStringLiteral("B"));
    m_c = std::make_unique<RevisionMockBackend>(QStringLiteral("C"));
    m_registry->registerBackendInstance(QStringLiteral("A"), m_a.get());
    m_registry->registerBackendInstance(QStringLiteral("B"), m_b.get());
    m_registry->registerBackendInstance(QStringLiteral("C"), m_c.get());

    m_host = std::make_unique<RegistrySyncHost>(m_registry.get());

    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(
        m_tmpDir->filePath(QStringLiteral("baselines.db")));

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setSkipUnchangedMappings(true);
    m_engine->setBaselineStore(m_baselines.get());
}

void TestEngineFixpointPasses::cleanup()
{
    m_engine.reset();
    m_baselines.reset();
    m_host.reset();
    m_c.reset();
    m_b.reset();
    m_a.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

QFuture<QList<SyncResult>> TestEngineFixpointPasses::runOnce()
{
    SyncRequest req; // empty mappingIds => all-enabled => multi-mapping driver => fast path.
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto f = m_engine->runSync(req);
    (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
    return f;
}

void TestEngineFixpointPasses::setupChainFixture()
{
    // Deliberately hostile order: the downstream mapping (B<->C) is listed
    // BEFORE the upstream mapping (A<->B) that will dirty its source.
    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m2"), QStringLiteral("B"), QStringLiteral("C")),
        makeMapping(QStringLiteral("m1"), QStringLiteral("A"), QStringLiteral("B")),
    });
}

void TestEngineFixpointPasses::setupSingleMappingFixture()
{
    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"), QStringLiteral("A"), QStringLiteral("B")),
    });
}

void TestEngineFixpointPasses::testChainConvergesInOneRun()
{
    setupChainFixture();

    // Prime: all backends empty, settles sync-progress tokens at zero cost.
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        for (const auto &r : f.resultAt(0))
            QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    }

    m_a->addIncidence(QString::fromLatin1(kCalendarId),
                      makeEvent(QStringLiteral("rec-new"), QStringLiteral("New Record")));

    QSignalSpy spy(m_engine.get(), &SyncEngine::syncPassStarted);

    auto f = runOnce();
    QVERIFY(f.isFinished());
    const QList<SyncResult> results = f.resultAt(0);
    for (const auto &r : results)
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    QVERIFY2(m_c->allUids(QString::fromLatin1(kCalendarId)).contains(QStringLiteral("rec-new")),
             "L2: one run must converge a write across a hostile-order mapping chain");

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.at(0);
    QCOMPARE(args.at(0).toInt(), 2);
    QCOMPARE(args.at(1).toInt(), SyncEngine::kMaxSyncPasses);
}

void TestEngineFixpointPasses::testQuiescentRunIsSinglePass()
{
    setupChainFixture();

    // Prime, then converge once (same as above) so state is settled.
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        for (const auto &r : f.resultAt(0))
            QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    }
    m_a->addIncidence(QString::fromLatin1(kCalendarId),
                      makeEvent(QStringLiteral("rec-new"), QStringLiteral("New Record")));
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        for (const auto &r : f.resultAt(0))
            QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    }

    // Now everything is settled: run again with no changes.
    QSignalSpy spy(m_engine.get(), &SyncEngine::syncPassStarted);
    auto f = runOnce();
    QVERIFY(f.isFinished());
    const QList<SyncResult> results = f.resultAt(0);
    for (const auto &r : results)
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    QCOMPARE(spy.count(), 0);
    QCOMPARE(results.size(), 2); // one result per mapping, all skips
}

void TestEngineFixpointPasses::testPassCapRespected()
{
    setupChainFixture();

    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        for (const auto &r : f.resultAt(0))
            QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    }
    m_a->addIncidence(QString::fromLatin1(kCalendarId),
                      makeEvent(QStringLiteral("rec-new"), QStringLiteral("New Record")));

    QSignalSpy spy(m_engine.get(), &SyncEngine::syncPassStarted);
    auto f = runOnce();
    QVERIFY(f.isFinished());
    for (const auto &r : f.resultAt(0))
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    for (const auto &call : spy) {
        QVERIFY2(call.at(0).toInt() <= SyncEngine::kMaxSyncPasses,
                 "no syncPassStarted emission may report a pass beyond the cap");
    }
}

void TestEngineFixpointPasses::testSelfDirtyDoesNotRepass()
{
    setupSingleMappingFixture();

    // Prime.
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        for (const auto &r : f.resultAt(0))
            QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    }

    m_a->addIncidence(QString::fromLatin1(kCalendarId),
                      makeEvent(QStringLiteral("rec-new"), QStringLiteral("New Record")));

    QSignalSpy spy(m_engine.get(), &SyncEngine::syncPassStarted);
    auto f = runOnce();
    QVERIFY(f.isFinished());
    const QList<SyncResult> results = f.resultAt(0);
    for (const auto &r : results)
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    QVERIFY2(m_b->allUids(QString::fromLatin1(kCalendarId)).contains(QStringLiteral("rec-new")),
             "M1 must have written B");
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestEngineFixpointPasses)
#include "tst_engine_fixpoint_passes.moc"
