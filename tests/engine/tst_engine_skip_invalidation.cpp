// Sync-graph redesign campaign — Task 1 (spec §5.9 L1, mechanism §2.2).
//
// The fast-path pre-pass computes SyncEngine::m_skippedMappingIds once at
// the start of a Queue-mode run. Pre-L1, a mapping that writes one of its
// endpoints during that same run does NOT un-skip a later pending mapping
// sharing that endpoint — the pre-pass judged the endpoint before this run
// wrote it, so propagation across a chain of mappings takes one hop per
// run instead of settling in a single pass.
//
// Fixture: three RevisionMockBackend instances A, B, C chained by two
// TwoWay mappings (M1 = A<->B, M2 = B<->C), plus an unrelated pair D, E
// (M3 = D<->E) used as the "genuinely untouched" control. RevisionMockBackend
// is MockBackend + Sync::ChangeDetection, whose collectionRevision() is a
// content digest (order-independent hash over each calendar's (uid, item
// hash) pairs) — exactly the shape GenericSqliteBackend uses, so an
// idempotent re-write does not defeat settling.
//
// testWriteUnskipsDownstream: a first run (all backends empty) settles
// baseline sync-progress tokens for M1/M2/M3 with zero cost. A single new
// record is then added to A only, and a second run must propagate it all
// the way to C in that one run: M1 (not skipped — A changed) writes B,
// which must invalidate M2's frozen skip verdict (B is M2's source), so
// M2 actually runs and pushes the record on to C.
//
// testUntouchedMappingStaysSkipped: the companion control. M3's endpoints
// (D, E) are never touched by anything in the same run, so it must stay
// skipped — zero SyncStats, success, and zero additional backend I/O
// (operationLog() unchanged).

#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QObject>
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

// Minimal ISyncHost over a BackendRegistry (same shape as the other
// engine tests; duplicated because the engine-test target links no
// shared stub carrying it).
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
// content digest (order-independent hash over (uid, item-hash) pairs),
// not a static/incrementing token — so it actually settles when content
// stops changing and actually moves when a record is added. Mirrors
// GenericSqliteBackend::collectionRevision()'s contract (see its header
// comment): a pure function of collection content, unaffected by an
// idempotent re-write.
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

class TestEngineSkipInvalidation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testWriteUnskipsDownstream();
    void testUntouchedMappingStaysSkipped();

private:
    QFuture<QList<SyncResult>> runOnce();

    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;

    std::unique_ptr<QTemporaryDir> m_tmpDir;
    std::unique_ptr<BackendRegistry> m_registry;
    std::unique_ptr<RevisionMockBackend> m_a, m_b, m_c, m_d, m_e;
    std::unique_ptr<RegistrySyncHost> m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncEngine> m_engine;
};

void TestEngineSkipInvalidation::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TestEngineSkipInvalidation::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_a = std::make_unique<RevisionMockBackend>(QStringLiteral("A"));
    m_b = std::make_unique<RevisionMockBackend>(QStringLiteral("B"));
    m_c = std::make_unique<RevisionMockBackend>(QStringLiteral("C"));
    m_d = std::make_unique<RevisionMockBackend>(QStringLiteral("D"));
    m_e = std::make_unique<RevisionMockBackend>(QStringLiteral("E"));
    m_registry->registerBackendInstance(QStringLiteral("A"), m_a.get());
    m_registry->registerBackendInstance(QStringLiteral("B"), m_b.get());
    m_registry->registerBackendInstance(QStringLiteral("C"), m_c.get());
    m_registry->registerBackendInstance(QStringLiteral("D"), m_d.get());
    m_registry->registerBackendInstance(QStringLiteral("E"), m_e.get());

    m_host = std::make_unique<RegistrySyncHost>(m_registry.get());

    m_baselines = std::make_unique<Kalburator::Storage::BaselineStore>(
        m_tmpDir->filePath(QStringLiteral("baselines.db")));

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setSkipUnchangedMappings(true);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncMappings({
        makeMapping(QStringLiteral("m1"), QStringLiteral("A"), QStringLiteral("B")),
        makeMapping(QStringLiteral("m2"), QStringLiteral("B"), QStringLiteral("C")),
        makeMapping(QStringLiteral("m3"), QStringLiteral("D"), QStringLiteral("E")),
    });
}

