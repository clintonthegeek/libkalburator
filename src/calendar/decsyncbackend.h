#ifndef DECSYNCBACKEND_H
#define DECSYNCBACKEND_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QMap>
#include <QSet>
#include <KCalendarCore/MemoryCalendar>
#include "syncbackend.h"
#include "syncoperation.h"
#include "backendrecord.h"
#include "collectioninfo.h"

namespace Kalburator::Sync {

struct BackendCapabilities;
class DecSyncDir;
class DecSyncCollection;
class DecSyncActiveController;
class DecSyncControllerStore;
class SyncthingMonitor;

/**
 * @brief SyncBackend implementation for DecSync v2 format.
 *
 * Enables calendar and task sync with Android devices via Syncthing
 * (no cloud server needed). Uses DecSyncLib for file format operations.
 *
 * CalendarId mapping convention:
 * - "foo"        -> DecSync syncType="calendars", collectionId="foo"
 * - "tasks/foo"  -> DecSync syncType="tasks",     collectionId="foo"
 *
 * Config: {"decsyncDir": "/path/to/DecSync", "appId": "planstan-hostname"}
 */
class DecSyncBackend : public SyncBackend
{
    Q_OBJECT

public:
    explicit DecSyncBackend(const QString &decsyncDir, const QString &appId,
                            QObject *parent = nullptr);
    ~DecSyncBackend() override;

    static SyncBackend* create(const QVariantMap &config, QObject *parent);

    static const QString BackendTypeName;
    QString backendType() const override;

    // Calendar discovery & loading
    void loadCalendars(const QString &collectionId) override;
    void loadItems(KCalendarCore::MemoryCalendar* cal, bool suppressSignals = false) override;

    // Incidence CRUD
    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;
    void storeItems(KCalendarCore::MemoryCalendar* cal,
                    const QList<KCalendarCore::Incidence::Ptr> &items,
                    const TranscodingPlan& plan) override;
    void updateItem(KCalendarCore::MemoryCalendar* cal,
                    const KCalendarCore::Incidence::Ptr &item,
                    const QString &icalData,
                    const TranscodingPlan& plan) override;
    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar* calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions,
                   const TranscodingPlan& plan) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    // Operation-based API
    FetchOperation* fetchItems(const QString &calendarId) override;
    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items) override;
    DeleteOperation* deleteItems(const QString &calendarId,
                                  const QStringList &uids) override;

    // Calendar-level CRUD
    bool supportsCalendarCreation() const override;
    bool createCalendar(const QString &collectionId, const QString &calendarId,
                        const QString &name, CalendarType type = CalendarType::Hybrid) override;
    bool updateCalendar(const QString &collectionId, const QString &calendarId,
                        const QVariantMap &properties) override;
    bool deleteCalendar(const QString &collectionId, const QString &calendarId) override;

    // Calendar property discovery
    CalendarType discoveredCalendarType(const QString &calendarId) const override;
    QColor discoveredColor(const QString &calendarId) const override;
    QString discoveredDisplayName(const QString &calendarId) const override;
    QColor calendarColor(const QString &calendarId) const override;

    // Capabilities
    BackendCapabilities capabilities() const override;

    // Binding metadata
    QStringList bindingMetadataKeys() const override;
    void populateBindingMetadata(const DiscoveredCalendar &discovered,
                                 CalendarBackendBinding &binding) const override;
    void prepareCreationMetadata(const QString &calendarId,
                                 CalendarBackendBinding &binding) const override;

    // Raw ICS access
    QString getRawIcs(const QString &calendarId, const QString &uid) const override;
    bool setRawIcs(const QString &calendarId, const QString &uid,
                   const QString &icsContent) override;

    /// Run active sync for a standalone DecSync calendar (case C).
    void checkForRemoteChanges(const QString &calendarId);

    /// Get the active controller for a calendar (for SyncCoordinator integration).
    DecSyncActiveController* activeController(const QString &calendarId) const;

    /// Set a Syncthing monitor for event-driven sync triggers.
    /// The backend does NOT own the monitor — just connects signals.
    /// When set, remoteChangesReady triggers checkForRemoteChanges() for all calendars.
    void setSyncthingMonitor(SyncthingMonitor *monitor);

    // =========================================================================
    // IBlobBackend overrides (Phase D Task 16)
    //
    // Mirrors LocalBackend semantics (file-based I/O via DecSync's API).
    // recordId     = uid (DecSync "resources" map key)
    // collectionId = calendarId
    // data         = raw iCal bytes (DecSync stores iCal strings natively)
    // contentHash  = SHA-256 of the iCal bytes
    // lastModified = parsed from DecSyncEntry::datetime (ISO 8601)
    // =========================================================================

    // Identity
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // Collections
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override;

    // Records
    QList<BackendRecord>         loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString                      createRecord(const QString &collectionId,
                                              const BackendRecord &record) override;
    bool                         updateRecord(const BackendRecord &record) override;
    bool                         deleteRecord(const QString &recordId) override;

    // Change detection — filters by DecSyncEntry::datetime
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool                 supportsDeleteTracking() const override { return true; }

    // Batch — DecSync write operations are synchronous; no batching in Phase D
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

