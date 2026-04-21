#ifndef SYNCTRANSACTIONITEM_H
#define SYNCTRANSACTIONITEM_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

class SyncBackend;

/**
 * @brief Abstract base for individual sync operations with rollback capability.
 *
 * SyncTransactionItem represents a single atomic operation (create, update, delete)
 * within a SyncTransaction. Each item follows a three-phase lifecycle:
 *
 * 1. **Simulate** (async): Dry-run to detect conflicts before commitment
 * 2. **Commit** (sync): Apply the change to the backend
 * 3. **Rollback** (sync): Undo the change if transaction fails
 *
 * Subclasses implement the specific logic for each operation type:
 * - CreateIncidenceItem: Create a new incidence
 * - UpdateIncidenceItem: Update an existing incidence
 * - DeleteIncidenceItem: Delete an incidence
 *
 * Design note: simulate() is async to allow pre-flight checks against remote
 * backends (e.g., checking if UID already exists on CalDAV server).
 *
 * @see SyncTransaction for batch management
 */
class SyncTransactionItem : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Type of operation this item represents.
     */
    enum class ItemType {
        Create,     ///< Creating a new incidence
        Update,     ///< Updating an existing incidence
        Delete      ///< Deleting an incidence
    };
    Q_ENUM(ItemType)

    /**
     * @brief Result of simulation phase.
     */
    enum class SimulationResult {
        Pending,    ///< Simulation not yet started or in progress
        Success,    ///< No conflicts detected, safe to commit
        Conflict,   ///< Conflict detected (e.g., UID exists, concurrent modification)
        Error       ///< Error during simulation (e.g., network failure)
    };
    Q_ENUM(SimulationResult)

    /**
     * @brief Construct a transaction item.
     *
     * @param calendarId Calendar this operation targets
     * @param uid UID of the incidence being operated on
     * @param type Type of operation (Create/Update/Delete)
     * @param parent Parent QObject
     */
    explicit SyncTransactionItem(const QString &calendarId,
                                  const QString &uid,
                                  ItemType type,
                                  QObject *parent = nullptr);

    ~SyncTransactionItem() override;

    // ========== Core Transaction Lifecycle ==========

    /**
     * @brief Simulate the operation (async).
     *
     * Performs a dry-run to detect conflicts without making changes.
     * Emits simulationFinished() when complete.
     *
     * Subclasses should:
     * - Check preconditions (e.g., item exists/doesn't exist)
     * - Detect conflicts (e.g., concurrent modification)
     * - Call setSimulationResult() and emit simulationFinished()
     *
     * @note This is async - connect to simulationFinished() to get results.
     */
    virtual void simulate() = 0;

    /**
     * @brief Commit the operation (sync).
     *
     * Applies the change to the backend. Only call after successful simulation.
     *
     * @return true if commit succeeded, false otherwise
     */
    virtual bool commit() = 0;

    /**
     * @brief Rollback the operation (sync).
     *
     * Undoes the change made by commit(). Only call if isCommitted() is true.
     *
     * @return true if rollback succeeded, false otherwise
     */
    virtual bool rollback() = 0;

    // ========== Metadata ==========

    /**
     * @brief Human-readable description of this operation.
     *
     * Used for logging and user-facing messages.
     * Example: "Create event: Team Meeting (work calendar)"
     */
    virtual QString description() const = 0;

    /**
     * @brief Serialize to JSON for persistence.
     *
     * Used by CalendarJournal for crash recovery.
     * Subclasses should call base implementation and add their data.
     */
    virtual QJsonObject toJson() const;

    // ========== Accessors ==========

    QString calendarId() const { return m_calendarId; }
    QString uid() const { return m_uid; }
    ItemType type() const { return m_type; }
    bool isCommitted() const { return m_committed; }
    QString errorString() const { return m_errorString; }
    SimulationResult simulationResult() const { return m_simulationResult; }

    /**
     * @brief Get the backend this item operates on.
     */
    SyncBackend* backend() const { return m_backend; }

    /**
     * @brief Set the backend for this item.
     */
    void setBackend(SyncBackend *backend) { m_backend = backend; }

    /**
     * @brief Helper to convert ItemType to string for JSON.
     */
    static QString itemTypeToString(ItemType type);

    /**
     * @brief Helper to convert string to ItemType.
     */
    static ItemType stringToItemType(const QString &str);

signals:
    /**
     * @brief Emitted when async simulation completes.
     *
     * @param result The simulation result
     */
    void simulationFinished(SyncTransactionItem::SimulationResult result);

    /**
     * @brief Emitted when a conflict is detected during simulation.
     *
     * @param description Human-readable description of the conflict
     */
    void conflictDetected(const QString &description);

    /**
     * @brief Emitted when an error occurs.
     *
     * @param error Human-readable error message
     */
    void errorOccurred(const QString &error);

protected:
    /**
     * @brief Mark the item as committed (call after successful commit).
     */
    void setCommitted(bool committed);

    /**
     * @brief Set the error string (call before returning false from commit/rollback).
     */
    void setErrorString(const QString &error);

    /**
     * @brief Set the simulation result (call before emitting simulationFinished).
     */
    void setSimulationResult(SimulationResult result);

private:
    QString m_calendarId;
    QString m_uid;
    ItemType m_type;
    SyncBackend *m_backend = nullptr;
    bool m_committed = false;
    QString m_errorString;
    SimulationResult m_simulationResult = SimulationResult::Pending;
};

} // namespace Kalburator::Sync

#endif // SYNCTRANSACTIONITEM_H
