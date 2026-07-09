#ifndef SYNCBACKEND_H
#define SYNCBACKEND_H

// Phase K.4: this header is now a calendar-typed extension of the
// domain-neutral `SyncBackendBase` (see `src/sync/syncbackendbase.h`).
//
// `SyncBackendBase` carries:
//   - identity (backendType, nativeShapes, resourceId, shapeFor)
//   - operation tracking (cancellation, pending-operation queries)
//   - default IBlobBackend implementations
//   - domain-neutral telemetry signals (transcodingWarning,
//     fetch/write started/finished, syncCompleted)
//
// This file (`SyncBackend`) layers on top:
//   - calendar-typed pure virtuals (loadCalendars, storeCalendars,
//     startSync, removeItem)
//   - calendar-typed operation factory methods (fetchItems, pushItems,
//     deleteItems) returning Fetch/Push/DeleteOperation handles
//   - calendar-CRUD virtuals with default returns (createCalendar,
//     updateCalendar, renameCalendar, deleteCalendar, etc.)
//   - calendar-typed signals (calendarDiscovered, calendarLoaded,
//     itemLoaded, itemRemoved, calendarCreated, ...)
//   - getRawIcs/setRawIcs, capabilities, RecurrenceCapabilities
//
// Non-calendar backends (RawFilesBackend, GenericSqliteBackend,
// RemoteContactsBackend, blob-only adapters) inherit `SyncBackendBase`
// directly and do NOT carry calendar stubs. Calendar backends
// (LocalBackend, OrgBackend, RemoteCalendarBackend, AkonadiBackend,
// SubscriptionBackend, DecSyncBackend, MockBackend) inherit
// `SyncBackend`.

#include <QList>
#include <QMap>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QSharedPointer>
#include <QVariantMap>
#include <QColor>

#include <functional>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/Recurrence>

#include "calendartype.h"   // CalendarType enum
#include "discoveredcalendar.h" // DiscoveredCalendar DTO (Plan 9 aggregate accessor)
#include "syncbackendbase.h" // domain-neutral base (Phase K.4)

namespace Kalburator::Sync {

struct BackendCapabilities;
struct CalendarBackendBinding;
struct DiscoveredCalendar;
class FetchOperation;
class PushOperation;
class DeleteOperation;

/**
 * @brief Describes what recurrence features a backend supports.
 */
struct RecurrenceCapabilities
{
    bool supportsDaily = true;
    bool supportsWeekly = true;
    bool supportsMonthly = true;
    bool supportsYearly = true;
    bool supportsHourly = false;
    bool supportsMinutely = false;
    bool supportsSecondly = false;

    bool supportsByDay = false;
    bool supportsByMonthDay = false;
    bool supportsByYearDay = false;
    bool supportsByWeekNo = false;
    bool supportsByMonth = false;
    bool supportsBySetPos = false;

    bool supportsCount = false;
    bool supportsUntil = false;
    int maxInterval = 0;

    bool supportsMultipleRRules = false;
    bool supportsExRules = false;
    bool supportsRDates = false;
    bool supportsExDates = false;

    QString backendType;
    QString displayName;

    bool supportsFrequency(KCalendarCore::RecurrenceRule::PeriodType type) const;
    QString limitationsDescription() const;
};

/**
 * @brief Describes what would be lost when saving an incidence to a backend.
 */
struct RecurrenceLossInfo
{
    bool hasLoss = false;
    bool frequencyLost = false;
    bool byRulesLost = false;
    bool countUntilLost = false;
    bool multipleRulesLost = false;
    bool exceptionsLost = false;

    QStringList lostDetails;

    QString summary() const;
};

/**
 * @brief Calendar-typed sync-backend base.
 *
 * Inherits the domain-neutral `SyncBackendBase` and adds calendar-typed
 * pure virtuals, calendar-CRUD virtuals with default no-op returns, and
 * calendar-typed signals.
 *
 * Calendar backends (LocalBackend, OrgBackend, RemoteCalendarBackend,
 * etc.) inherit this class. Non-calendar backends inherit
 * `SyncBackendBase` directly and do not carry these calendar APIs.
 */
class SyncBackend : public SyncBackendBase
{
    Q_OBJECT

public:
    explicit SyncBackend(QObject *parent = nullptr);
    virtual ~SyncBackend() = default;

    // ========== Calendar Discovery & Loading ==========
    //
    // Phase K.4 (2026-05-09): these used to be pure virtuals; now they
    // have empty default implementations on SyncBackend so that
    // backends inheriting from this class but with no calendar surface
    // (e.g. RemoteContactsBackend) don't have to define no-op stubs.
    // Real calendar backends still override.

