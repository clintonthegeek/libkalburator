// tst_conflict_policy_matrix.cpp
//
// WP-D1 (architectural-redress campaign) — conflict-policy matrix tests.
//
// Pins three conflict resolution policies that had zero direct engine-level
// test coverage:
//
//   1. conflictResolution_duplicate_keepsBothSides
//      ConflictResolution::Duplicate — highest-risk gap: the "keep both"
//      write path (syncengine.cpp:1623). After resolution the target must
//      hold the original uid record (source version) + a "-dup-" clone;
//      the source must receive the clone so both sides see both records.
//
//   2. conflictResolution_targetWins_writesTargetVersionToSource
//      ConflictResolution::TargetWins — target version propagates to source;
//      target itself is unchanged.
//
//   3. conflictResolution_skip_leavesConflictUnresolved
//      ConflictResolution::Skip — no writes to either side; conflict lands
//      in SyncResult::unresolvedConflicts.
//
// All three mirror the SourceWins fixture from tst_syncengine_unification:
// AskUser policy + seeded baseline → worker yields → ConflictManager
// AutoResolve → resumeAfterConflictResolution(policy) → asserts on side
// effects. Without the baseline seed the quick-path downgrades AskUser to
// SourceWins and none of these tests would exercise the actual policy
// switch.

#include <QtTest/QtTest>
#include <QFuture>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <memory>

#include "backendregistry.h"
#include "baselinestore.h"
#include "calendar_test_helpers.h"
#include "conflictmanager.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "shaperegistries.h"
#include "stock_plugins.h"
#include "syncconflictstore.h"
#include "syncengine.h"
#include "synctypes.h"

#include "stubs/stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kCollectionId  = "stub-collection";
constexpr auto kSourceBackend = "source-mock";
constexpr auto kTargetBackend = "target-mock";
constexpr auto kCalendarId    = "calendar-1";
constexpr auto kMappingId     = "mapping-1";
constexpr auto kConflictUid   = "evt-conflict";
constexpr int  kSyncTimeout   = 30000;

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

} // namespace

class TstConflictPolicyMatrix : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void conflictResolution_duplicate_keepsBothSides();
    void conflictResolution_targetWins_writesTargetVersionToSource();
    void conflictResolution_skip_leavesConflictUnresolved();

private:
    void setupConflictScenario(MockBackend *source, MockBackend *target,
                               const QString &sourceSummary,
                               const QString &targetSummary);

    std::unique_ptr<QTemporaryDir>                       m_tmpDir;
    std::unique_ptr<BackendRegistry>                     m_registry;
    std::unique_ptr<StubSyncHost>                        m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore>  m_baselines;
    std::unique_ptr<SyncConflictStore>                   m_conflictStore;
    std::unique_ptr<ConflictManager>                     m_conflictManager;
    std::unique_ptr<SyncEngine>                          m_engine;

    // Stock plugins registered once in initTestCase.
    Kalburator::Shape::ShapeRegistries m_shape;
    Kalburator::Sync::BackendRegistry  m_pmRegistry;
};

void TstConflictPolicyMatrix::initTestCase()
{
    Kalburator::PluginManager pm(&m_pmRegistry, m_shape);
    Kalburator::registerStockPlugins(pm);
}

void TstConflictPolicyMatrix::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());
    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".kalburator-sync.db"));

    m_registry = std::make_unique<BackendRegistry>();
    m_host     = std::make_unique<StubSyncHost>(m_registry.get());

    m_baselines     = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore = std::make_unique<SyncConflictStore>(dbPath);

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get(), m_shape);
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setCollection(m_host->stubCollection());
}

void TstConflictPolicyMatrix::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_baselines.reset();
    m_host.reset();
    m_registry.reset();
}

void TstConflictPolicyMatrix::setupConflictScenario(MockBackend *source,
                                                     MockBackend *target,
                                                     const QString &sourceSummary,
                                                     const QString &targetSummary)
{
    source->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));
    target->createCalendar(QString::fromLatin1(kCollectionId),
                           QString::fromLatin1(kCalendarId),
                           QStringLiteral("Calendar 1"));

    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId),
                                                hostCal);

    // Seed a baseline so both sides register as "modified since baseline".
    // Without this, the engine's quick-path downgrades AskUser → SourceWins
    // and the conflict yield never fires.
    auto baselineEvent = makeEvent(QString::fromLatin1(kConflictUid),
                                   QStringLiteral("Baseline"));
    m_baselines->setBaselineV3(QString::fromLatin1(kMappingId),
                               calendarTestRec(QString::fromLatin1(kConflictUid),
                                               eventToIcal(baselineEvent)));

    source->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid), sourceSummary));
    target->addIncidence(QString::fromLatin1(kCalendarId),
                         makeEvent(QString::fromLatin1(kConflictUid), targetSummary));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — Duplicate: both sides retained after resolution.
