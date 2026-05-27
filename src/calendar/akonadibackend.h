#ifndef AKONADIBACKEND_H
#define AKONADIBACKEND_H

#ifdef HAVE_AKONADI

#include "syncbackend.h"
#include "syncoperation.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include "../backend/changedetection.h"
#include "akonadirevisionstore.h"

#include <Akonadi/Session>
#include <Akonadi/Monitor>
#include <Akonadi/Collection>
#include <Akonadi/Item>

#include <QMap>
#include <QSet>
#include <memory>

namespace Kalburator::Sync {

/**
 * @brief Akonadi client backend for PlanStan.
 *
 * Acts as an Akonadi client, letting PlanStan read/write calendars
 * managed by Akonadi resources (CalDAV, Google, EWS, etc.).
 *
 * Calendar ID scheme: "akonadi-<collectionId>" (e.g., "akonadi-42").
 * This is stable across sessions since Akonadi::Collection::Id persists.
 *
 * Uses Akonadi::Monitor to watch for external changes (e.g., from KOrganizer)
 * and maps those to SyncBackend signals. Writes use a dedicated Session
 * that the Monitor ignores to prevent feedback loops.
 */
class AkonadiBackend : public SyncBackend,
                       public Kalburator::Backend::ChangeDetection
{
    Q_OBJECT

public:
    explicit AkonadiBackend(QObject *parent = nullptr);
    ~AkonadiBackend() override;

    /**
     * @brief Factory method for BackendRegistry.
     *
     * Config keys: (none required - Akonadi discovers resources automatically)
     */
    static SyncBackend* create(const QVariantMap &config, QObject *parent);

    // === Core Backend Identity ===
    static const QString BackendTypeName;
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // === Calendar Discovery & Loading ===
    void loadCalendars(const QString &collectionId) override;

    // === Incidence CRUD Operations ===
    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;
    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar *calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;

    /// Test-only passthrough to incidenceFromRecord (no Akonadi server needed).
    KCalendarCore::Incidence::Ptr incidenceFromRecordForTest(const BackendRecord &record) const
    { return incidenceFromRecord(record); }

    // === Operation-Based Async API ===
    FetchOperation*  fetchItems(const QString &calendarId) override;

    PushOperation*   pushItems(const QString &calendarId,
                               const QList<KCalendarCore::Incidence::Ptr> &items) override;

    DeleteOperation* deleteItems(const QString &calendarId,
                                 const QStringList &uids) override;

    // === Discovery Metadata ===
    CalendarType discoveredCalendarType(const QString &calendarId) const override;
    QColor       discoveredColor(const QString &calendarId) const override;
    QString      discoveredDisplayName(const QString &calendarId) const override;
    bool         discoveredWritable(const QString &calendarId) const override;

    // === Calendar Property Getters ===
    QColor  calendarColor(const QString &calendarId) const override;
    QString calendarDescription(const QString &calendarId) const override;

    // === Calendar CRUD ===
    bool supportsCalendarCreation() const override;
    bool createCalendar(const QString &collectionId, const QString &calendarId,
                        const QString &name,
                        CalendarType type = CalendarType::Hybrid) override;
    bool deleteCalendar(const QString &collectionId, const QString &calendarId) override;

    // === Capabilities & Metadata ===
    BackendCapabilities capabilities() const override;
    QStringList bindingMetadataKeys() const override;
    void populateBindingMetadata(const DiscoveredCalendar &discovered,
                                 CalendarBackendBinding &binding) const override;
    void prepareCreationMetadata(const QString &calendarId,
                                 CalendarBackendBinding &binding) const override;

    // =========================================================================
    // IBlobBackend overrides (Phase D Task 15)
    //
    // Gated: compiled only when KALBURATOR_HAVE_AKONADI=ON (and HAVE_AKONADI
    // is defined at build time).
    //
    // recordId     = Akonadi::Item::id().toString()
    // collectionId = calendarId ("akonadi-<collectionId>")
    // data         = serialized iCal bytes of the incidence
    // contentHash  = SHA-256 of the iCal bytes
    // lastModified = item's modification time from Akonadi::Item
    //
    // Phase D stubs: all methods emit qWarning and return empty/false because
    // Akonadi async jobs require a running Akonadi server. Phase F will wrap
    // these properly with QEventLoop or a dedicated worker thread strategy.
    // The type-system change (SyncBackend : IBlobBackend) is honored here.
    // =========================================================================

    // Identity
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // Collections
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override;

    // Records — stub implementations; Akonadi live server required
    QList<BackendRecord>         loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;
    QString                      createRecord(const QString &collectionId,
                                              const BackendRecord &record) override;
    bool                         updateRecord(const BackendRecord &record) override;
    bool                         deleteRecord(const QString &recordId) override;

    // Change detection — stubs
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool                 supportsDeleteTracking() const override { return false; }

    // Batch — no-op; Akonadi has its own transaction layer
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

    // === Backend::ChangeDetection ===
    QString collectionRevision(const QString &collectionId) override;
    QString cachedCollectionRevision(const QString &collectionId) const override;
    void    primeRevisionCache(const QMap<QString, QString> &cache) override;

private slots:
    // Monitor callbacks for external changes
    void onItemAdded(const Akonadi::Item &item, const Akonadi::Collection &col);
    void onItemChanged(const Akonadi::Item &item, const QSet<QByteArray> &parts);
    void onItemRemoved(const Akonadi::Item &item);
    void onCollectionAdded(const Akonadi::Collection &col, const Akonadi::Collection &parent);
    void onCollectionChanged(const Akonadi::Collection &col, const QSet<QByteArray> &attrs);
    void onCollectionRemoved(const Akonadi::Collection &col);

private:
    void setupMonitor();

    /// Convert Akonadi Collection::Id to our calendar ID string
    QString calendarIdForCollection(Akonadi::Collection::Id id) const;

    /// Convert our calendar ID string to Akonadi Collection::Id
    Akonadi::Collection::Id collectionIdForCalendar(const QString &calendarId) const;

    /// Look up the cached Akonadi::Item for a given calendar + uid
    Akonadi::Item findItemByUid(const QString &calendarId, const QString &uid) const;

    /// Resolve a cross-backend record id (iCal UID) to its cached Akonadi
    /// item and owning calendar id. Returns an invalid Item if not found.
    Akonadi::Item findCachedItem(const QString &uid, QString *outCalendarId) const;

    /// Extract KCalendarCore::Incidence::Ptr from an Akonadi::Item
    KCalendarCore::Incidence::Ptr extractIncidence(const Akonadi::Item &item) const;

    /// Inverse of loadRecords serialization: parse BackendRecord.data
    /// (iCal bytes) into an Incidence. Returns null on parse failure.
    KCalendarCore::Incidence::Ptr incidenceFromRecord(const BackendRecord &record) const;

    /// Check if a collection contains calendar MIME types
    bool isCalendarCollection(const Akonadi::Collection &col) const;

    /// Determine CalendarType from collection content MIME types
    CalendarType calendarTypeForCollection(const Akonadi::Collection &col) const;

    Akonadi::Session  *m_session  = nullptr;  // For our writes (ignored by monitor)
    Akonadi::Monitor  *m_monitor  = nullptr;  // Watches external changes

    /// When non-empty, this backend is scoped to a single collection
    /// (set by create() via "akonadiCollectionId" config key, Phase L.5).
    QString m_scopedCollectionId;

    // Collection ID <-> calendar ID mapping
    QMap<Akonadi::Collection::Id, QString> m_collectionToCalId;
    QMap<QString, Akonadi::Collection>     m_collections;

    // Item tracking: calendarId -> (uid -> Akonadi::Item)
    QMap<QString, QMap<QString, Akonadi::Item>> m_itemsByCalendar;

    /// Lazily-constructed persistent revision token store.
    Kalburator::Sync::AkonadiRevisionStore *revisionStore() const;
    mutable std::unique_ptr<Kalburator::Sync::AkonadiRevisionStore> m_revisionStore;

    /// uid -> (Akonadi item revision, cached contentHash). Lets loadRecords
    /// skip re-serializing+re-hashing an item whose revision is unchanged.
    mutable QMap<QString, QPair<int, QString>> m_hashMemo;
};

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI

#endif // AKONADIBACKEND_H
