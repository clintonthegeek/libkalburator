#include "synctransaction.h"
#include "synctesthooks.h"
#include <QDebug>

namespace Kalburator::Sync {

SyncTransaction::SyncTransaction(const QString &transactionId,
                                 QObject *parent)
    : QObject(parent)
    , m_transactionId(transactionId)
{
}

SyncTransaction::~SyncTransaction()
{
    // Clean up owned items
    qDeleteAll(m_items);
    m_items.clear();
}

void SyncTransaction::addItem(SyncTransactionItem *item)
{
    if (!item) {
        qWarning() << "SyncTransaction::addItem: Attempted to add null item";
        return;
    }

    item->setParent(this);  // Take ownership
    m_items.append(item);
}

void SyncTransaction::setConflictPolicy(ConflictPolicy policy)
{
    m_conflictPolicy = policy;
}

void SyncTransaction::setSequential(bool sequential)
{
    m_sequential = sequential;
}

QStringList SyncTransaction::conflictDescriptions() const
{
    QStringList descriptions;
    for (const SyncTransactionItem *item : m_conflictingItems) {
        descriptions.append(item->description());
    }
    return descriptions;
}

// -----------------------------------------------------------------------------
// Simulation
// -----------------------------------------------------------------------------

void SyncTransaction::simulateAll()
{
    emit simulationStarted();
    m_conflictingItems.clear();
    m_simulatedCount = 0;
    m_isSimulating = true;

    // Test hook: transaction start
    SYNC_HOOK_CALL(onTransactionStart, m_transactionId);
#ifdef PLANSTAN_TESTING
    SyncTestHooks::instance().transactionCount++;
    SyncTestHooks::instance().lastTransactionId = m_transactionId;
#endif

    if (m_items.isEmpty()) {
        m_isSimulating = false;
        emit simulationCompleted(true);
        SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, true);
        return;
    }

    // Start first simulation (others chained via onItemSimulationFinished)
    processNextSimulation();
}

void SyncTransaction::processNextSimulation()
{
    if (m_simulatedCount >= m_items.size()) {
        // All done
        m_isSimulating = false;
        bool success = m_conflictingItems.isEmpty();

        if (!success && m_conflictPolicy == ConflictPolicy::AskUser) {
            emit userDecisionRequired(m_conflictingItems);
        }

        emit simulationCompleted(success);
        return;
    }

    SyncTransactionItem *item = m_items[m_simulatedCount];

    // Test hook: before item simulate
    SYNC_HOOK_CALL(onBeforeItemSimulate, m_transactionId, m_simulatedCount);

    // Check if test hook wants to force failure
    if (SYNC_HOOK_CHECK_FAIL(shouldFailSimulate, m_transactionId, m_simulatedCount)) {
        // Simulate failure
        m_simulatedCount++;
        emit simulationProgress(m_simulatedCount, m_items.size());
        SYNC_HOOK_CALL(onAfterItemSimulate, m_transactionId, m_simulatedCount - 1, 3);  // 3 = Error
        processNextSimulation();
        return;
    }

    // Connect to get notified when simulation finishes
    connect(item, &SyncTransactionItem::simulationFinished,
            this, &SyncTransaction::onItemSimulationFinished);

    // Also forward conflict detection signal
    connect(item, &SyncTransactionItem::conflictDetected,
            this, [this, item](const QString &description) {
                emit conflictDetected(item, description);
            });

    item->simulate();  // Async call
}

void SyncTransaction::onItemSimulationFinished(SyncTransactionItem::SimulationResult result)
{
    SyncTransactionItem *item = qobject_cast<SyncTransactionItem*>(sender());
    if (!item) {
        qWarning() << "SyncTransaction::onItemSimulationFinished: sender is not a SyncTransactionItem";
        return;
    }

    // Disconnect signals for this item
    disconnect(item, &SyncTransactionItem::simulationFinished,
               this, &SyncTransaction::onItemSimulationFinished);

    // Test hook: after item simulate
    SYNC_HOOK_CALL(onAfterItemSimulate, m_transactionId, m_simulatedCount, static_cast<int>(result));

    if (result == SyncTransactionItem::SimulationResult::Conflict) {
        m_conflictingItems.append(item);
#ifdef PLANSTAN_TESTING
        SyncTestHooks::instance().conflictCount++;
#endif
    } else if (result == SyncTransactionItem::SimulationResult::Error) {
        // Log the error but continue with other simulations
        qWarning() << "SyncTransaction: Simulation error for item:" << item->description()
                   << "-" << item->errorString();
        emit errorOccurred(tr("Simulation failed for: %1 - %2")
                          .arg(item->description(), item->errorString()));
#ifdef PLANSTAN_TESTING
        SyncTestHooks::instance().lastErrorMessage = item->errorString();
#endif
    }

    m_simulatedCount++;
    emit simulationProgress(m_simulatedCount, m_items.size());

    // Process next item (sequential)
    processNextSimulation();
}

// -----------------------------------------------------------------------------
// Commit
// -----------------------------------------------------------------------------