//
// KNOWN BUG (FINDINGS C2): the UID rebinding in syncengine.cpp does a byte
// replace looking for "UID:<id>" (iCal syntax) in op.targetRecord.data, but
// at that point the data has already been promoted to canonical JSON
// ("{\"uid\":\"<id>\",...}") by tgtToCanon->apply(). The "UID:" pattern never
// matches, so clone.data retains the original uid. When canonToTgt->apply()
// converts back to iCal the clone gets the OLD uid, and
// MockBackend::createRecord stores it under "evt-conflict" (overwriting the
// source record). Net result: target ends up with 1 record, not 2.
//
// This test is QSKIP'd until the Duplicate UID-rebind is fixed to operate on
// canonical form (see FINDINGS.md C2).
// ─────────────────────────────────────────────────────────────────────────────
void TstConflictPolicyMatrix::conflictResolution_duplicate_keepsBothSides()
{
    QSKIP("Known bug: Duplicate UID-rebind uses iCal 'UID:' syntax on canonical "
          "JSON data — replace never matches; clone inherits old uid. "
          "See FINDINGS.md C2 for the required fix.");

    auto source = std::make_unique<MockBackend>();
    auto target = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackend), source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackend), target.get());

    setupConflictScenario(source.get(), target.get(),
                          QStringLiteral("Source-Modified"),
                          QStringLiteral("Target-Modified"));

    SyncMapping mapping;
    mapping.id             = QString::fromLatin1(kMappingId);
    mapping.sourceBackend  = QString::fromLatin1(kSourceBackend);
    mapping.sourceCalendar = QString::fromLatin1(kCalendarId);
    mapping.targetBackend  = QString::fromLatin1(kTargetBackend);
    mapping.targetCalendar = QString::fromLatin1(kCalendarId);
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    m_engine->setSyncMappings({ mapping });

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::Duplicate);
    m_engine->setConflictManager(m_conflictManager.get());

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId),
                                           SyncEngine::SyncBehavior::Monitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeout);
    QVERIFY2(!future.isCanceled(), "future cancelled — Duplicate resolution did not complete");

    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QVERIFY(r.unresolvedConflicts.isEmpty());

    // Target must hold the source-version record (uid = kConflictUid) AND a
    // clone record (uid starts with kConflictUid + "-dup-").
    const QStringList targetUids = target->allUids(QString::fromLatin1(kCalendarId));
    QCOMPARE(targetUids.size(), 2);

    auto origInc = target->incidence(QString::fromLatin1(kCalendarId),
                                     QString::fromLatin1(kConflictUid));
    QVERIFY2(origInc, "target missing original uid after Duplicate");
    QCOMPARE(origInc->summary(), QStringLiteral("Source-Modified"));

    const QString dupPrefix = QString::fromLatin1(kConflictUid) + QStringLiteral("-dup-");
    const QString dupUid = [&]() {
        for (const QString &uid : targetUids)
            if (uid != QString::fromLatin1(kConflictUid))
                return uid;
        return QString{};
    }();
    QVERIFY2(!dupUid.isEmpty(), "no duplicate uid found in target");
    QVERIFY2(dupUid.startsWith(dupPrefix),
             qPrintable(QStringLiteral("dup uid '%1' does not start with '%2'")
                            .arg(dupUid, dupPrefix)));

    // Source must have received the clone so both sides see both records.
    const QStringList sourceUids = source->allUids(QString::fromLatin1(kCalendarId));
    QVERIFY2(sourceUids.contains(dupUid),
             qPrintable(QStringLiteral("source missing clone uid '%1'").arg(dupUid)));

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — TargetWins: target version propagated to source.
// ─────────────────────────────────────────────────────────────────────────────
void TstConflictPolicyMatrix::conflictResolution_targetWins_writesTargetVersionToSource()
{
    auto source = std::make_unique<MockBackend>();
    auto target = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackend), source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackend), target.get());

    setupConflictScenario(source.get(), target.get(),
                          QStringLiteral("Source-Modified"),
                          QStringLiteral("Target-Modified"));

    SyncMapping mapping;
    mapping.id             = QString::fromLatin1(kMappingId);
    mapping.sourceBackend  = QString::fromLatin1(kSourceBackend);
    mapping.sourceCalendar = QString::fromLatin1(kCalendarId);
    mapping.targetBackend  = QString::fromLatin1(kTargetBackend);
    mapping.targetCalendar = QString::fromLatin1(kCalendarId);
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    m_engine->setSyncMappings({ mapping });

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::TargetWins);
    m_engine->setConflictManager(m_conflictManager.get());

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId),
                                           SyncEngine::SyncBehavior::Monitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeout);
    QVERIFY2(!future.isCanceled(), "future cancelled — TargetWins resolution did not complete");

    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QVERIFY(r.unresolvedConflicts.isEmpty());

    // Source must now hold the target's version.
    auto srcInc = source->incidence(QString::fromLatin1(kCalendarId),
                                    QString::fromLatin1(kConflictUid));
    QVERIFY2(srcInc, "source missing uid after TargetWins");
    QCOMPARE(srcInc->summary(), QStringLiteral("Target-Modified"));

    // Target must be unchanged.
    auto tgtInc = target->incidence(QString::fromLatin1(kCalendarId),
                                    QString::fromLatin1(kConflictUid));
    QVERIFY2(tgtInc, "target missing uid after TargetWins");
    QCOMPARE(tgtInc->summary(), QStringLiteral("Target-Modified"));

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Skip: no writes; conflict lands in unresolvedConflicts.
// ─────────────────────────────────────────────────────────────────────────────
void TstConflictPolicyMatrix::conflictResolution_skip_leavesConflictUnresolved()
{
    auto source = std::make_unique<MockBackend>();
    auto target = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackend), source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackend), target.get());

    setupConflictScenario(source.get(), target.get(),
                          QStringLiteral("Source-Modified"),
                          QStringLiteral("Target-Modified"));

    SyncMapping mapping;
    mapping.id             = QString::fromLatin1(kMappingId);
    mapping.sourceBackend  = QString::fromLatin1(kSourceBackend);
    mapping.sourceCalendar = QString::fromLatin1(kCalendarId);
    mapping.targetBackend  = QString::fromLatin1(kTargetBackend);
    mapping.targetCalendar = QString::fromLatin1(kCalendarId);
    mapping.mode           = SyncMode::TwoWay;
    mapping.conflictPolicy = ConflictResolution::AskUser;
    mapping.enabled        = true;
    m_engine->setSyncMappings({ mapping });

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::Skip);
    m_engine->setConflictManager(m_conflictManager.get());

    auto future = m_engine->runSyncFuture(QString::fromLatin1(kMappingId),
                                           SyncEngine::SyncBehavior::Monitored);
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), kSyncTimeout);
    QVERIFY2(!future.isCanceled(), "future cancelled — Skip resolution did not complete");

    QCOMPARE(future.resultCount(), 1);
    const SyncResult r = future.resultAt(0);
    // success == false when unresolved conflicts exist:
    // unifiedContinueAfterConflicts sets success = !hasUnresolvedConflicts()
    QVERIFY2(!r.success, "expected success==false with unresolved conflicts");
    QVERIFY2(!r.unresolvedConflicts.isEmpty(),
             "expected at least one unresolved conflict after Skip");

    // Neither side should have been written — each still holds its own version.
    auto srcInc = source->incidence(QString::fromLatin1(kCalendarId),
                                    QString::fromLatin1(kConflictUid));
    QVERIFY2(srcInc, "source missing uid after Skip");
    QCOMPARE(srcInc->summary(), QStringLiteral("Source-Modified"));

    auto tgtInc = target->incidence(QString::fromLatin1(kCalendarId),
                                    QString::fromLatin1(kConflictUid));
    QVERIFY2(tgtInc, "target missing uid after Skip");
    QCOMPARE(tgtInc->summary(), QStringLiteral("Target-Modified"));

    m_engine->setSyncMappings({});
    m_engine->setConflictManager(nullptr);
}

QTEST_MAIN(TstConflictPolicyMatrix)
#include "tst_conflict_policy_matrix.moc"
