#ifndef CALENDARMANAGER_H
#define CALENDARMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

#include "logicalcalendar.h"

namespace Kalburator::Sync {

class ISyncHost;
class ISyncConfigStore;
class ICalendarCollection;
class SyncBackend;

/**
 * @brief Mode for calendar deletion operations.
 *
 * These modes allow fine-grained control over what happens to calendar data
 * when a calendar is removed from the UI.
 */
enum class DeleteMode {
    Hide,              ///< Keep calendar, just hide from UI (set visible=false)
    Disable,           ///< Unload from memory, keep in config (set enabled=false)
    DisconnectSync,    ///< Remove secondary bindings, keep primary
    Forget,            ///< Remove from config, keep backend data
    DeleteFromAll      ///< Delete from ALL backends (destructive)
};

/**
 * @brief Result of a calendar creation operation.
 */
struct CreationResult {
    bool success = false;
    QString logicalCalendarId;
    QStringList errors;                ///< Per-backend errors
    QStringList warnings;              ///< Data loss warnings from transcoding
    QMap<QString, bool> backendResults; ///< backendId -> success
};

/**
 * @brief Result of a calendar deletion operation.
 */
struct DeletionResult {
    bool success = false;
    QString logicalCalendarId;
    QStringList errors;
    QMap<QString, bool> backendResults; ///< backendId -> success
};

/**
 * @brief Snapshot of a calendar's state for undo/redo support.
 */
struct CalendarSnapshot {
    LogicalCalendar logicalCalendar;
    QList<KCalendarCore::Incidence::Ptr> incidences;
    QDateTime capturedAt;

    bool isValid() const { return !logicalCalendar.id.isEmpty(); }
};

/**
 * @brief Operation type for validation.
 */
enum class OperationType {
    Create,
    Update,
    Delete
};

/**
 * @brief Centralized calendar CRUD operations manager.
 *
 * CalendarManager provides IMMEDIATE, synchronous CRUD operations on ALL backends.
 * This replaces the "staged for sync" pattern with direct execution.
 *
 * Key design principles:
 * - IMMEDIATE execution: Operations complete before methods return
 * - ALL backends: Changes propagate to all enabled bindings
 * - Backend-neutral: No backend type checks; uses BackendCapabilities
 *
 * Usage:
 * @code
 * CalendarManager *mgr = controller->calendarManager();
 *
 * // Create calendar - executes immediately on all backends
 * LogicalCalendar cal;
 * cal.displayName = "Work";
 * // ... set up bindings ...
 * CreationResult result = mgr->createCalendar(cal);
 *
 * // Delete with mode selection
 * mgr->deleteCalendar(calId, DeleteMode::DeleteFromAll);
 * @endcode
 */
class CalendarManager : public QObject
{
    Q_OBJECT

public:
    explicit CalendarManager(ISyncHost *host,
                             ICalendarCollection *collection,
                             QObject *parent = nullptr);
    ~CalendarManager() override;

    // ========== IMMEDIATE Calendar CRUD ==========

    /**
     * @brief Create calendar on ALL bindings IMMEDIATELY (not staged).
     *
     * This method:
     * 1. Adds LogicalCalendar to config
     * 2. Creates calendar on EACH enabled backend binding
     * 3. Creates MemoryCalendar for in-memory storage
     * 4. Regenerates sync mappings
     * 5. Saves config
     *
     * @param logCal The LogicalCalendar with bindings configured
     * @return CreationResult with success status and any errors/warnings
     */
    CreationResult createCalendar(const LogicalCalendar &logCal);

    /**
     * @brief Update calendar properties on ALL backends IMMEDIATELY.
     *
     * @param logicalCalendarId The logical calendar to update
     * @param properties Properties to update (displayName, color, description, etc.)
     * @return true if all updates succeeded
     */
    bool updateCalendar(const QString &logicalCalendarId,
                        const QVariantMap &properties);

    /**
     * @brief Delete calendar based on mode IMMEDIATELY.
     *
     * @param logicalCalendarId The logical calendar to delete
     * @param mode How aggressively to delete (Hide, Disable, Forget, DeleteFromAll)
     * @return DeletionResult with success status and any errors
     */
    DeletionResult deleteCalendar(const QString &logicalCalendarId,
                                  DeleteMode mode);

    // ========== IMMEDIATE Binding CRUD ==========

