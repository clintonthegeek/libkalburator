#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QRandomGenerator>
#include <QUuid>

#include "conflictmanager.h"
#include "iconflictresolver.h"
#include "syncconflictstore.h"
#include "synctypes.h"

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;


/**
 * @brief Mock conflict resolver for testing.
 *
 * Allows tests to specify predetermined resolution values and tracks
 * how many times resolveConflict() was called.
 */
class MockConflictResolver : public IConflictResolver
{
public:
    MockConflictResolver() = default;
    ~MockConflictResolver() override = default;

    ConflictResolution resolveConflict(const ConflictInfo &conflict,
                                        QWidget *parentWidget) override
    {
        Q_UNUSED(parentWidget)

        m_callCount++;
        m_lastConflict = conflict;

        // If we have queued resolutions, use them in order
        if (!m_queuedResolutions.isEmpty()) {
            return m_queuedResolutions.takeFirst();
        }

        return m_resolution;
    }

    // Configuration
    ConflictResolution m_resolution = ConflictResolution::SourceWins;
    QList<ConflictResolution> m_queuedResolutions;

    // Tracking
    int m_callCount = 0;
    ConflictInfo m_lastConflict;

    void reset()
    {
        m_callCount = 0;
        m_lastConflict = ConflictInfo();
        m_queuedResolutions.clear();
    }
};

class TestConflictManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic functionality
    void testDefaultWorkflowMode();
    void testSetWorkflowMode();
    void testSetAutoResolutionPolicy();
    void testSetHybridThreshold();

    // Immediate mode with mock resolver
    void testImmediateModeSourceWins();
    void testImmediateModeTargetWins();
    void testImmediateModeSkip();
    void testImmediateModeKeepBoth();

    // Deferred mode
    void testDeferredModeQueuesConflict();
    void testDeferredModeEmitsSignal();

    // Hybrid mode
    void testHybridModeFewConflicts();
    void testHybridModeManyConflicts();

    // Auto-resolve mode
    void testAutoResolveSourceWins();
    void testAutoResolveTargetWins();
    void testAutoResolveLastWriteWins();

    // Multiple conflicts
    void testHandleMultipleConflictsImmediate();
    void testHandleMultipleConflictsSkipQueueRemaining();

    // Signal emission
    void testConflictResolvedSignal();
    void testConflictQueuedSignal();
    void testUnresolvedCountChangedSignal();

    // SyncConflictStore integration
    void testConflictRecordedInStore();
    void testConflictResolvedInStore();
    void testUnresolvedConflictCount();
    void testRepresentingSameConflictDoesNotDuplicateRow();
    void testRepresentingSameConflictPopulatesIcalColumns();

    // Display name fields
    void testDisplayNameFieldsPassedToResolver();
    void testDisplayNameFieldsFallbackToBackendId();

private:
    ConflictInfo createTestConflict(const QString &id = QString());

    QTemporaryDir m_tempDir;
    SyncConflictStore *m_syncStore = nullptr;
    ConflictManager *m_conflictManager = nullptr;
    MockConflictResolver *m_mockResolver = nullptr;
};

void TestConflictManager::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void TestConflictManager::cleanupTestCase()
{
}