private:
    /// Parse calendarId into syncType + collectionId
    void parseSyncId(const QString &calendarId, QString &syncType, QString &collectionId) const;

    /// Get or create a DecSyncCollection for the given calendarId
    DecSyncCollection* collectionFor(const QString &calendarId) const;

    /// Serialize an incidence to iCalendar string
    QString serializeIncidence(const KCalendarCore::Incidence::Ptr &incidence) const;

    /// Deserialize an iCalendar string to incidences
    QList<KCalendarCore::Incidence::Ptr> deserializeIcal(const QString &icalData) const;

    /// Check if an incidence type is allowed in the given calendar collection.
    /// For hybrid calendars, all types are allowed.
    /// For single-type calendars, returns true only if the type matches.
    bool isTypeAllowed(const QString &calendarId,
                       const KCalendarCore::Incidence::Ptr &incidence) const;

    /// Get the CalendarType that corresponds to an incidence's actual type.
    static CalendarType incidenceCalendarType(const KCalendarCore::Incidence::Ptr &incidence);

    /// Check if a calendarId is a hybrid calendar (accepts both VEVENTs and VTODOs).
    /// True if explicitly created as Hybrid or discovered with both dirs / hybrid flag.
    bool isHybridCalendar(const QString &calendarId) const;

    /// Get the tasks/ companion collection for a hybrid calendar.
    /// Returns nullptr if not hybrid or if the tasks/ dir doesn't exist yet.
    DecSyncCollection* todoCollectionFor(const QString &calendarId) const;

    /// Lazily create the calendars/ collection for a hybrid calendar.
    /// Creates the directory, writes pending metadata and hybrid flag.
    DecSyncCollection* ensureEventCollection(const QString &calendarId);

    /// Lazily create the tasks/ collection for a hybrid calendar.
    /// Creates the directory, writes pending metadata and hybrid flag.
    DecSyncCollection* ensureTodoCollection(const QString &calendarId);

    /// Write pending metadata (name, color) to a newly created collection.
    void writePendingMetadata(DecSyncCollection *coll, const QString &calendarId);

    DecSyncDir *m_dir;
    QString m_appId;
    mutable QMap<QString, DecSyncCollection*> m_collections;  // cache
    mutable QSet<QString> m_hybridIds;  // calendarIds that accept both types
    QMap<QString, QVariantMap> m_pendingHybridMeta;  // deferred metadata for lazy creation
    DecSyncControllerStore *m_controllerStore = nullptr;
    mutable QMap<QString, DecSyncActiveController*> m_controllers;
    DecSyncActiveController* ensureController(const QString &calendarId) const;
    SyncthingMonitor *m_syncthingMonitor = nullptr;
};

} // namespace Kalburator::Sync

#endif // DECSYNCBACKEND_H