    /// Load calendar folders for a collection (emits calendarDiscovered for each)
    virtual void loadCalendars(const QString &collectionId) {
        Q_UNUSED(collectionId);
    }

    // ========== Incidence CRUD Operations ==========

    /// Save calendar list (if applicable)
    virtual void storeCalendars(const QString &collectionId,
                                const QList<KCalendarCore::MemoryCalendar*> &calendars) {
        Q_UNUSED(collectionId); Q_UNUSED(calendars);
    }

    /// Perform full sync with staged creations, updates, and deletions
    virtual void startSync(const QString &collectionId,
                           KCalendarCore::MemoryCalendar* calendar,
                           const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                           const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                           const QMap<QString, QString> &stagedDeletions) {
        Q_UNUSED(collectionId); Q_UNUSED(calendar);
        Q_UNUSED(stagedCreations); Q_UNUSED(stagedUpdates); Q_UNUSED(stagedDeletions);
    }

    /// Remove an item by calendar ID and item UID
    virtual void removeItem(const QString &calId, const QString &itemUid) {
        Q_UNUSED(calId); Q_UNUSED(itemUid);
    }

    // ========== Operation-Based API (calendar-typed pushItems) ==========
    // `fetchItems` and `deleteItems` are inherited from SyncBackendBase.
    // Only the Incidence::Ptr-typed pushItems overload lives here,
    // since it requires KCalendarCore.

    virtual PushOperation* pushItems(const QString &calendarId,
                                     const QList<KCalendarCore::Incidence::Ptr> &items);

    // ========== Calendar-Level CRUD Operations ==========

    virtual bool supportsCalendarCreation() const { return false; }

    /**
     * @brief Aggregate discovery facts for a calendar as one DTO (Plan 9).
     *
     * The single accessor that supersedes the per-field discovered* getters.
     * The default fills only the neutral writability primitive; backends that
     * discover richer metadata (color, component support, display name, URL)
     * override this. Unset DTO fields keep their defaults, which match the
     * retired per-field getter defaults: invalid color, Hybrid type (both
     * supports* flags true), empty name/url.
     */
    virtual DiscoveredCalendar discoveredCalendar(const QString &calendarId) const {
        DiscoveredCalendar d;
        d.calendarId = calendarId;
        d.writable = discoveredWritable(calendarId);
        return d;
    }

    // Per-field discovery getters, superseded by discoveredCalendar() (Plan 9).
    // Now non-virtual forwarders into the DTO accessor (so polymorphism flows
    // through the single overridable discoveredCalendar()); kept [[deprecated]]
    // for the PlanStan migration window, deleted once it lands (Plan 11).
    [[deprecated("use discoveredCalendar(id).calendarType()")]]
    CalendarType discoveredCalendarType(const QString &calendarId) const {
        return discoveredCalendar(calendarId).calendarType();
    }

    [[deprecated("use discoveredCalendar(id).color")]]
    QColor discoveredColor(const QString &calendarId) const {
        return discoveredCalendar(calendarId).color;
    }

    [[deprecated("use discoveredCalendar(id).name")]]
    QString discoveredDisplayName(const QString &calendarId) const {
        return discoveredCalendar(calendarId).name;
    }

    virtual bool createCalendar(const QString &collectionId,
                                const QString &calendarId,
                                const QString &name,
                                CalendarType type = CalendarType::Hybrid) {
        Q_UNUSED(collectionId); Q_UNUSED(calendarId); Q_UNUSED(name); Q_UNUSED(type);
        return false;
    }

    virtual bool updateCalendar(const QString &collectionId,
                                const QString &calendarId,
                                const QVariantMap &properties) {
        Q_UNUSED(collectionId); Q_UNUSED(calendarId); Q_UNUSED(properties);
        return false;
    }

    virtual bool renameCalendar(const QString &collectionId,
                                const QString &oldCalendarId,
                                const QString &newCalendarId) {
        Q_UNUSED(collectionId); Q_UNUSED(oldCalendarId); Q_UNUSED(newCalendarId);
        return false;
    }

    virtual bool deleteCalendar(const QString &collectionId, const QString &calendarId) {
        Q_UNUSED(collectionId); Q_UNUSED(calendarId);
        return false;
    }

