#ifndef SUBSCRIPTIONBACKEND_H
#define SUBSCRIPTIONBACKEND_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QHash>
#include <QDate>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Incidence>
#include "syncbackend.h"
#include "backendrecord.h"
#include "collectioninfo.h"
#include <QDateTime>

namespace Kalburator::Sync {

struct BackendCapabilities;

/**
 * @brief Base class for read-only subscription calendar backends.
 *
 * SubscriptionBackend provides infrastructure for calendars that are
 * generated from external sources (holidays, webcal feeds, RSS, etc.)
 * and cannot be edited by the user. These calendars are strictly read-only.
 *
 * Each subscription source (e.g., a holiday region or webcal URL) creates
 * a separate calendar in the collection. For example:
 * - "US Holidays" calendar from holiday region "us_en-us"
 * - "UK Holidays" calendar from holiday region "gb_en-gb"
 * - "Team Calendar" from webcal://example.com/team.ics
 *
 * Subclasses implement fetchEventsForSource() to generate events from
 * their specific source type.
 *
 * ## Read-Only Enforcement
 * All write operations (storeItems, updateItem, removeItem, etc.) are
 * no-ops or return errors. The backend always reports calendars as
 * read-only via discoveredWritable().
 *
 * ## Extensibility
 * Adding new subscription types (webcal, RSS, public Google Calendar, etc.)
 * requires only creating a new subclass that implements fetchEventsForSource().
 */
class SubscriptionBackend : public SyncBackend
{
    Q_OBJECT

public:
    explicit SubscriptionBackend(QObject *parent = nullptr);
    ~SubscriptionBackend() override = default;

    // ========== SyncBackend Interface ==========

    static const QString BackendTypeName;
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    void loadCalendars(const QString &collectionId) override;
    FetchOperation* fetchItems(const QString &calendarId) override;

    // Subscription backends are strictly read-only — pushItems always
    // returns a Failed PushOperation.
    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items) override;

    // ========== Read-Only Enforcement ==========
    // These operations do nothing or return errors since subscriptions are read-only

    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;

    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar* calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions) override;

    void removeItem(const QString &calId, const QString &itemUid) override;

    // Always returns false - subscription calendars are never writable
    bool discoveredWritable(const QString &calendarId) const override;

    // Never supports calendar creation
    bool supportsCalendarCreation() const override { return false; }

    // Backend capabilities
    BackendCapabilities capabilities() const override;

    // ========== IBlobBackend Overrides (Phase D Task 17) ==========
    // Read-only: writes return {} / false.
    // recordId     = uid (from incidence->uid())
    // collectionId = sourceId
    // data         = serialised iCal bytes via ICalFormat::toString()
    // contentHash  = SHA-256 of data
    // lastModified = QDateTime::currentDateTimeUtc() (no per-incidence mtime)

    // Identity
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // Collections
    QList<CollectionInfo> availableCollections() override;
    CollectionInfo        collectionInfo(const QString &collectionId) override;
    QString               createCollection(const CollectionInfo &info) override;

    // Records — loadRecords / loadRecord use fetchEventsForSource()
    QList<BackendRecord>         loadRecords(const QString &collectionId) override;
    std::optional<BackendRecord> loadRecord(const QString &recordId) override;

    // Writes — always rejected (read-only backend)
    QString createRecord(const QString &collectionId,
                         const BackendRecord &record) override;
    bool    updateRecord(const BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId) override;

    // Change detection
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool supportsDeleteTracking() const override { return false; }

    // Batch — no-op
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

    // ========== Subscription Source Management ==========

    /**
     * @brief Add a subscription source.
     *
     * Each source creates a separate calendar in the collection.
     *
     * @param sourceId Unique identifier for this source (e.g., "us_en-us", "webcal_1")
     * @param sourceType Type of source (e.g., "holiday", "webcal", "rss")
     * @param config Source-specific configuration (e.g., region code, URL, refresh interval)
     */
    void addSource(const QString &sourceId, const QString &sourceType, const QVariantMap &config);

    /**
     * @brief Remove a subscription source.
     *
     * @param sourceId The source ID to remove
     */
    void removeSource(const QString &sourceId);

    /**
     * @brief Get list of all source IDs.
     */
    QStringList sources() const;

    /**
     * @brief Get the source type for a source ID.
     */
    QString sourceType(const QString &sourceId) const;

    /**
     * @brief Get the configuration for a source.
     */
    QVariantMap sourceConfig(const QString &sourceId) const;

protected:
    /**
     * @brief Fetch events from a subscription source.
     *
     * Subclasses implement this to generate events from their specific
     * source type (holidays, webcal, RSS, etc.).
     *
     * Events should:
     * - Have unique UIDs within the source
     * - Be marked read-only via setReadOnly(true)
     * - Optionally have custom properties to identify the source
     * - Optionally have categories for filtering (e.g., "Holiday")
     *
     * @param sourceId The source ID (corresponds to a calendar)
     * @param startDate Start of date range to fetch (inclusive)
     * @param endDate End of date range to fetch (inclusive)
     * @return List of events in the date range
     */
    virtual QList<KCalendarCore::Incidence::Ptr> fetchEventsForSource(
        const QString &sourceId,
        const QDate &startDate,
        const QDate &endDate) = 0;

    /**
     * @brief Get the display name for a source.
     *
     * Subclasses can override to provide user-friendly names.
     * Default implementation returns the source ID.
     */
    virtual QString sourceDisplayName(const QString &sourceId) const;

    struct SourceInfo {
        QString sourceId;
        QString sourceType;
        QVariantMap config;
    };

    QHash<QString, SourceInfo> m_sources;  // sourceId -> source info
    QString m_currentCollectionId;         // Collection ID for emitting signals
};

} // namespace Kalburator::Sync

#endif // SUBSCRIPTIONBACKEND_H