void TestConflictManager::init()
{
    // Create fresh SyncConflictStore for each test with UUID-based unique path
    QString dbPath = m_tempDir.filePath(QStringLiteral("test_%1.db").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    m_syncStore = new SyncConflictStore(dbPath, this);
    QVERIFY(m_syncStore->isOpen());

    // Create ConflictManager with mock resolver
    m_conflictManager = new ConflictManager(this);
    m_conflictManager->setSyncConflictStore(m_syncStore);

    // Create and inject mock resolver
    m_mockResolver = new MockConflictResolver();
    m_conflictManager->setConflictResolver(m_mockResolver);
}

void TestConflictManager::cleanup()
{
    delete m_conflictManager;
    m_conflictManager = nullptr;
    m_mockResolver = nullptr;  // Owned by ConflictManager

    delete m_syncStore;
    m_syncStore = nullptr;
}

ConflictInfo TestConflictManager::createTestConflict(const QString &id)
{
    // Use a combination of timestamp and random number for uniqueness
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    int random = QRandomGenerator::global()->bounded(10000);
    QString uniqueSuffix = QStringLiteral("%1-%2").arg(timestamp).arg(random);

    ConflictInfo conflict;
    conflict.conflictId = id;
    conflict.mappingId = QStringLiteral("test-mapping");
    conflict.sourceId = QStringLiteral("source-uid-%1").arg(uniqueSuffix);
    conflict.targetId = QStringLiteral("target-id-%1").arg(uniqueSuffix);
    conflict.calendarId = QStringLiteral("test-calendar");
    conflict.type = ConflictType::BothModified;
    conflict.sourceBackendId = QStringLiteral("local-backend");
    conflict.targetBackendId = QStringLiteral("remote-backend");
    conflict.sourceDescription = QStringLiteral("Source Event");
    conflict.targetDescription = QStringLiteral("Target Event");
    conflict.sourceIcalData = QStringLiteral("BEGIN:VEVENT\nSUMMARY:Source\nEND:VEVENT");
    conflict.targetIcalData = QStringLiteral("BEGIN:VEVENT\nSUMMARY:Target\nEND:VEVENT");
    conflict.sourceModified = QDateTime::currentDateTime().addSecs(-3600);  // 1 hour ago
    conflict.targetModified = QDateTime::currentDateTime().addSecs(-1800);  // 30 mins ago
    conflict.detectedAt = QDateTime::currentDateTime();
    return conflict;
}

// ============================================================================
// Basic functionality tests
// ============================================================================

void TestConflictManager::testDefaultWorkflowMode()
{
    // Default should be Immediate
    QCOMPARE(m_conflictManager->workflowMode(), ConflictManager::WorkflowMode::Immediate);
}

void TestConflictManager::testSetWorkflowMode()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);
    QCOMPARE(m_conflictManager->workflowMode(), ConflictManager::WorkflowMode::Deferred);

    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Hybrid);
    QCOMPARE(m_conflictManager->workflowMode(), ConflictManager::WorkflowMode::Hybrid);

    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    QCOMPARE(m_conflictManager->workflowMode(), ConflictManager::WorkflowMode::AutoResolve);
}

void TestConflictManager::testSetAutoResolutionPolicy()
{
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::TargetWins);
    QCOMPARE(m_conflictManager->autoResolutionPolicy(), ConflictResolution::TargetWins);

    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::LastWriteWins);
    QCOMPARE(m_conflictManager->autoResolutionPolicy(), ConflictResolution::LastWriteWins);
}

void TestConflictManager::testSetHybridThreshold()
{
    m_conflictManager->setHybridThreshold(5);
    QCOMPARE(m_conflictManager->hybridThreshold(), 5);

    // Should clamp to minimum of 1
    m_conflictManager->setHybridThreshold(0);
    QCOMPARE(m_conflictManager->hybridThreshold(), 1);
}

// ============================================================================
// Immediate mode tests with mock resolver
// ============================================================================

void TestConflictManager::testImmediateModeSourceWins()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::SourceWins;

    ConflictInfo conflict = createTestConflict();
    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    QCOMPARE(result, ConflictResolution::SourceWins);
    QCOMPARE(m_mockResolver->m_callCount, 1);
}

void TestConflictManager::testImmediateModeTargetWins()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::TargetWins;

    ConflictInfo conflict = createTestConflict();
    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    QCOMPARE(result, ConflictResolution::TargetWins);
    QCOMPARE(m_mockResolver->m_callCount, 1);
}

void TestConflictManager::testImmediateModeSkip()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::Skip;

    ConflictInfo conflict = createTestConflict();
    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    QCOMPARE(result, ConflictResolution::Skip);
    QCOMPARE(m_mockResolver->m_callCount, 1);

    // Skip should NOT mark the conflict as resolved
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 1);
}