    /**
     * @brief Add binding and create on backend IMMEDIATELY.
     *
     * @param logicalCalendarId The logical calendar to add binding to
     * @param binding The binding to add (with needsCreation set appropriately)
     * @return true if binding was added and backend creation succeeded
     */
    bool addBinding(const QString &logicalCalendarId,
                    const CalendarBackendBinding &binding);

    /**
     * @brief Remove binding and optionally delete from backend IMMEDIATELY.
     *
     * @param logicalCalendarId The logical calendar to remove binding from
     * @param backendId The backend ID of the binding to remove
     * @param deleteFromBackend If true, delete calendar from the backend too
     * @return true if binding was removed successfully
     */
    bool removeBinding(const QString &logicalCalendarId,
                       const QString &backendId,
                       bool deleteFromBackend = false);

    /**
     * @brief Update binding properties (role, enabled, metadata).
     *
     * @param logicalCalendarId The logical calendar containing the binding
     * @param backendId The backend ID of the binding to update
     * @param newBinding Updated binding data
     * @return true if binding was updated successfully
     */
    bool updateBinding(const QString &logicalCalendarId,
                       const QString &backendId,
                       const CalendarBackendBinding &newBinding);

    // ========== IMMEDIATE Incidence CRUD ==========

    /**
     * @brief Create incidence on ALL enabled bindings, asynchronously (E11 /
     * audit B7, FINDINGS O39).
     *
     * No longer blocks the caller's thread on the backend round-trip (the
     * old synchronous form spun a nested QEventLoop here — a GUI-thread
     * re-entrancy hazard). Dispatch is fire-and-forget; completion is
     * signal-driven: `incidenceCreated` fires once every enabled binding's
     * push has settled successfully, `operationFailed` fires (per binding)
     * on any failure. Connect to those signals instead of relying on a
     * return value.
     *
     * @param logicalCalendarId The logical calendar to create incidence in
     * @param incidence The incidence to create
     */
    void createIncidence(const QString &logicalCalendarId,
                         const KCalendarCore::Incidence::Ptr &incidence);

    /**
     * @brief Update incidence on ALL enabled bindings, asynchronously.
     * See createIncidence() for the async/signal-driven contract.
     *
     * @param logicalCalendarId The logical calendar containing the incidence
     * @param incidence The updated incidence
     */
    void updateIncidence(const QString &logicalCalendarId,
                         const KCalendarCore::Incidence::Ptr &incidence);

    /**
     * @brief Delete incidence from ALL enabled bindings, asynchronously.
     * See createIncidence() for the async/signal-driven contract.
     *
     * @param logicalCalendarId The logical calendar containing the incidence
     * @param uid The UID of the incidence to delete
     */
    void deleteIncidence(const QString &logicalCalendarId,
                         const QString &uid);

    // ========== Transcoding Integration ==========

    /**
     * @brief Validate operation against backend capabilities.
     *
     * Returns warnings about potential data loss if the incidence uses
     * features not supported by all target backends.
     *
     * @param logicalCalendarId The logical calendar
     * @param incidence The incidence to validate
     * @param op The operation type (Create, Update, Delete)
     * @return List of warning messages (empty if no issues)
     */
    QStringList validateOperation(const QString &logicalCalendarId,
                                  const KCalendarCore::Incidence::Ptr &incidence,
                                  OperationType op);

    // ========== Snapshot for Undo ==========

    /**
     * @brief Capture current state of a calendar for potential undo.
     *
     * @param logicalCalendarId The calendar to snapshot
     * @return CalendarSnapshot containing calendar config and all incidences
     */
    CalendarSnapshot captureSnapshot(const QString &logicalCalendarId) const;

    /**
     * @brief Restore calendar from a snapshot (for undo).
     *
     * @param snapshot The snapshot to restore from
     * @return true if restoration succeeded
     */
    bool restoreFromSnapshot(const CalendarSnapshot &snapshot);

    // ========== Accessors ==========

    ISyncHost* host() const { return m_controller; }
    ISyncConfigStore* configManager() const { return m_configManager; }
    void setCollection(ICalendarCollection *collection) { m_collection = collection; }

    // ========== Batch mode ==========

