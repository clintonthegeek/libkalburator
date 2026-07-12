#ifndef REMOTECALENDARBACKEND_H
#define REMOTECALENDARBACKEND_H

#include "syncbackend.h"
#include "syncoperation.h"  // complete FetchOperation/DeleteOperation for covariant overrides
#include "backendrecord.h"
#include "collectioninfo.h"
#include "../sync/changedetection.h"
#include "../sync/writeoperation.h"  // E5.3: applyRecords() return type
#include "../sync/writerbatch.h"     // E5.3: applyRecords() batch parameter type
#include <KDAV/DavUrl>
#include <KDAV/DavCollection>
#include <KDAV/DavItem>
#include <KDAV/EtagCache>
#include <QUrl>
#include <QMap>
#include <QSet>
#include <QColor>
#include <QDateTime>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

class QNetworkAccessManager;
class KJob;

namespace Kalburator::Sync {

class CalDavContentCache;
class CTagStore;
struct BackendCapabilities;

class RemoteCalendarBackend : public SyncBackend,
                              public Kalburator::Sync::ChangeDetection
{
    Q_OBJECT

public:
    explicit RemoteCalendarBackend(const QUrl &url,
                           const QString &username,
                           const QString &password,
                           QObject *parent = nullptr);
    ~RemoteCalendarBackend() override;

    /**
     * @brief Set the DB file path so the private CTagStore can be initialised.
     * Must be called before using CTag-based sync optimizations.
     */
    void setDbPath(const QString &dbPath);

    /**
     * @brief Override the directory used for the delta-sync content cache.
     *
     * When set (non-empty), the cache DB lives under @p dir instead of
     * QStandardPaths::CacheLocation (see CalDavContentCache). The host app
     * uses this to keep the cache inside a per-collection profile folder.
     * Must be called before the first fetchItems() (which lazily opens it).
     */
    void setCacheDir(const QString &dir);

    /**
     * @brief Override the multiget REPORT chunk size (default 75).
     *
     * fetchItems() splits the changed-item href list into batches of at
     * most this many hrefs per DavItemsFetchJob/REPORT, run sequentially.
     * Test-only affordance so a small fixture can exercise multi-batch
     * behavior without generating hundreds of items (N4 fix).
     */
    void setMultigetChunkSize(int size);

    /**
     * @brief Override the QNAM transfer timeout (default 30s, H1.2/O22).
     *
     * Re-applies to the existing QNAM if one has already been created.
     * Test-only affordance so a dropped-request test doesn't have to wait
     * out the real 30s timeout.
     */
    void setTransferTimeoutMs(int ms);

    /**
     * @brief Re-entrancy depth of the backend's synchronous operation bodies
     * (E5.2 / audit B7).
     *
     * A backend-thread operation body (e.g. fetchItems') increments this on
     * entry and decrements on exit via a scoped guard. It is > 0 exactly while
     * such a body is on the stack. If a queued call is delivered onto the
     * backend thread and observes a value > 0, it was run *nested inside* a
     * suspended operation body — the B7 re-entrancy hazard a nested
     * QEventLoop::exec() creates. Once the fetch/CTag paths are async (E5.2)
     * the body returns to the event loop before any network wait, so any
     * queued call delivered during that wait observes 0.
     *
     * Only meaningful when read from the backend's own thread (the counter is
     * not synchronised — it is single-thread state by construction). Exposed
     * for the E5.2 re-entrancy pin and as a permanent regression tripwire.
     */
    int reentrancyDepth() const { return m_reentrancyDepth; }

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

    // ---- Sync::ChangeDetection ----
    // The engine's ONLY ctag entry points (consumed via
    // dynamic_cast<Sync::ChangeDetection*>). The backend's own ctag
    // accessors are private since Plan 7 T6 — one public face per concept.
    QString collectionRevision(const QString &collectionId) override;
    QMap<QString, QString>
    collectionRevisions(const QStringList &collectionIds) override;
    // E5.2 / audit B7 (amendment A6): the async fresh-revision query the engine
    // fast-path uses. Overrides ChangeDetection's default (which would adapt the
    // synchronous, nested-loop collectionRevisions) with a real
    // davSyncRequestAsync-based CTag PROPFIND — no backend-thread nested loop.
    void collectionRevisionsAsync(
        const QStringList &collectionIds,
        std::function<void(QMap<QString, QString>)> done) override;
    QString cachedCollectionRevision(const QString &collectionId) const override;

    /**
     * @brief Check if discovered calendar is writable.
     *
     * Note: KDAV doesn't expose privilege information, so this returns true
     * by default. For accurate writable detection, use CalDavCapabilityDiscovery
     * which parses current-user-privilege-set.
     */
    bool discoveredWritable(const QString &calendarId) const override;

    /// Aggregate discovery facts as one DTO (Plan 9). Self-contained read of
    /// the per-calendar CalendarFacts; supersedes the per-field getters above.
    DiscoveredCalendar discoveredCalendar(const QString &calendarId) const override;

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

    // Calendar CRUD operations (RFC 4791 MKCALENDAR / PROPPATCH / DELETE).
    // E11 (audit B7 / FINDINGS O39): the synchronous createCalendar/
    // updateCalendar/deleteCalendar overrides are gone — SyncBackend's base
    // default (return false) is an intentional poison pill. Everything now
    // goes through the Async trio below, which is the only form that talks
    // DAV without a nested QEventLoop (davSyncRequest died with them).
    bool supportsCalendarCreation() const override { return true; }
    void createCalendarAsync(const QString &collectionId, const QString &calendarId,
                             const QString &name, CalendarType type,
                             std::function<void(bool)> done) override;
    void updateCalendarAsync(const QString &collectionId, const QString &calendarId,
                             const QVariantMap &properties,
                             std::function<void(bool)> done) override;
    void deleteCalendarAsync(const QString &collectionId, const QString &calendarId,
                             std::function<void(bool)> done) override;

    // ========== Operation-Based API (Preferred) ==========
    // These return trackable operations and work with calendar IDs

    FetchOperation* fetchItems(const QString &calendarId) override;

    PushOperation* pushItems(const QString &calendarId,
                             const QList<KCalendarCore::Incidence::Ptr> &items) override;

    DeleteOperation* deleteItems(const QString &calendarId,
                                 const QStringList &uids) override;

    // E5.3 (audit B7 / CP-A): the engine's write path. Drives the existing
    // KDAV job chains (create -> DavItemCreateJob, update -> the new async
    // setRawIcsAsync, delete -> DavItemDeleteJob) through E5.1's per-collection
    // FIFO queue, fanned in exactly like pushItems/deleteItems. Supersedes
    // the engine ever calling RecordWriter::apply() against this backend.
    Kalburator::Sync::WriteOperation* applyRecords(const QString &collectionId,
                                                   const Kalburator::Sync::WriterBatch &batch) override;

    // Debug/Raw ICS access
    QString getRawIcs(const QString &calendarId, const QString &uid) const override;
    bool setRawIcs(const QString &calendarId, const QString &uid,
                   const QString &icsContent) override;

    // Async counterpart of setRawIcs() (E5.3): same wire behaviour (PUT with
    // If-Match on the cached ETag), but the continuation runs off
    // QNetworkReply::finished — no nested QEventLoop — via davSyncRequestAsync.
    // `done(true)` on 200/201/204, `done(false)` otherwise (network failure,
    // rejected write, or watchdog timeout). Only called from applyRecords()'s
    // update loop; exposed on the class (not file-local) so it can share
    // startJobWithWatchdog-equivalent timeout handling with `this` as context.
    void setRawIcsAsync(const QString &calendarId, const QString &uid,
                        const QByteArray &icsContent,
                        std::function<void(bool)> done);

    // =========================================================================
    // IBlobBackend overrides (Phase D Task 13)
    //
    // recordId     = uid (CalDAV uses uid.ics naming; href contains the uid)
    // collectionId = calendarId (maps to a registered CalDAV calendar URL)
    // data         = raw iCal bytes from a CalDAV GET
    // contentHash  = SHA-256 of the bytes (NOT the ETag — content equality)
    // lastModified = QDateTime::currentDateTimeUtc() (ETag-opaque; no getlastmodified)
    //
    // E5.3 update: createRecord()/deleteRecord() are direct synchronous
    // davSyncRequest() PUT/DELETE calls (no nested QEventLoop of their own);
    // updateRecord() routes through the (pre-existing, unchanged) synchronous
    // setRawIcs(), also davSyncRequest()-based. None of the three await a
    // KDAV job via QEventLoop::exec any more — the engine's own write path
    // never calls them at all (SyncEngineWorker::applyBatch calls
    // applyRecords() directly); they remain as synchronous IBlobBackend
    // entry points for other direct callers (dispatchFirstSync's inline blob
    // mirror, FilteredCollectionBackend forwarding), always invoked already
    // marshaled onto this backend's own thread.
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
    bool recordsFromLastFetch(const QString &collectionId,
                              QList<BackendRecord> &records,
                              QString &errorMessage) override;
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
    // ---- B7 re-entrancy guard (E5.2) ----
    // See reentrancyDepth(). Single-thread state (the backend's own thread);
    // never touched cross-thread.
    int m_reentrancyDepth = 0;
    struct ReentryGuard {
        int *depth;
        explicit ReentryGuard(int *d) : depth(d) { ++(*depth); }
        ~ReentryGuard() { --(*depth); }
        ReentryGuard(const ReentryGuard &) = delete;
        ReentryGuard &operator=(const ReentryGuard &) = delete;
    };

    // ---- Stored-CTag store (persisted change-detection tokens) ----
    // Private since Plan 7 T6: the engine reaches these only through the
    // Sync::ChangeDetection overrides above; nothing else ever called them.
    QString ctag(const QString &calendarId) const;
    void setCtag(const QString &calendarId, const QString &ctag);
    void clearCtag(const QString &calendarId);

    /**
     * @brief Fetch fresh CTags for multiple calendars in a single network operation.
     *
     * Groups the requested calendars by parent URL and issues one Depth:1
     * PROPFIND per group, collapsing N round-trips into K (K ≤ N, typically 1).
     * Calendars whose CTag the server did not return are absent from the
     * result; network failures yield an empty map. Synchronous (QEventLoop).
     */
    QMap<QString, QString> fetchAllCtags(const QStringList &calendarIds);

    /**
     * @brief Async twin of fetchAllCtags (E5.2 / audit B7, amendment A6).
     *
     * Same grouping-by-parent-URL and one-PROPFIND-per-group behaviour, but
     * each PROPFIND goes through davSyncRequestAsync — no nested QEventLoop on
     * the backend thread. @p done is invoked exactly once, on the backend
     * thread, after every group's reply has landed (a shared counter fans the
     * per-group continuations back in). Backs collectionRevisionsAsync.
     */
    void fetchAllCtagsAsync(const QStringList &calendarIds,
                            std::function<void(QMap<QString, QString>)> done);

    /**
     * @brief Second half of fetchItems(): merge network-fetched items with
     * cached content for the unchanged items, complete the operation.
     *
     * Split out of fetchItems() (N4 fix) so it can run once, after ALL
     * multiget batches have completed successfully — @p fetchedItemsMap must
     * be the complete accumulation across every batch; a caller must never
     * invoke this with a partial map (i.e. after a batch failure).
     *
     * @p davUrl is threaded through (E7/O36) so the completion can hand off
     * to bootstrapSyncTokenIfNeeded() before settling @p op.
     */
    void processFetchedItems(FetchOperation *op, const QString &calendarId,
                              const KDAV::DavUrl &davUrl,
                              const KDAV::DavItem::List &allItems,
                              const QMap<QString, QString> &serverEtags,
                              const QMap<QString, KDAV::DavItem> &fetchedItemsMap);

    // ---- Sync-token store (RFC 6578 sync-collection, E7/O36) ----
    // Backend cache-validity token, persisted alongside the CTag in the same
    // CTagStore row. NOTE the layering (load-bearing, see §10 of the
    // sync-excellence phase plan): this is NOT the engine's per-mapping H3
    // sync-progress token — the two must never be conflated. Private for the
    // same reason ctag()/setCtag()/clearCtag() are: nothing outside this
    // class touches it.
    QString syncToken(const QString &calendarId) const;
    void setSyncToken(const QString &calendarId, const QString &token);
    void clearSyncToken(const QString &calendarId);

    /**
     * @brief Probe DAV:supported-report-set for @p calendarId (E7/O36).
     *
     * Depth:0 PROPFIND issued once per calendar right after discovery.
     * Records whether the server advertises RFC 6578 sync-collection in
     * m_calendars[calendarId].supportsSyncCollection (false — the permanent-
     * fallback default — until/unless this observes it advertised). @p done
     * is invoked on the backend thread once the probe settles, success or
     * failure alike; a failed probe never fails the caller, it just leaves
     * the calendar on the CTag+listing fallback forever.
     */
    void probeSyncCollectionSupport(const QString &calendarId, std::function<void()> done);

    /**
     * @brief RFC 6578 REPORT sync-collection fetch path (E7/O36).
     *
     * Issued instead of continueFetchWithListing's Depth:1 listing when the
     * collection advertises the capability AND a stored sync-token exists.
     * Falls back to continueFetchWithListing on token invalidation (RFC
     * 6578 §3.3: HTTP 409/410/507) or any other unexpected/unparseable
     * response — the CTag+listing path is the permanent fallback, never
     * weakened by this addition.
     */
    void continueFetchWithSyncCollection(FetchOperation *op, const QString &calendarId,
                                         const KDAV::DavUrl &davUrl,
                                         const QString &freshCtag,
                                         const QString &storedToken);

    /**
     * @brief Second half of continueFetchWithSyncCollection() (E7/O36).
     *
     * Multigets the changed hrefs, then completes @p op. Tombstones in
     * @p deletedHrefs are applied by the caller before batching starts (no
     * multiget needed for a deletion). Commits @p newToken (and the CTag)
     * only when every changed href materialized — same N5 completeness
     * discipline as processFetchedItems, applied to the token instead of
     * just the CTag.
     */
    void completeSyncCollectionFetch(FetchOperation *op, const QString &calendarId,
                                     const KDAV::DavUrl &davUrl,
                                     const QMap<QString, QString> &changedHrefs,
                                     const QStringList &deletedHrefs,
                                     const QString &newToken,
                                     const QString &freshCtag,
                                     const QMap<QString, KDAV::DavItem> &fetchedItemsMap);

    /**
     * @brief Acquire an initial sync-token once a full listing settles (E7/O36).
     *
     * Design step 3's "capture from the initial REPORT": right after the
     * (one-time, per collection-lifetime) full listing fetch materializes
     * successfully, a sync-collection-capable calendar with no stored token
     * yet issues one empty-token REPORT to learn the server's current
     * sync-token, so every later cycle can use continueFetchWithSyncCollection
     * instead of relisting forever. A no-op (@p continuation runs
     * immediately) when the calendar isn't capable or already has a token.
     * @p continuation always runs eventually, regardless of the probe's
     * outcome.
     */
    void bootstrapSyncTokenIfNeeded(const QString &calendarId, const KDAV::DavUrl &davUrl,
                                    std::function<void()> continuation);

    // No SyncStore member — CTags have their own CTagStore below
    std::unique_ptr<CTagStore> m_ctags; // Owned; constructed lazily in setDbPath()

    /**
     * @brief Shared QNetworkAccessManager for all raw davSyncRequest() calls.
     *
     * Lazily created on first use (see nam()) so it acquires the thread
     * affinity of whichever thread first issues a DAV request — the backend
     * may be moveToThread()'d before that happens (D1), never after.
     * Parented to `this`: destroyed with the backend, relocates with it if
     * moved before first use.
     */
    mutable QNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager *nam() const;
    int m_transferTimeoutMs = 30000; // H1.2/O22 — see setTransferTimeoutMs()

    /**
     * @brief Start a KDAV job under a per-job transfer-timeout watchdog (H5.5/O25).
     *
     * KDAV's job classes run their traffic on KDAV's own internal network
     * stack — untouched by the setTransferTimeout() we apply to nam() (H1.2)
     * — and none of them override KJob::doKill(), so KJob::kill() is inert
     * (returns false, emits nothing). A frozen/never-answering server would
     * therefore hang the job, and with it the engine's fetch gate, forever
     * (O25, the live half of O22). This wraps @p job in a single-shot QTimer
     * of @p m_transferTimeoutMs; on expiry it detaches the job from our
     * result handlers (so its eventual — possibly never — real completion
     * cannot double-settle the operation) and runs @p onTimeout, which must
     * fail/settle the owning SyncOperation exactly as that site's normal
     * job-error branch would. Must be called on the backend's own thread
     * (where every job is created). @p onTimeout may be empty for
     * fire-and-forget jobs that own no operation.
     */
    void startJobWithWatchdog(KJob *job,
                              const std::function<void()> &onTimeout = {});

    QUrl m_url;
    QString m_username;
    QString m_password;
    int m_multigetChunkSize = 75; // N4 fix — see setMultigetChunkSize()

    /**
     * @brief One row of per-calendar discovery/registration state.
     *
     * Replaces four parallel maps (davUrl / color / contentTypes / pending
     * ctag — the backend-internal half of audit-supplement S2's "one href in
     * five maps"): one calendarId, one facts row.
     */
    struct CalendarFacts {
        KDAV::DavUrl davUrl;   ///< empty url == not registered (see davUrlFor)
        QColor color;          ///< invalid == undiscovered
        KDAV::DavCollection::ContentTypes contentTypes = {};
        /// False == never discovered/registered; the component getters then
        /// assume events+todos (the historical map-miss default).
        bool hasContentTypes = false;
        /// Discovery/fetch ctag awaiting persist-after-successful-fetch.
        QString pendingCtag;
        /// E7/O36: whether this collection's supported-report-set PROPFIND
        /// (probed once at discovery, see probeSyncCollectionSupport())
        /// advertised RFC 6578 sync-collection. False — the permanent CTag+
        /// listing fallback — for unprobed calendars (including every
        /// primed calendar: priming deliberately issues zero PROPFINDs, so
        /// it never runs this probe either).
        bool supportsSyncCollection = false;
        /// O42: true once probeSyncCollectionSupport() has completed for
        /// this calendar in THIS backend instance — at discovery or via
        /// fetchItems' lazy first-fetch probe. Guards the lazy probe so a
        /// non-advertising server is probed at most once per instance, not
        /// once per fetch cycle.
        bool syncCollectionProbed = false;
    };
    // QMap keeps key-sorted iteration: availableCollections() ordering and
    // the first-match-wins scans in loadRecord/updateRecord/deleteRecord
    // stay deterministic (same as the old QMap m_davUrls).
    QMap<QString, CalendarFacts> m_calendars;

    /// The registered DAV URL, or nullopt when the calendar has none.
    std::optional<KDAV::DavUrl> davUrlFor(const QString &calendarId) const;

    /// Resolve which registered calendar owns @p uid: first by the ETag map
    /// (an item written or fetched through this backend instance), then by
    /// the persistent content cache (an item fetched in a prior session).
    /// nullopt when no registered calendar shows any record of the uid —
    /// callers must FAIL rather than guess (O32: no try-all-calendars
    /// fallback, which could write/delete against the wrong calendar).
    std::optional<QString> findOwningCalendar(const QString &uid) const;

    /// Target URL for calendar-level CRUD (MKCALENDAR/PROPPATCH/DELETE):
    /// the registered per-calendar DAV URL when known (credentials stripped),
    /// else the derived calendarUrlForCrud(). Using the registered URL is
    /// essential for prefixed multiproto ids, whose literal value is NOT a valid
    /// path segment (concatenating it onto the base 404s).
    QUrl crudCalendarUrl(const QString &calendarId) const;

    // Calendars seeded via primeCalendars(); when non-empty, loadCalendars()
    // short-circuits the server walk and replays these directly (insertion
    // order preserved for deterministic calendarDiscovered emission).
    QStringList m_primedCalendarIds;

    // ETag pair, both load-bearing: KDAV's EtagCache feeds DavItemsListJob's
    // delta detection but exposes NO etag(url) getter, so m_localEtags is the
    // only readable store (If-Match headers, ownership probing). Every write
    // path updates both via noteItemWritten/noteItemErased.
    std::shared_ptr<KDAV::EtagCache> m_etagCache;
    QMap<QString, QString> m_localEtags;

    // E6/O35: calendars whose m_etagCache rows have already been seeded from
    // m_contentCache this backend instance's lifetime. Seeding is per-
    // collection-once — see continueFetchWithListing().
    QSet<QString> m_etagCacheSeededCalendars;

    // Persistent delta-sync payload cache (own SQLite connection; see
    // caldavcontentcache.h). Lazily opened on first fetchItems()/pushItems().
    std::unique_ptr<CalDavContentCache> m_contentCache;

    // Phase B5 finding: the last VERBATIM raw iCal bytes served for each uid
    // (whether from the network or the content cache), keyed by uid.
    // loadRecords() uses this instead of re-deriving bytes via
    // icalFromIncidence(incidence) when available. Re-deriving is NOT
    // equivalent to the original bytes: KCalendarCore::Incidence defaults
    // created()/lastModified() to the parse-time wall clock when the source
    // lacks those properties, and its ICalFormat writer unconditionally
    // regenerates DTSTAMP on every serialize (RFC 5545 semantics — DTSTAMP is
    // "when this representation was produced"). Re-serializing on every
    // loadRecords() call therefore bakes a fresh, non-deterministic
    // timestamp into bytes that are otherwise unchanged, making
    // BackendRecord.contentHash unstable across independent loadRecords()
    // calls for the SAME server-side content — silently defeating
    // convergence (see docs/campaign/2026-07-03-sync-convergence-roadmap.md
    // Phase B5). Populated by serveCachedItems(), the all-from-cache branch,
    // and processFetchedItems() — every site that parses raw ics text into
    // Incidence objects.
    QHash<QString, QByteArray> m_lastRawIcsByUid;

    // H5/O23: single-shot memo of the last successful fetchItems() per
    // collection, so recordsFromLastFetch() can serve it without a second
    // listing+multiget round trip. Populated by a finished-signal hook in
    // fetchItems() (fires uniformly across every completion branch — cache
    // hit, cache miss, full network fetch); cleared once served.
    QHash<QString, QList<BackendRecord>> m_lastFetchRecords;

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
    KDAV::DavUrl configuredDavUrl(const QString &rawUrl) const;

    // Fresh CS:getctag via a Depth:0 PROPFIND on the calendar's URL, async
    // (E5.2 / audit B7 — replaces the old synchronous nested-loop
    // fetchFreshCtag). @p done is invoked on the backend thread with the fresh
    // CTag, or an empty string when unregistered or on failure.
    void fetchFreshCtagAsync(const QString &calendarId,
                             std::function<void(const QString &)> done);

    // Second half of fetchItems(), split out (E5.2) so it can run either
    // synchronously (no stored CTag) or as the continuation of the async CTag
    // PROPFIND: lists items (DavItemsListJob), fetches changed items via
    // chunked multiget, and completes @p op. @p freshCtag is the CTag just
    // observed (empty if none), staged as pendingCtag for post-fetch commit.
    void continueFetchWithListing(FetchOperation *op, const QString &calendarId,
                                  const KDAV::DavUrl &davUrl,
                                  const QString &freshCtag);

    // Principal-path collection URL for MKCALENDAR / PROPPATCH / DELETE
    // (Radicale-style /<username>/<calendarId>/ when the base URL has no
    // path; credentials stripped — they travel in the auth header).
    QUrl calendarUrlForCrud(const QString &calendarId) const;

    // Post-write bookkeeping shared by the create/modify/push success paths:
    // both etag stores + the content cache. No-op when @p etag is empty.
    void noteItemWritten(const QString &urlKey, const QString &etag,
                         const QString &icalData);

    // Post-delete bookkeeping: evict from both etag stores and the content
    // cache (cache eviction normalized across all delete paths, Plan 7 T4).
    void noteItemErased(const QString &urlKey);

    // One startSync modify job (PUT with If-Match: @p etag). When
    // @p retryOn412 is set, a 412 retries once with If-Match: * (the
    // user-resolved-conflict force push); the retry itself never loops.
    void launchStartSyncModify(const QString &calId,
                               KCalendarCore::MemoryCalendar *calendar,
                               const KCalendarCore::Incidence::Ptr &inc,
                               const QString &etag, bool retryOn412,
                               const std::function<void()> &checkDone);
};

} // namespace Kalburator::Sync

#endif // REMOTECALENDARBACKEND_H