void TestConflictManager::testImmediateModeKeepBoth()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::Duplicate;

    ConflictInfo conflict = createTestConflict();
    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    QCOMPARE(result, ConflictResolution::Duplicate);
    QCOMPARE(m_mockResolver->m_callCount, 1);
}

// ============================================================================
// Deferred mode tests
// ============================================================================

void TestConflictManager::testDeferredModeQueuesConflict()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    ConflictInfo conflict = createTestConflict();
    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    // Deferred returns AskUser to indicate it was queued
    QCOMPARE(result, ConflictResolution::AskUser);

    // Mock resolver should NOT have been called
    QCOMPARE(m_mockResolver->m_callCount, 0);

    // Conflict should be in the store as unresolved
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 1);
}

void TestConflictManager::testDeferredModeEmitsSignal()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    QSignalSpy queuedSpy(m_conflictManager, &ConflictManager::conflictQueued);

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);

    QCOMPARE(queuedSpy.count(), 1);
}

// ============================================================================
// Hybrid mode tests
// ============================================================================

void TestConflictManager::testHybridModeFewConflicts()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Hybrid);
    m_conflictManager->setHybridThreshold(3);
    m_mockResolver->m_resolution = ConflictResolution::SourceWins;

    QSignalSpy resolvedSpy(m_conflictManager, &ConflictManager::conflictResolved);

    // Create fewer conflicts than threshold
    QList<ConflictInfo> conflicts;
    conflicts << createTestConflict();
    conflicts << createTestConflict();

    m_conflictManager->handleConflicts(conflicts);

    // Should have used immediate mode (called resolver)
    QCOMPARE(m_mockResolver->m_callCount, 2);
    // Both conflicts should have been resolved (via signal)
    QCOMPARE(resolvedSpy.count(), 2);
    // No unresolved conflicts left
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 0);
}

void TestConflictManager::testHybridModeManyConflicts()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Hybrid);
    m_conflictManager->setHybridThreshold(2);

    // Create more conflicts than threshold
    QList<ConflictInfo> conflicts;
    conflicts << createTestConflict();
    conflicts << createTestConflict();
    conflicts << createTestConflict();
    conflicts << createTestConflict();

    auto results = m_conflictManager->handleConflicts(conflicts);

    // Should have used deferred mode (NOT called resolver)
    QCOMPARE(m_mockResolver->m_callCount, 0);
    QCOMPARE(results.size(), 0);  // Deferred returns empty map

    // All conflicts should be queued
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 4);
}

// ============================================================================
// Auto-resolve mode tests
// ============================================================================

void TestConflictManager::testAutoResolveSourceWins()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::SourceWins);

    ConflictInfo conflict = createTestConflict();
    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    QCOMPARE(result, ConflictResolution::SourceWins);

    // Auto-resolve should NOT call the dialog resolver
    QCOMPARE(m_mockResolver->m_callCount, 0);

    // Conflict should be recorded and immediately resolved
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 0);
}

void TestConflictManager::testAutoResolveTargetWins()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::TargetWins);

    ConflictInfo conflict = createTestConflict();
    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    QCOMPARE(result, ConflictResolution::TargetWins);
}

void TestConflictManager::testAutoResolveLastWriteWins()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::AutoResolve);
    m_conflictManager->setAutoResolutionPolicy(ConflictResolution::LastWriteWins);

    // Create conflict where target is more recent
    ConflictInfo conflict = createTestConflict();
    conflict.sourceModified = QDateTime::currentDateTime().addSecs(-3600);  // 1 hour ago
    conflict.targetModified = QDateTime::currentDateTime().addSecs(-60);    // 1 min ago

    ConflictResolution result = m_conflictManager->handleConflict(conflict);

    // Target is more recent, so should become TargetWins
    QCOMPARE(result, ConflictResolution::TargetWins);

    // Now test with source more recent
    m_mockResolver->reset();
    conflict.sourceModified = QDateTime::currentDateTime().addSecs(-60);    // 1 min ago
    conflict.targetModified = QDateTime::currentDateTime().addSecs(-3600);  // 1 hour ago

    result = m_conflictManager->handleConflict(conflict);
    QCOMPARE(result, ConflictResolution::SourceWins);
}

