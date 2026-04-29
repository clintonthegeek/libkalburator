#ifndef SYNCBACKEND_H
#define SYNCBACKEND_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QSharedPointer>
#include <QVariantMap>
#include <QColor>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/Recurrence>

#include "calendartype.h"   // CalendarType enum
#include "datadomain.h"    // DataDomain enum
#include "iblobbackend.h"  // IBlobBackend pure interface (Phase D Group 2)

namespace Kalburator::Sync {

struct BackendCapabilities;
struct CalendarBackendBinding;
struct DiscoveredCalendar;
class SyncOperation;
class FetchOperation;
class PushOperation;
class DeleteOperation;

/**
 * @brief Describes what recurrence features a backend supports.
 *
 * This allows the UI to detect potential data loss when syncing incidences
 * with complex recurrence patterns to backends with limited recurrence support.
 *
 * Example: org-mode only supports simple repeaters (+1d, +1w, +1m, +1y),
 * so a weekly RRULE with BYDAY=MO,WE,FR would lose the specific days.
 */
struct RecurrenceCapabilities
{
    // ========== Frequency Support ==========
    bool supportsDaily = true;
    bool supportsWeekly = true;
    bool supportsMonthly = true;
    bool supportsYearly = true;
    bool supportsHourly = false;
    bool supportsMinutely = false;
    bool supportsSecondly = false;

    // ========== By-Rule Support ==========
    // Most backends don't support these advanced patterns
    bool supportsByDay = false;      // BYDAY (e.g., MO,WE,FR)
    bool supportsByMonthDay = false; // BYMONTHDAY (e.g., 15,-1)
    bool supportsByYearDay = false;  // BYYEARDAY
    bool supportsByWeekNo = false;   // BYWEEKNO
    bool supportsByMonth = false;    // BYMONTH (e.g., 1,6,12)
    bool supportsBySetPos = false;   // BYSETPOS (e.g., -1 for "last")

    // ========== Rule Limits ==========
    bool supportsCount = false;      // RRULE COUNT= (repeat N times)
    bool supportsUntil = false;      // RRULE UNTIL= (repeat until date)
    int maxInterval = 0;             // 0 = unlimited, >0 = max interval value

    // ========== Multiple Rules & Exceptions ==========
    bool supportsMultipleRRules = false;  // Multiple RRULE per incidence
    bool supportsExRules = false;         // EXRULE (exclusion rules)
    bool supportsRDates = false;          // RDATE (additional dates)
    bool supportsExDates = false;         // EXDATE (exception dates)

    // ========== Backend-Specific Metadata ==========
    QString backendType;             // E.g., "orgmode", "local", "caldav"
    QString displayName;             // Human-readable name for UI

    /**
     * @brief Check if this backend supports a specific frequency.
     */
    bool supportsFrequency(KCalendarCore::RecurrenceRule::PeriodType type) const;

    /**
     * @brief Get a human-readable description of limitations.
     */
    QString limitationsDescription() const;
};

/**
 * @brief Describes what would be lost when saving an incidence to a backend.
 *
 * This struct is populated by analyzeRecurrenceLoss() and used by the UI
 * to inform users about potential data loss before syncing.
 */
struct RecurrenceLossInfo
{
    bool hasLoss = false;            // True if any information would be lost
    bool frequencyLost = false;      // Unsupported frequency (e.g., hourly)
    bool byRulesLost = false;        // By-rules not supported (BYDAY, etc.)
    bool countUntilLost = false;     // COUNT/UNTIL not supported
    bool multipleRulesLost = false;  // Multiple RRULEs collapsed to one
    bool exceptionsLost = false;     // EXRULE/EXDATE lost

    QStringList lostDetails;         // Human-readable descriptions of losses

    /**
     * @brief Get a summary suitable for display in a dialog.
     */
    QString summary() const;
};

/**
 * @brief Abstract base class for all sync backends.
 *
 * Backends handle storage and retrieval of calendar data from different
 * sources (local files, org-mode, CalDAV, etc.). The interface is designed
 * to support the future SyncRouter and qsynccore integration.
 */
class SyncBackend : public QObject, public IBlobBackend
{
    Q_OBJECT

public:
    explicit SyncBackend(QObject *parent = nullptr);
    virtual ~SyncBackend() = default;

    // ========== Core Backend Identity ==========

