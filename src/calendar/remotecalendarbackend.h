#ifndef REMOTECALENDARBACKEND_H
#define REMOTECALENDARBACKEND_H

#include "syncbackend.h"
#include "syncoperation.h"  // complete FetchOperation/DeleteOperation for covariant overrides
#include "backendrecord.h"
#include "collectioninfo.h"
#include "../backend/changedetection.h"
#include <KDAV/DavUrl>
#include <KDAV/DavCollection>
#include <KDAV/EtagCache>
#include <KDAV/DavJobBase>
#include <QUrl>
#include <QMap>
#include <QPointer>
#include <QSqlDatabase>
#include <QColor>
#include <QDateTime>
#include <QStringList>
#include <memory>

namespace Kalburator::Sync {

class CTagStore;
struct BackendCapabilities;

class RemoteCalendarBackend : public SyncBackend,
                              public Kalburator::Backend::ChangeDetection
{
    Q_OBJECT

public:
    explicit RemoteCalendarBackend(const QUrl &url,
                           const QString &username,
                           const QString &password,
                           QObject *parent = nullptr);
    ~RemoteCalendarBackend() override;

    /**
     * @brief Factory method for BackendRegistry.
     *
     * Expected config keys:
     *   - url: QString - CalDAV server URL
     *   - username: QString - Authentication username
     *   - password: QString - Authentication password
     *
     * @param config Backend configuration map
     * @param parent Parent QObject
     * @return New RemoteCalendarBackend instance
     */
    static SyncBackend* create(const QVariantMap &config, QObject *parent);

    /**
     * @brief Set the DB file path so the private CTagStore can be initialised.
     * Must be called before using CTag-based sync optimizations.
     */
    void setDbPath(const QString &dbPath);

    /**
     * @brief Override the directory used for the delta-sync content cache.
     *
     * When set (non-empty), initContentCache() places the cache DB under @p dir
     * instead of QStandardPaths::CacheLocation. The host app uses this to keep
     * the cache inside a per-collection profile folder. Must be called before
     * the first fetchItems() (which lazily initialises the cache).
     */
    void setCacheDir(const QString &dir);

    // ---- Per-backend CTag access (CalDAV sync optimisation) ----
    /**
     * @brief Get the stored CTag for a calendar.
     * @param calendarId The calendar ID
     * @return Stored CTag, or empty string if not cached
     */
    QString ctag(const QString &calendarId) const;

    /**
     * @brief Store a CTag for a calendar.
     * @param calendarId The calendar ID
     * @param ctag The CTag value from the server
     */
    void setCtag(const QString &calendarId, const QString &ctag);

    /**
     * @brief Remove the stored CTag for a calendar.
     * @param calendarId The calendar ID
     */
    void clearCtag(const QString &calendarId);

    void loadCalendars(const QString &collectionId) override;

    /**
     * @brief Pre-register a calendar URL from stored configuration.
     *
     * This allows calendars discovered during collection creation to be
     * immediately usable without re-discovering from the server.
     *
     * @param calendarId The calendar ID (e.g., "work", "home")
     * @param davUrl The full CalDAV URL for the calendar
     */
    void registerCalendarUrl(const QString &calendarId, const QString &davUrl);

    /**
     * @brief Get the discovered URL for a calendar.
     *
     * Returns the actual server URL discovered during loadCalendars(),
     * which may differ from a URL constructed from the display name
     * (e.g., server uses "acquire" but display name is "Acquire").
     *
     * @param calendarId The calendar ID (display name)
     * @return The discovered DAV URL, or empty string if not found
     */
    QString discoveredUrl(const QString &calendarId) const;

    /**
     * @brief Get the discovered color for a calendar.
     *
     * Returns the color from the CalDAV apple:calendar-color property
     * discovered during loadCalendars().
     *
     * @param calendarId The calendar ID (display name)
     * @return The discovered color, or invalid QColor if not found
     */
    QColor discoveredColor(const QString &calendarId) const override;

    /**
     * @brief Fetch fresh CTags for multiple calendars in a single network operation.
     *
     * Groups the requested calendars by parent URL (e.g. all calendars under
     * /cal.php/calendars/user/) and issues one Depth:1 PROPFIND per group,
     * collapsing N round-trips into K (K ≤ N, typically K = 1).
     *
     * @param calendarIds List of discovered calendar IDs to query.
     * @return Map of calendarId -> fresh CTag. Calendars whose CTag the server
     *         did not return are absent from the map. Network failures yield
     *         an empty result (caller should treat as "fall back to per-call PROPFIND").
     *
     * Synchronous; runs an internal QEventLoop. Safe to call before sync starts.
     */
    QMap<QString, QString> fetchAllCtags(const QStringList &calendarIds);

    /**
     * @brief Per-calendar metadata the provider already discovered at connect().
     *
     * Carries exactly what loadCalendars() would otherwise re-fetch per backend
     * (minus the CTag, which is not part of the discovery walk). The raw
     * @ref davUrl is configured with credentials internally, so a primed
     * backend is self-sufficient.
     */
    struct PrimedCalendar {
        QString calendarId;   ///< Discovery key (== server display name); the id emitted by loadCalendars
        QString davUrl;       ///< Raw calendar DAV URL (credentials added internally)
        QColor  color;        ///< apple:calendar-color, may be invalid
        KDAV::DavCollection::ContentTypes contentTypes;  ///< synthesized from supported components
    };

    /**
     * @brief Seed per-calendar metadata the provider already discovered.
     *
     * Populates the same internal maps loadCalendars() fills (davUrls, colors,
     * content types) directly from connect-time discovery, and marks the listed
     * calendars as primed. After priming, the next loadCalendars(collectionId)
     * skips its DavCollectionsFetchJob entirely and emits calendarDiscovered for
     * each primed calendar, then loadCalendarsFinished — zero PROPFINDs.
     *
     * Re-priming overwrites. Priming nothing is a no-op and loadCalendars falls
     * back to the network walk (standalone backends, tests).
     */
    void primeCalendars(const QList<PrimedCalendar> &calendars);

    // ---- Backend::ChangeDetection (Phase K.1) ----
    // Thin delegations to the existing CTag surface above. The engine
    // consumes these via dynamic_cast<Backend::ChangeDetection*> in K.2;
    // the qobject_cast<RemoteCalendarBackend*> path retires there.
    QString collectionRevision(const QString &collectionId) override
    {
        const auto map = fetchAllCtags({collectionId});
        return map.value(collectionId);
    }
    QMap<QString, QString>
    collectionRevisions(const QStringList &collectionIds) override
    {
        return fetchAllCtags(collectionIds);
    }
    QString cachedCollectionRevision(const QString &collectionId) const override
    {
        return ctag(collectionId);
    }
    void primeRevisionCache(const QMap<QString, QString> &cache) override
    {
        for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
            setCtag(it.key(), it.value());
    }

    /**
     * @brief Check if discovered calendar supports VEVENT components.
     */
    bool discoveredSupportsEvents(const QString &calendarId) const;

    /**
     * @brief Check if discovered calendar supports VTODO components.
     */
    bool discoveredSupportsTodos(const QString &calendarId) const;

    /**
     * @brief Get the CalendarType based on discovered content types.
     *
     * Maps the KDAV content types to CalendarType:
     * - VEVENT only -> Event
     * - VTODO only -> Todo
     * - Both or unknown -> Hybrid
     */
    CalendarType discoveredCalendarType(const QString &calendarId) const override;

    /**
     * @brief Check if discovered calendar is writable.
     *
     * Note: KDAV doesn't expose privilege information, so this returns true
     * by default. For accurate writable detection, use CalDavCapabilityDiscovery
     * which parses current-user-privilege-set.
     */
    bool discoveredWritable(const QString &calendarId) const override;

    // ========== Calendar Property Getters (for Property Sync) ==========

    /**
     * @brief Get the current color of a calendar.
     *
     * Returns the calendar color from the internal cache (populated during
     * discovery or after updateCalendar). For CalDAV, this corresponds to
     * the apple:calendar-color property.
     *
     * @param calendarId The calendar ID
     * @return Current calendar color, or invalid QColor if not set
     */
    QColor calendarColor(const QString &calendarId) const override;

    /**
     * @brief Get the current description of a calendar.
     *
     * Returns the calendar description. KDAV's DavCollection does not expose
     * a calendar-description property (only displayName, color, contentTypes,
     * privileges, and CTag), so this always returns empty string. A separate
     * PROPFIND for the calendar-description DAV property would be required to
     * populate this.
     *
     * @param calendarId The calendar ID
     * @return Current calendar description (always empty — not available from KDAV)
     */
    QString calendarDescription(const QString &calendarId) const override;

    void storeCalendars(const QString &collectionId,
                        const QList<KCalendarCore::MemoryCalendar*> &calendars) override;
    void startSync(const QString &collectionId,
                   KCalendarCore::MemoryCalendar *calendar,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                   const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                   const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;
    static const QString BackendTypeName;
    QString backendType() const override;
    QList<Kalburator::Shape::Shape> nativeShapes() const override;

    // Backend capabilities
    BackendCapabilities capabilities() const override;

    // Binding metadata support
    QStringList bindingMetadataKeys() const override;
    void populateBindingMetadata(const DiscoveredCalendar &discovered,
                                 CalendarBackendBinding &binding) const override;
    void prepareCreationMetadata(const QString &calendarId,
                                 CalendarBackendBinding &binding) const override;

    // Calendar CRUD operations (RFC 4791 MKCALENDAR / PROPPATCH / DELETE)
    bool supportsCalendarCreation() const override { return true; }
    bool createCalendar(const QString &collectionId, const QString &calendarId,
                        const QString &name,
                        CalendarType type = CalendarType::Hybrid) override;
    bool updateCalendar(const QString &collectionId, const QString &calendarId,
                        const QVariantMap &properties) override;
    bool deleteCalendar(const QString &collectionId, const QString &calendarId) override;

    // ========== Operation-Based API (Preferred) ==========
    // These return trackable operations and work with calendar IDs

    FetchOperation* fetchItems(const QString &calendarId) override;

    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items) override;

    DeleteOperation* deleteItems(const QString &calendarId,
                                 const QStringList &uids) override;

    // Debug/Raw ICS access
    QString getRawIcs(const QString &calendarId, const QString &uid) const override;
    bool setRawIcs(const QString &calendarId, const QString &uid,
                   const QString &icsContent) override;

    // =========================================================================
    // IBlobBackend overrides (Phase D Task 13)
    //
    // recordId     = uid (CalDAV uses uid.ics naming; href contains the uid)
    // collectionId = calendarId (maps to a registered CalDAV calendar URL)
    // data         = raw iCal bytes from a CalDAV GET
    // contentHash  = SHA-256 of the bytes (NOT the ETag — content equality)
    // lastModified = QDateTime::currentDateTimeUtc() (ETag-opaque; no getlastmodified)
    //
    // All methods that need network I/O wrap async KDAV jobs in QEventLoop::exec.
    // This is acceptable because the blob view is called from the engine worker (worker thread).
    // Phase F revisits true async; Phase D blocks on the worker thread.
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

    // Change detection — short-circuits on CTagStore when CTag is unchanged
    QList<BackendRecord> modifiedSince(const QString &collectionId,
                                       const QDateTime &since) override;
    QStringList          deletedSince(const QString &collectionId,
                                      const QDateTime &since) override;
    bool                 supportsDeleteTracking() const override { return false; }

    // Batch — network I/O is synchronous-per-call; no real batching in Phase D
    void beginBatch()    override {}
    bool commitBatch()   override { return true; }
    void rollbackBatch() override {}
    bool supportsBatch() const override { return false; }

signals:
    /**
     * @brief Emitted when a calendar is successfully created on the server.
     */
    void calendarCreated(const QString &collectionId, const QString &calendarId);

    /**
     * @brief Emitted when a calendar is successfully deleted from the server.
     */
    void calendarDeleted(const QString &collectionId, const QString &calendarId);

    /**
     * @brief Emitted when a calendar operation fails.
     */
    void calendarOperationError(const QString &calendarId, const QString &errorMessage);

private:
    // No SyncStore member — CTags have their own CTagStore below
    std::unique_ptr<CTagStore> m_ctags; // Owned; constructed lazily in setSyncStore()
    QUrl m_url;
    QString m_username;
    QString m_password;
    QMap<QString, KDAV::DavUrl> m_davUrls;
    QMap<QString, QColor> m_calendarColors;  // CalendarId -> discovered color
    QMap<QString, KDAV::DavCollection::ContentTypes> m_calendarContentTypes;  // CalendarId -> content types
    QMap<QString, QString> m_calendarCtags;  // CalendarId -> CTag from discovery
    // Calendars seeded via primeCalendars(); when non-empty, loadCalendars()
    // short-circuits the server walk and replays these directly (insertion
    // order preserved for deterministic calendarDiscovered emission).
    QStringList m_primedCalendarIds;
    std::shared_ptr<KDAV::EtagCache> m_etagCache;
    // Our own local etag cache: map from remote URL string to ETag
    QMap<QString, QString> m_localEtags;

    // ========== Content Cache for Delta Sync ==========
    // SQLite cache for item content to avoid re-fetching unchanged items
    QString m_cacheConnectionName;
    bool m_cacheInitialized = false;
    QString m_cacheDirOverride;  // when non-empty, overrides CacheLocation (setCacheDir)

    // Initialize the cache database (lazy, called on first fetchItems)
    void initContentCache();

    // Get cached iCal content for a URL (returns empty if not cached or stale)
    QString getCachedContent(const QString &itemUrl, const QString &expectedEtag) const;

    // Store iCal content in cache
    void setCachedContent(const QString &itemUrl, const QString &etag, const QString &icalContent);

    // Remove an item from the cache
    void removeCachedContent(const QString &itemUrl);

    // Helper to get our cached etag string for a remote item URL
    QString cachedEtag(const QString &remoteUrl) const;

    /**
     * @brief Normalize a URL for use as a cache key.
     *
     * Removes user credentials from the URL to ensure consistent cache keys.
     * KDAV's EtagCache internally uses URLs without credentials, so we must
     * match that format to avoid cache misses.
     *
     * @param urlString The URL string (may contain credentials)
     * @return Normalized URL string (credentials removed)
     */
    static QString normalizeUrlKey(const QString &urlString);

    // Serve all cached items for a calendar from the content cache SQLite database
    QList<KCalendarCore::Incidence::Ptr> serveCachedItems(const QString &calendarId, const KDAV::DavUrl &davUrl);

    QUrl generateItemUrl(const KDAV::DavUrl &davUrl, const QString &itemUid) const;
    KDAV::DavUrl configuredDavUrl(const QString &rawUrl);

    // Fresh CS:getctag via a Depth:0 PROPFIND on the calendar's URL (shared
    // by fetchItems and modifiedSince). Empty when unregistered or on failure.
    QString fetchFreshCtag(const QString &calendarId);

    // Principal-path collection URL for MKCALENDAR / PROPPATCH / DELETE
    // (Radicale-style /<username>/<calendarId>/ when the base URL has no
    // path; credentials stripped — they travel in the auth header).
    QUrl calendarUrlForCrud(const QString &calendarId) const;
};

} // namespace Kalburator::Sync

#endif // REMOTECALENDARBACKEND_H