// ============================================================================
// Multiple conflicts tests
// ============================================================================

void TestConflictManager::testHandleMultipleConflictsImmediate()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::SourceWins;

    QSignalSpy resolvedSpy(m_conflictManager, &ConflictManager::conflictResolved);

    QList<ConflictInfo> conflicts;
    conflicts << createTestConflict();
    conflicts << createTestConflict();
    conflicts << createTestConflict();

    m_conflictManager->handleConflicts(conflicts);

    QCOMPARE(m_mockResolver->m_callCount, 3);
    // All should have been resolved (via signal)
    QCOMPARE(resolvedSpy.count(), 3);
    // No unresolved conflicts left
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 0);

    // All should be resolved as SourceWins (check signals)
    for (int i = 0; i < resolvedSpy.count(); ++i) {
        QList<QVariant> args = resolvedSpy.at(i);
        QCOMPARE(args.at(1).value<ConflictResolution>(), ConflictResolution::SourceWins);
    }
}

void TestConflictManager::testHandleMultipleConflictsSkipQueueRemaining()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);

    // Queue resolutions: SourceWins, Skip, then remaining should be deferred
    m_mockResolver->m_queuedResolutions << ConflictResolution::SourceWins
                                        << ConflictResolution::Skip;

    QSignalSpy resolvedSpy(m_conflictManager, &ConflictManager::conflictResolved);
    QSignalSpy queuedSpy(m_conflictManager, &ConflictManager::conflictQueued);

    QList<ConflictInfo> conflicts;
    conflicts << createTestConflict();
    conflicts << createTestConflict();
    conflicts << createTestConflict();
    conflicts << createTestConflict();

    m_conflictManager->handleConflicts(conflicts);

    // Should have called resolver only twice (stopped at Skip)
    QCOMPARE(m_mockResolver->m_callCount, 2);

    // First conflict resolved (SourceWins)
    QCOMPARE(resolvedSpy.count(), 1);

    // Second skipped + 2 queued = 3 conflicts queued
    QCOMPARE(queuedSpy.count(), 2);  // Only the 2 remaining after skip were queued

    // Remaining 3 conflicts should be unresolved (1 skipped + 2 queued)
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 3);
}

// ============================================================================
// Signal emission tests
// ============================================================================

void TestConflictManager::testConflictResolvedSignal()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::SourceWins;

    QSignalSpy resolvedSpy(m_conflictManager, &ConflictManager::conflictResolved);

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);

    QCOMPARE(resolvedSpy.count(), 1);

    QList<QVariant> args = resolvedSpy.takeFirst();
    QVERIFY(!args.at(0).toString().isEmpty());  // conflictId
    QCOMPARE(args.at(1).value<ConflictResolution>(), ConflictResolution::SourceWins);
}

void TestConflictManager::testConflictQueuedSignal()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    QSignalSpy queuedSpy(m_conflictManager, &ConflictManager::conflictQueued);

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);

    QCOMPARE(queuedSpy.count(), 1);

    QList<QVariant> args = queuedSpy.takeFirst();
    ConflictInfo queuedConflict = args.at(0).value<ConflictInfo>();
    QVERIFY(!queuedConflict.conflictId.isEmpty());
}

void TestConflictManager::testUnresolvedCountChangedSignal()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    QSignalSpy countSpy(m_conflictManager, &ConflictManager::unresolvedCountChanged);

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);

    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(countSpy.takeFirst().at(0).toInt(), 1);

    m_conflictManager->handleConflict(createTestConflict());
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(countSpy.takeFirst().at(0).toInt(), 2);
}

// ============================================================================
// SyncConflictStore integration tests
// ============================================================================

void TestConflictManager::testConflictRecordedInStore()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);

    // Conflict should have been recorded (and unresolved)
    auto unresolvedConflicts = m_syncStore->unresolvedConflicts();
    QCOMPARE(unresolvedConflicts.size(), 1);
}