    /// Return a unique backend type string, e.g. "local", "orgmode", "caldav"
    virtual QString backendType() const = 0;

    /// Return the data domain this backend belongs to. Default: Calendar.
    virtual DataDomain dataDomain() const { return DataDomain::Calendar; }

    // ========== Calendar Discovery & Loading ==========

    /// Load calendar folders for a collection (emits calendarDiscovered for each)
    virtual void loadCalendars(const QString &collectionId) = 0;

    /// Load all incidences for the given calendar
    /// @param suppressSignals If true, don't emit itemLoaded/calendarLoaded signals
    ///        (used during sync to avoid UI updates while loading records for comparison)
    /// @deprecated Use fetchItems() instead for the new unified loading API
    [[deprecated("Use fetchItems() instead - loadItems() will be removed in a future version")]]
    virtual void loadItems(KCalendarCore::MemoryCalendar* cal, bool suppressSignals = false) = 0;

    // ========== Incidence CRUD Operations ==========

    /// Save calendar list (if applicable)
    virtual void storeCalendars(const QString &collectionId,
                                const QList<KCalendarCore::MemoryCalendar*> &calendars) = 0;

    /// Save multiple incidences into calendar
    virtual void storeItems(KCalendarCore::MemoryCalendar* cal,
                            const QList<KCalendarCore::Incidence::Ptr> &items) = 0;

    /// Update single incidence item in calendar with given iCal data
    virtual void updateItem(KCalendarCore::MemoryCalendar* cal,
                            const KCalendarCore::Incidence::Ptr &item,
                            const QString &icalData) = 0;

    /// Perform full sync with staged creations, updates, and deletions
    virtual void startSync(const QString &collectionId,
                           KCalendarCore::MemoryCalendar* calendar,
                           const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                           const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                           const QMap<QString, QString> &stagedDeletions) = 0;

    /// Remove an item by calendar ID and item UID
    virtual void removeItem(const QString &calId, const QString &itemUid) = 0;

    // ========== Operation-Based API (Preferred) ==========
    // These methods return trackable SyncOperation handles and work with
    // calendar IDs instead of raw pointers. This allows proper async lifecycle
    // management and prevents crashes from deleted calendars.

    /**
     * @brief Fetch all items from a calendar.
     *
     * Returns a FetchOperation that tracks the async fetch. When complete,
     * call fetchedItems() on the operation to get the results.
     *
     * Callers are responsible for:
     * - Connecting to finished() signal
     * - Looking up the calendar by ID when applying results
     * - Deleting the operation when done (or using deleteLater())
     *
     * @param calendarId The calendar ID to fetch from
     * @return FetchOperation* tracking the operation (caller owns)
     */
    virtual FetchOperation* fetchItems(const QString &calendarId);

    /**
     * @brief Push items to a calendar.
     *
     * Returns a PushOperation that tracks which items succeeded/failed.
     *
     * @param calendarId The calendar ID to push to
     * @param items The incidences to push
     * @return PushOperation* tracking the operation (caller owns)
     */
    virtual PushOperation* pushItems(const QString &calendarId,
                                     const QList<KCalendarCore::Incidence::Ptr> &items);

    /**
     * @brief Delete items from a calendar.
     *
     * @param calendarId The calendar ID to delete from
     * @param uids The UIDs of incidences to delete
     * @return DeleteOperation* tracking the operation (caller owns)
     */
    virtual DeleteOperation* deleteItems(const QString &calendarId,
                                         const QStringList &uids);

    // ========== Operation Tracking ==========

    /**
     * @brief Check if any operations are pending for this backend.
     */
    virtual bool hasPendingOperations() const;

    /**
     * @brief Check if operations are pending for a specific calendar.
     */
    virtual bool hasPendingOperationsFor(const QString &calendarId) const;

    /**
     * @brief Get all pending operations.
     */
    virtual QList<SyncOperation*> pendingOperations() const;

    /**
     * @brief Get pending operations for a specific calendar.
     */
    virtual QList<SyncOperation*> pendingOperationsFor(const QString &calendarId) const;

    /**
     * @brief Cancel all pending operations for a calendar.
     *
     * Waits for cancellation to complete before returning.
     */
    virtual void cancelOperationsFor(const QString &calendarId);

    /**
     * @brief Cancel all pending operations.
     */
    virtual void cancelAllOperations();