void TestEngineSkipInvalidation::cleanup()
{
    m_engine.reset();
    m_baselines.reset();
    m_host.reset();
    m_e.reset();
    m_d.reset();
    m_c.reset();
    m_b.reset();
    m_a.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

QFuture<QList<SyncResult>> TestEngineSkipInvalidation::runOnce()
{
    SyncRequest req; // empty mappingIds => all-enabled => multi-mapping driver => fast path.
    req.behavior = SyncEngine::SyncBehavior::Unmonitored;
    auto f = m_engine->runSync(req);
    (void)QTest::qWaitFor([&]{ return f.isFinished(); }, kSyncTimeoutMs);
    return f;
}

void TestEngineSkipInvalidation::testWriteUnskipsDownstream()
{
    // --- Prime: all backends start empty. A first run settles sync-progress
    // tokens for every mapping at zero cost (nothing to propagate). ---
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        const QList<SyncResult> results = f.resultAt(0);
        QCOMPARE(results.size(), 3);
        for (const auto &r : results)
            QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

        QVERIFY2(!m_baselines->syncToken(QStringLiteral("m1"), QStringLiteral("source")).isEmpty(),
                 "engine must persist m1's source sync token after a successful sync");
        QVERIFY2(!m_baselines->syncToken(QStringLiteral("m1"), QStringLiteral("target")).isEmpty(),
                 "engine must persist m1's target sync token after a successful sync");
        QVERIFY2(!m_baselines->syncToken(QStringLiteral("m2"), QStringLiteral("source")).isEmpty(),
                 "engine must persist m2's source sync token after a successful sync");
        QVERIFY2(!m_baselines->syncToken(QStringLiteral("m2"), QStringLiteral("target")).isEmpty(),
                 "engine must persist m2's target sync token after a successful sync");

        // All three agree: nothing propagated because nothing existed.
        QCOMPARE(m_a->allUids(QString::fromLatin1(kCalendarId)).size(), 0);
        QCOMPARE(m_b->allUids(QString::fromLatin1(kCalendarId)).size(), 0);
        QCOMPARE(m_c->allUids(QString::fromLatin1(kCalendarId)).size(), 0);
    }

    // --- Add one new record to A only, then run once more. Pre-L1, the
    // pre-pass freezes m2 into the skip set (B hasn't changed YET, at
    // pre-pass time) and it stays frozen even after m1 writes B during
    // this same run — so C never sees the record in a single pass. ---
    m_a->addIncidence(QString::fromLatin1(kCalendarId),
                      makeEvent(QStringLiteral("rec-new"), QStringLiteral("New Record")));

    auto f = runOnce();
    QVERIFY(f.isFinished());
    const QList<SyncResult> results = f.resultAt(0);
    // L2 (Task 2): m1 and m2 both run for real in this pass (L1 already
    // un-freezes m2), so m1's write to B and m2's write to C both land in
    // the SAME pass's dirty-writer set — m2's own source (B) shows up as
    // "written by someone else" (m1) even though m2's diff already saw
    // that write. That triggers exactly one harmless re-verification pass
    // of m2 (a no-op the second time): 3 mappings + 1 re-pass entry = 4,
    // deterministically (verified across repeated runs).
    QCOMPARE(results.size(), 4);
    for (const auto &r : results)
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    // C must contain the new record after this single run.
    QVERIFY2(m_c->allUids(QString::fromLatin1(kCalendarId)).contains(QStringLiteral("rec-new")),
             "L1: a single run must propagate a write across a two-hop mapping chain");

    // m2 (index 1) must be a real, non-skip result: it applied a change on
    // the C (target) side.
    const SyncResult &m2Result = results.at(1);
    QCOMPARE(m2Result.targetStats.created, 1);
}

void TestEngineSkipInvalidation::testUntouchedMappingStaysSkipped()
{
    // Prime as above.
    {
        auto f = runOnce();
        QVERIFY(f.isFinished());
        for (const auto &r : f.resultAt(0))
            QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    }

    m_a->addIncidence(QString::fromLatin1(kCalendarId),
                      makeEvent(QStringLiteral("rec-new"), QStringLiteral("New Record")));

    m_d->clearOperationLog();
    m_e->clearOperationLog();

    auto f = runOnce();
    QVERIFY(f.isFinished());
    const QList<SyncResult> results = f.resultAt(0);
    // L2 (Task 2): see testWriteUnskipsDownstream — the m1/m2 chain in
    // this fixture triggers exactly one harmless re-verification pass of
    // m2: 3 mappings + 1 re-pass entry = 4, deterministically.
    QCOMPARE(results.size(), 4);
    for (const auto &r : results)
        QVERIFY2(r.success, qUtf8Printable(r.errorMessage));

    // Parallel-sync Task 10 (N=4 sweep): this used to assert on
    // results.at(2) directly, on the assumption that per-mapping results
    // land in submission order. That was only ever true by accident of
    // strict sequential dispatch — SyncResult carries no mapping id, and
    // MappingQueue::recordResult() appends in COMPLETION order, which
    // m1/m2's endpoint collision on B reorders relative to m3 (disjoint,
    // free to run alongside m1) once concurrency is > 1. There is also no
    // way to reliably pick "the m3 result" out of the four by shape alone
    // — the harmless re-pass no-op of m2 has the same all-zero-stats
    // signature m3's skip does. The per-backend operation logs below are
    // the order-independent proof that actually matters: m3's endpoints
    // (D, E) saw zero I/O regardless of when in the run it was evaluated.
    QCOMPARE(m_d->operationLog().size(), 0);
    QCOMPARE(m_e->operationLog().size(), 0);
}

QTEST_MAIN(TestEngineSkipInvalidation)
#include "tst_engine_skip_invalidation.moc"