bool SyncTransaction::commitAll()
{
    emit commitStarted();
    m_committedCount = 0;
    m_committedItems.clear();

    for (int i = 0; i < m_items.size(); ++i) {
        SyncTransactionItem *item = m_items[i];

        // Check conflict policy
        if (m_conflictingItems.contains(item)) {
            if (m_conflictPolicy == ConflictPolicy::AbortOnConflict) {
                emit errorOccurred(tr("Aborting transaction due to conflict: %1")
                                  .arg(item->description()));
                rollbackCommitted();
                emit commitCompleted(false);
                SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, false);
                return false;
            } else if (m_conflictPolicy == ConflictPolicy::SkipConflicting) {
                // Skip this item
                continue;
            }
            // AskUser: assume user already decided to proceed
        }

        // Skip items that had simulation errors
        if (item->simulationResult() == SyncTransactionItem::SimulationResult::Error) {
            qWarning() << "SyncTransaction: Skipping item with simulation error:" << item->description();
            continue;
        }

        // Test hook: before item commit
        SYNC_HOOK_CALL(onBeforeItemCommit, m_transactionId, i);

        // Check if test hook wants to force failure
        if (SYNC_HOOK_CHECK_FAIL(shouldFailCommit, m_transactionId, i)) {
            emit errorOccurred(tr("Commit failed (test hook) for: %1").arg(item->description()));
            SYNC_HOOK_CALL(onAfterItemCommit, m_transactionId, i, false);
            rollbackCommitted();
            emit commitCompleted(false);
            SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, false);
            return false;
        }

        if (!item->commit()) {
            emit errorOccurred(tr("Commit failed for: %1 - %2")
                              .arg(item->description(), item->errorString()));
            SYNC_HOOK_CALL(onAfterItemCommit, m_transactionId, i, false);
#ifdef PLANSTAN_TESTING
            SyncTestHooks::instance().lastErrorMessage = item->errorString();
#endif
            rollbackCommitted();
            emit commitCompleted(false);
            SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, false);
            return false;
        }

        // Test hook: after item commit (success)
        SYNC_HOOK_CALL(onAfterItemCommit, m_transactionId, i, true);
#ifdef PLANSTAN_TESTING
        SyncTestHooks::instance().commitCount++;
#endif

        m_committedItems.append(item);
        m_committedCount++;
        emit commitProgress(m_committedCount, m_items.size());
    }

    emit commitCompleted(true);
    SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, true);
    return true;
}

bool SyncTransaction::commitNonConflicting()
{
    emit commitStarted();
    m_committedCount = 0;
    m_committedItems.clear();

    int nonConflictingCount = m_items.size() - m_conflictingItems.size();

    for (int i = 0; i < m_items.size(); ++i) {
        SyncTransactionItem *item = m_items[i];

        // Skip conflicting items
        if (m_conflictingItems.contains(item)) {
            continue;
        }

        // Skip items that had simulation errors
        if (item->simulationResult() == SyncTransactionItem::SimulationResult::Error) {
            qWarning() << "SyncTransaction: Skipping item with simulation error:" << item->description();
            continue;
        }

        // Test hook: before item commit
        SYNC_HOOK_CALL(onBeforeItemCommit, m_transactionId, i);

        // Check if test hook wants to force failure
        if (SYNC_HOOK_CHECK_FAIL(shouldFailCommit, m_transactionId, i)) {
            emit errorOccurred(tr("Commit failed (test hook) for: %1").arg(item->description()));
            SYNC_HOOK_CALL(onAfterItemCommit, m_transactionId, i, false);
            rollbackCommitted();
            emit commitCompleted(false);
            SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, false);
            return false;
        }

        if (!item->commit()) {
            emit errorOccurred(tr("Commit failed for: %1 - %2")
                              .arg(item->description(), item->errorString()));
            SYNC_HOOK_CALL(onAfterItemCommit, m_transactionId, i, false);
            rollbackCommitted();
            emit commitCompleted(false);
            SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, false);
            return false;
        }

        // Test hook: after item commit (success)
        SYNC_HOOK_CALL(onAfterItemCommit, m_transactionId, i, true);
#ifdef PLANSTAN_TESTING
        SyncTestHooks::instance().commitCount++;
#endif

        m_committedItems.append(item);
        m_committedCount++;
        emit commitProgress(m_committedCount, nonConflictingCount);
    }

    emit commitCompleted(true);
    SYNC_HOOK_CALL(onTransactionEnd, m_transactionId, true);
    return true;
}

// -----------------------------------------------------------------------------
// Rollback
// -----------------------------------------------------------------------------

bool SyncTransaction::rollbackAll()
{
    emit rollbackStarted();

    bool allSucceeded = true;

    // Rollback ALL items, not just committed ones
    // (useful for manually triggering full rollback)
    for (int i = m_items.size() - 1; i >= 0; --i) {
        SyncTransactionItem *item = m_items[i];

        if (!item->isCommitted()) {
            continue;  // Nothing to rollback
        }

        if (!item->rollback()) {
            emit errorOccurred(tr("Rollback failed for: %1 - %2")
                              .arg(item->description(), item->errorString()));
            allSucceeded = false;
            // Continue trying to rollback remaining items
        }
    }

    m_committedCount = 0;
    m_committedItems.clear();

    emit rollbackCompleted(allSucceeded);
    return allSucceeded;
}

void SyncTransaction::rollbackCommitted()
{
    emit rollbackStarted();

    // Rollback in reverse order of commit
    for (int i = m_committedItems.size() - 1; i >= 0; --i) {
        SyncTransactionItem *item = m_committedItems[i];

        if (!item->rollback()) {
            emit errorOccurred(tr("Rollback failed for: %1 - %2")
                              .arg(item->description(), item->errorString()));
            // Continue trying to rollback remaining items
        }
    }

    m_committedCount = 0;
    m_committedItems.clear();

    emit rollbackCompleted(true);
}


} // namespace Kalburator::Sync