    // ========== Calendar-Level CRUD Operations ==========
    // These are required for SyncRouter to manage calendars across backends

    /// Returns true if backend supports calendar creation/modification
    virtual bool supportsCalendarCreation() const { return false; }

    /// Get the discovered CalendarType for a calendar (from server discovery)
    /// Returns Hybrid by default if not discovered or unknown
    virtual CalendarType discoveredCalendarType(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return CalendarType::Hybrid;
    }

    /// Get the discovered color for a calendar (from server discovery)
    /// Returns invalid color if not discovered
    virtual QColor discoveredColor(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QColor();
    }

    /// Get the discovered display name for a calendar (from server discovery)
    /// Returns empty string if not discovered
    virtual QString discoveredDisplayName(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QString();
    }

    /// Get whether the discovered calendar is writable (from server discovery)
    /// Returns true by default if not discovered or unknown
    virtual bool discoveredWritable(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return true;
    }

    /// Create a new calendar with the given ID and name
    /// @param collectionId The collection to add the calendar to
    /// @param calendarId The unique identifier for the new calendar
    /// @param name Display name for the calendar
    /// @param type Calendar type (Event, Todo, or Hybrid) - used for CalDAV component restrictions
    /// Returns true on success, false on failure
    virtual bool createCalendar(const QString &collectionId,
                                const QString &calendarId,
                                const QString &name,
                                CalendarType type = CalendarType::Hybrid) {
        Q_UNUSED(collectionId); Q_UNUSED(calendarId); Q_UNUSED(name); Q_UNUSED(type);
        return false;
    }

    /// Update calendar properties (color, description, etc.)
    /// Properties passed as QVariantMap with keys like "color", "description", "accessMode"
    /// Returns true on success, false on failure
    virtual bool updateCalendar(const QString &collectionId,
                                const QString &calendarId,
                                const QVariantMap &properties) {
        Q_UNUSED(collectionId); Q_UNUSED(calendarId); Q_UNUSED(properties);
        return false;
    }

    /// Rename a calendar (change its ID)
    /// Returns true on success, false on failure
    virtual bool renameCalendar(const QString &collectionId,
                                const QString &oldCalendarId,
                                const QString &newCalendarId) {
        Q_UNUSED(collectionId); Q_UNUSED(oldCalendarId); Q_UNUSED(newCalendarId);
        return false;
    }

    /// Delete a calendar by ID
    /// Returns true on success, false on failure
    virtual bool deleteCalendar(const QString &collectionId, const QString &calendarId) {
        Q_UNUSED(collectionId); Q_UNUSED(calendarId);
        return false;
    }

    // ========== Calendar Property Getters ==========
    // These methods retrieve current calendar properties (color, description)
    // for use during property sync. Unlike discoveredXxx() methods which only
    // return values from initial discovery, these fetch the actual current state.

    /**
     * @brief Get the current color of a calendar.
     *
     * Returns the calendar's current color property, fetching from the backend
     * if necessary. This may involve reading metadata files, PROPFIND requests,
     * or parsing file headers depending on the backend type.
     *
     * @param calendarId The calendar ID
     * @return Current calendar color, or invalid QColor if not set or not supported
     */
    virtual QColor calendarColor(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QColor();  // Invalid color = not set/supported
    }

    /**
     * @brief Get the current description of a calendar.
     *
     * Returns the calendar's current description property.
     *
     * @param calendarId The calendar ID
     * @return Current calendar description, or empty string if not set or not supported
     */
    virtual QString calendarDescription(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QString();  // Empty = not set/supported
    }

    // ========== Binding Metadata Support ==========
    // These methods support LogicalCalendarBuilder's metadata handling

    /**
     * @brief Get the metadata keys this backend expects in bindings.
     *
     * Used by LogicalCalendarBuilder to prepare binding metadata.
     *
     * Example returns:
     * - CalDAV: {"davUrl", "etag"}
     * - OrgMode: {"filePath", "headline"}
     * - Local: {"directory"}
     *
     * @return List of metadata key names
     */
    virtual QStringList bindingMetadataKeys() const { return {}; }