    /**
     * @brief Async siblings of the calendar-CRUD trio above (E11 / audit B7,
     * absorbs FINDINGS O39's Group C).
     *
     * Same relationship as `ChangeDetection::collectionRevisions()` /
     * `collectionRevisionsAsync()`: the default adapts the synchronous form
     * (correct for every backend whose CRUD has no nested loop — Local,
     * DecSync, Org, Akonadi, Mock). Only `RemoteCalendarBackend` overrides
     * these for real, using `davSyncRequestAsync` — its own sync
     * `createCalendar`/`updateCalendar`/`deleteCalendar` overrides are gone
     * (E11 Stage 1), so callers MUST go through the Async form + a
     * `blockOnAsync`-style rendezvous on a non-backend thread (see
     * `src/sync/blockonasync.h`) to get a synchronous answer.
     */
    virtual void createCalendarAsync(const QString &collectionId,
                                     const QString &calendarId,
                                     const QString &name,
                                     CalendarType type,
                                     std::function<void(bool)> done) {
        done(createCalendar(collectionId, calendarId, name, type));
    }

    virtual void updateCalendarAsync(const QString &collectionId,
                                     const QString &calendarId,
                                     const QVariantMap &properties,
                                     std::function<void(bool)> done) {
        done(updateCalendar(collectionId, calendarId, properties));
    }

    virtual void deleteCalendarAsync(const QString &collectionId,
                                     const QString &calendarId,
                                     std::function<void(bool)> done) {
        done(deleteCalendar(collectionId, calendarId));
    }

    // ========== Calendar Property Getters ==========

    virtual QColor calendarColor(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QColor();
    }

    virtual QString calendarDescription(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QString();
    }

    // ========== Binding Metadata Support ==========

    virtual QStringList bindingMetadataKeys() const { return {}; }

    virtual void populateBindingMetadata(
        const DiscoveredCalendar &discovered,
        CalendarBackendBinding &binding) const;

    virtual void prepareCreationMetadata(
        const QString &calendarId,
        CalendarBackendBinding &binding) const;

    // ========== Source File Access ==========

    virtual QString sourceFilePath(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return {};
    }

    // ========== Debug/Raw ICS Access ==========

    virtual QString getRawIcs(const QString &calendarId, const QString &uid) const {
        Q_UNUSED(calendarId); Q_UNUSED(uid);
        return QString();
    }

    virtual bool setRawIcs(const QString &calendarId, const QString &uid,
                           const QString &icsContent) {
        Q_UNUSED(calendarId); Q_UNUSED(uid); Q_UNUSED(icsContent);
        return false;
    }

    // ========== Backend Capabilities ==========

    virtual BackendCapabilities capabilities() const;

    [[deprecated("Use capabilities().recurrence instead")]]
    virtual RecurrenceCapabilities recurrenceCapabilities() const;

    RecurrenceLossInfo analyzeRecurrenceLoss(const KCalendarCore::Incidence::Ptr &incidence) const;

Q_SIGNALS:
    // ========== Calendar Discovery & Loading Events ==========

    void calendarDiscovered(const QString &collectionId, const QString &calendarId);

    void itemLoaded(KCalendarCore::MemoryCalendar* cal,
                    KCalendarCore::Incidence::Ptr incidence,
                    const QString &versionIdentifier);

    void calendarLoaded(KCalendarCore::MemoryCalendar* cal);

    void itemRemoved(const QString &calId, const QString &itemUid);

    void loadCalendarsFinished(const QString &collectionId, bool success,
                               const QString &errorMessage = QString());

    // ========== Calendar CRUD Events ==========

    void calendarCreated(const QString &collectionId, const QString &calendarId);
    void calendarUpdated(const QString &collectionId, const QString &calendarId);
    void calendarRenamed(const QString &collectionId,
                         const QString &oldCalendarId,
                         const QString &newCalendarId);
    void calendarDeleted(const QString &collectionId, const QString &calendarId);
    void calendarError(const QString &collectionId,
                       const QString &calendarId,
                       const QString &errorMessage);

    /// A loaded item's type does not match the collection's expected type.
    void typeViolationDetected(const QString &calendarId,
                               const QString &itemUid,
                               CalendarType expectedType,
                               CalendarType actualType);

    // ========== Streaming Fetch Events (calendar-typed) ==========

    /// Batched streaming fetch (E9, sync-excellence campaign, O34): fires
    /// once per fetch pass (LocalBackend) or per multiget chunk / cache
    /// pass (RemoteCalendarBackend) with the FULL list of incidences that
    /// pass fetched or served — not debounced/timer-batched, only natural
    /// pass/chunk boundaries. The batch-form replacement for the per-item
    /// itemFetched signal, DELETED at E10/v0.90.1 after its one-release
    /// deprecation window (every backend now emits the batch form).
    void itemsFetched(const QString &calendarId,
                      const QList<KCalendarCore::Incidence::Ptr> &items);
};

} // namespace Kalburator::Sync

#endif // SYNCBACKEND_H
