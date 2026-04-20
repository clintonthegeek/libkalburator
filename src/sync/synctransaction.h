/**
 * @file synctransaction.h
 * @brief Atomic transaction system for sync operations with rollback capability.
 *
 * Used by SyncWorker to apply multi-item changes to backends atomically.
 * Provides simulation phase for conflict detection and automatic rollback
 * on failure.
 *
 * See also: CreateIncidenceItem, UpdateIncidenceItem, DeleteIncidenceItem
 */

#ifndef SYNCTRANSACTION_H
#define SYNCTRANSACTION_H

#include "synctransactionitem.h"
#include <QObject>
#include <QList>

/**
 * @brief Manages a batch of SyncTransactionItem with atomic commit/rollback.
 *
 * SyncTransaction provides transaction semantics for sync operations:
 * 1. Add items via addItem()
 * 2. Call simulateAll() - async, emits simulationCompleted()
 * 3. Call commitAll() or commitNonConflicting() - sync, rolls back on failure
 *
 * Conflict handling is controlled by ConflictPolicy:
 * - AbortOnConflict: Abort entire transaction if any conflict detected
 * - SkipConflicting: Commit non-conflicting items, skip conflicts
 * - AskUser: Emit signal for user decision before proceeding
 *
 */
class SyncTransaction : public QObject
{
    Q_OBJECT

public:
    enum class ConflictPolicy {
        AbortOnConflict,    ///< Abort entire transaction if any conflict
        SkipConflicting,    ///< Commit non-conflicting items, skip conflicts
        AskUser             ///< Emit signal, wait for user decision
    };
    Q_ENUM(ConflictPolicy)

    /**
     * @brief Construct a new SyncTransaction.
     *
     * @param transactionId Unique identifier for this transaction
     * @param parent Parent QObject
     */
    explicit SyncTransaction(const QString &transactionId,
                             QObject *parent = nullptr);

    ~SyncTransaction() override;

    // -------------------------------------------------------------------------
    // Transaction lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Add a transaction item to this transaction.
     *
     * Takes ownership of the item.
     */
    void addItem(SyncTransactionItem *item);

    /**
     * @brief Start async simulation of all items.
     *
     * Items are simulated sequentially. Progress is reported via signals.
     * When complete, simulationCompleted(bool) is emitted.
     */
    void simulateAll();

    /**
     * @brief Commit all items (sync).
     *
     * If any commit fails, all previously committed items are rolled back.
     * Respects conflict policy for conflicting items.
     *
     * @return true if all (non-skipped) items committed successfully
     */
    bool commitAll();

    /**
     * @brief Commit only non-conflicting items (sync).
     *
     * Skips items that had conflicts during simulation.
     *
     * @return true if all non-conflicting items committed successfully
     */
    bool commitNonConflicting();

    /**
     * @brief Rollback all committed items (sync).
     *
     * Rolls back in reverse order.
     *
     * @return true if all rollbacks succeeded
     */
    bool rollbackAll();

    // -------------------------------------------------------------------------
    // Conflict policy
    // -------------------------------------------------------------------------

    void setConflictPolicy(ConflictPolicy policy);
    ConflictPolicy conflictPolicy() const { return m_conflictPolicy; }

    // -------------------------------------------------------------------------
    // State accessors
    // -------------------------------------------------------------------------

    QString transactionId() const { return m_transactionId; }
    int itemCount() const { return m_items.size(); }
    bool hasConflicts() const { return !m_conflictingItems.isEmpty(); }
    QList<SyncTransactionItem*> conflictingItems() const { return m_conflictingItems; }
    QStringList conflictDescriptions() const;
    bool isCommitted() const { return m_committedCount > 0; }
    bool isSimulating() const { return m_isSimulating; }

    /**
     * @brief Get all items in this transaction.
     */
    QList<SyncTransactionItem*> items() const { return m_items; }

    // -------------------------------------------------------------------------
    // Sequential vs concurrent execution
    // -------------------------------------------------------------------------

    /**
     * @brief Set whether to process items sequentially (default: true).
     *
     * Sequential processing is safer but slower.
     */
    void setSequential(bool sequential);
    bool isSequential() const { return m_sequential; }

signals:
    // Simulation signals
    void simulationStarted();
    void simulationProgress(int completed, int total);
    void simulationCompleted(bool success);

    // Commit signals
    void commitStarted();
    void commitProgress(int completed, int total);
    void commitCompleted(bool success);

    // Rollback signals
    void rollbackStarted();
    void rollbackCompleted(bool success);

    // Conflict handling
    void conflictDetected(SyncTransactionItem *item, const QString &description);
    void userDecisionRequired(const QList<SyncTransactionItem*> &conflicts);

    // Error handling
    void errorOccurred(const QString &error);

private slots:
    void onItemSimulationFinished(SyncTransactionItem::SimulationResult result);

private:
    void rollbackCommitted();
    void processNextSimulation();

    QString m_transactionId;
    QList<SyncTransactionItem*> m_items;
    QList<SyncTransactionItem*> m_conflictingItems;
    QList<SyncTransactionItem*> m_committedItems;  // Track for rollback order
    ConflictPolicy m_conflictPolicy = ConflictPolicy::AbortOnConflict;
    bool m_sequential = true;
    int m_committedCount = 0;
    int m_simulatedCount = 0;
    bool m_isSimulating = false;
};

#endif // SYNCTRANSACTION_H