    /**
     * @brief Populate binding metadata from a discovered calendar.
     *
     * Called by LogicalCalendarBuilder when creating binding from discovery.
     * Backend can copy/transform metadata as needed.
     *
     * Default implementation copies the metadata map directly.
     *
     * @param discovered Source discovered calendar
     * @param binding Target binding (modify metadata in place)
     */
    virtual void populateBindingMetadata(
        const DiscoveredCalendar &discovered,
        CalendarBackendBinding &binding) const;

    /**
     * @brief Prepare metadata for a pending calendar creation.
     *
     * Called when needsCreation=true. Backend can set default values
     * or compute derived values (e.g., URL from base + calendarId).
     *
     * @param calendarId The calendar ID to create
     * @param binding Target binding (modify metadata in place)
     */
    virtual void prepareCreationMetadata(
        const QString &calendarId,
        CalendarBackendBinding &binding) const;

    // ========== Source File Access ==========

    /**
     * @brief Get the on-disk file path for a calendar, if file-based.
     *
     * Returns the source file path for backends that store data as files
     * (e.g., org-mode .org files, local .ics files). Returns empty string
     * for network-based backends (CalDAV, etc.).
     *
     * @param calendarId The calendar ID
     * @return File path or empty string if not file-based
     */
    virtual QString sourceFilePath(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return {};
    }

    // ========== Debug/Raw ICS Access ==========

    /**
     * @brief Get the raw .ics content for an incidence.
     *
     * Used by debug features to allow direct editing of the underlying
     * iCalendar data. Returns empty string if not supported or not found.
     *
     * @param calendarId The calendar containing the incidence
     * @param uid The UID of the incidence
     * @return Raw .ics file content, or empty string on failure
     */
    virtual QString getRawIcs(const QString &calendarId, const QString &uid) const {
        Q_UNUSED(calendarId); Q_UNUSED(uid);
        return QString();
    }

    /**
     * @brief Set the raw .ics content for an incidence.
     *
     * Used by debug features to allow direct editing of the underlying
     * iCalendar data. This bypasses normal validation and should only
     * be used for testing/debugging.
     *
     * @param calendarId The calendar containing the incidence
     * @param uid The UID of the incidence
     * @param icsContent The new raw .ics content
     * @return true if successfully written, false on failure
     */
    virtual bool setRawIcs(const QString &calendarId, const QString &uid,
                           const QString &icsContent) {
        Q_UNUSED(calendarId); Q_UNUSED(uid); Q_UNUSED(icsContent);
        return false;
    }

    // ========== Backend Capabilities ==========

    /**
     * @brief Get comprehensive capabilities of this backend.
     *
     * Returns a BackendCapabilities struct describing all capabilities:
     * incidence types, recurrence, properties, structural features,
     * calendar CRUD, and sync characteristics.
     *
     * Subclasses should override this to describe their capabilities.
     * The default implementation returns full iCalendar capability.
     *
     * @return BackendCapabilities describing this backend
     */
    virtual BackendCapabilities capabilities() const;

    /**
     * @brief Get the recurrence capabilities of this backend.
     *
     * @deprecated Use capabilities().recurrence instead.
     * This method is kept for backwards compatibility.
     */
    [[deprecated("Use capabilities().recurrence instead")]]
    virtual RecurrenceCapabilities recurrenceCapabilities() const;

    /**
     * @brief Analyze what recurrence information would be lost when saving to this backend.
     *
     * This checks the incidence's recurrence against the backend's capabilities
     * and returns a description of what would be lost.
     *
     * @param incidence The incidence to analyze
     * @return RecurrenceLossInfo describing what would be lost (empty if no loss)
     */
    RecurrenceLossInfo analyzeRecurrenceLoss(const KCalendarCore::Incidence::Ptr &incidence) const;

    // ========== Factory Support for BackendRegistry ==========

    /// Static factory method - subclasses should implement this pattern:
    /// static SyncBackend* create(const QVariantMap &config, QObject *parent);
    ///
    /// Config keys vary by backend type:
    ///   local:   { "rootPath": "/path/to/storage" }
    ///   orgmode: { "rootPath": "/path/to/org/files" }
    ///   caldav:  { "url": "https://...", "username": "...", "password": "..." }

    // ========== IBlobBackend default implementations ==========
    // These are default bodies that emit a qWarning if called before a
    // concrete backend overrides them (Tasks 11-18). They keep the build
    // green across the Group 2 migration window.
    //
    // Identity/capability — sensible fallbacks:
    QString backendId() const override;
    QString displayName() const override;
    bool    isAvailable() const override;
    bool    supportsBatch() const override;
    bool    supportsDeleteTracking() const override;

