#ifndef KALBURATOR_TESTS_FAKECALDAVSERVER_H
#define KALBURATOR_TESTS_FAKECALDAVSERVER_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

class QTcpSocket;

/**
 * @brief Minimal fake CalDAV server fixture for CalDavProvider tests.
 *
 * Listens on QHostAddress::LocalHost on a test-allocated random free port.
 * Handles the three PROPFIND requests CalDavCapabilityDiscovery walks,
 * plus CalDAV CRUD operations needed for E2E sync tests:
 *
 *   PROPFIND "/"                          -> current-user-principal href
 *   PROPFIND "/principals/users/testuser/" -> calendar-home-set href
 *   PROPFIND "/calendars/testuser/"       -> calendar list (Depth 1)
 *   REPORT  "/calendars/testuser/<cal>/"  -> calendar-query (ETag list)
 *                                            or calendar-multiget (full data)
 *   PUT     "/calendars/testuser/<cal>/<uid>.ics" -> store event
 *                                            (honors If-Match / If-None-Match
 *                                            preconditions — 412 on mismatch
 *                                            or on an existing resource with
 *                                            If-None-Match: *, E4/O32)
 *   MKCALENDAR <collection href>          -> 201 (or 405 if it exists)
 *   PROPPATCH  <collection href>          -> 207 if known, else 404
 *   DELETE     <collection href>          -> 204 if known, else 404
 *                                            (item DELETE unchanged)
 *
 * The default calendar is "Personal" at "/calendars/testuser/personal/".
 *
 * Configurable failure modes for negative tests:
 *   - setReturn401(true) : every request gets 401 Unauthorized
 *   - setReturn500(true) : every request gets 500 Internal Server Error
 *   - setCalendars(...)  : control which calendars are reported
 */
class FakeCalDavServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit FakeCalDavServer(QObject *parent = nullptr);
    ~FakeCalDavServer() override;

    /// Bind to localhost on a random free port. Returns true on success.
    bool startListening();

    /// e.g. "http://127.0.0.1:<port>/"
    QUrl baseUrl() const;

    /// Number of well-formed requests received for @p method (e.g. "PROPFIND",
    /// "REPORT", "PUT") since the last startListening(). Lets tests assert
    /// request shape — e.g. that a primed loadCalendars issues zero additional
    /// PROPFINDs beyond the connect-time discovery walk.
    int requestCount(const QByteArray &method) const;

    void setReturn401(bool on)   { m_return401 = on; }
    void setReturn500(bool on)   { m_return500 = on; }

    /// When true, every request is read off the socket and then dropped —
    /// no response is written and the socket is kept open (closing it would
    /// produce an immediate connection-reset error, not a stall). Simulates
    /// a server that accepted a connection but never replies, for QNAM
    /// transfer-timeout tests (H1.2/O22).
    void setDropRequests(bool on) { m_dropRequests = on; }

    /// Delay every response by @p ms before it's handled (via a deferred
    /// QTimer::singleShot on the server's own thread — never a blocking
    /// sleep, which would freeze the fake server's event loop instead of
    /// just simulating a slow network). D1 T1.5's GUI-stall probe uses this
    /// to inject latency a relocated backend must absorb without stalling
    /// whichever thread is polling for a freeze.
    void setResponseDelayMs(int ms) { m_responseDelayMs = ms; }

    /// E5.3: delay only requests of @p method (e.g. "PUT", "DELETE") by
    /// @p ms, leaving every other method's response timing at whatever
    /// setResponseDelayMs() (default 0) says. Lets a test isolate a slow
    /// WRITE phase from a fast READ/classify phase — setResponseDelayMs()
    /// alone can't do this (it delays every method uniformly), which made
    /// it impossible to land a cancel/teardown precisely "mid-apply"
    /// without also stalling the classify read that always precedes it.
    /// Pass ms <= 0 to clear a previously-set per-method delay.
    void setResponseDelayForMethod(const QByteArray &method, int ms)
    {
        if (ms > 0) {
            m_perMethodDelayMs[method] = ms;
        } else {
            m_perMethodDelayMs.remove(method);
        }
    }

    /// O45: serve at most ONE request at a time, FIFO across all
    /// connections — a request's response delay (setResponseDelayMs /
    /// setResponseDelayForMethod) blocks every request queued behind it.
    /// Models a single-threaded server (dev Radicale): a burst of N
    /// concurrent requests drains at one per delay, so the Nth request's
    /// wall-clock completion is N x delay after dispatch even though each
    /// individual request is fast once served.
    void setSerializeResponses(bool on) { m_serializeResponses = on; }

    /// E8/O28: kill the fake mid-push, simulating a SIGKILLed server
    /// process. After @p n item write requests (PUT create/update, or
    /// item DELETE) have had their response sent since the last
    /// startListening()/reviveOnSamePort(), the fake stops listening
    /// entirely (QTcpServer::close()) — every subsequent connection
    /// attempt gets ECONNREFUSED at the TCP level, exactly like a real
    /// client trying to reach a process that no longer exists (not
    /// setDropRequests()'s "accepted but silent" shape, which simulates a
    /// hung server, not a dead one). Pass 0 (the default) to disable.
    void setDieAfterNWrites(int n) { m_dieAfterNWrites = n; m_writesSinceRevive = 0; }

    /// Bring the fake back to life after setDieAfterNWrites() killed it —
    /// re-listens on the SAME port it was using before death (so a test's
    /// already-registered backend URL, captured from baseUrl() before the
    /// death, stays valid), and resets the die-after-N counter so a
    /// second round of writes gets a fresh budget. No-op (returns true)
    /// if the fake never died. Returns false if re-binding the captured
    /// port fails.
    bool reviveOnSamePort();

    /// Fail the Nth calendar-multiget REPORT (1-based) with a 500 response
    /// instead of serving it normally. 0 (the default) means never fail.
    /// Lets tests exercise N4's chunked-batch error path without needing a
    /// transport-level fault injector.
    void setFailNthMultigetReport(int n) { m_failNthMultigetReport = n; }

    /// Configure the CS:getctag value a Depth:0 PROPFIND on @p href reports
    /// (href must match one of the hrefs set via setCalendars()). Without a
    /// configured value, a Depth:0 PROPFIND on any collection 404s (matching
    /// this fake's original behavior — the backend just skips its CTag
    /// optimisation). Lets N5 tests drive the CTag-match/serve-path logic.
    void setCollectionCtag(const QString &href, const QString &ctagValue)
    {
        m_ctagByHref[href] = ctagValue;
    }

    /// Number of calendar-multiget REPORTs specifically (a subset of
    /// requestCount("REPORT"), which also counts calendar-query REPORTs —
    /// DavItemsListJob issues more than one of those per fetch for reasons
    /// unrelated to multiget chunking). Reset on startListening().
    int multigetReportCount() const { return m_multigetReportCount; }

    /// E7/O36: advertise RFC 6578 sync-collection in supported-report-set
    /// PROPFIND responses (Radicale >=3 and Nextcloud both do) and answer
    /// REPORT sync-collection requests with a real delta computed from this
    /// fake's per-collection change journal (see logChange()). Off by
    /// default so every pre-E7 test exercising the CTag+listing fallback is
    /// unaffected.
    void setSupportsSyncCollection(bool on) { m_supportsSyncCollection = on; }

    /// When true, every REPORT sync-collection carrying a non-empty
    /// sync-token gets HTTP 410 Gone instead of a delta — simulates RFC
    /// 6578 §3.3 token invalidation (e.g. after server-side DB maintenance
    /// expires old tokens).
    void setInvalidateSyncTokens(bool on) { m_invalidateSyncTokens = on; }

    /// Number of REPORT sync-collection requests received (a subset of
    /// requestCount("REPORT")). Reset on startListening().
    int syncCollectionReportCount() const { return m_syncCollectionReportCount; }

    /// Emulate a NextCloud-style deployment (RFC 6764 well-known discovery):
    ///   - the DAV endpoints live under @p contextPath (e.g. "/remote.php/dav")
    ///   - GET/PROPFIND "/.well-known/caldav" returns 301 -> "<contextPath>/"
    ///   - PROPFIND on the bare root "/" returns 405 (it is the web UI, not DAV)
    /// All principal/home/calendar hrefs are emitted under @p contextPath.
    /// Pass an empty string to restore the default root-served behavior.
    void setContextPath(const QString &contextPath) { m_contextPath = contextPath; }

    /// Each pair is (displayName, href). Default is one calendar
    /// "Personal" at "/calendars/testuser/personal/".
    void setCalendars(const QList<QPair<QString, QString>> &cals);

    /// Mark the listed calendar hrefs as read-only. The calendar-list PROPFIND
    /// then advertises a <current-user-privilege-set> containing only <read/>
    /// for those collections (no write/write-content/bind/unbind), so
    /// CalDavCapabilityDiscovery reports them as writable=false. Calendars not
    /// listed here emit no privilege-set and default to writable. hrefs must
    /// match those set via setCalendars() (e.g. "/calendars/testuser/work/").
    void setReadOnlyCalendars(const QStringList &hrefs) { m_readOnlyHrefs = hrefs; }

    /// Override the <cal:supported-calendar-component-set> advertised for one
    /// calendar href (e.g. {"VTODO"} for a tasks-only list). Calendars without
    /// an override keep the default VEVENT-only set. href must match one set
    /// via setCalendars().
    void setCalendarComponents(const QString &href, const QStringList &components)
    {
        m_componentsByHref.insert(href, components);
    }

    /// Pre-populate a calendar collection with iCal event blobs.
    /// Each blob must be a full VCALENDAR containing a UID property.
    /// collectionHref must match one of the hrefs set via setCalendars()
    /// (e.g. "/calendars/testuser/personal/").
    void setSeedEvents(const QString &collectionHref,
                       const QList<QByteArray> &events);

    /// Returns true if an event with the given UID exists in the collection.
    bool hasEvent(const QString &collectionHref, const QString &uid) const;

    /// Returns the raw iCal blobs currently stored for a collection.
    QList<QByteArray> storedEvents(const QString &collectionHref) const;

    /// Remove a previously-seeded event from a collection out-of-band (i.e.
    /// without going through a client DELETE request). Simulates another
    /// client deleting an item directly on the server. No-op if the uid
    /// isn't present. Lets Phase B5 convergence tests exercise "remote
    /// delete" without a real second client.
    void removeEvent(const QString &collectionHref, const QString &uid);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    struct IcsRecord {
        QByteArray data;
        QString    etag;
    };

    void handleRequest(QTcpSocket *socket, const QByteArray &fullRequest);
    /// E8/O28: counts one item write toward setDieAfterNWrites()'s budget;
    /// closes the listening socket once the budget is exhausted.
    void maybeDieAfterWrite();
    void handleReport(QTcpSocket *socket, const QString &path,
                      const QByteArray &body);
    void handleSyncCollectionReport(QTcpSocket *socket, const QString &collectionHref,
                                    const QByteArray &body);
    void handlePut(QTcpSocket *socket, const QString &path,
                   const QByteArray &body, const QByteArray &headers);
    void handleDelete(QTcpSocket *socket, const QString &path);
    void handleMkCalendar(QTcpSocket *socket, const QString &path);
    void handleProppatch(QTcpSocket *socket, const QString &path);
    bool isKnownCollection(const QString &href) const;
    void writeResponse(QTcpSocket *socket,
                       int statusCode,
                       const QByteArray &reasonPhrase,
                       const QByteArray &body,
                       const QByteArray &extraHeaders = QByteArray());

    QString xmlForPrincipal() const;
    QString xmlForHome() const;
    QString xmlForCalendars() const;
    QByteArray xmlForCalendarQuery(const QString &collectionHref) const;
    QByteArray xmlForCalendarMultiget(const QString &collectionHref,
                                      const QList<QString> &hrefs) const;
    QByteArray xmlForCtag(const QString &collectionHref) const;
    QByteArray xmlForSupportedReportSet(const QString &collectionHref) const;
    QByteArray xmlForSyncCollection(const QString &collectionHref, int clientToken) const;

    /// Append one mutation to @p collectionHref's change journal (E7/O36).
    /// Every PUT/DELETE and every setSeedEvents()/removeEvent() call (real
    /// writes and out-of-band server-side simulation alike) logs here; the
    /// journal's length IS the collection's current sync-token, and REPORT
    /// sync-collection answers a stored token T with every entry after
    /// index T, deduped to the last state per uid.
    void logChange(const QString &collectionHref, const QString &uid, bool deleted);

    static QString uidFromIcs(const QByteArray &ics);
    static QString uidFromPath(const QString &path);
    static QString makeEtag(const QByteArray &data);
    static QList<QString> parseHrefsFromBody(const QByteArray &body);
    static QString parseSyncTokenFromBody(const QByteArray &body);
    /// Case-insensitive header lookup over the raw header block (everything
    /// before "\r\n\r\n"). Empty if the header is absent.
    static QByteArray headerValue(const QByteArray &headers, const QByteArray &name);

    /// O45: delay (in ms) for @p request per the per-method override map,
    /// falling back to the uniform m_responseDelayMs.
    int delayForRequest(const QByteArray &request) const;

    /// O45: dequeue-and-serve loop for setSerializeResponses(true).
    void processSerialQueue();

    bool m_return401 = false;
    bool m_return500 = false;
    bool m_dropRequests = false;
    bool m_serializeResponses = false;   // O45
    bool m_serialBusy = false;           // O45
    QList<QPair<QPointer<QTcpSocket>, QByteArray>> m_serialQueue;  // O45
    int m_responseDelayMs = 0;
    int m_dieAfterNWrites = 0;     // E8/O28: 0 = never die
    int m_writesSinceRevive = 0;   // reset by setDieAfterNWrites()/reviveOnSamePort()
    quint16 m_lastBoundPort = 0;   // captured in startListening() for reviveOnSamePort()
    QHash<QByteArray, int> m_perMethodDelayMs;  // E5.3: per-method response delay override
    int m_failNthMultigetReport = 0;    // 0 = never fail; else 1-based index
    int m_multigetReportCount = 0;      // reset on startListening()
    QHash<QString, QString> m_ctagByHref;  // href -> CS:getctag value, if configured
    QString m_contextPath;  // empty => DAV served at root; else NextCloud-style
    QList<QPair<QString, QString>> m_calendars;
    QSet<QString> m_createdCollections;  // hrefs created via MKCALENDAR
    QStringList m_readOnlyHrefs;  // hrefs advertised with read-only privilege-set
    QHash<QString, QStringList> m_componentsByHref;  // component-set overrides
    QHash<QByteArray, int> m_requestCounts;  // method -> count, reset on startListening()

    /// Keyed by collectionHref (e.g. "/calendars/testuser/personal/")
    /// then by UID.
    QHash<QString, QHash<QString, IcsRecord>> m_store;

    // ---- E7/O36: RFC 6578 sync-collection ----
    struct ChangeEntry {
        QString uid;
        bool deleted = false;
    };
    bool m_supportsSyncCollection = false;
    bool m_invalidateSyncTokens = false;
    int m_syncCollectionReportCount = 0;  // reset on startListening()
    /// collectionHref -> chronological mutations. The journal's size() at
    /// any moment IS that collection's current sync-token (as a decimal
    /// string); a REPORT with client token T is answered with entries
    /// [T, size()).
    QHash<QString, QList<ChangeEntry>> m_changeLog;
};

#endif // KALBURATOR_TESTS_FAKECALDAVSERVER_H