void TestConflictManager::testConflictResolvedInStore()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::TargetWins;

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);

    // Should be no unresolved conflicts (it was resolved)
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 0);
}

void TestConflictManager::testUnresolvedConflictCount()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    QCOMPARE(m_conflictManager->unresolvedConflictCount(), 0);

    m_conflictManager->handleConflict(createTestConflict());
    QCOMPARE(m_conflictManager->unresolvedConflictCount(), 1);

    m_conflictManager->handleConflict(createTestConflict());
    QCOMPARE(m_conflictManager->unresolvedConflictCount(), 2);
}

void TestConflictManager::testRepresentingSameConflictDoesNotDuplicateRow()
{
    // docs/bugs/sync-conflict-store-duplicate-rows.md: the engine re-presents
    // an unresolved conflict every sync cycle with a fresh ConflictInfo
    // (conflictId always empty) for the same (mappingId, sourceId) identity.
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 1);

    m_conflictManager->handleConflict(conflict);
    m_conflictManager->handleConflict(conflict);
    QCOMPARE(m_syncStore->unresolvedConflictCount(), 1);
}

void TestConflictManager::testRepresentingSameConflictPopulatesIcalColumns()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Deferred);

    ConflictInfo conflict = createTestConflict();
    m_conflictManager->handleConflict(conflict);

    ConflictInfo updated = conflict;
    updated.sourceIcalData = QStringLiteral("BEGIN:VEVENT\nSUMMARY:Source v2\nEND:VEVENT");
    updated.targetIcalData = QStringLiteral("BEGIN:VEVENT\nSUMMARY:Target v2\nEND:VEVENT");
    m_conflictManager->handleConflict(updated);

    const auto unresolved = m_syncStore->unresolvedConflicts(conflict.mappingId);
    QCOMPARE(unresolved.size(), 1);
    QCOMPARE(unresolved.first().sourceIcalData, updated.sourceIcalData);
    QCOMPARE(unresolved.first().targetIcalData, updated.targetIcalData);
}

// ============================================================================
// Display name field tests
// ============================================================================

void TestConflictManager::testDisplayNameFieldsPassedToResolver()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::SourceWins;

    ConflictInfo conflict = createTestConflict();
    conflict.sourceBackendDisplayName = QStringLiteral("Local Storage");
    conflict.targetBackendDisplayName = QStringLiteral("CalDAV Server");

    m_conflictManager->handleConflict(conflict);

    // Verify the mock resolver received the display names
    QCOMPARE(m_mockResolver->m_callCount, 1);
    QCOMPARE(m_mockResolver->m_lastConflict.sourceBackendDisplayName,
             QStringLiteral("Local Storage"));
    QCOMPARE(m_mockResolver->m_lastConflict.targetBackendDisplayName,
             QStringLiteral("CalDAV Server"));
}

void TestConflictManager::testDisplayNameFieldsFallbackToBackendId()
{
    m_conflictManager->setWorkflowMode(ConflictManager::WorkflowMode::Immediate);
    m_mockResolver->m_resolution = ConflictResolution::SourceWins;

    ConflictInfo conflict = createTestConflict();
    // Leave display names empty — the UI should fall back to backend IDs
    QVERIFY(conflict.sourceBackendDisplayName.isEmpty());
    QVERIFY(conflict.targetBackendDisplayName.isEmpty());
    QVERIFY(!conflict.sourceBackendId.isEmpty());
    QVERIFY(!conflict.targetBackendId.isEmpty());

    m_conflictManager->handleConflict(conflict);

    // Verify the raw backend IDs are still there
    QCOMPARE(m_mockResolver->m_lastConflict.sourceBackendId,
             QStringLiteral("local-backend"));
    QCOMPARE(m_mockResolver->m_lastConflict.targetBackendId,
             QStringLiteral("remote-backend"));
    // Display names should remain empty (resolution happens in SyncEngine, not ConflictManager)
    QVERIFY(m_mockResolver->m_lastConflict.sourceBackendDisplayName.isEmpty());
}

QTEST_MAIN(TestConflictManager)
#include "tst_conflictmanager.moc"