    // Collections:
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const CollectionInfo &info) override;

    // Records:
    QList<BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId, const BackendRecord &record) override;
    bool    updateRecord(const BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId) override;

    // Change detection:
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;

    // Batch — no-op defaults (IBlobBackend already has inline defaults,
    // but re-declare here so the QObject/IBlobBackend vtable is unambiguous):
    void beginBatch() override;
    bool commitBatch() override;
    void rollbackBatch() override;

Q_SIGNALS:
    // ========== Calendar Discovery & Loading Events ==========

    /// New calendar discovered during loadCalendars
    void calendarDiscovered(const QString &collectionId, const QString &calendarId);

    /// Incidence loaded into calendar
    void itemLoaded(KCalendarCore::MemoryCalendar* cal,
                    KCalendarCore::Incidence::Ptr incidence,
                    const QString &versionIdentifier);

    /// Calendar loading complete
    void calendarLoaded(KCalendarCore::MemoryCalendar* cal);

    /// Sync operation finished for collection
    void syncCompleted(const QString &collectionId);

    /// Incidence removed from backend
    void itemRemoved(const QString &calId, const QString &itemUid);

    /// Emitted when loadCalendars() completes (success or failure)
    void loadCalendarsFinished(const QString &collectionId, bool success,
                               const QString &errorMessage = QString());

    // ========== Calendar CRUD Events ==========

    /// Calendar was successfully created
    void calendarCreated(const QString &collectionId, const QString &calendarId);

    /// Calendar properties were updated
    void calendarUpdated(const QString &collectionId, const QString &calendarId);

    /// Calendar was renamed
    void calendarRenamed(const QString &collectionId,
                         const QString &oldCalendarId,
                         const QString &newCalendarId);

    /// Calendar was deleted
    void calendarDeleted(const QString &collectionId, const QString &calendarId);

    /// Error occurred during calendar operation
    void calendarError(const QString &collectionId,
                       const QString &calendarId,
                       const QString &errorMessage);

    /// A loaded item's type does not match the collection's expected type.
    /// Emitted when e.g. a VTODO is found in a VEVENT-only collection.
    /// The item is still loaded (data preservation), but callers should
    /// not propagate it to type-restricted backends.
    void typeViolationDetected(const QString &calendarId,
                               const QString &itemUid,
                               CalendarType expectedType,
                               CalendarType actualType);

    // ========== Streaming Fetch Events (for real-time UI updates) ==========

    /// Emitted when a fetch operation begins, with the expected total item count
    /// (totalItems may be -1 if count is unknown until fetch completes)
    void fetchStarted(const QString &calendarId, int totalItems);

    /// Emitted for EACH item as it is fetched - allows real-time view updates
    void itemFetched(const QString &calendarId,
                     const KCalendarCore::Incidence::Ptr &incidence);

    /// Emitted periodically during fetch to update progress UI
    void fetchProgressChanged(const QString &calendarId, int current, int total);

    /// Emitted when fetch operation completes (success or failure)
    void fetchFinished(const QString &calendarId, bool success,
                       const QString &errorMessage = QString());

    // ========== Streaming Write Events (for real-time UI updates) ==========

    /// Emitted when a write operation begins, with the expected total item count
    void writeStarted(const QString &calendarId, int totalItems);

    /// Emitted periodically during write to update progress UI
    void writeProgressChanged(const QString &calendarId, int current, int total);

    /// Emitted when write operation completes (success or failure)
    void writeFinished(const QString &calendarId, bool success,
                       const QString &errorMessage = QString());

protected:
    // ========== Operation Tracking Implementation ==========

    /**
     * @brief Register an operation as pending.
     *
     * Subclasses should call this when starting an operation.
     * The operation will be automatically removed when it emits finished().
     */
    void registerOperation(SyncOperation *op);

    /**
     * @brief Remove an operation from tracking.
     *
     * Called automatically when operation emits finished().
     */
    void unregisterOperation(SyncOperation *op);

    /// Pending operations indexed by calendar ID
    QHash<QString, QList<SyncOperation*>> m_pendingOperations;
};

} // namespace Kalburator::Sync

#endif // SYNCBACKEND_H