    /**
     * @brief Suppress regenerateSyncMappings() during a sequence of mutations.
     *
     * Nested guards are supported; the deferred regeneration runs when the
     * outermost guard is destroyed, and only if at least one operation during
     * the batch would have triggered regeneration.
     */
    class BatchGuard
    {
    public:
        explicit BatchGuard(CalendarManager *mgr) : m_mgr(mgr) { if (m_mgr) m_mgr->beginBatch(); }
        ~BatchGuard() { if (m_mgr) m_mgr->endBatch(); }
        BatchGuard(const BatchGuard &) = delete;
        BatchGuard &operator=(const BatchGuard &) = delete;
    private:
        CalendarManager *m_mgr;
    };

    void beginBatch();
    void endBatch();

signals:
    // Calendar lifecycle
    void calendarCreated(const QString &logicalCalendarId);
    void calendarUpdated(const QString &logicalCalendarId);
    void calendarDeleted(const QString &logicalCalendarId);

    // Binding lifecycle
    void bindingAdded(const QString &logicalCalendarId, const QString &backendId);
    void bindingRemoved(const QString &logicalCalendarId, const QString &backendId);
    void bindingUpdated(const QString &logicalCalendarId, const QString &backendId);

    // Incidence lifecycle
    void incidenceCreated(const QString &logicalCalendarId, const QString &uid);
    void incidenceUpdated(const QString &logicalCalendarId, const QString &uid);
    void incidenceDeleted(const QString &logicalCalendarId, const QString &uid);

    // Operation status
    void operationFailed(const QString &operation, const QString &error);
    void dataLossWarning(const QString &logicalCalendarId, const QStringList &warnings);

    // Progress (for batch operations)
    void operationProgress(const QString &operation, int current, int total);

    // G.9.a Task 67 (landed Phase G) — host callbacks decoupled from ISyncHost.
    void calendarUnloadRequested(const QString &calendarId);
    void syncMappingRegenerationRequested();

private:
    ISyncHost *m_controller;
    ISyncConfigStore *m_configManager;
    ICalendarCollection *m_collection = nullptr;

    int m_batchDepth = 0;
    bool m_regenPending = false;

    // ========== Internal Helpers ==========

    /**
     * @brief Execute an operation on a single backend.
     *
     * @param backendId The backend to execute on
     * @param calendarId The calendar ID on that backend
     * @param operation The operation to execute (receives SyncBackend pointer)
     * @return true if operation succeeded
     */
    bool executeOnBackend(const QString &backendId,
                          const QString &calendarId,
                          std::function<bool(SyncBackend*)> operation);

    /**
     * @brief Execute an operation on ALL enabled bindings of a logical calendar.
     *
     * This is the key pattern for IMMEDIATE multi-backend operations.
     *
     * @param logCal The logical calendar with bindings
     * @param operation The operation to execute (receives backend and binding)
     * @return true if all operations succeeded
     */
    bool executeOnAllBindings(const LogicalCalendar &logCal,
                              std::function<bool(SyncBackend*, const CalendarBackendBinding&)> operation);

    /**
     * @brief Clone an incidence for a target backend.
     *
     * Conversion is the backend/shape graph's responsibility; this method
     * now returns a plain clone of the incidence.
     *
     * @param sourceBackendType Unused (kept for ABI stability)
     * @param targetBackendType Unused (kept for ABI stability)
     * @param incidence The incidence to clone
     * @return Clone of the incidence
     */
    KCalendarCore::Incidence::Ptr transcodeForBackend(
        const QString &sourceBackendType,
        const QString &targetBackendType,
        const KCalendarCore::Incidence::Ptr &incidence);

    /**
     * @brief Get backend type string for a backend ID.
     */
    QString getBackendType(const QString &backendId) const;

    /**
     * @brief Ensure a MemoryCalendar exists for a calendar ID.
     */
    void ensureMemoryCalendar(const QString &calendarId, const QString &displayName);

    /**
     * @brief Regenerate sync mappings after calendar changes.
     */
    void regenerateSyncMappings();

    /**
     * @brief Process deferred calendar creations for bindings with needsCreation=true.
     */
    bool processNeedsCreation(const LogicalCalendar &logCal);
};

// Qt metatype declarations

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::DeleteMode)
Q_DECLARE_METATYPE(Kalburator::Sync::CreationResult)
Q_DECLARE_METATYPE(Kalburator::Sync::DeletionResult)
Q_DECLARE_METATYPE(Kalburator::Sync::CalendarSnapshot)
Q_DECLARE_METATYPE(Kalburator::Sync::OperationType)

#endif // CALENDARMANAGER_H
