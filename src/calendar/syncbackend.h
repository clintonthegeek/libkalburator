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
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/Recurrence>

#include "calendartype.h"   // CalendarType enum
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

    virtual CalendarType discoveredCalendarType(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return CalendarType::Hybrid;
    }

    virtual QColor discoveredColor(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QColor();
    }

    virtual QString discoveredDisplayName(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return QString();
    }

    virtual bool discoveredWritable(const QString &calendarId) const {
        Q_UNUSED(calendarId);
        return true;
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

    /// Per-item streaming fetch — calendar-typed (Incidence::Ptr).
    void itemFetched(const QString &calendarId,
                     const KCalendarCore::Incidence::Ptr &incidence);
};

} // namespace Kalburator::Sync

#endif // SYNCBACKEND_H
