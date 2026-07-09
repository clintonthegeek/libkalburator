#include "remotecalendarbackend.h"
#include "caldavcontentcache.h"
#include "icalcodec.h"
#include "icaltimestamp.h"
#include "syncoperation.h"
#include "backendcapabilities.h"
#include "logicalcalendar.h"
#include "discoveredcalendar.h"
#include "backendrecord.h"
#include "collectioninfo.h"

#include <KDAV/DavCollectionsFetchJob>
#include <KDAV/DavItemsListJob>
#include <KDAV/DavItemsFetchJob>
#include <KDAV/DavJobBase>
#include <KDAV/DavItemCreateJob>
#include <KDAV/DavItemModifyJob>
#include <KDAV/DavItemDeleteJob>
#include <KCalendarCore/ICalFormat>
#include <KIO/Job>
#include <KJob>

#include <QPointer>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QSqlQuery>
#include <QSqlError>
#include <QXmlStreamReader>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QTimeZone>

namespace {

// Returns the parent collection URL for a calendar URL.
// Strips the last path segment (plus its trailing slash, if any) so that
// sibling calendars under the same principal are grouped together and served
// by a single Depth:1 PROPFIND.
QUrl parentUrl(const QUrl &url)
{
    QString path = url.path();
    if (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    int lastSlash = path.lastIndexOf(QLatin1Char('/'));
    if (lastSlash <= 0) return url;
    QUrl parent = url;
    parent.setPath(path.left(lastSlash + 1));
    return parent;
}

} // anonymous namespace

namespace Kalburator::Sync {

// Forward declaration: defined in the anonymous namespace near loadRecords()
// below, needed by the recordsFromLastFetch() memo hook in fetchItems().
namespace { BackendRecord blobRecordFromIcal(const QString &uid, const QByteArray &icalBytes); }

// ============================================================================
// CTagStore — private inner store for per-backend CalDAV CTags
//
// Persists to a `remote_ctags` table in the .kalburator-sync.db file.
// BackendId is fixed at construction time so callers only pass calendarId.
// ============================================================================

class CTagStore
{
public:
    explicit CTagStore(const QString &dbPath, const QString &backendId)
        : m_dbPath(dbPath)
        , m_backendId(backendId)
        , m_connectionName(QStringLiteral("CTagStore_%1_%2")
                               .arg(backendId)
                               .arg(reinterpret_cast<quintptr>(this)))
    {
        if (dbPath.isEmpty()) {
            qWarning() << "CTagStore: empty dbPath for backend" << backendId;
        }
    }

    ~CTagStore()
    {
        if (QSqlDatabase::contains(m_connectionName)) {
            QSqlDatabase::database(m_connectionName).close();
            QSqlDatabase::removeDatabase(m_connectionName);
        }
    }

    QString get(const QString &calendarId)
    {
        if (!ensureOpen()) return QString();
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT ctag FROM remote_ctags "
            "WHERE backend_id = ? AND calendar_id = ?"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        if (q.exec() && q.next())
            return q.value(0).toString();
        return QString();
    }

    // E7/O36: update-else-insert rather than the old INSERT OR REPLACE — a
    // REPLACE re-inserts the whole row, which would silently null out
    // sync_token (a column NOT in this statement's column list) on every
    // plain CTag commit. UPDATE only touches the ctag column, so a
    // previously-set sync_token survives a CTag-only commit untouched.
    bool set(const QString &calendarId, const QString &ctag)
    {
        if (!ensureOpen()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);

        QSqlQuery upd(db);
        upd.prepare(QStringLiteral(
            "UPDATE remote_ctags SET ctag = ? WHERE backend_id = ? AND calendar_id = ?"));
        upd.addBindValue(ctag);
        upd.addBindValue(m_backendId);
        upd.addBindValue(calendarId);
        if (!upd.exec()) {
            qWarning() << "CTagStore::set failed:" << upd.lastError().text();
            return false;
        }
        if (upd.numRowsAffected() > 0) return true;

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO remote_ctags (backend_id, calendar_id, ctag) VALUES (?, ?, ?)"));
        ins.addBindValue(m_backendId);
        ins.addBindValue(calendarId);
        ins.addBindValue(ctag);
        if (!ins.exec()) {
            qWarning() << "CTagStore::set insert failed:" << ins.lastError().text();
            return false;
        }
        return true;
    }

    bool clear(const QString &calendarId)
    {
        if (!ensureOpen()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "DELETE FROM remote_ctags "
            "WHERE backend_id = ? AND calendar_id = ?"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        return q.exec();
    }

    // ---- E7/O36: RFC 6578 sync-token, persisted alongside the CTag ----

    QString getToken(const QString &calendarId)
    {
        if (!ensureOpen()) return QString();
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT sync_token FROM remote_ctags "
            "WHERE backend_id = ? AND calendar_id = ?"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        if (q.exec() && q.next())
            return q.value(0).toString();
        return QString();
    }

    bool setToken(const QString &calendarId, const QString &token)
    {
        if (!ensureOpen()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);

        QSqlQuery upd(db);
        upd.prepare(QStringLiteral(
            "UPDATE remote_ctags SET sync_token = ? "
            "WHERE backend_id = ? AND calendar_id = ?"));
        upd.addBindValue(token);
        upd.addBindValue(m_backendId);
        upd.addBindValue(calendarId);
        if (!upd.exec()) {
            qWarning() << "CTagStore::setToken failed:" << upd.lastError().text();
            return false;
        }
        if (upd.numRowsAffected() > 0) return true;

        // No row yet for this (backend_id, calendar_id) — the sync-token
        // bootstrap can land before the first CTag commit (it runs right
        // after the first full listing, same fetch cycle). Insert a row
        // with an empty ctag; the CTag commit that follows uses the same
        // update-else-insert set() above and will not clobber this token.
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO remote_ctags (backend_id, calendar_id, ctag, sync_token) "
            "VALUES (?, ?, '', ?)"));
        ins.addBindValue(m_backendId);
        ins.addBindValue(calendarId);
        ins.addBindValue(token);
        if (!ins.exec()) {
            qWarning() << "CTagStore::setToken insert failed:" << ins.lastError().text();
            return false;
        }
        return true;
    }

    bool clearToken(const QString &calendarId)
    {
        if (!ensureOpen()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "UPDATE remote_ctags SET sync_token = NULL "
            "WHERE backend_id = ? AND calendar_id = ?"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        return q.exec();
    }

private:
    // Opens the SQLite connection on first use — deferred past construction
    // so the connection's thread affinity is whichever thread first calls
    // get/set/clear (the backend's own thread, post-D1-relocation), not
    // whichever thread happened to call setDbPath() (D1 T1.2).
    bool ensureOpen()
    {
        if (m_openAttempted) return m_open;
        m_openAttempted = true;

        if (m_dbPath.isEmpty()) return false;

        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            qWarning() << "CTagStore: failed to open" << m_dbPath
                       << ":" << db.lastError().text();
            QSqlDatabase::removeDatabase(m_connectionName);
            return false;
        }
        m_open = ensureSchema();
        return m_open;
    }

    bool ensureSchema()
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        bool ok = q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS remote_ctags ("
            "  backend_id   TEXT NOT NULL,"
            "  calendar_id  TEXT NOT NULL,"
            "  ctag         TEXT NOT NULL,"
            "  PRIMARY KEY (backend_id, calendar_id)"
            ")"));
        if (!ok) {
            qWarning() << "CTagStore::ensureSchema failed:" << q.lastError().text();
            return false;
        }
        return ensureSyncTokenColumn(db);
    }

    // E7/O36: additive, self-migrating column for the RFC 6578 sync-token.
    // SQLite has no "ADD COLUMN IF NOT EXISTS", so probe the column set via
    // PRAGMA table_info first — idempotent, safe on every open. Same pattern
    // as BaselineStore::ensureSchemaV6's source_hash/target_hash columns.
    bool ensureSyncTokenColumn(QSqlDatabase &db)
    {
        bool hasSyncToken = false;
        {
            QSqlQuery info(db);
            if (!info.exec(QStringLiteral("PRAGMA table_info(remote_ctags)"))) {
                qWarning() << "CTagStore::ensureSyncTokenColumn: table_info failed:"
                           << info.lastError().text();
                return false;
            }
            while (info.next()) {
                if (info.value(1).toString() == QLatin1String("sync_token")) {
                    hasSyncToken = true;
                    break;
                }
            }
        }
        if (hasSyncToken) return true;

        QSqlQuery alter(db);
        if (!alter.exec(QStringLiteral(
                "ALTER TABLE remote_ctags ADD COLUMN sync_token TEXT"))) {
            qWarning() << "CTagStore::ensureSyncTokenColumn: ADD COLUMN failed:"
                       << alter.lastError().text();
            return false;
        }
        return true;
    }

    QString m_dbPath;
    QString m_backendId;
    QString m_connectionName;
    bool m_openAttempted = false;
    bool m_open = false;
};

// (CTagStore class ends above; RemoteCalendarBackend methods continue below in the same namespace)

static int getHttpStatusCode(KJob *job)
{
    if (auto kioJob = qobject_cast<KIO::Job *>(job)) {
        const KIO::MetaData metadata = kioJob->metaData();
        QVariant statusVar = metadata.value(QStringLiteral("HTTP-STATUS"));
        if (statusVar.isValid()) {
            return statusVar.toInt();
        }
    }
    return 0; // No valid HTTP status found
}

// Helper to safely log URLs without exposing passwords
static QString safeUrlString(const QUrl &url)
{
    return url.toString(QUrl::RemovePassword);
}

namespace {

// One synchronous DAV round-trip. Collapses the QNetworkAccessManager +
// Basic-auth + QEventLoop boilerplate that was duplicated across every
// calendar-CRUD / ctag-PROPFIND / raw-ICS method (AUDIT MAJOR; Plan 7 T3).
// Credentials never travel in the URL; they go in the Authorization header,
// matching every site this replaces.
struct DavResponse {
    int status = 0;                       // HTTP status; 0 = no HTTP response
    QByteArray body;
    QString etag;                         // response ETag header, unquoted
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString errorString;
    bool transportOk() const { return error == QNetworkReply::NoError; }
};

// Build the authenticated QNetworkRequest shared by the sync and async DAV
// entry points. Credentials go in the Authorization header, never the URL.
QNetworkRequest buildDavRequest(const QUrl &url,
                                const QString &username, const QString &password,
                                bool hasBody,
                                const QList<std::pair<QByteArray, QByteArray>> &rawHeaders,
                                const QByteArray &contentType)
{
    QUrl cleanUrl = url;
    cleanUrl.setUserInfo(QString());

    QNetworkRequest request(cleanUrl);
    if (hasBody) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QString::fromLatin1(contentType));
    }
    const QString credentials = username + QLatin1Char(':') + password;
    request.setRawHeader("Authorization",
                         "Basic " + credentials.toUtf8().toBase64());
    for (const auto &h : rawHeaders) {
        request.setRawHeader(h.first, h.second);
    }
    return request;
}

// Populate a DavResponse from a finished reply (shared by sync and async).
DavResponse davResponseFromReply(QNetworkReply *reply)
{
    DavResponse resp;
    resp.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    resp.error = reply->error();
    if (resp.error != QNetworkReply::NoError) {
        resp.errorString = reply->errorString();
    }
    resp.body = reply->readAll();
    QString etag = QString::fromUtf8(reply->rawHeader("ETag"));
    if (etag.startsWith(QLatin1Char('"')) && etag.endsWith(QLatin1Char('"'))) {
        etag = etag.mid(1, etag.length() - 2);
    }
    resp.etag = etag;
    return resp;
}

// Asynchronous DAV round-trip (E5.2 / audit B7). Same wire behaviour as
// davSyncRequest() below, but with NO nested QEventLoop: @p done runs from the
// reply's finished signal on the backend thread's own event loop, so the
// thread stays free to process other work during the network wait instead of
// re-entering the in-flight caller's stack. This is the primitive the fetch /
// CTag paths use; callers that need a synchronous answer block on a NON-backend
// thread (worker/GUI) while this chain runs (see the engine's operation gates).
void davSyncRequestAsync(QNetworkAccessManager *nam, const QUrl &url,
                         const QByteArray &verb,
                         const QString &username, const QString &password,
                         const QByteArray &body,
                         const QList<std::pair<QByteArray, QByteArray>> &rawHeaders,
                         const QByteArray &contentType,
                         std::function<void(const DavResponse &)> done)
{
    // Continuation runs on nam's thread; the request must be issued there too.
    Q_ASSERT(QThread::currentThread() == nam->thread());

    QNetworkRequest request =
        buildDavRequest(url, username, password, !body.isEmpty(), rawHeaders, contentType);

    QNetworkReply *reply = nam->sendCustomRequest(request, verb, body);
    // Context object = nam (backend thread), so the continuation is delivered
    // on the backend thread and is auto-disconnected if the backend/nam dies.
    QObject::connect(reply, &QNetworkReply::finished, nam,
                     [reply, done = std::move(done)]() {
        const DavResponse resp = davResponseFromReply(reply);
        reply->deleteLater();
        done(resp);
    });
}

DavResponse davSyncRequest(QNetworkAccessManager *nam, const QUrl &url,
                           const QByteArray &verb,
                           const QString &username, const QString &password,
                           const QByteArray &body = {},
                           const QList<std::pair<QByteArray, QByteArray>> &rawHeaders = {},
                           const QByteArray &contentType =
                               QByteArrayLiteral("application/xml; charset=utf-8"))
{
    // E11 correction (2026-07-09): the phase plan's §14b claimed Group C
    // (create/update/deleteCalendar) was this helper's LAST caller and
    // scheduled its deletion once Group C converted. That was stale — by
    // the time E11 landed, Group C had already stopped being the only
    // survivor: fetchAllCtags() (A6, deliberately kept synchronous, tripwire-
    // guarded), getRawIcs()/setRawIcs() (debug-only accessors), and
    // createRecord()/deleteRecord() (E5.3's documented top-level-bridge
    // deviation, see the comment above this function) all still call it
    // directly and are NOT B7 hazards (none are invoked from inside an
    // in-flight operation's own body). Group C's calls converted to
    // davSyncRequestAsync in E11 and this helper's nested QEventLoop no
    // longer survives group C, but the helper itself stays — it has five
    // legitimate non-reentrant callers left. Every current caller runs on
    // the backend's (== nam's) own thread; the assert turns a future
    // thread-affinity bug into a loud failure instead of a silent race (D1,
    // invariant §1.1).
    Q_ASSERT(QThread::currentThread() == nam->thread());

    QNetworkRequest request =
        buildDavRequest(url, username, password, !body.isEmpty(), rawHeaders, contentType);

    QEventLoop loop;
    DavResponse resp;

    QNetworkReply *reply = nam->sendCustomRequest(request, verb, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        resp = davResponseFromReply(reply);
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();
    return resp;
}

// N4 fix — a truthful error message for a failed KDAV job: the KJob-level
// message alone collapses distinct failures into confusing text (a real
// example: an HTTP/2 stream reset on a 673-href multiget surfaced to the app
// as "Invalid username/password (401)", though no 401 ever occurred and no
// credentials were wrong). DavJobBase::latestResponseCode() is 0 when the
// failure never reached the HTTP level (a transport error), so include both
// pieces of information rather than picking one.
QString davJobErrorMessage(KDAV::DavJobBase *job)
{
    const int httpStatus = job->latestResponseCode();
    if (httpStatus > 0) {
        return QStringLiteral("%1 (HTTP %2)").arg(job->errorString()).arg(httpStatus);
    }
    return QStringLiteral("%1 (no HTTP response — transport-level failure)")
        .arg(job->errorString());
}

// href -> ctag entries from a 207 multistatus PROPFIND response. Compares
// local element names only, so any namespace prefix (or none) matches.
QMap<QString, QString> parseCtagMultistatus(const QByteArray &xml)
{
    QMap<QString, QString> result;
    QXmlStreamReader reader(xml);
    QString currentHref;
    QString currentCtag;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == u"response") {
                currentHref.clear();
                currentCtag.clear();
            } else if (reader.name() == u"href") {
                currentHref = reader.readElementText();
            } else if (reader.name() == u"getctag") {
                currentCtag = reader.readElementText();
            }
        } else if (reader.isEndElement() && reader.name() == u"response") {
            if (!currentHref.isEmpty() && !currentCtag.isEmpty()) {
                result.insert(currentHref, currentCtag);
            }
        }
    }
    if (reader.hasError()) {
        qWarning() << "parseCtagMultistatus: XML parse error:" << reader.errorString();
    }
    return result;
}

const QByteArray kCtagPropfindBody = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
    "<D:propfind xmlns:D=\"DAV:\" xmlns:CS=\"http://calendarserver.org/ns/\">"
    "  <D:prop><CS:getctag/></D:prop>"
    "</D:propfind>");

// E7/O36: capability-detection PROPFIND — asks a collection which REPORTs
// it supports (RFC 3253 §3.1.5). Radicale >=3 and Nextcloud both include
// sync-collection in the answer.
const QByteArray kSupportedReportSetPropfindBody = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
    "<D:propfind xmlns:D=\"DAV:\">"
    "  <D:prop><D:supported-report-set/></D:prop>"
    "</D:propfind>");

// True if a supported-report-set PROPFIND response (@p xml) advertises the
// RFC 6578 sync-collection REPORT. Namespace-agnostic (local element name
// only), matching this file's other multistatus parsers.
bool parseSupportsSyncCollection(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == u"sync-collection") {
            return true;
        }
    }
    return false;
}

// One RFC 6578 sync-collection REPORT's parsed delta.
struct SyncCollectionDelta {
    QMap<QString, QString> changedHrefs; // href (path) -> getetag
    QStringList deletedHrefs;            // href (path); a 404-status response
    QString newToken;                    // the multistatus's sibling <D:sync-token>
};

// Parses a REPORT sync-collection 207 multistatus body. <D:response> entries
// with a 404 status are deletion tombstones (RFC 6578 §3.6); everything else
// with a getetag is a changed href. <D:sync-token> is a sibling of the
// <D:response> elements, not nested inside one.
SyncCollectionDelta parseSyncCollectionMultistatus(const QByteArray &xml)
{
    SyncCollectionDelta delta;
    QXmlStreamReader reader(xml);
    QString currentHref;
    QString currentEtag;
    QString currentStatus;
    bool inResponse = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == u"response") {
                inResponse = true;
                currentHref.clear();
                currentEtag.clear();
                currentStatus.clear();
            } else if (inResponse && reader.name() == u"href") {
                currentHref = reader.readElementText();
            } else if (inResponse && reader.name() == u"getetag") {
                currentEtag = reader.readElementText();
            } else if (inResponse && reader.name() == u"status") {
                currentStatus = reader.readElementText();
            } else if (!inResponse && reader.name() == u"sync-token") {
                delta.newToken = reader.readElementText();
            }
        } else if (reader.isEndElement() && reader.name() == u"response") {
            inResponse = false;
            if (currentHref.isEmpty()) continue;
            if (currentStatus.contains(QStringLiteral("404"))) {
                delta.deletedHrefs << currentHref;
            } else if (!currentEtag.isEmpty()) {
                delta.changedHrefs.insert(currentHref, currentEtag);
            }
        }
    }
    if (reader.hasError()) {
        qWarning() << "parseSyncCollectionMultistatus: XML parse error:" << reader.errorString();
    }
    return delta;
}

// E5.3 (audit B7 / CP-A, 2026-07-08) DEVIATION — documented exception:
// createRecord()/deleteRecord() no longer use this (reimplemented as direct
// davSyncRequest() calls above); the engine's live write path never calls
// this helper at all (SyncEngineWorker::applyBatch calls
// SyncBackendBase::applyRecords() directly). The ONE remaining call site is
// loadRecords() below, kept deliberately: it is a top-level, non-reentrant
// synchronous bridge (fetchItems() + wait), never invoked from INSIDE an
// in-flight operation's own body — i.e. not an instance of the B7 hazard
// this campaign targets (nested loops that pump a queued call mid-wait
// while a *suspended operation's* ReentryGuard is held). loadRecords() is a
// genuine, still-directly-tested public IBlobBackend entry point (20+ call
// sites across tests/calendar/tst_remotecalendarbackend_blob_view.cpp and
// others call it directly, not through the engine) with no synchronous
// replacement that avoids hand-rolling a second REPORT/multiget XML client
// duplicating continueFetchWithListing's already-tested logic — reducing it
// to a stub would break those tests, which the acceptance gate's "full
// suite green" also requires. The phase's "grep awaitOperation empty" gate
// is amended accordingly: empty except this one, loadRecords()-only,
// non-nested call.
bool awaitOperation(Kalburator::Sync::SyncOperation *op)
{
    if (!op->isFinished()) {
        QEventLoop loop;
        QObject::connect(op, &Kalburator::Sync::SyncOperation::finished,
                         &loop, &QEventLoop::quit);
        loop.exec();
    }
    return op->state() == Kalburator::Sync::SyncOperation::Succeeded;
}

// Affiliate a freshly-created operation with `owner`'s thread.
//
// The op-based API (fetchItems/pushItems/deleteItems) is driven from a sync
// worker thread via the blob-view CRUD adapters, but the backend itself lives
// on the owning (main) thread, and each operation's completion lambda —
// QMetaObject::invokeMethod(this, ...) — runs there and emits `finished` from
// there. Parenting the op to the backend across that boundary is illegal
// ("QObject: Cannot create children for a parent that is in a different
// thread") and strands the op on the caller's thread. So the op is created
// unparented and pushed onto the backend thread; this is a no-op when the API
// is called from the backend thread directly (the common, primed-provider
// path). Ownership: the synchronous adapters deleteLater() the op they await;
// there are no raw async consumers relying on parent-based cleanup.
template <typename Op>
Op *onOwnerThread(Op *op, const QObject *owner)
{
    if (op->thread() != owner->thread()) {
        op->moveToThread(owner->thread());
    }
    return op;
}

} // anonymous namespace

// Constructor
RemoteCalendarBackend::RemoteCalendarBackend(const QUrl &url,
                             const QString &username,
                             const QString &password,
                             QObject *parent)
    : SyncBackend(parent)
    , m_url(url)
    , m_username(username)
    , m_password(password)
    , m_etagCache(std::make_shared<KDAV::EtagCache>())
    , m_contentCache(std::make_unique<CalDavContentCache>(url.host() + url.path()))
{
    m_url.setUserName(m_username);
    m_url.setPassword(m_password);
    qDebug() << "RemoteCalendarBackend initialized with URL:" << safeUrlString(m_url);
}

RemoteCalendarBackend::~RemoteCalendarBackend() = default;

QNetworkAccessManager *RemoteCalendarBackend::nam() const
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(const_cast<RemoteCalendarBackend *>(this));
        m_nam->setTransferTimeout(m_transferTimeoutMs);
    }
    return m_nam;
}

void RemoteCalendarBackend::setTransferTimeoutMs(int ms)
{
    m_transferTimeoutMs = ms;
    if (m_nam) {
        m_nam->setTransferTimeout(m_transferTimeoutMs);
    }
}

void RemoteCalendarBackend::startJobWithWatchdog(KJob *job,
                                                 const std::function<void()> &onTimeout)
{
    // H5.5/O25: KDAV jobs carry their traffic on KDAV's internal network
    // stack (not our nam(), so H1.2's setTransferTimeout never reaches them)
    // and do not override KJob::doKill() — kill() is a no-op that emits
    // nothing, so we cannot lean on the job's own error path. A single-shot
    // watchdog fails the operation directly instead. m_transferTimeoutMs<=0
    // disables the watchdog (unbounded wait, as before H5.5).
    if (m_transferTimeoutMs > 0) {
        auto *watchdog = new QTimer(this);
        watchdog->setSingleShot(true);
        watchdog->setInterval(m_transferTimeoutMs);
        const int timeoutMs = m_transferTimeoutMs;

        // Normal completion: tear the watchdog down.
        connect(job, &KJob::result, watchdog, [watchdog]() {
            watchdog->stop();
            watchdog->deleteLater();
        });

        // Expiry: detach the job from every slot we own (so its eventual —
        // possibly never — real completion can't double-settle the op),
        // best-effort kill it (inert on KDAV but harmless), then run the
        // caller's failure action. Context object is `job`: if the job is
        // somehow destroyed first, this connection is auto-removed.
        connect(watchdog, &QTimer::timeout, job, [this, job, watchdog, onTimeout, timeoutMs]() {
            qWarning() << "RemoteCalendarBackend: KDAV job exceeded transfer timeout ("
                       << timeoutMs << "ms) — abandoning it and failing the operation";
            watchdog->deleteLater();
            job->disconnect(this);
            job->kill(KJob::Quietly);
            if (onTimeout) {
                onTimeout();
            }
        });

        watchdog->start();
    }

    job->start();
}

void RemoteCalendarBackend::setDbPath(const QString &dbPath)
{
    if (!dbPath.isEmpty() && !m_ctags) {
        m_ctags = std::make_unique<CTagStore>(dbPath, backendType());
    }
}

void RemoteCalendarBackend::setCacheDir(const QString &dir)
{
    m_contentCache->setCacheDir(dir);
}

void RemoteCalendarBackend::setMultigetChunkSize(int size)
{
    if (size > 0)
        m_multigetChunkSize = size;
}

QString RemoteCalendarBackend::ctag(const QString &calendarId) const
{
    if (m_ctags)
        return m_ctags->get(calendarId);
    return QString();
}

void RemoteCalendarBackend::setCtag(const QString &calendarId, const QString &ctagValue)
{
    if (m_ctags)
        m_ctags->set(calendarId, ctagValue);
}

void RemoteCalendarBackend::clearCtag(const QString &calendarId)
{
    if (m_ctags)
        m_ctags->clear(calendarId);
}

QString RemoteCalendarBackend::syncToken(const QString &calendarId) const
{
    if (m_ctags)
        return m_ctags->getToken(calendarId);
    return QString();
}

void RemoteCalendarBackend::setSyncToken(const QString &calendarId, const QString &token)
{
    if (m_ctags)
        m_ctags->setToken(calendarId, token);
}

void RemoteCalendarBackend::clearSyncToken(const QString &calendarId)
{
    if (m_ctags)
        m_ctags->clearToken(calendarId);
}

// Static factory method for BackendRegistry
SyncBackend* RemoteCalendarBackend::create(const QVariantMap &config, QObject *parent)
{
    QUrl url = QUrl::fromUserInput(config.value(QStringLiteral("url")).toString());
    QString username = config.value(QStringLiteral("username")).toString();
    QString password = config.value(QStringLiteral("password")).toString();
    return new RemoteCalendarBackend(url, username, password, parent);
}

BackendCapabilities RemoteCalendarBackend::capabilities() const
{
    return BackendCapabilities::caldavDefaults();
}

// ============================================================================
// Binding Metadata Support
// ============================================================================

QStringList RemoteCalendarBackend::bindingMetadataKeys() const
{
    return {QStringLiteral("davUrl")};
}

void RemoteCalendarBackend::populateBindingMetadata(
    const DiscoveredCalendar &discovered,
    CalendarBackendBinding &binding) const
{
    // Copy davUrl from discovery metadata
    binding.setDavUrl(discovered.davUrl());
}

void RemoteCalendarBackend::prepareCreationMetadata(
    const QString &calendarId,
    CalendarBackendBinding &binding) const
{
    // Compute davUrl from base URL + calendar ID
    QUrl baseUrl = m_url;
    QString path = baseUrl.path();
    if (!path.endsWith(QLatin1Char('/'))) {
        path += QLatin1Char('/');
    }
    path += calendarId + QLatin1Char('/');
    baseUrl.setPath(path);
    binding.setDavUrl(baseUrl.toString());
}

// Helper to generate full item URL inside a calendar collection
QUrl RemoteCalendarBackend::generateItemUrl(const KDAV::DavUrl &davUrl, const QString &itemUid) const
{
    QUrl url = davUrl.url();
    QString path = url.path();
    if (!path.endsWith('/')) {
        path += '/';
    }
    path += itemUid + ".ics";
    url.setPath(path);
    return url;
}

// Normalize a URL for use as a cache key (removes credentials)
QString RemoteCalendarBackend::normalizeUrlKey(const QString &urlString)
{
    QUrl url(urlString);
    url.setUserInfo(QString());
    return url.toString();
}

// Retrieve stored ETag for quick access
QString RemoteCalendarBackend::cachedEtag(const QString &remoteUrl) const
{
    return m_localEtags.value(normalizeUrlKey(remoteUrl), QString());
}

// Calendar list loading
void RemoteCalendarBackend::loadCalendars(const QString &collectionId)
{
    // Primed fast-path: the provider already discovered this calendar at
    // connect() and seeded our maps via primeCalendars(). Replay the cached
    // discovery instead of re-PROPFINDing the whole server (the per-backend
    // redundancy fixed in v0.63). Mirrors the fetchItems() primed-CTag pattern.
    if (!m_primedCalendarIds.isEmpty()) {
        qDebug() << "RemoteCalendarBackend: loadCalendars served from primed cache for"
                 << collectionId << "(" << m_primedCalendarIds.size()
                 << "calendar(s), 0 PROPFIND)";
        for (const QString &calId : m_primedCalendarIds) {
            emit calendarDiscovered(collectionId, calId);
        }
        emit loadCalendarsFinished(collectionId, true);
        return;
    }

    qDebug() << "RemoteCalendarBackend: Loading calendars for collection:" << collectionId;
    KDAV::DavUrl davUrl(m_url, KDAV::CalDav);
    auto *fetchJob = new KDAV::DavCollectionsFetchJob(davUrl, this);

    connect(fetchJob, &KDAV::DavCollectionsFetchJob::result, this, [this, collectionId, fetchJob](KJob *job) {
        if (job->error()) {
            qWarning() << "RemoteCalendarBackend: Failed to fetch collections:" << job->errorString();
            emit loadCalendarsFinished(collectionId, false, job->errorString());
            return;
        }

        const auto collections = fetchJob->collections();
        QStringList discoveredIds;
        for (const KDAV::DavCollection &col : collections) {
            // Identify calendar collections by content type flags
            if (col.contentTypes() & (KDAV::DavCollection::Events | KDAV::DavCollection::Todos | KDAV::DavCollection::Calendar)) {
                QString calId = col.displayName();
                QUrl rawUrl = col.url().url();

                // Normalize and configure URL with user info
                KDAV::DavUrl configuredUrl = configuredDavUrl(rawUrl.toString());

                CalendarFacts &facts = m_calendars[calId];
                facts.davUrl = configuredUrl;

                // Store discovered color (from apple:calendar-color property)
                QColor calColor = col.color();
                if (calColor.isValid()) {
                    facts.color = calColor;
                    qDebug() << "RemoteCalendarBackend: discovered calendar:" << calId
                             << "with color:" << calColor.name();
                } else {
                    qDebug() << "RemoteCalendarBackend: discovered calendar:" << calId
                             << "(no color set)";
                }

                // Store discovered content types (VEVENT, VTODO support)
                facts.contentTypes = col.contentTypes();
                facts.hasContentTypes = true;

                // Stash the discovery CTag; persisted after a successful fetch
                QString ctag = col.CTag();
                if (!ctag.isEmpty()) {
                    facts.pendingCtag = ctag;
                }

                qDebug() << "RemoteCalendarBackend: calendar URL:" << safeUrlString(configuredUrl.url())
                         << ", content types:" << col.contentTypes();

                emit calendarDiscovered(collectionId, calId);
                discoveredIds << calId;
            }
        }

        // E7/O36: probe each newly discovered calendar's supported-report-set
        // (design step 1) so fetchItems can choose the sync-collection REPORT
        // path over the Depth:1 listing fallback. Fanned in before
        // loadCalendarsFinished so a caller that awaits that signal already
        // has accurate capabilities by the time it calls fetchItems.
        if (discoveredIds.isEmpty()) {
            emit loadCalendarsFinished(collectionId, true);
            return;
        }
        auto remaining = std::make_shared<int>(discoveredIds.size());
        for (const QString &calId : discoveredIds) {
            probeSyncCollectionSupport(calId, [this, collectionId, remaining]() {
                if (--(*remaining) == 0) {
                    emit loadCalendarsFinished(collectionId, true);
                }
            });
        }
    });

    startJobWithWatchdog(fetchJob, [this, collectionId]() {
        emit loadCalendarsFinished(collectionId, false,
                                   QStringLiteral("collections discovery timed out"));
    });
}

const QString RemoteCalendarBackend::BackendTypeName = QStringLiteral("caldav");

QString RemoteCalendarBackend::backendType() const
{
    return BackendTypeName;
}

QList<Kalburator::Shape::Shape> RemoteCalendarBackend::nativeShapes() const
{
    return { Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}

void RemoteCalendarBackend::registerCalendarUrl(const QString &calendarId, const QString &davUrl)
{
    if (calendarId.isEmpty() || davUrl.isEmpty()) {
        qWarning() << "RemoteCalendarBackend::registerCalendarUrl: Empty calendarId or davUrl";
        return;
    }

    KDAV::DavUrl configuredUrl = configuredDavUrl(davUrl);
    m_calendars[calendarId].davUrl = configuredUrl;

    qDebug() << "RemoteCalendarBackend::registerCalendarUrl: Registered calendar" << calendarId
             << "with URL:" << configuredUrl.url().toString(QUrl::RemovePassword);
}

QString RemoteCalendarBackend::discoveredUrl(const QString &calendarId) const
{
    // [[deprecated]] forwarder; the "is this calendar registered?" predicate
    // semantics are preserved by the DTO builder's only-if-registered davUrl guard.
    return discoveredCalendar(calendarId).davUrl();
}

void RemoteCalendarBackend::primeCalendars(const QList<PrimedCalendar> &calendars)
{
    for (const PrimedCalendar &c : calendars) {
        if (c.calendarId.isEmpty()) {
            qWarning() << "RemoteCalendarBackend::primeCalendars: skipping entry with empty calendarId";
            continue;
        }
        // Configure the URL with credentials, exactly as registerCalendarUrl /
        // the network discovery path do, so a primed backend can sync without
        // any further PROPFIND.
        CalendarFacts &facts = m_calendars[c.calendarId];
        if (!c.davUrl.isEmpty()) {
            facts.davUrl = configuredDavUrl(c.davUrl);
        }
        if (c.color.isValid()) {
            facts.color = c.color;
        }
        facts.contentTypes = c.contentTypes;
        facts.hasContentTypes = true;
        if (!m_primedCalendarIds.contains(c.calendarId)) {
            m_primedCalendarIds.append(c.calendarId);
        }
    }
    qDebug() << "RemoteCalendarBackend::primeCalendars: primed" << calendars.size()
             << "calendar(s) (total primed now" << m_primedCalendarIds.size() << ")";
}

QMap<QString, QString> RemoteCalendarBackend::fetchAllCtags(const QStringList &calendarIds)
{
    // E5.2 / audit B7 tripwire (amendment A6): this synchronous helper spins
    // davSyncRequest's nested QEventLoop on the backend thread. Holding the
    // re-entrancy guard across it makes any call marshaled onto the backend
    // thread mid-query observe depth 1 — the same seam the fetchItems body
    // uses. The async fast-path (collectionRevisionsAsync -> fetchAllCtagsAsync)
    // does NOT come through here, so it observes depth 0. Inert (an int
    // inc/dec); a permanent regression tripwire for the CTag path.
    ReentryGuard reentryGuard(&m_reentrancyDepth);

    QMap<QString, QString> result;
    if (calendarIds.isEmpty()) return result;

    // Group calendar IDs by parent URL.
    QMap<QUrl, QStringList> groups;    // parentUrl -> [calId, ...]
    QMap<QString, QString> hrefByCalId; // calId -> URL path (for match-back)
    for (const QString &calId : calendarIds) {
        const auto davUrl = davUrlFor(calId);
        if (!davUrl) continue;
        const QUrl url = davUrl->url();
        QUrl parent = parentUrl(url);
        parent.setUserName(QString());
        parent.setPassword(QString());
        groups[parent].append(calId);
        hrefByCalId[calId] = url.path();
    }

    if (groups.isEmpty()) return result;

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const DavResponse resp = davSyncRequest(
            nam(), it.key(), QByteArrayLiteral("PROPFIND"), m_username, m_password,
            kCtagPropfindBody, {{QByteArrayLiteral("Depth"), QByteArrayLiteral("1")}});
        if (!resp.transportOk()) {
            qWarning() << "RemoteCalendarBackend::fetchAllCtags: PROPFIND failed for"
                       << it.key() << ":" << resp.errorString;
            continue;
        }

        // Match each href back to its calId by path comparison.
        // TODO: if the server returns URL-encoded hrefs for calendars with
        // non-ASCII characters, this comparison may fail; normalize both sides
        // with QUrl::fromPercentEncoding if that becomes a concern.
        const QMap<QString, QString> ctagsByHref = parseCtagMultistatus(resp.body);
        for (const QString &calId : it.value()) {
            const auto hrefIt = ctagsByHref.constFind(hrefByCalId.value(calId));
            if (hrefIt != ctagsByHref.constEnd()) {
                result[calId] = hrefIt.value();
            }
        }
    }

    qDebug() << "RemoteCalendarBackend::fetchAllCtags: requested" << calendarIds.size()
             << "calendars across" << groups.size() << "parent URLs, got"
             << result.size() << "ctags";
    return result;
}

void RemoteCalendarBackend::fetchFreshCtagAsync(
    const QString &calendarId, std::function<void(const QString &)> done)
{
    // E5.2 / audit B7: the async replacement for fetchFreshCtag. Issues the
    // Depth:0 CS:getctag PROPFIND without a nested QEventLoop — the
    // continuation runs on the backend thread from the reply, so an in-flight
    // fetchItems no longer re-enters app-side calls mid-wait.
    const auto davUrl = davUrlFor(calendarId);
    if (!davUrl) {
        done(QString());
        return;
    }

    davSyncRequestAsync(
        nam(), davUrl->url(), QByteArrayLiteral("PROPFIND"),
        m_username, m_password, kCtagPropfindBody,
        {{QByteArrayLiteral("Depth"), QByteArrayLiteral("0")}},
        QByteArrayLiteral("application/xml; charset=utf-8"),
        [done = std::move(done)](const DavResponse &resp) {
            if (!resp.transportOk()) {
                done(QString());
                return;
            }
            const QMap<QString, QString> ctags = parseCtagMultistatus(resp.body);
            done(ctags.isEmpty() ? QString() : ctags.first());
        });
}

void RemoteCalendarBackend::probeSyncCollectionSupport(const QString &calendarId,
                                                        std::function<void()> done)
{
    // E7/O36 design step 1. m_calendars[calendarId].supportsSyncCollection
    // already defaults to false (the permanent CTag+listing fallback), so a
    // missing URL or a failed/unparseable probe just leaves it there.
    const auto davUrl = davUrlFor(calendarId);
    if (!davUrl) {
        done();
        return;
    }

    davSyncRequestAsync(
        nam(), davUrl->url(), QByteArrayLiteral("PROPFIND"),
        m_username, m_password, kSupportedReportSetPropfindBody,
        {{QByteArrayLiteral("Depth"), QByteArrayLiteral("0")}},
        QByteArrayLiteral("application/xml; charset=utf-8"),
        [this, calendarId, done = std::move(done)](const DavResponse &resp) {
            if (resp.transportOk() && resp.status == 207
                && parseSupportsSyncCollection(resp.body)) {
                m_calendars[calendarId].supportsSyncCollection = true;
            }
            // O42: probed even on failure — a transport error leaves the
            // listing fallback for this instance's lifetime (same outcome a
            // failed discovery-time probe always had), rather than paying a
            // re-probe every fetch cycle against a dead/non-advertising URL.
            m_calendars[calendarId].syncCollectionProbed = true;
            done();
        });
}

std::optional<KDAV::DavUrl> RemoteCalendarBackend::davUrlFor(const QString &calendarId) const
{
    const auto it = m_calendars.constFind(calendarId);
    if (it != m_calendars.constEnd() && !it->davUrl.url().isEmpty()) {
        return it->davUrl;
    }

    // Discovery has not yet populated the per-calendar URL. This is the
    // first-sync race window for a directly-configured (non-primed) CalDAV
    // backend: async loadCalendars has not finished, so the map is still empty
    // when the first sync's fetchItems/startSync runs. The calendar URL is
    // deterministically derivable from the base server URL + calendarId — the
    // exact derivation createCalendar/updateCalendar/deleteCalendar already use
    // (calendarUrlForCrud) — so fall back to it rather than failing the
    // operation and silently dropping the write. Once loadCalendars completes it
    // overwrites m_calendars[calendarId].davUrl with the server-authoritative
    // URL; this fallback only covers the gap before that.
    if (calendarId.isEmpty() || m_url.isEmpty()) {
        return std::nullopt;
    }
    return configuredDavUrl(calendarUrlForCrud(calendarId).toString());
}

// ---- Sync::ChangeDetection ----------------------------------------------

QString RemoteCalendarBackend::collectionRevision(const QString &collectionId)
{
    return fetchAllCtags({collectionId}).value(collectionId);
}

QMap<QString, QString>
RemoteCalendarBackend::collectionRevisions(const QStringList &collectionIds)
{
    return fetchAllCtags(collectionIds);
}

void RemoteCalendarBackend::fetchAllCtagsAsync(
    const QStringList &calendarIds,
    std::function<void(QMap<QString, QString>)> done)
{
    // Same grouping as fetchAllCtags (one Depth:1 PROPFIND per parent URL),
    // but with NO nested QEventLoop — each group's PROPFIND runs through
    // davSyncRequestAsync and its continuation lands on the backend thread.
    QMap<QUrl, QStringList> groups;    // parentUrl -> [calId, ...]
    QMap<QString, QString> hrefByCalId; // calId -> URL path (for match-back)
    for (const QString &calId : calendarIds) {
        const auto davUrl = davUrlFor(calId);
        if (!davUrl) continue;
        const QUrl url = davUrl->url();
        QUrl parent = parentUrl(url);
        parent.setUserName(QString());
        parent.setPassword(QString());
        groups[parent].append(calId);
        hrefByCalId[calId] = url.path();
    }

    if (groups.isEmpty()) {
        done({});
        return;
    }

    // Fan the per-group replies back in: a shared result map + a countdown of
    // outstanding groups. The last continuation to land invokes done(). No
    // `this` capture is needed — parseCtagMultistatus is a free function — so
    // the continuations stay safe if the backend/nam is torn down mid-flight
    // (davSyncRequestAsync auto-disconnects them in that case).
    auto result = std::make_shared<QMap<QString, QString>>();
    auto remaining = std::make_shared<int>(groups.size());
    auto hrefMap = std::make_shared<QMap<QString, QString>>(std::move(hrefByCalId));
    auto sharedDone =
        std::make_shared<std::function<void(QMap<QString, QString>)>>(std::move(done));

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QStringList calIds = it.value();
        davSyncRequestAsync(
            nam(), it.key(), QByteArrayLiteral("PROPFIND"),
            m_username, m_password, kCtagPropfindBody,
            {{QByteArrayLiteral("Depth"), QByteArrayLiteral("1")}},
            QByteArrayLiteral("application/xml; charset=utf-8"),
            [calIds, result, remaining, hrefMap, sharedDone](const DavResponse &resp) {
                if (resp.transportOk()) {
                    const QMap<QString, QString> ctagsByHref =
                        parseCtagMultistatus(resp.body);
                    for (const QString &calId : calIds) {
                        const auto hrefIt =
                            ctagsByHref.constFind(hrefMap->value(calId));
                        if (hrefIt != ctagsByHref.constEnd())
                            result->insert(calId, hrefIt.value());
                    }
                } else {
                    qWarning() << "RemoteCalendarBackend::fetchAllCtagsAsync: "
                                  "PROPFIND failed:" << resp.errorString;
                }
                if (--(*remaining) == 0) {
                    (*sharedDone)(*result);
                }
            });
    }
}

void RemoteCalendarBackend::collectionRevisionsAsync(
    const QStringList &collectionIds,
    std::function<void(QMap<QString, QString>)> done)
{
    fetchAllCtagsAsync(collectionIds, std::move(done));
}

QString RemoteCalendarBackend::cachedCollectionRevision(const QString &collectionId) const
{
    return ctag(collectionId);
}

QColor RemoteCalendarBackend::calendarColor(const QString &calendarId) const
{
    // Return from cache (populated during discovery or after updateCalendar)
    return m_calendars.value(calendarId).color;
}

QString RemoteCalendarBackend::calendarDescription(const QString &calendarId) const
{
    Q_UNUSED(calendarId);
    // KDAV's DavCollection does not expose a calendar-description property
    // (only displayName, color, contentTypes, privileges, and CTag).
    // A separate PROPFIND for the DAV calendar-description property would be
    // needed to retrieve this; not implemented.
    return QString();
}

bool RemoteCalendarBackend::discoveredSupportsEvents(const QString &calendarId) const
{
    return discoveredCalendar(calendarId).supportsVEvent;
}

bool RemoteCalendarBackend::discoveredSupportsTodos(const QString &calendarId) const
{
    return discoveredCalendar(calendarId).supportsVTodo;
}

bool RemoteCalendarBackend::discoveredWritable(const QString &calendarId) const
{
    Q_UNUSED(calendarId);
    // KDAV doesn't expose current-user-privilege-set from CalDAV,
    // so we return true by default. For accurate writability detection,
    // use CalDavCapabilityDiscovery which parses privileges directly.
    return true;
}

DiscoveredCalendar RemoteCalendarBackend::discoveredCalendar(const QString &calendarId) const
{
    DiscoveredCalendar d;
    d.calendarId = calendarId;

    const CalendarFacts facts = m_calendars.value(calendarId);
    d.color = facts.color;
    if (!facts.hasContentTypes) {
        // Historical map-miss default: assume events + todos (Hybrid).
        d.supportsVEvent = true;
        d.supportsVTodo = true;
    } else {
        d.supportsVEvent = (facts.contentTypes & KDAV::DavCollection::Events)
                        || (facts.contentTypes & KDAV::DavCollection::Calendar);
        d.supportsVTodo  = (facts.contentTypes & KDAV::DavCollection::Todos)
                        || (facts.contentTypes & KDAV::DavCollection::Calendar);
    }
    d.writable = discoveredWritable(calendarId);  // KDAV: always true (see above)

    // Only the URL discovery/registration actually recorded — the davUrlFor
    // derive-on-miss fallback is deliberately NOT reflected (same "is this
    // calendar registered?" predicate the retired discoveredUrl() had).
    const auto it = m_calendars.constFind(calendarId);
    if (it != m_calendars.constEnd() && !it->davUrl.url().isEmpty())
        d.setDavUrl(it->davUrl.url().toString());

    return d;
}

void RemoteCalendarBackend::removeItem(const QString &calId, const QString &itemUid)
{
    if (!davUrlFor(calId)) {
        qWarning() << "RemoteCalendarBackend::removeItem: Unknown calendar DAV URL for" << calId;
        return;
    }
    if (itemUid.isEmpty()) {
        qWarning() << "RemoteCalendarBackend::removeItem: Empty item UID";
        return;
    }

    KDAV::DavUrl davUrl = *davUrlFor(calId);

    QUrl itemUrl = generateItemUrl(davUrl, itemUid);
    KDAV::DavUrl itemDavUrl(itemUrl, davUrl.protocol());

    QString oldEtag = cachedEtag(itemUrl.toString());

    KDAV::DavItem davItem;
    davItem.setUrl(itemDavUrl);
    davItem.setContentType(QStringLiteral("text/calendar"));
    davItem.setData(QByteArray());
    davItem.setEtag(oldEtag);

    auto *deleteJob = new KDAV::DavItemDeleteJob(davItem, this);

    connect(deleteJob, &KDAV::DavItemDeleteJob::result, this, [this, calId, itemUid, itemUrl](KJob *job) {
        if (job->error()) {
            qWarning() << "RemoteCalendarBackend::removeItem: Failed to delete item:" << job->errorString();
            return;
        }

        noteItemErased(normalizeUrlKey(itemUrl.toString()));

        qDebug() << "RemoteCalendarBackend::removeItem: Deleted incidence UID:" << itemUid << "from calendar" << calId;

        emit itemRemoved(calId, itemUid);
    });

    startJobWithWatchdog(deleteJob, [itemUid]() {
        qWarning() << "RemoteCalendarBackend::removeItem: delete job timed out for" << itemUid;
    });
}


KDAV::DavUrl RemoteCalendarBackend::configuredDavUrl(const QString &rawUrl) const
{
    QUrl url(rawUrl);
    // QUrl::fromUserInput() treats absolute paths like "/calendars/foo/" as
    // "file:///calendars/foo/" — avoid it. If the URL has no scheme, resolve
    // it against the base server URL so relative paths work correctly.
    if (url.isRelative() || url.scheme().isEmpty()) {
        QUrl base;
        base.setScheme(m_url.scheme().isEmpty() ? QStringLiteral("http") : m_url.scheme());
        base.setHost(m_url.host());
        if (m_url.port() > 0)
            base.setPort(m_url.port());
        url = base.resolved(QUrl(rawUrl));
    }

    url.setUserName(m_username);
    url.setPassword(m_password);

    if (!url.path().endsWith(QLatin1Char('/')))
        url.setPath(url.path() + QLatin1Char('/'));

    return KDAV::DavUrl(url, KDAV::CalDav);
}



void RemoteCalendarBackend::startSync(const QString &collectionId,
                              KCalendarCore::MemoryCalendar *calendar,
                              const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                              const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                              const QMap<QString, QString> &stagedDeletions)
{
    if (!calendar) {
        qWarning() << "RemoteCalendarBackend::startSync: Null calendar";
        emit syncCompleted(collectionId);
        return;
    }

    const QString calId = calendar->id();
    if (calId.isEmpty()) {
        qWarning() << "RemoteCalendarBackend::startSync: Empty calendar ID";
        emit syncCompleted(collectionId);
        return;
    }

    if (!davUrlFor(calId)) {
        qWarning() << "RemoteCalendarBackend::startSync: No DAV URL for calendar" << calId;
        emit syncCompleted(collectionId);
        return;
    }

    const KDAV::DavUrl baseDavUrl = *davUrlFor(calId);

    // Count total jobs for completion tracking
    const int totalJobs = stagedCreations.size() + stagedUpdates.size() + stagedDeletions.size();
    if (totalJobs == 0) {
        emit syncCompleted(collectionId);
        return;
    }

    int *completedJobs = new int(0);
    QPointer<RemoteCalendarBackend> safeThis(this);

    // Emit write started signal
    emit writeStarted(calId, totalJobs);

    auto checkDone = [safeThis, completedJobs, totalJobs, collectionId, calId]() {
        if (!safeThis) {
            delete completedJobs;
            return;
        }
        (*completedJobs)++;

        // Emit write progress signal
        emit safeThis->writeProgressChanged(calId, *completedJobs, totalJobs);

        if (*completedJobs >= totalJobs) {
            delete completedJobs;
            emit safeThis->syncCompleted(collectionId);
            qDebug() << "RemoteCalendarBackend::startSync: All jobs completed for collection" << collectionId;
        }
    };

    // Launch create jobs. A 412 means the item already exists on the server
    // (DavItemCreateJob PUTs with If-None-Match: *); retry as a normal update.
    for (const auto &inc : stagedCreations) {
        if (!inc) {
            checkDone();
            continue;
        }

        KDAV::DavItem davItem;
        davItem.setUrl(KDAV::DavUrl(generateItemUrl(baseDavUrl, inc->uid()),
                                    baseDavUrl.protocol()));
        davItem.setContentType(QStringLiteral("text/calendar"));
        davItem.setData(icalFromIncidence(inc));  // create PUT without ETag

        auto *createJob = new KDAV::DavItemCreateJob(davItem, this);

        connect(createJob, &KDAV::DavItemCreateJob::result, this,
                [this, createJob, inc, calendar, calId, checkDone](KJob *job) {
            if (job->error()) {
                if (getHttpStatusCode(job) == 412) {
                    qDebug() << "Create job 412 Precondition Failed, switching to update for" << inc->uid();
                    const QString etag = cachedEtag(
                        generateItemUrl(davUrlFor(calId).value_or(KDAV::DavUrl()), inc->uid()).toString());
                    launchStartSyncModify(calId, calendar, inc, etag,
                                          /*retryOn412=*/true, checkDone);
                    return;
                }
                qWarning() << "Create job failed for" << inc->uid() << ":" << job->errorString();
                checkDone();
                return;
            }

            const auto createdItem = createJob->item();
            const QString remoteUrl = normalizeUrlKey(createdItem.url().url().toString());
            noteItemWritten(remoteUrl, createdItem.etag(),
                            QString::fromUtf8(icalFromIncidence(inc)));
            emit itemLoaded(calendar, inc, createdItem.etag());
            checkDone();
        });

        startJobWithWatchdog(createJob, [inc, checkDone]() {
            qWarning() << "RemoteCalendarBackend::startSync: create job timed out for"
                       << inc->uid();
            checkDone();
        });
    }

    // Launch update jobs (modify with the cached ETag; 412 retries as force).
    for (const auto &inc : stagedUpdates) {
        if (!inc) {
            checkDone();
            continue;
        }
        const QString etag = cachedEtag(generateItemUrl(baseDavUrl, inc->uid()).toString());
        launchStartSyncModify(calId, calendar, inc, etag,
                              /*retryOn412=*/true, checkDone);
    }

    // Launch delete jobs
    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        const QString &uid = it.key();

        KDAV::DavItem davItem;
        davItem.setUrl(KDAV::DavUrl(generateItemUrl(baseDavUrl, uid),
                                    baseDavUrl.protocol()));
        davItem.setContentType(QStringLiteral("text/calendar"));
        davItem.setData(QByteArray());
        davItem.setEtag(it.value());

        auto *deleteJob = new KDAV::DavItemDeleteJob(davItem, this);
        const QUrl itemUrl = davItem.url().url();

        connect(deleteJob, &KDAV::DavItemDeleteJob::result, this,
                [this, uid, calId, itemUrl, checkDone](KJob *job) {
                    if (job->error()) {
                        qWarning() << "Delete job failed for" << uid << ":" << job->errorString();
                        checkDone();
                        return;
                    }

                    qDebug() << "Delete job succeeded for" << uid;
                    noteItemErased(normalizeUrlKey(itemUrl.toString()));
                    emit itemRemoved(calId, uid);
                    checkDone();
                });

        startJobWithWatchdog(deleteJob, [uid, checkDone]() {
            qWarning() << "RemoteCalendarBackend::startSync: delete job timed out for" << uid;
            checkDone();
        });
    }
}

void RemoteCalendarBackend::launchStartSyncModify(const QString &calId,
                                                  KCalendarCore::MemoryCalendar *calendar,
                                                  const KCalendarCore::Incidence::Ptr &inc,
                                                  const QString &etag, bool retryOn412,
                                                  const std::function<void()> &checkDone)
{
    const KDAV::DavUrl base = davUrlFor(calId).value_or(KDAV::DavUrl());
    KDAV::DavItem davItem;
    davItem.setUrl(KDAV::DavUrl(generateItemUrl(base, inc->uid()),
                                base.protocol()));
    davItem.setContentType(QStringLiteral("text/calendar"));
    davItem.setData(icalFromIncidence(inc));
    davItem.setEtag(etag);  // "*" on the force path (RFC 7232 If-Match: *)

    auto *modifyJob = new KDAV::DavItemModifyJob(davItem, this);

    connect(modifyJob, &KDAV::DavItemModifyJob::result, this,
            [this, modifyJob, inc, calendar, calId, retryOn412, checkDone](KJob *job) {
                if (job->error()) {
                    if (retryOn412 && getHttpStatusCode(job) == 412) {
                        // ETag mismatch (server has a newer version) during a
                        // sync where the user resolved a conflict to push
                        // local: retry once with If-Match: * — the user's
                        // explicit intent. The retry never loops.
                        qDebug() << "Update job 412 Precondition Failed for" << inc->uid()
                                 << "- retrying with force update (If-Match: *)";
                        launchStartSyncModify(calId, calendar, inc,
                                              QStringLiteral("*"),
                                              /*retryOn412=*/false, checkDone);
                        return;
                    }
                    qWarning() << "Update job failed for" << inc->uid() << ":" << job->errorString();
                    checkDone();
                    return;
                }

                const auto updatedItem = modifyJob->item();
                const QString url = normalizeUrlKey(updatedItem.url().url().toString());
                noteItemWritten(url, updatedItem.etag(),
                                QString::fromUtf8(icalFromIncidence(inc)));
                emit itemLoaded(calendar, inc, updatedItem.etag());
                checkDone();
            });

    startJobWithWatchdog(modifyJob, [inc, checkDone]() {
        qWarning() << "RemoteCalendarBackend::launchStartSyncModify: modify job timed out for"
                   << inc->uid();
        checkDone();
    });
}

void RemoteCalendarBackend::noteItemWritten(const QString &urlKey, const QString &etag,
                                            const QString &icalData)
{
    if (etag.isEmpty()) {
        return;
    }
    if (m_etagCache) {
        m_etagCache->setEtag(urlKey, etag);
    }
    m_localEtags[urlKey] = etag;
    m_contentCache->store(urlKey, etag, icalData);
}

void RemoteCalendarBackend::noteItemErased(const QString &urlKey)
{
    if (m_etagCache) {
        m_etagCache->removeEtag(urlKey);
    }
    m_localEtags.remove(urlKey);
    m_contentCache->remove(urlKey);
}



void RemoteCalendarBackend::storeCalendars(const QString &, const QList<KCalendarCore::MemoryCalendar*> &)
{
    // Stub - no calendar-level save implemented yet
}

// ============================================================================
// Calendar CRUD Operations (RFC 4791 MKCALENDAR / DELETE)
// ============================================================================

QUrl RemoteCalendarBackend::crudCalendarUrl(const QString &calendarId) const
{
    // Prefer the registered per-calendar DAV URL (correct path + case, and the
    // only valid URL for prefixed multiproto ids). Strip credentials — they
    // travel in the Authorization header. Fall back to the derived URL for
    // calendars that were never registered (e.g. a brand-new MKCALENDAR target).
    if (const auto davUrl = davUrlFor(calendarId)) {
        QUrl u = davUrl->url();
        u.setUserName(QString());
        u.setPassword(QString());
        return u;
    }
    return calendarUrlForCrud(calendarId);
}

void RemoteCalendarBackend::createCalendarAsync(const QString &collectionId, const QString &calendarId,
                                    const QString &name, CalendarType type,
                                    std::function<void(bool)> done)
{
    const QUrl calendarUrl = crudCalendarUrl(calendarId);

    qDebug() << "RemoteCalendarBackend::createCalendarAsync: Creating calendar at" << safeUrlString(calendarUrl)
             << "type:" << static_cast<int>(type);

    // Build component set based on CalendarType
    QString componentSet;
    if (type == CalendarType::Event) {
        componentSet = QStringLiteral("        <C:comp name=\"VEVENT\"/>\n");
    } else if (type == CalendarType::Todo) {
        componentSet = QStringLiteral("        <C:comp name=\"VTODO\"/>\n");
    } else {  // CalendarType::Hybrid
        componentSet = QStringLiteral(
            "        <C:comp name=\"VEVENT\"/>\n"
            "        <C:comp name=\"VTODO\"/>\n"
            "        <C:comp name=\"VJOURNAL\"/>\n");
    }

    // Build MKCALENDAR request body per RFC 4791 Section 5.3.1
    QString displayName = name.isEmpty() ? calendarId : name;
    QString requestBody = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
        "<C:mkcalendar xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
        "  <D:set>\n"
        "    <D:prop>\n"
        "      <D:displayname>%1</D:displayname>\n"
        "      <C:supported-calendar-component-set>\n"
        "%2"
        "      </C:supported-calendar-component-set>\n"
        "    </D:prop>\n"
        "  </D:set>\n"
        "</C:mkcalendar>\n"
    ).arg(displayName.toHtmlEscaped(), componentSet);

    davSyncRequestAsync(nam(), calendarUrl, QByteArrayLiteral("MKCALENDAR"),
                        m_username, m_password, requestBody.toUtf8(), {},
                        QByteArrayLiteral("application/xml; charset=utf-8"),
        [this, collectionId, calendarId, calendarUrl, type, done = std::move(done)]
        (const DavResponse &resp) {
            qDebug() << "RemoteCalendarBackend::createCalendarAsync: HTTP status" << resp.status;

            // Register the calendar URL helper (used on success)
            auto registerCalendar = [this, calendarId, calendarUrl, type]() {
                QUrl davCalendarUrl = calendarUrl;
                davCalendarUrl.setUserName(m_username);
                davCalendarUrl.setPassword(m_password);
                CalendarFacts &facts = m_calendars[calendarId];
                facts.davUrl = configuredDavUrl(davCalendarUrl.toString());

                // Store the calendar type so discoveredCalendar().calendarType() reports it
                if (type == CalendarType::Event) {
                    facts.contentTypes = KDAV::DavCollection::Events;
                } else if (type == CalendarType::Todo) {
                    facts.contentTypes = KDAV::DavCollection::Todos;
                } else {
                    facts.contentTypes = KDAV::DavCollection::Events | KDAV::DavCollection::Todos;
                }
                facts.hasContentTypes = true;
            };

            if (resp.status == 201) {
                qDebug() << "RemoteCalendarBackend::createCalendarAsync: Calendar created successfully:" << calendarId;
                registerCalendar();
                emit calendarCreated(collectionId, calendarId);
                emit calendarDiscovered(collectionId, calendarId);
                done(true);
                return;
            }
            if (resp.status == 405 || resp.status == 409) {
                // Idempotent: 405 Method Not Allowed or 409 Conflict means calendar already exists
                qDebug() << "RemoteCalendarBackend::createCalendarAsync: Calendar already exists:" << calendarId << "(HTTP" << resp.status << ")";
                registerCalendar();
                done(true);
                return;
            }
            const QString errorMessage = !resp.transportOk()
                ? resp.errorString
                : QStringLiteral("Unexpected HTTP status: %1").arg(resp.status);
            qWarning() << "RemoteCalendarBackend::createCalendarAsync: Failed:" << errorMessage
                       << "HTTP status:" << resp.status;
            emit calendarOperationError(calendarId, errorMessage);
            done(false);
        });
}

void RemoteCalendarBackend::updateCalendarAsync(const QString &collectionId, const QString &calendarId,
                                    const QVariantMap &properties,
                                    std::function<void(bool)> done)
{
    const QUrl calendarUrl = crudCalendarUrl(calendarId);

    qDebug() << "RemoteCalendarBackend::updateCalendarAsync: Updating calendar at" << safeUrlString(calendarUrl);

    // Build PROPPATCH request body for CalDAV
    QString propsXml;

    if (properties.contains(QStringLiteral("displayName"))) {
        QString name = properties.value(QStringLiteral("displayName")).toString();
        propsXml += QStringLiteral("      <D:displayname>%1</D:displayname>\n").arg(name.toHtmlEscaped());
    }

    if (properties.contains(QStringLiteral("color"))) {
        QColor color = properties.value(QStringLiteral("color")).value<QColor>();
        if (!color.isValid()) {
            color = QColor(properties.value(QStringLiteral("color")).toString());
        }
        if (color.isValid()) {
            // Apple CalDAV color format: CSS color with alpha (e.g., #FF0000FF)
            QString colorStr = color.name() + QStringLiteral("FF");  // Append full alpha
            propsXml += QStringLiteral("      <apple:calendar-color xmlns:apple=\"http://apple.com/ns/ical/\">%1</apple:calendar-color>\n")
                .arg(colorStr);
        }
    }

    if (properties.contains(QStringLiteral("description"))) {
        QString description = properties.value(QStringLiteral("description")).toString();
        propsXml += QStringLiteral("      <C:calendar-description>%1</C:calendar-description>\n")
            .arg(description.toHtmlEscaped());
    }

    if (propsXml.isEmpty()) {
        qDebug() << "RemoteCalendarBackend::updateCalendarAsync: No supported properties to update";
        done(true);
        return;
    }

    QString requestBody = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
        "<D:propertyupdate xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
        "  <D:set>\n"
        "    <D:prop>\n"
        "%1"
        "    </D:prop>\n"
        "  </D:set>\n"
        "</D:propertyupdate>\n"
    ).arg(propsXml);

    davSyncRequestAsync(nam(), calendarUrl, QByteArrayLiteral("PROPPATCH"),
                        m_username, m_password, requestBody.toUtf8(), {},
                        QByteArrayLiteral("application/xml; charset=utf-8"),
        [this, collectionId, calendarId, properties, done = std::move(done)]
        (const DavResponse &resp) {
            qDebug() << "RemoteCalendarBackend::updateCalendarAsync: HTTP status" << resp.status;

            // 207 Multi-Status is the expected response for PROPPATCH
            if (resp.status == 207 || resp.status == 200 || resp.status == 204) {
                qDebug() << "RemoteCalendarBackend::updateCalendarAsync: Calendar updated successfully:" << calendarId;

                // Update local cache to reflect the change
                if (properties.contains(QStringLiteral("color"))) {
                    QColor color = properties.value(QStringLiteral("color")).value<QColor>();
                    if (!color.isValid()) {
                        color = QColor(properties.value(QStringLiteral("color")).toString());
                    }
                    if (color.isValid()) {
                        m_calendars[calendarId].color = color;
                    }
                }

                emit calendarUpdated(collectionId, calendarId);
                done(true);
                return;
            }
            const QString errorMessage = !resp.transportOk()
                ? resp.errorString
                : QStringLiteral("Unexpected HTTP status: %1").arg(resp.status);
            qWarning() << "RemoteCalendarBackend::updateCalendarAsync: Failed:" << errorMessage
                       << "HTTP status:" << resp.status;
            emit calendarOperationError(calendarId, errorMessage);
            done(false);
        });
}

void RemoteCalendarBackend::deleteCalendarAsync(const QString &collectionId, const QString &calendarId,
                                    std::function<void(bool)> done)
{
    const QUrl calendarUrl = crudCalendarUrl(calendarId);
    qDebug() << "RemoteCalendarBackend::deleteCalendarAsync: Deleting calendar at" << safeUrlString(calendarUrl);

    davSyncRequestAsync(nam(), calendarUrl, QByteArrayLiteral("DELETE"),
                        m_username, m_password, {}, {},
                        QByteArrayLiteral("application/xml; charset=utf-8"),
        [this, collectionId, calendarId, done = std::move(done)](const DavResponse &resp) {
            qDebug() << "RemoteCalendarBackend::deleteCalendarAsync: HTTP status" << resp.status;

            // 200 OK or 204 No Content are both valid DELETE responses
            if (resp.status == 200 || resp.status == 204) {
                qDebug() << "RemoteCalendarBackend::deleteCalendarAsync: Calendar deleted successfully:" << calendarId;
                m_calendars[calendarId].davUrl = KDAV::DavUrl();  // keep other facts (old per-map remove)
                emit calendarDeleted(collectionId, calendarId);
                done(true);
                return;
            }
            if (resp.status == 404) {
                // Calendar doesn't exist - report false to indicate it wasn't deleted
                qDebug() << "RemoteCalendarBackend::deleteCalendarAsync: Calendar not found:" << calendarId;
                m_calendars[calendarId].davUrl = KDAV::DavUrl();  // keep other facts (old per-map remove)
                done(false);
                return;
            }
            QString errorMessage = resp.errorString;
            if (errorMessage.isEmpty()) {
                errorMessage = QStringLiteral("HTTP status: %1").arg(resp.status);
            }
            qWarning() << "RemoteCalendarBackend::deleteCalendarAsync: Failed:" << errorMessage;
            emit calendarOperationError(calendarId, errorMessage);
            done(false);
        });
}

QUrl RemoteCalendarBackend::calendarUrlForCrud(const QString &calendarId) const
{
    // Principal URL + calendar slug. For Radicale-style servers the username
    // must be in the path (/username/calendar/) when the base URL has none;
    // userinfo credentials move to the Authorization header instead.
    QUrl url = m_url;
    QString path = url.path();
    if ((path.isEmpty() || path == QLatin1String("/")) && !m_username.isEmpty()) {
        path = QLatin1Char('/') + m_username + QLatin1Char('/');
    }
    url.setUserName(QString());
    url.setPassword(QString());
    if (!path.endsWith(QLatin1Char('/'))) {
        path += QLatin1Char('/');
    }
    path += calendarId + QLatin1Char('/');
    url.setPath(path);
    return url;
}

// ============================================================================
// Operation-Based API Implementation
// ============================================================================

QList<KCalendarCore::Incidence::Ptr> RemoteCalendarBackend::serveCachedItems(
    const QString &calendarId, const KDAV::DavUrl &davUrl)
{
    QList<KCalendarCore::Incidence::Ptr> cachedIncidences;
    const auto rows = m_contentCache->rowsByPathFragment(davUrl.url().path());
    for (const auto &row : rows) {
        const auto incidences = incidencesFromIcal(row.ical);
        for (const auto &incidence : incidences) {
            // Phase B5: remember the verbatim bytes this incidence came
            // from — see m_lastRawIcsByUid's doc comment.
            m_lastRawIcsByUid[incidence->uid()] = row.ical.toUtf8();
            cachedIncidences.append(incidence);
        }
    }
    // E9.1 (sync-excellence campaign, O34): batch signal, once per
    // cache-hit-all pass, with the full item list (syncbackend.h).
    emit itemsFetched(calendarId, cachedIncidences);
    return cachedIncidences;
}

FetchOperation* RemoteCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = onOwnerThread(new FetchOperation(calendarId), this);

    // H5/O23: capture this fetch's results into the recordsFromLastFetch()
    // memo on every successful completion, regardless of which internal
    // branch completed it (cache hit, cache miss, full network fetch).
    // setFetchedItems() and m_lastRawIcsByUid are always populated before
    // complete() is called, and finished() fires synchronously from
    // complete() on this same thread, so both are ready here.
    connect(op, &SyncOperation::finished, this, [this, op, calendarId]() {
        if (op->state() != SyncOperation::Succeeded) return;
        QList<BackendRecord> records;
        for (const auto &incidence : op->fetchedItems()) {
            if (incidence.isNull()) continue;
            const QByteArray rawIcs = m_lastRawIcsByUid.value(incidence->uid());
            records.append(blobRecordFromIcal(
                incidence->uid(), !rawIcs.isEmpty() ? rawIcs : icalFromIncidence(incidence)));
        }
        m_lastFetchRecords[calendarId] = records;
    });

    if (!davUrlFor(calendarId)) {
        qWarning() << "RemoteCalendarBackend::fetchItems: No DAV URL for calendar:" << calendarId;
        // Track the op (enqueueOperation would, but this path bypasses the
        // queue — a no-URL calendar never syncs, so it has nothing to serialize
        // against) and defer the failure so the caller can connect signals.
        registerOperation(op);
        QTimer::singleShot(0, op, [op, calendarId, this]() {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
            emit fetchFinished(calendarId, false, QStringLiteral("No DAV URL registered"));
        });
        return op;
    }

    // Initialize content cache on first fetch (lazy initialization). This runs
    // SYNCHRONOUSLY before the queued op body — a load-bearing contract: the
    // content-cache DB must exist by the time fetchItems() returns (WP-D8 +
    // the cache-filename determinism test both assert it, and neither spins an
    // event loop afterward). It is a local SQLite open, not the B7 nested-loop
    // hazard, so keeping it out of the deferred body is safe.
    m_contentCache->ensureOpen();

    KDAV::DavUrl davUrl = *davUrlFor(calendarId);

    // E5.2: route the (network) op body through E5.1's per-collection FIFO
    // queue. enqueueOperation registers the op and defers its body one
    // event-loop turn, but only once this op reaches the front of calendarId's
    // queue — so concurrent fetch/push/delete on the same collection serialize
    // instead of interleaving their backend-thread state. The op starts life
    // Pending and flips to Running inside the body when dequeued (the engine's
    // fetch gate keys off isFinished(), not state()==Running — H1.1/O24).
    //
    // E5.2 / audit B7: the CTag PROPFIND is async, so this body returns to the
    // event loop before the network wait instead of spinning a nested
    // QEventLoop — a call marshaled onto the backend thread mid-fetch no longer
    // re-enters this op's suspended stack.
    enqueueOperation(calendarId, op, [this, op, davUrl, calendarId]() {
        // Guard held only for this synchronous span, which ends when the async
        // CTag PROPFIND is dispatched (or immediately, absent a stored CTag) —
        // see reentrancyDepth().
        ReentryGuard reentryGuard(&m_reentrancyDepth);

        op->setState(SyncOperation::Running);

        // CTag optimization: a lightweight Depth:0 CS:getctag PROPFIND, since
        // KDAV's DavCollectionsFetchJob doesn't return CTag for individual
        // calendar URLs. Only meaningful when we already have a stored CTag to
        // compare against; otherwise skip straight to the listing.
        const QString storedCtag = ctag(calendarId);
        if (storedCtag.isEmpty()) {
            continueFetchWithListing(op, calendarId, davUrl, QString());
            return;
        }

        fetchFreshCtagAsync(calendarId,
            [this, op, davUrl, calendarId, storedCtag](const QString &freshCtag) {
                if (op->state() == SyncOperation::Cancelled) {
                    emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                    return;
                }

                if (!freshCtag.isEmpty() && freshCtag == storedCtag) {
                    auto cachedIncidences = serveCachedItems(calendarId, davUrl);

                    if (!cachedIncidences.isEmpty()) {
                        qDebug() << "RemoteCalendarBackend::fetchItems: CTag unchanged for" << calendarId
                                 << "(" << freshCtag << ") - serving from cache";

                        emit fetchStarted(calendarId, cachedIncidences.size());
                        qDebug() << "RemoteCalendarBackend::fetchItems: Served" << cachedIncidences.size()
                                 << "incidences from cache (CTag match) for" << calendarId;

                        op->setFetchedItems(cachedIncidences);
                        op->complete();
                        emit fetchFinished(calendarId, true);
                        return;
                    }

                    // N5 fix: a CTag match that serves ZERO cached items is
                    // suspicious — either the content cache is missing/stale
                    // for a non-empty calendar (the CTag-ahead-of-cache bug: a
                    // 673-item calendar whose every multiget had previously
                    // failed read back as "empty, fresh, success" forever after)
                    // or the calendar really is empty. Don't trust the match
                    // either way; clear the stale CTag and fall through to the
                    // normal list+fetch below. A genuinely empty calendar
                    // re-lists cheaply (one PROPFIND, 0 items) and legitimately
                    // re-commits its CTag afterward.
                    qWarning() << "RemoteCalendarBackend::fetchItems: CTag match for" << calendarId
                               << "(" << freshCtag << ") served 0 cached items"
                               << "- distrusting the match, re-listing";
                    clearCtag(calendarId);
                }

                // E7/O36 design step 3: something changed (or there was no
                // CTag to compare — storedCtag is non-empty on this branch,
                // see the isEmpty() early-out above it). A sync-collection-
                // capable calendar with a stored token gets the server to
                // compute the delta directly; everyone else (no capability,
                // or capable but no token yet — e.g. this collection's very
                // first fetch through this branch) keeps the existing
                // Depth:1 listing fallback, forever.
                const QString storedToken = syncToken(calendarId);

                // O42: the capability flag is in-memory and normally
                // populated by discovery's probe — but a consumer may drive
                // the first fetch of a process BEFORE discovery completes
                // (PlanStan's auto-sync-on-load). A persisted token proves a
                // prior process probed successfully, so paying the Depth:1
                // listing here is pure waste: lazily probe once, then decide.
                // Deliberately NOT persisted (fix candidate (a)): a stored
                // capability would keep forcing the REPORT against a server
                // that stopped advertising it, and only 409/410/507 fall
                // back — a fresh per-instance probe is self-healing.
                if (!m_calendars.value(calendarId).syncCollectionProbed
                    && !storedToken.isEmpty()) {
                    probeSyncCollectionSupport(
                        calendarId, [this, op, calendarId, davUrl, freshCtag]() {
                            const QString token = syncToken(calendarId);
                            if (m_calendars.value(calendarId).supportsSyncCollection
                                && !token.isEmpty()) {
                                continueFetchWithSyncCollection(op, calendarId, davUrl,
                                                                freshCtag, token);
                            } else {
                                continueFetchWithListing(op, calendarId, davUrl, freshCtag);
                            }
                        });
                    return;
                }

                if (m_calendars.value(calendarId).supportsSyncCollection
                    && !storedToken.isEmpty()) {
                    continueFetchWithSyncCollection(op, calendarId, davUrl, freshCtag, storedToken);
                } else {
                    continueFetchWithListing(op, calendarId, davUrl, freshCtag);
                }
            });
    });

    return op;
}

void RemoteCalendarBackend::continueFetchWithListing(FetchOperation *op,
                                                     const QString &calendarId,
                                                     const KDAV::DavUrl &davUrl,
                                                     const QString &freshCtag)
{
    // CTag changed or unavailable — stage it for commit after a full fetch.
    if (!freshCtag.isEmpty()) {
        m_calendars[calendarId].pendingCtag = freshCtag;
    }

    // E6/O35: KDAV's EtagCache is in-memory and per-backend-instance, so a
    // fresh instance (e.g. after an app restart) starts every collection's
    // delta detection from nothing — the listing below would see every
    // server item as "changed" even though m_contentCache already holds
    // unchanged items' bytes under the correct ETag. Seed once per
    // collection per instance lifetime, lazily here (not the constructor,
    // which may run pre-relocation on the GUI thread) and BEFORE the
    // DavItemsListJob below so its changed-set is computed against the
    // persisted state.
    if (!m_etagCacheSeededCalendars.contains(calendarId)) {
        m_etagCacheSeededCalendars.insert(calendarId);
        const auto seedRows = m_contentCache->urlEtagPairs(davUrl.url().path());
        for (const auto &[url, etag] : seedRows) {
            m_etagCache->setEtag(url, etag);
        }
    }

    // Fetch list of items with ETag comparison
    // DavItemsListJob compares server ETags against our EtagCache to identify changes
    KDAV::DavItemsListJob *listJob = new KDAV::DavItemsListJob(davUrl, m_etagCache, this);

    connect(listJob, &KDAV::DavItemsListJob::result, this, [this, op, calendarId, listJob, davUrl](KJob *job) {
        if (op->state() == SyncOperation::Cancelled) {
            return;  // Operation was cancelled
        }

        if (job->error()) {
            QString errorMsg = QStringLiteral("Failed to list items: %1").arg(job->errorString());
            op->fail(errorMsg);
            emit fetchFinished(calendarId, false, errorMsg);
            return;
        }

        // Get all items from the server (for ETag/URL info)
        const auto allItems = listJob->items();

        // Get only the items that changed since last sync (ETag differs from cache)
        const auto changedItems = listJob->changedItems();

        // Get items that were deleted on the server
        const auto deletedItems = listJob->deletedItems();

        // Build list of URLs we need to fetch (only changed items)
        QStringList urlsToFetch;
        QMap<QString, QString> serverEtags;  // url (no creds) -> etag for all items

        for (const auto &item : allItems) {
            // Use normalizeUrlKey for consistent URL format across all cache operations
            serverEtags[normalizeUrlKey(item.url().url().toString())] = item.etag();
        }

        for (const auto &item : changedItems) {
            // Keep credentials in URLs for DavItemsFetchJob (needs auth)
            urlsToFetch << item.url().toDisplayString();
        }

        // Handle deleted items - remove from cache and ETag tracking
        // Note: deletedItems() returns URLs as strings, not DavItem objects
        // IMPORTANT: Filter to only items in THIS calendar (EtagCache is shared across all calendars)
        QString calendarPath = davUrl.url().path();  // e.g., "/remote.php/dav/calendars/user/acquire/"
        int deletedFromThisCalendar = 0;
        for (const QString &urlStr : deletedItems) {
            // Only process if this URL's path starts with the current calendar's path
            QUrl deletedUrl(urlStr);
            if (!deletedUrl.path().startsWith(calendarPath)) {
                // This URL is from a different calendar - skip it
                continue;
            }
            noteItemErased(urlStr);
            deletedFromThisCalendar++;
        }
        if (deletedFromThisCalendar > 0) {
            qDebug() << "RemoteCalendarBackend::fetchItems: Removed" << deletedFromThisCalendar << "deleted items from cache for" << calendarId;
        }

        // Log delta sync stats
        qDebug() << "RemoteCalendarBackend::fetchItems: Delta sync -"
                 << allItems.size() << "total,"
                 << urlsToFetch.size() << "changed,"
                 << deletedFromThisCalendar << "deleted,"
                 << (allItems.size() - urlsToFetch.size()) << "from cache";

        if (allItems.isEmpty()) {
            // No items - complete with empty list
            emit fetchStarted(calendarId, 0);
            bootstrapSyncTokenIfNeeded(calendarId, davUrl, [this, op, calendarId]() {
                op->setFetchedItems({});
                op->complete();
                emit fetchFinished(calendarId, true);
            });
            return;
        }

        // Emit fetchStarted with total items (cached + to-fetch)
        emit fetchStarted(calendarId, allItems.size());

        // If no items to fetch, serve everything from cache
        if (urlsToFetch.isEmpty()) {
            QList<KCalendarCore::Incidence::Ptr> fetchedIncidences;
            int currentItem = 0;
            int countSkipped = 0;

            for (const auto &item : allItems) {
                if (op->state() == SyncOperation::Cancelled) {
                    emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                    return;
                }

                // Use URL without credentials for cache keys (matches KDAV's internal format)
                QUrl urlNoCreds = item.url().url();
                urlNoCreds.setUserInfo(QString());
                QString urlKey = urlNoCreds.toDisplayString();
                QString etag = serverEtags.value(urlKey);

                // Get content from cache
                QString cachedIcal = m_contentCache->content(urlKey, etag);
                if (cachedIcal.isEmpty()) {
                    // Cache miss - shouldn't happen if item wasn't in changedItems
                    // but handle gracefully by skipping
                    qWarning() << "RemoteCalendarBackend::fetchItems: Cache miss for unchanged item:" << urlKey;
                    countSkipped++;
                    currentItem++;
                    emit fetchProgressChanged(calendarId, currentItem, allItems.size());
                    continue;
                }

                const auto incidences = incidencesFromIcal(cachedIcal);
                if (incidences.isEmpty()) {
                    qWarning() << "RemoteCalendarBackend::fetchItems: Could not parse cached iCal for:" << urlKey;
                    countSkipped++;
                    currentItem++;
                    emit fetchProgressChanged(calendarId, currentItem, allItems.size());
                    continue;
                }

                // Update ETag caches - use urlKey (no credentials) to match KDAV's format
                if (!etag.isEmpty()) {
                    m_localEtags[urlKey] = etag;
                    if (m_etagCache) {
                        m_etagCache->setEtag(urlKey, etag);
                    }
                }

                for (const auto &incidence : incidences) {
                    // Phase B5: remember the verbatim bytes — see
                    // m_lastRawIcsByUid's doc comment.
                    m_lastRawIcsByUid[incidence->uid()] = cachedIcal.toUtf8();
                    fetchedIncidences.append(incidence);
                }

                currentItem++;
                emit fetchProgressChanged(calendarId, currentItem, allItems.size());
            }

            qDebug() << "RemoteCalendarBackend::fetchItems: Served" << fetchedIncidences.size()
                     << "incidences from cache for calendar" << calendarId
                     << (countSkipped > 0 ? QString(" (%1 skipped)").arg(countSkipped)
                                          : QString());

            // E9.1 (sync-excellence campaign, O34): batch signal, once per
            // partial-cache-hit pass, with the full item list (syncbackend.h).
            emit itemsFetched(calendarId, fetchedIncidences);

            // N5 fix: only commit the pending CTag when every item
            // actually materialized. A skip here means the content
            // cache is missing bytes for an item the CTag says is
            // current — committing anyway would let a later cycle's
            // CTag-match short-circuit serve an incomplete set silently
            // (the "CTag ahead of content cache" bug). Leaving the
            // stored CTag untouched makes the next cycle re-list.
            if (countSkipped == 0) {
                const QString pendingCtag = m_calendars.value(calendarId).pendingCtag;
                if (!pendingCtag.isEmpty()) {
                    setCtag(calendarId, pendingCtag);
                }
            } else {
                qWarning() << "RemoteCalendarBackend::fetchItems: NOT committing CTag for"
                           << calendarId << "-" << countSkipped
                           << "item(s) served incomplete from cache";
            }

            bootstrapSyncTokenIfNeeded(calendarId, davUrl,
                [this, op, calendarId, fetchedIncidences]() {
                    op->setFetchedItems(fetchedIncidences);
                    op->complete();
                    emit fetchFinished(calendarId, true);
                });
            return;
        }

        // Fetch only the changed items from the server via MULTIGET,
        // chunked into sequential batches (N4 fix). A single REPORT with
        // hundreds of hrefs reliably triggers transport-level failures on
        // some servers (a 673-href multiget died with an HTTP/2 stream
        // reset, misreported by KIO as "Invalid username/password (401)"
        // even though no 401 ever occurred); chunking keeps each request
        // body small. Batches run strictly sequentially — never
        // parallel, since these hit one possibly-rate-limited host — and
        // ANY batch failure fails the whole op before a single item is
        // processed: fetchedItemsMap must be complete or the op must
        // fail, never a partial map reaching processFetchedItems (whose
        // CTag commit assumes completeness — see N5/B3).
        QList<QStringList> hrefBatches;
        for (int i = 0; i < urlsToFetch.size(); i += m_multigetChunkSize)
            hrefBatches.append(urlsToFetch.mid(i, m_multigetChunkSize));

        auto fetchedItemsMap = std::make_shared<QMap<QString, KDAV::DavItem>>();
        auto batchIndex = std::make_shared<int>(0);
        auto runNextBatch = std::make_shared<std::function<void()>>();
        const int totalBatches = hrefBatches.size();

        *runNextBatch = [this, op, calendarId, davUrl, hrefBatches, fetchedItemsMap,
                          batchIndex, runNextBatch, allItems, serverEtags, totalBatches]() {
            if (*batchIndex >= hrefBatches.size()) {
                processFetchedItems(op, calendarId, davUrl, allItems, serverEtags, *fetchedItemsMap);
                return;
            }

            KDAV::DavItemsFetchJob *fetchJob =
                new KDAV::DavItemsFetchJob(davUrl, hrefBatches.at(*batchIndex), this);

            connect(fetchJob, &KDAV::DavItemsFetchJob::result, this,
                    [this, op, calendarId, fetchJob, fetchedItemsMap, batchIndex,
                     runNextBatch, totalBatches](KJob *fj) {
                if (op->state() == SyncOperation::Cancelled) {
                    emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                    return;
                }

                if (fj->error()) {
                    const QString errorMsg = QStringLiteral(
                        "Failed to fetch items (batch %1/%2): %3")
                            .arg(*batchIndex + 1).arg(totalBatches)
                            .arg(davJobErrorMessage(static_cast<KDAV::DavJobBase *>(fj)));
                    op->fail(errorMsg);
                    emit fetchFinished(calendarId, false, errorMsg);
                    return;
                }

                // Use normalizeUrlKey (strips credentials) so the map key matches
                // regardless of whether the multiget response URL includes user-info
                // or not. Discovery URLs carry credentials (http://user@host/...);
                // multiget response URLs typically don't (http://host/...) — using
                // a raw toDisplayString() key here caused a systematic lookup miss
                // (FINDINGS 2026-05-09 "FakeCalDavServer multiget REPORT").
                for (const auto &davItem : fetchJob->items()) {
                    (*fetchedItemsMap)[normalizeUrlKey(davItem.url().url().toString())] = davItem;
                }

                ++(*batchIndex);
                (*runNextBatch)();
            });

            startJobWithWatchdog(fetchJob, [this, op, calendarId, batchIndex, totalBatches]() {
                if (op->isFinished()) {
                    return;
                }
                const QString errorMsg = QStringLiteral(
                    "Failed to fetch items (batch %1/%2): transfer timed out")
                        .arg(*batchIndex + 1).arg(totalBatches);
                op->fail(errorMsg);
                emit fetchFinished(calendarId, false, errorMsg);
            });
        };

        (*runNextBatch)();
    });

    startJobWithWatchdog(listJob, [this, op, calendarId]() {
        if (op->isFinished()) {
            return;
        }
        const QString errorMsg = QStringLiteral("Failed to list items: transfer timed out");
        op->fail(errorMsg);
        emit fetchFinished(calendarId, false, errorMsg);
    });
}

void RemoteCalendarBackend::processFetchedItems(FetchOperation *op, const QString &calendarId,
                                                 const KDAV::DavUrl &davUrl,
                                                 const KDAV::DavItem::List &allItems,
                                                 const QMap<QString, QString> &serverEtags,
                                                 const QMap<QString, KDAV::DavItem> &fetchedItemsMap)
{
    QList<KCalendarCore::Incidence::Ptr> fetchedIncidences;

    int currentItem = 0;
    const int totalItems = allItems.size();
    int countFromNetwork = 0;
    int countFromCache = 0;
    int countSkipped = 0;

    // Process all items, using fetched data for changed items and cache for unchanged
    for (const auto &item : allItems) {
        if (op->state() == SyncOperation::Cancelled) {
            emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
            return;
        }

        // URL without credentials for both fetchedItemsMap lookup and cache
        QString urlKey = normalizeUrlKey(item.url().url().toString());

        QString etag = serverEtags.value(urlKey);
        QString icalData;
        bool fromNetwork = false;

        // Check if this item was fetched from network
        if (fetchedItemsMap.contains(urlKey)) {
            const KDAV::DavItem &davItem = fetchedItemsMap[urlKey];
            icalData = QString::fromUtf8(davItem.data());
            etag = davItem.etag();
            fromNetwork = true;

            // Update cache with fresh content
            if (!icalData.isEmpty() && !etag.isEmpty()) {
                m_contentCache->store(urlKey, etag, icalData);
            }
        } else {
            // Serve from cache
            icalData = m_contentCache->content(urlKey, etag);
            if (icalData.isEmpty()) {
                qWarning() << "RemoteCalendarBackend::fetchItems: Cache miss for item:" << urlKey;
                countSkipped++;
                currentItem++;
                emit fetchProgressChanged(calendarId, currentItem, totalItems);
                continue;
            }
        }

        const auto incidences = incidencesFromIcal(icalData);
        if (incidences.isEmpty()) {
            qWarning() << "RemoteCalendarBackend::fetchItems: Could not parse iCal data for item:"
                       << urlKey << (fromNetwork ? "(from network)" : "(from cache)");
            countSkipped++;
            currentItem++;
            emit fetchProgressChanged(calendarId, currentItem, totalItems);
            continue;
        }

        if (fromNetwork) {
            countFromNetwork++;
        } else {
            countFromCache++;
        }

        // Update ETag caches - use urlKey (no credentials) to match KDAV's format
        if (!etag.isEmpty()) {
            m_localEtags[urlKey] = etag;
            if (m_etagCache) {
                m_etagCache->setEtag(urlKey, etag);
            }
        }

        for (const auto &incidence : incidences) {
            // Phase B5: remember the verbatim bytes this incidence came
            // from (network response or cache) — see m_lastRawIcsByUid's
            // doc comment.
            m_lastRawIcsByUid[incidence->uid()] = icalData.toUtf8();
            fetchedIncidences.append(incidence);
        }

        currentItem++;
        emit fetchProgressChanged(calendarId, currentItem, totalItems);
    }

    qDebug() << "RemoteCalendarBackend::fetchItems: Fetched" << fetchedIncidences.size()
             << "incidences for calendar" << calendarId
             << "(" << countFromNetwork << "from network,"
             << countFromCache << "from cache"
             << (countSkipped > 0 ? QString(", %1 skipped)").arg(countSkipped)
                                  : QStringLiteral(")"));

    // E9.1 (sync-excellence campaign, O34): batch signal, once per
    // full/mixed network+cache multiget pass, with the full item list
    // (syncbackend.h).
    emit itemsFetched(calendarId, fetchedIncidences);

    // N5 fix: only commit the pending CTag when every item materialized
    // (this function is only reached after every multiget batch already
    // succeeded — see the batch runner in fetchItems — so the remaining
    // completeness gate is countSkipped). Committing on a partial result
    // would let a later CTag-match short-circuit silently serve the
    // incomplete set (the "CTag ahead of content cache" bug).
    if (countSkipped == 0) {
        const QString pendingCtag = m_calendars.value(calendarId).pendingCtag;
        if (!pendingCtag.isEmpty()) {
            setCtag(calendarId, pendingCtag);
        }
    } else {
        qWarning() << "RemoteCalendarBackend::fetchItems: NOT committing CTag for"
                   << calendarId << "-" << countSkipped << "item(s) skipped";
    }

    bootstrapSyncTokenIfNeeded(calendarId, davUrl, [this, op, calendarId, fetchedIncidences]() {
        op->setFetchedItems(fetchedIncidences);
        op->complete();
        emit fetchFinished(calendarId, true);
    });
}

void RemoteCalendarBackend::bootstrapSyncTokenIfNeeded(const QString &calendarId,
                                                       const KDAV::DavUrl &davUrl,
                                                       std::function<void()> continuation)
{
    if (!m_calendars.value(calendarId).supportsSyncCollection
        || !syncToken(calendarId).isEmpty()) {
        continuation();
        return;
    }

    // E7/O36 design step 3 ("capture from the initial REPORT"): the full
    // listing this call follows just materialized every item — acquire the
    // collection's CURRENT sync-token via one empty-token REPORT so every
    // later cycle can use continueFetchWithSyncCollection instead of
    // relisting forever. Best-effort: any failure here just leaves the
    // calendar on the CTag+listing fallback for one more cycle, it does not
    // fail @p continuation.
    const QByteArray body = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:sync-collection xmlns:D=\"DAV:\">"
        "<D:sync-token/>"
        "<D:sync-level>1</D:sync-level>"
        "<D:prop><D:getetag/></D:prop>"
        "</D:sync-collection>");

    davSyncRequestAsync(nam(), davUrl.url(), QByteArrayLiteral("REPORT"),
                        m_username, m_password, body,
                        {{QByteArrayLiteral("Depth"), QByteArrayLiteral("0")}},
                        QByteArrayLiteral("application/xml; charset=utf-8"),
        [this, calendarId, continuation](const DavResponse &resp) {
            if (resp.transportOk() && resp.status == 207) {
                const SyncCollectionDelta delta = parseSyncCollectionMultistatus(resp.body);
                if (!delta.newToken.isEmpty()) {
                    setSyncToken(calendarId, delta.newToken);
                }
            }
            continuation();
        });
}

void RemoteCalendarBackend::continueFetchWithSyncCollection(FetchOperation *op,
                                                             const QString &calendarId,
                                                             const KDAV::DavUrl &davUrl,
                                                             const QString &freshCtag,
                                                             const QString &storedToken)
{
    if (!freshCtag.isEmpty()) {
        m_calendars[calendarId].pendingCtag = freshCtag;
    }

    const QByteArray body = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:sync-collection xmlns:D=\"DAV:\">"
        "<D:sync-token>") + storedToken.toUtf8() + QByteArrayLiteral("</D:sync-token>"
        "<D:sync-level>1</D:sync-level>"
        "<D:prop><D:getetag/></D:prop>"
        "</D:sync-collection>");

    davSyncRequestAsync(nam(), davUrl.url(), QByteArrayLiteral("REPORT"),
                        m_username, m_password, body,
                        {{QByteArrayLiteral("Depth"), QByteArrayLiteral("0")}},
                        QByteArrayLiteral("application/xml; charset=utf-8"),
        [this, op, calendarId, davUrl, freshCtag](const DavResponse &resp) {
            if (op->state() == SyncOperation::Cancelled) {
                emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                return;
            }
            // RFC 6578 §3.3: the server rejects an unrecognized/expired
            // sync-token with 409/410/507 (a valid-sync-token precondition
            // failure). Checked BEFORE the transport-error branch below —
            // Qt's QNetworkAccessManager surfaces any non-2xx HTTP status as
            // a QNetworkReply error (transportOk() == false), but
            // davResponseFromReply still populates the real HTTP status via
            // HttpStatusCodeAttribute regardless of resp.error, so
            // resp.status is trustworthy here. Clear the stale token and
            // fall back to a full listing for THIS cycle —
            // bootstrapSyncTokenIfNeeded() re-acquires a fresh token once
            // that listing completes.
            if (resp.status == 409 || resp.status == 410 || resp.status == 507) {
                qWarning() << "RemoteCalendarBackend::fetchItems: sync-token invalid for"
                           << calendarId << "(HTTP" << resp.status
                           << ") - clearing token, falling back to full listing";
                clearSyncToken(calendarId);
                continueFetchWithListing(op, calendarId, davUrl, freshCtag);
                return;
            }
            if (!resp.transportOk()) {
                const QString errorMsg = QStringLiteral(
                    "sync-collection REPORT failed: %1").arg(resp.errorString);
                op->fail(errorMsg);
                emit fetchFinished(calendarId, false, errorMsg);
                return;
            }
            if (resp.status != 207) {
                const QString errorMsg = QStringLiteral(
                    "sync-collection REPORT returned unexpected status %1")
                        .arg(resp.status);
                op->fail(errorMsg);
                emit fetchFinished(calendarId, false, errorMsg);
                return;
            }

            const SyncCollectionDelta delta = parseSyncCollectionMultistatus(resp.body);
            if (delta.newToken.isEmpty()) {
                // Malformed/unparseable response — fail soft to the listing
                // path rather than trust a delta we can't verify landed.
                qWarning() << "RemoteCalendarBackend::fetchItems: sync-collection "
                              "REPORT for" << calendarId
                           << "carried no sync-token - falling back to full listing";
                continueFetchWithListing(op, calendarId, davUrl, freshCtag);
                return;
            }

            // Tombstones apply immediately — sync-collection's other O36
            // half is that deletions no longer need a full listing either.
            for (const QString &hrefPath : delta.deletedHrefs) {
                QUrl itemUrl = davUrl.url();
                itemUrl.setPath(hrefPath);
                noteItemErased(normalizeUrlKey(itemUrl.toString()));
            }

            if (delta.changedHrefs.isEmpty()) {
                // Nothing to download, but recordsFromLastFetch()'s contract
                // (H5/O23) is a FULL current-collection snapshot every
                // cycle, same as the listing path — a delta-only result
                // would look to the engine's diff like everything else in
                // the collection just got deleted. Tombstones (if any) were
                // already applied to the content cache above, so re-reading
                // it now yields exactly the current set.
                auto currentIncidences = serveCachedItems(calendarId, davUrl);
                emit fetchStarted(calendarId, currentIncidences.size());
                op->setFetchedItems(currentIncidences);
                op->complete();
                emit fetchFinished(calendarId, true);
                setSyncToken(calendarId, delta.newToken);
                if (!freshCtag.isEmpty()) setCtag(calendarId, freshCtag);
                return;
            }

            emit fetchStarted(calendarId, delta.changedHrefs.size());

            QStringList urlsToFetch;
            for (auto it = delta.changedHrefs.constBegin(); it != delta.changedHrefs.constEnd(); ++it) {
                QUrl itemUrl = davUrl.url();
                itemUrl.setPath(it.key());
                urlsToFetch << itemUrl.toDisplayString();
            }

            QList<QStringList> hrefBatches;
            for (int i = 0; i < urlsToFetch.size(); i += m_multigetChunkSize)
                hrefBatches.append(urlsToFetch.mid(i, m_multigetChunkSize));

            auto fetchedItemsMap = std::make_shared<QMap<QString, KDAV::DavItem>>();
            auto batchIndex = std::make_shared<int>(0);
            auto runNextBatch = std::make_shared<std::function<void()>>();
            const int totalBatches = hrefBatches.size();

            *runNextBatch = [this, op, calendarId, davUrl, hrefBatches, fetchedItemsMap,
                              batchIndex, runNextBatch, delta, freshCtag, totalBatches]() {
                if (*batchIndex >= hrefBatches.size()) {
                    completeSyncCollectionFetch(op, calendarId, davUrl,
                                                delta.changedHrefs, delta.deletedHrefs,
                                                delta.newToken, freshCtag, *fetchedItemsMap);
                    return;
                }

                KDAV::DavItemsFetchJob *fetchJob =
                    new KDAV::DavItemsFetchJob(davUrl, hrefBatches.at(*batchIndex), this);

                connect(fetchJob, &KDAV::DavItemsFetchJob::result, this,
                        [this, op, calendarId, fetchJob, fetchedItemsMap, batchIndex,
                         runNextBatch, totalBatches](KJob *fj) {
                    if (op->state() == SyncOperation::Cancelled) {
                        emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                        return;
                    }
                    if (fj->error()) {
                        const QString errorMsg = QStringLiteral(
                            "sync-collection multiget failed (batch %1/%2): %3")
                                .arg(*batchIndex + 1).arg(totalBatches)
                                .arg(davJobErrorMessage(static_cast<KDAV::DavJobBase *>(fj)));
                        op->fail(errorMsg);
                        emit fetchFinished(calendarId, false, errorMsg);
                        return;
                    }
                    for (const auto &davItem : fetchJob->items()) {
                        (*fetchedItemsMap)[normalizeUrlKey(davItem.url().url().toString())] = davItem;
                    }
                    ++(*batchIndex);
                    (*runNextBatch)();
                });

                startJobWithWatchdog(fetchJob, [this, op, calendarId, batchIndex, totalBatches]() {
                    if (op->isFinished()) return;
                    const QString errorMsg = QStringLiteral(
                        "sync-collection multiget timed out (batch %1/%2)")
                            .arg(*batchIndex + 1).arg(totalBatches);
                    op->fail(errorMsg);
                    emit fetchFinished(calendarId, false, errorMsg);
                });
            };

            (*runNextBatch)();
        });
}

void RemoteCalendarBackend::completeSyncCollectionFetch(
    FetchOperation *op, const QString &calendarId, const KDAV::DavUrl &davUrl,
    const QMap<QString, QString> &changedHrefs, const QStringList &deletedHrefs,
    const QString &newToken, const QString &freshCtag,
    const QMap<QString, KDAV::DavItem> &fetchedItemsMap)
{
    int countFetched = 0;
    int countSkipped = 0;

    // Store every changed item's fresh content into the persistent cache +
    // both ETag stores. Deliberately does NOT build the op's result from
    // just these items — see the serveCachedItems() call below.
    for (auto it = changedHrefs.constBegin(); it != changedHrefs.constEnd(); ++it) {
        QUrl itemUrl = davUrl.url();
        itemUrl.setPath(it.key());
        const QString urlKey = normalizeUrlKey(itemUrl.toString());

        const auto fetchedIt = fetchedItemsMap.constFind(urlKey);
        if (fetchedIt == fetchedItemsMap.constEnd()) {
            qWarning() << "RemoteCalendarBackend::fetchItems: sync-collection multiget "
                          "response missing item:" << urlKey;
            ++countSkipped;
            continue;
        }

        const QByteArray icalData = fetchedIt->data();
        const QString etag = fetchedIt->etag();
        if (icalData.isEmpty() || etag.isEmpty()) {
            ++countSkipped;
            continue;
        }

        m_contentCache->store(urlKey, etag, QString::fromUtf8(icalData));
        m_localEtags[urlKey] = etag;
        if (m_etagCache) m_etagCache->setEtag(urlKey, etag);
        ++countFetched;
    }

    // recordsFromLastFetch()'s contract (H5/O23) is a FULL current-collection
    // snapshot every cycle — the same contract the CTag+listing path honors
    // by processing every item (cache-hit or network-fetched) on every
    // cycle. A sync-collection cycle only downloads the delta, but the
    // content cache now holds the union of "everything seen before" plus
    // "what just changed" minus "what the caller already erased for
    // deletedHrefs" — re-reading it whole reconstructs the same full
    // snapshot the listing path would have produced, without a listing.
    auto fetchedIncidences = serveCachedItems(calendarId, davUrl);

    qDebug() << "RemoteCalendarBackend::fetchItems: sync-collection fetched"
             << countFetched << "changed," << deletedHrefs.size()
             << "deleted, snapshot now" << fetchedIncidences.size()
             << "for calendar" << calendarId
             << (countSkipped > 0 ? QString(" (%1 skipped)").arg(countSkipped) : QString());

    // N5 discipline, applied to the token: only commit the new sync-token
    // (and CTag) when every changed href actually materialized. Committing
    // on a partial result would let the NEXT cycle's delta start counting
    // from a token that already claims these hrefs are caught up, silently
    // losing them forever.
    if (countSkipped == 0) {
        setSyncToken(calendarId, newToken);
        const QString pendingCtag = m_calendars.value(calendarId).pendingCtag;
        if (!pendingCtag.isEmpty()) {
            setCtag(calendarId, pendingCtag);
        } else if (!freshCtag.isEmpty()) {
            setCtag(calendarId, freshCtag);
        }
    } else {
        qWarning() << "RemoteCalendarBackend::fetchItems: NOT committing sync-token for"
                   << calendarId << "-" << countSkipped << "item(s) skipped";
    }

    op->setFetchedItems(fetchedIncidences);
    op->complete();
    emit fetchFinished(calendarId, true);
}

PushOperation* RemoteCalendarBackend::pushItems(const QString &calendarId,
                                        const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = onOwnerThread(new PushOperation(calendarId, items), this);

    // Use shared counter to track completion
    auto remaining = std::make_shared<int>(items.size());
    auto anyError = std::make_shared<bool>(false);

    // E5.2: serialize on calendarId via E5.1's per-collection FIFO queue (same
    // shape as fetchItems). The empty-items and no-URL early exits now settle
    // the op from inside the queued body, so they too respect FIFO ordering.
    enqueueOperation(calendarId, op, [this, op, calendarId, items, remaining, anyError]() mutable {
        op->setState(SyncOperation::Running);

        if (items.isEmpty()) {
            op->complete();
            return;
        }

        const auto maybeDavUrl = davUrlFor(calendarId);
        if (!maybeDavUrl) {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
            return;
        }
        const KDAV::DavUrl davUrl = *maybeDavUrl;

        // Initialize content cache so we can store pushed items for subsequent fetches
        m_contentCache->ensureOpen();

        // Shared accounting tail for every per-item outcome. When the last
        // item lands: invalidate the stored CTag (our push changed it
        // server-side) and settle the op — fail only when nothing succeeded;
        // partial success completes with failedUids tracked (the F2 operation
        // contract). Pre-T4 the three accounting sites disagreed (a trailing
        // null item never settled the op at all; a trailing serialization
        // failure failed a partly-successful push).
        auto settleIfDone = [this, op, remaining, anyError]() {
            if (--(*remaining) != 0) {
                return;
            }
            if (!op->succeededUids().isEmpty()) {
                clearCtag(op->calendarId());
            }
            if ((*anyError || !op->failedUids().isEmpty())
                && op->succeededUids().isEmpty()) {
                op->fail(QStringLiteral("All items failed to push"));
            } else {
                op->complete();
            }
        };

        for (const auto &incidence : items) {
            if (incidence.isNull()) {
                settleIfDone();
                continue;
            }

            const QString icalData = QString::fromUtf8(icalFromIncidence(incidence));

            if (icalData.isEmpty()) {
                qWarning() << "RemoteCalendarBackend::pushItems: Failed to convert incidence to iCal:" << incidence->uid();
                op->addFailedUid(incidence->uid());
                settleIfDone();
                continue;
            }

            QUrl itemUrl = generateItemUrl(davUrl, incidence->uid());

            KDAV::DavItem davItem;
            davItem.setUrl(KDAV::DavUrl(itemUrl, davUrl.protocol()));
            davItem.setContentType(QStringLiteral("text/calendar"));
            davItem.setData(icalData.toUtf8());

            auto *createJob = new KDAV::DavItemCreateJob(davItem, this);
            QString uid = incidence->uid();

            connect(createJob, &KDAV::DavItemCreateJob::result, this,
                    [this, op, createJob, uid, anyError, icalData, settleIfDone](KJob *job) {
                if (op->state() == SyncOperation::Cancelled) {
                    return;
                }

                if (job->error()) {
                    qWarning() << "RemoteCalendarBackend::pushItems: Failed to create item:" << job->errorString();
                    op->addFailedUid(uid);
                    *anyError = true;
                } else {
                    const KDAV::DavItem createdItem = createJob->item();
                    noteItemWritten(normalizeUrlKey(createdItem.url().url().toString()),
                                    createdItem.etag(), icalData);
                    op->addSucceededUid(uid);
                    qDebug() << "RemoteCalendarBackend::pushItems: Created" << uid << "ETag:" << createdItem.etag();
                }

                settleIfDone();
            });

            startJobWithWatchdog(createJob, [op, uid, anyError, settleIfDone]() {
                if (op->isFinished()) {
                    return;
                }
                qWarning() << "RemoteCalendarBackend::pushItems: create job timed out for" << uid;
                op->addFailedUid(uid);
                *anyError = true;
                settleIfDone();
            });
        }
    });

    return op;
}


DeleteOperation* RemoteCalendarBackend::deleteItems(const QString &calendarId,
                                            const QStringList &uids)
{
    auto *op = onOwnerThread(new DeleteOperation(calendarId, uids), this);

    auto remaining = std::make_shared<int>(uids.size());
    auto anyError = std::make_shared<bool>(false);

    // E5.2: serialize on calendarId via E5.1's per-collection FIFO queue (same
    // shape as fetchItems/pushItems).
    enqueueOperation(calendarId, op, [this, op, calendarId, uids, remaining, anyError]() {
        op->setState(SyncOperation::Running);

        if (uids.isEmpty()) {
            op->complete();
            return;
        }

        const auto maybeDavUrl = davUrlFor(calendarId);
        if (!maybeDavUrl) {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
            return;
        }
        const KDAV::DavUrl davUrl = *maybeDavUrl;

        // Shared accounting tail (parallel to pushItems' settleIfDone): decrement
        // the outstanding-job counter and, on the last one, invalidate the CTag
        // and settle the op. Reused by both the per-job result handler and the
        // H5.5 watchdog so a timed-out delete settles identically to a failed one.
        auto settleIfDone = [this, op, remaining, anyError]() {
            if (--(*remaining) != 0) {
                return;
            }
            if (!op->succeededUids().isEmpty()) {
                clearCtag(op->calendarId());
            }
            if ((*anyError || !op->failedUids().isEmpty())
                && op->succeededUids().isEmpty()) {
                op->fail(QStringLiteral("All items failed to delete"));
            } else {
                op->complete();
            }
        };

        for (const QString &uid : uids) {
            QUrl itemUrl = generateItemUrl(davUrl, uid);
            KDAV::DavUrl itemDavUrl(itemUrl, davUrl.protocol());

            QString oldEtag = cachedEtag(itemUrl.toString());

            KDAV::DavItem davItem;
            davItem.setUrl(itemDavUrl);
            davItem.setContentType(QStringLiteral("text/calendar"));
            davItem.setData(QByteArray());
            davItem.setEtag(oldEtag);

            auto *deleteJob = new KDAV::DavItemDeleteJob(davItem, this);

            connect(deleteJob, &KDAV::DavItemDeleteJob::result, this,
                    [this, op, uid, itemUrl, anyError, settleIfDone](KJob *job) {
                if (op->state() == SyncOperation::Cancelled) {
                    return;
                }

                if (job->error()) {
                    qWarning() << "RemoteCalendarBackend::deleteItems: Failed to delete" << uid << ":" << job->errorString();
                    op->addFailedUid(uid);
                    *anyError = true;
                } else {
                    noteItemErased(normalizeUrlKey(itemUrl.toString()));
                    op->addSucceededUid(uid);
                    qDebug() << "RemoteCalendarBackend::deleteItems: Deleted" << uid;
                }

                settleIfDone();
            });

            startJobWithWatchdog(deleteJob, [op, uid, anyError, settleIfDone]() {
                if (op->isFinished()) {
                    return;
                }
                qWarning() << "RemoteCalendarBackend::deleteItems: delete job timed out for" << uid;
                op->addFailedUid(uid);
                *anyError = true;
                settleIfDone();
            });
        }
    });

    return op;
}

// ============================================================================
// E5.3 (audit B7 / CP-A): the write path — applyRecords()
// ============================================================================
//
// The engine's live write path (SyncEngineWorker::applyBatch) calls this
// instead of ever calling RecordWriter::apply()/createRecord()/updateRecord()/
// deleteRecord(). Same shape as pushItems()/deleteItems() above: an op is
// enqueued via E5.1's per-collection FIFO queue, its body fans out one KDAV
// job (or async PUT) per record with a shared remaining-counter, and
// settles the op once every job has reported in. Creates and deletes reuse
// the exact same job types pushItems()/deleteItems() already use; updates
// route through the new setRawIcsAsync() below (async counterpart of
// setRawIcs(), which updateRecord() still uses synchronously — that single-
// record IBlobBackend virtual has its own, unrelated callers and is
// unaffected by this).
WriteOperation* RemoteCalendarBackend::applyRecords(const QString &calendarId,
                                                    const WriterBatch &batch)
{
    auto *op = onOwnerThread(new WriteOperation(calendarId), this);

    enqueueOperation(calendarId, op, [this, op, calendarId, batch]() {
        op->setState(SyncOperation::Running);

        const int total = static_cast<int>(
            batch.creates.size() + batch.updates.size() + batch.deletes.size());
        if (total == 0) {
            op->complete();
            return;
        }

        const auto maybeDavUrl = davUrlFor(calendarId);
        if (!maybeDavUrl) {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
            return;
        }
        const KDAV::DavUrl davUrl = *maybeDavUrl;

        m_contentCache->ensureOpen();

        // E5.3 crash fix: every async completion below (KJob::result,
        // watchdog timeout, setRawIcsAsync's done callback) can fire well
        // after this op's caller has moved on — e.g. a cancel settles the op
        // and the engine's applyBatch deleteLater()s it immediately, while
        // an already-in-flight KDAV job or watchdog timer (unaffected by
        // cancellation — nothing here kills the underlying network request)
        // still has a queued completion pending. A raw `op` capture would
        // dereference freed memory when that fires; QPointer makes every
        // capture site check-before-use instead (mirrors how the engine's
        // OWN fetch/write gates already treat SyncOperation* as a QPointer
        // for exactly this cross-callback lifetime reason).
        QPointer<WriteOperation> opWeak(op);
        auto remaining = std::make_shared<int>(total);
        auto anyError = std::make_shared<bool>(false);

        // Shared accounting tail — same shape as pushItems'/deleteItems'
        // settleIfDone: decrement the outstanding-job counter and, on the
        // last one, invalidate the CTag (our write changed the server's
        // state) and settle the op. Fail only when nothing at all succeeded.
        auto settleIfDone = [this, opWeak, remaining, anyError]() {
            if (--(*remaining) != 0) {
                return;
            }
            if (opWeak.isNull()) {
                return;
            }
            if (!opWeak->succeededUids().isEmpty()) {
                clearCtag(opWeak->calendarId());
            }
            if ((*anyError || !opWeak->failedUids().isEmpty())
                && opWeak->succeededUids().isEmpty()) {
                opWeak->fail(QStringLiteral("All records failed to apply"));
            } else {
                opWeak->complete();
            }
        };

        for (const auto &rec : batch.creates) {
            QUrl itemUrl = generateItemUrl(davUrl, rec.id);

            KDAV::DavItem davItem;
            davItem.setUrl(KDAV::DavUrl(itemUrl, davUrl.protocol()));
            davItem.setContentType(QStringLiteral("text/calendar"));
            davItem.setData(rec.data);

            auto *createJob = new KDAV::DavItemCreateJob(davItem, this);
            const QString uid = rec.id;
            const QByteArray icalData = rec.data;

            connect(createJob, &KDAV::DavItemCreateJob::result, this,
                    [this, opWeak, createJob, uid, anyError, icalData, settleIfDone](KJob *job) {
                if (opWeak.isNull() || opWeak->state() == SyncOperation::Cancelled) {
                    return;
                }
                if (job->error()) {
                    qWarning() << "RemoteCalendarBackend::applyRecords: Failed to create"
                               << uid << ":" << job->errorString();
                    opWeak->addFailedUid(uid);
                    *anyError = true;
                } else {
                    const KDAV::DavItem createdItem = createJob->item();
                    noteItemWritten(normalizeUrlKey(createdItem.url().url().toString()),
                                    createdItem.etag(), QString::fromUtf8(icalData));
                    opWeak->addSucceededUid(uid);
                    qDebug() << "RemoteCalendarBackend::applyRecords: Created" << uid
                             << "ETag:" << createdItem.etag();
                }
                settleIfDone();
            });

            startJobWithWatchdog(createJob, [opWeak, uid, anyError, settleIfDone]() {
                if (opWeak.isNull() || opWeak->isFinished()) {
                    return;
                }
                qWarning() << "RemoteCalendarBackend::applyRecords: create job timed out for" << uid;
                opWeak->addFailedUid(uid);
                *anyError = true;
                settleIfDone();
            });
        }

        for (const auto &rec : batch.updates) {
            const QString uid = rec.id;
            setRawIcsAsync(calendarId, uid, rec.data,
                          [opWeak, uid, anyError, settleIfDone](bool ok) {
                if (opWeak.isNull() || opWeak->state() == SyncOperation::Cancelled) {
                    return;
                }
                if (!ok) {
                    opWeak->addFailedUid(uid);
                    *anyError = true;
                } else {
                    opWeak->addSucceededUid(uid);
                }
                settleIfDone();
            });
        }

        for (const QString &uid : batch.deletes) {
            QUrl itemUrl = generateItemUrl(davUrl, uid);
            KDAV::DavUrl itemDavUrl(itemUrl, davUrl.protocol());

            QString oldEtag = cachedEtag(itemUrl.toString());

            KDAV::DavItem davItem;
            davItem.setUrl(itemDavUrl);
            davItem.setContentType(QStringLiteral("text/calendar"));
            davItem.setData(QByteArray());
            davItem.setEtag(oldEtag);

            auto *deleteJob = new KDAV::DavItemDeleteJob(davItem, this);

            connect(deleteJob, &KDAV::DavItemDeleteJob::result, this,
                    [this, opWeak, uid, itemUrl, anyError, settleIfDone](KJob *job) {
                if (opWeak.isNull() || opWeak->state() == SyncOperation::Cancelled) {
                    return;
                }
                if (job->error()) {
                    qWarning() << "RemoteCalendarBackend::applyRecords: Failed to delete"
                               << uid << ":" << job->errorString();
                    opWeak->addFailedUid(uid);
                    *anyError = true;
                } else {
                    noteItemErased(normalizeUrlKey(itemUrl.toString()));
                    opWeak->addSucceededUid(uid);
                    qDebug() << "RemoteCalendarBackend::applyRecords: Deleted" << uid;
                }
                settleIfDone();
            });

            startJobWithWatchdog(deleteJob, [opWeak, uid, anyError, settleIfDone]() {
                if (opWeak.isNull() || opWeak->isFinished()) {
                    return;
                }
                qWarning() << "RemoteCalendarBackend::applyRecords: delete job timed out for" << uid;
                opWeak->addFailedUid(uid);
                *anyError = true;
                settleIfDone();
            });
        }
    });

    return op;
}

// Async counterpart of setRawIcs() (E5.3): same wire behaviour (PUT with
// If-Match on the cached ETag), but the continuation runs off
// QNetworkReply::finished via davSyncRequestAsync — no nested QEventLoop.
// davSyncRequestAsync doesn't produce a KJob, so startJobWithWatchdog (which
// takes a KJob*) can't be reused directly; this inlines the identical "log +
// abandon + fail" shape with its own QTimer, guarded so only whichever of
// {reply finishes, timer fires} first can settle `done` (both paths check
// `*settled` before acting).
void RemoteCalendarBackend::setRawIcsAsync(const QString &calendarId, const QString &uid,
                                           const QByteArray &icsContent,
                                           std::function<void(bool)> done)
{
    if (calendarId.isEmpty() || uid.isEmpty() || icsContent.isEmpty()) {
        done(false);
        return;
    }

    const auto maybeDavUrl = davUrlFor(calendarId);
    if (!maybeDavUrl) {
        qWarning() << "RemoteCalendarBackend::setRawIcsAsync: No DAV URL for calendar:" << calendarId;
        done(false);
        return;
    }
    const KDAV::DavUrl davUrl = *maybeDavUrl;
    const QUrl itemUrl = generateItemUrl(davUrl, uid);

    QList<std::pair<QByteArray, QByteArray>> headers;
    const QString oldEtag = cachedEtag(itemUrl.toString());
    if (!oldEtag.isEmpty()) {
        headers.append({QByteArrayLiteral("If-Match"), oldEtag.toUtf8()});
    }

    auto settled = std::make_shared<bool>(false);
    QTimer *watchdog = nullptr;
    if (m_transferTimeoutMs > 0) {
        watchdog = new QTimer(this);
        watchdog->setSingleShot(true);
        watchdog->setInterval(m_transferTimeoutMs);
        const int timeoutMs = m_transferTimeoutMs;
        connect(watchdog, &QTimer::timeout, this,
                [settled, uid, done, watchdog, timeoutMs]() {
            if (*settled) return;
            *settled = true;
            qWarning() << "RemoteCalendarBackend::setRawIcsAsync: PUT job exceeded transfer timeout ("
                       << timeoutMs << "ms) for" << uid;
            watchdog->deleteLater();
            done(false);
        });
        watchdog->start();
    }

    davSyncRequestAsync(nam(), itemUrl, QByteArrayLiteral("PUT"), m_username, m_password,
                       icsContent, headers,
                       QByteArrayLiteral("text/calendar; charset=utf-8"),
        [this, settled, watchdog, calendarId, uid, itemUrl, done](const DavResponse &resp) {
            if (*settled) return;
            *settled = true;
            if (watchdog) {
                watchdog->stop();
                watchdog->deleteLater();
            }

            if (resp.status != 200 && resp.status != 201 && resp.status != 204) {
                qWarning() << "RemoteCalendarBackend::setRawIcsAsync: Failed, HTTP status:" << resp.status
                           << "error:" << resp.errorString << "body:" << resp.body;
                done(false);
                return;
            }

            const QString urlKey = normalizeUrlKey(itemUrl.toString());
            if (!resp.etag.isEmpty()) {
                m_localEtags[urlKey] = resp.etag;
                if (m_etagCache) {
                    m_etagCache->setEtag(urlKey, resp.etag);
                }
            } else {
                qWarning() << "RemoteCalendarBackend::setRawIcsAsync: Server didn't return ETag, clearing cache";
                m_localEtags.remove(urlKey);
                if (m_etagCache) {
                    m_etagCache->removeEtag(urlKey);
                }
            }
            clearCtag(calendarId);
            done(true);
        });
}

// ============================================================================
// Debug/Raw ICS Access
// ============================================================================

QString RemoteCalendarBackend::getRawIcs(const QString &calendarId, const QString &uid) const
{
    if (calendarId.isEmpty() || uid.isEmpty()) {
        return QString();
    }

    if (!davUrlFor(calendarId)) {
        qWarning() << "RemoteCalendarBackend::getRawIcs: No DAV URL for calendar:" << calendarId;
        return QString();
    }

    KDAV::DavUrl davUrl = *davUrlFor(calendarId);
    QUrl itemUrl = generateItemUrl(davUrl, uid);

    const DavResponse resp = davSyncRequest(nam(), itemUrl, QByteArrayLiteral("GET"),
                                            m_username, m_password);
    if (resp.status == 200 && resp.transportOk()) {
        return QString::fromUtf8(resp.body);
    }
    qWarning() << "RemoteCalendarBackend::getRawIcs: Failed to fetch, HTTP status:" << resp.status
               << "error:" << resp.errorString;
    return QString();
}

bool RemoteCalendarBackend::setRawIcs(const QString &calendarId, const QString &uid,
                               const QString &icsContent)
{
    if (calendarId.isEmpty() || uid.isEmpty() || icsContent.isEmpty()) {
        return false;
    }

    if (!davUrlFor(calendarId)) {
        qWarning() << "RemoteCalendarBackend::setRawIcs: No DAV URL for calendar:" << calendarId;
        return false;
    }

    KDAV::DavUrl davUrl = *davUrlFor(calendarId);
    QUrl itemUrl = generateItemUrl(davUrl, uid);

    // Cached ETag goes in If-Match to prevent overwriting concurrent changes.
    QList<std::pair<QByteArray, QByteArray>> headers;
    const QString oldEtag = cachedEtag(itemUrl.toString());
    if (!oldEtag.isEmpty()) {
        headers.append({QByteArrayLiteral("If-Match"), oldEtag.toUtf8()});
        qDebug() << "RemoteCalendarBackend::setRawIcs: Using ETag:" << oldEtag;
    }

    const DavResponse resp = davSyncRequest(
        nam(), itemUrl, QByteArrayLiteral("PUT"), m_username, m_password,
        icsContent.toUtf8(), headers,
        QByteArrayLiteral("text/calendar; charset=utf-8"));

    // 200 OK, 201 Created, or 204 No Content are valid PUT responses
    if (resp.status != 200 && resp.status != 201 && resp.status != 204) {
        // Sabre (and most CalDAV servers) put the exact rejection reason in
        // the response body (e.g. "This resource only supports valid
        // iCalendar 2.0 data..."). Discarding it costs real debugging time
        // (N4) — log it alongside the status/error.
        qWarning() << "RemoteCalendarBackend::setRawIcs: Failed, HTTP status:" << resp.status
                   << "error:" << resp.errorString
                   << "body:" << resp.body;
        return false;
    }

    const QString urlKey = normalizeUrlKey(itemUrl.toString());
    if (!resp.etag.isEmpty()) {
        m_localEtags[urlKey] = resp.etag;
        if (m_etagCache) {
            m_etagCache->setEtag(urlKey, resp.etag);
        }
        qDebug() << "RemoteCalendarBackend::setRawIcs: Updated ETag to:" << resp.etag;
    } else {
        // If server doesn't return ETag, clear the cached one to force refresh
        qWarning() << "RemoteCalendarBackend::setRawIcs: Server didn't return ETag, clearing cache";
        m_localEtags.remove(urlKey);
        if (m_etagCache) {
            m_etagCache->removeEtag(urlKey);
        }
    }
    qDebug() << "RemoteCalendarBackend::setRawIcs: Successfully updated, HTTP status:" << resp.status;

    // Invalidate stored CTag — the server's CTag changed due to our push
    clearCtag(calendarId);
    return true;
}



// ============================================================================
// IBlobBackend implementation (Phase D Task 13)
//
// recordId     = uid (uid.ics is the CalDAV href basename)
// collectionId = calendarId
// data         = raw iCal bytes
// contentHash  = SHA-256 of the bytes
// lastModified = the record's own LAST-MODIFIED (falling back to DTSTAMP,
//                then CREATED); invalid QDateTime if none is present (N3 fix
//                — stamping "now" made every remote record look freshly
//                modified on every load, defeating the LastWriteWins
//                tie-bias fix from v0.64).
// ============================================================================

namespace {

/// Build a BackendRecord from raw iCal bytes and a uid.
Kalburator::Sync::BackendRecord blobRecordFromIcal(
    const QString &uid,
    const QByteArray &icalBytes)
{
    Kalburator::Sync::BackendRecord rec;
    rec.id          = uid;
    rec.type        = QStringLiteral("calendar");
    rec.data        = icalBytes;
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(icalBytes, QCryptographicHash::Sha256).toHex());
    rec.lastModified = Kalburator::Calendar::extractICalTimestamp(icalBytes);
    rec.isDeleted   = false;
    return rec;
}

} // anonymous namespace

// --- Identity ---------------------------------------------------------------

QString RemoteCalendarBackend::backendId() const
{
    // Stable id: type + base URL (credentials stripped)
    QUrl cleanUrl = m_url;
    cleanUrl.setUserInfo(QString());
    const QByteArray h = QCryptographicHash::hash(
        (BackendTypeName + QLatin1Char(':') + cleanUrl.toString()).toUtf8(),
        QCryptographicHash::Sha256);
    return BackendTypeName + QLatin1Char(':') + QString::fromLatin1(h.toHex().left(16));
}

QString RemoteCalendarBackend::displayName() const
{
    QUrl cleanUrl = m_url;
    cleanUrl.setUserInfo(QString());
    return QStringLiteral("RemoteCalendarBackend(%1)").arg(cleanUrl.toString());
}

bool RemoteCalendarBackend::isAvailable() const
{
    return m_url.isValid() && !m_url.isEmpty();
}

// --- Collections ------------------------------------------------------------

QList<CollectionInfo> RemoteCalendarBackend::availableCollections()
{
    QList<CollectionInfo> result;
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        if (it->davUrl.url().isEmpty()) continue;
        CollectionInfo info;
        info.id   = it.key();
        info.name = it.key();
        info.path = it->davUrl.url().toString(QUrl::RemovePassword);
        info.type = QStringLiteral("calendar");
        result.append(info);
    }
    return result;
}

CollectionInfo RemoteCalendarBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id   = collectionId;
    info.name = collectionId;
    if (const auto davUrl = davUrlFor(collectionId)) {
        info.path = davUrl->url().toString(QUrl::RemovePassword);
    }
    info.type = QStringLiteral("calendar");
    return info;
}

QString RemoteCalendarBackend::createCollection(const CollectionInfo &info)
{
    // Delegate to the existing createCalendar which handles MKCALENDAR over CalDAV.
    // collectionId is used as both the calendarId and the name here.
    const QString name = info.name.isEmpty() ? info.id : info.name;
    if (createCalendar(QString(), info.id, name)) {
        return info.id;
    }
    return {};
}

// --- Records ----------------------------------------------------------------

QList<BackendRecord> RemoteCalendarBackend::loadRecords(const QString &collectionId)
{
    // Reuse the existing fetchItems FetchOperation, blocking on its finished signal.
    FetchOperation *op = fetchItems(collectionId);
    if (!op) {
        qWarning() << "RemoteCalendarBackend::loadRecords: fetchItems returned null for" << collectionId;
        return {};
    }

    QList<BackendRecord> result;
    if (!awaitOperation(op)) {
        qWarning() << "RemoteCalendarBackend::loadRecords: fetchItems failed for" << collectionId
                   << ":" << op->errorString();
        op->deleteLater();
        return result;
    }

    for (const auto &incidence : op->fetchedItems()) {
        if (incidence.isNull()) continue;
        // Phase B5 fix: prefer the verbatim bytes this incidence was parsed
        // from (network response or content cache) over re-deriving via
        // icalFromIncidence(). Re-serialization is not equivalent — see
        // m_lastRawIcsByUid's doc comment — and made contentHash unstable
        // across independent loadRecords() calls for unchanged server
        // content, silently defeating convergence. Only falls back to
        // re-derivation if the map has no entry (shouldn't happen for
        // anything fetchItems() actually produced, but fail soft rather
        // than drop the record).
        const QByteArray rawIcs = m_lastRawIcsByUid.value(incidence->uid());
        result.append(blobRecordFromIcal(
            incidence->uid(), !rawIcs.isEmpty() ? rawIcs : icalFromIncidence(incidence)));
    }

    op->deleteLater();
    return result;
}

bool RemoteCalendarBackend::recordsFromLastFetch(const QString &collectionId,
                                                 QList<BackendRecord> &records,
                                                 QString &errorMessage)
{
    auto it = m_lastFetchRecords.find(collectionId);
    if (it == m_lastFetchRecords.end()) {
        return SyncBackendBase::recordsFromLastFetch(collectionId, records, errorMessage);
    }
    records = it.value();
    m_lastFetchRecords.erase(it); // single-shot
    errorMessage.clear();
    return true;
}

std::optional<BackendRecord> RemoteCalendarBackend::loadRecord(const QString &recordId)
{
    // recordId == uid; search all registered calendars.
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        if (it->davUrl.url().isEmpty()) continue;
        const QString icsContent = getRawIcs(it.key(), recordId);
        if (!icsContent.isEmpty()) {
            return blobRecordFromIcal(recordId, icsContent.toUtf8());
        }
    }
    return std::nullopt;
}

QString RemoteCalendarBackend::createRecord(const QString &collectionId,
                                    const BackendRecord &record)
{
    // E5.3: reimplemented as a direct synchronous PUT (davSyncRequest — the
    // one surviving synchronous DAV helper, same one updateRecord's setRawIcs
    // already uses) instead of routing through pushItems()+awaitOperation().
    // Only reachable from a caller already marshaled onto this backend's own
    // thread (dispatchFirstSync's inline blob mirror, FilteredCollectionBackend
    // forwarding) — never from inside an in-flight operation body, so this is
    // not a B7 nested-loop hazard. The live engine steady-state write path
    // goes through applyRecords() instead, which never calls this method.
    if (collectionId.isEmpty() || record.id.isEmpty() || record.data.isEmpty())
        return {};

    const auto maybeDavUrl = davUrlFor(collectionId);
    if (!maybeDavUrl) {
        qWarning() << "RemoteCalendarBackend::createRecord: No DAV URL for calendar:" << collectionId;
        return {};
    }
    const KDAV::DavUrl davUrl = *maybeDavUrl;
    const QUrl itemUrl = generateItemUrl(davUrl, record.id);

    m_contentCache->ensureOpen();

    // New-resource PUT: no If-Match — nothing exists yet to conflict with.
    const DavResponse resp = davSyncRequest(
        nam(), itemUrl, QByteArrayLiteral("PUT"), m_username, m_password,
        record.data, {}, QByteArrayLiteral("text/calendar; charset=utf-8"));

    if (resp.status != 200 && resp.status != 201 && resp.status != 204) {
        qWarning() << "RemoteCalendarBackend::createRecord: Failed, HTTP status:" << resp.status
                   << "error:" << resp.errorString << "body:" << resp.body;
        return {};
    }

    noteItemWritten(normalizeUrlKey(itemUrl.toString()), resp.etag, QString::fromUtf8(record.data));
    clearCtag(collectionId);
    return record.id;
}

std::optional<QString> RemoteCalendarBackend::findOwningCalendar(const QString &uid) const
{
    // Pass 1: the ETag map — an item this backend instance has written or
    // fetched. Cheap, in-memory, the common case.
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        if (it->davUrl.url().isEmpty()) continue;
        const QUrl itemUrl = generateItemUrl(it->davUrl, uid);
        const QString urlKey = normalizeUrlKey(itemUrl.toString());
        if (m_localEtags.contains(urlKey)) return it.key();
    }
    // Pass 2: the persistent content cache — an item fetched in a prior
    // session (this instance's ETag map is empty on a fresh construction).
    if (m_contentCache) {
        for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
            if (it->davUrl.url().isEmpty()) continue;
            const QUrl itemUrl = generateItemUrl(it->davUrl, uid);
            const QString urlKey = normalizeUrlKey(itemUrl.toString());
            if (m_contentCache->contains(urlKey)) return it.key();
        }
    }
    return std::nullopt;
}

bool RemoteCalendarBackend::updateRecord(const BackendRecord &record)
{
    if (record.id.isEmpty() || record.data.isEmpty()) return false;

    const auto calId = findOwningCalendar(record.id);
    if (!calId) {
        // O32: never guess by trying every registered calendar — that can
        // write the item into the WRONG calendar on a multi-calendar backend.
        qWarning() << "RemoteCalendarBackend::updateRecord: uid not found in any owned calendar:" << record.id;
        return false;
    }

    return setRawIcs(*calId, record.id, QString::fromUtf8(record.data));
}

bool RemoteCalendarBackend::deleteRecord(const QString &recordId)
{
    // E5.3: reimplemented as a direct synchronous DELETE (davSyncRequest)
    // instead of routing through deleteItems()+awaitOperation() — same
    // reasoning as createRecord() above.
    if (recordId.isEmpty()) return false;

    const auto calId = findOwningCalendar(recordId);
    if (!calId) {
        qWarning() << "RemoteCalendarBackend::deleteRecord: uid not found in any owned calendar:" << recordId;
        return false;
    }

    const auto maybeDavUrl = davUrlFor(*calId);
    if (!maybeDavUrl) {
        qWarning() << "RemoteCalendarBackend::deleteRecord: No DAV URL for calendar:" << *calId;
        return false;
    }
    const KDAV::DavUrl davUrl = *maybeDavUrl;
    const QUrl itemUrl = generateItemUrl(davUrl, recordId);

    QList<std::pair<QByteArray, QByteArray>> headers;
    const QString oldEtag = cachedEtag(itemUrl.toString());
    if (!oldEtag.isEmpty()) {
        headers.append({QByteArrayLiteral("If-Match"), oldEtag.toUtf8()});
    }

    const DavResponse resp = davSyncRequest(nam(), itemUrl, QByteArrayLiteral("DELETE"),
                                            m_username, m_password, {}, headers);
    if (resp.status != 200 && resp.status != 204) {
        qWarning() << "RemoteCalendarBackend::deleteRecord: Failed, HTTP status:" << resp.status
                   << "error:" << resp.errorString;
        return false;
    }

    noteItemErased(normalizeUrlKey(itemUrl.toString()));
    clearCtag(*calId);
    return true;
}

// --- Change detection -------------------------------------------------------

QList<BackendRecord> RemoteCalendarBackend::modifiedSince(const QString &collectionId,
                                                   const QDateTime &since)
{
    // E5.2: this method has no production or test caller (the engine drives
    // change detection through collectionRevisions()/loadRecords(), not
    // modifiedSince()). Its former CTag short-circuit relied on the
    // synchronous nested-loop fetchFreshCtag() deleted in E5.2; rather than
    // re-introduce a backend-thread nested loop on a dead path, it now always
    // does a full load + filter (loadRecords already carries its own
    // CTag/cache optimization internally). Correct, just without the extra
    // early-out that nothing exercised.
    QList<BackendRecord> all = loadRecords(collectionId);
    QList<BackendRecord> result;
    for (const auto &rec : all) {
        if (!since.isValid() || rec.lastModified > since) {
            result.append(rec);
        }
    }
    return result;
}

QStringList RemoteCalendarBackend::deletedSince(const QString &collectionId,
                                         const QDateTime &since)
{
    // CalDAV has deletion tombstones but they require a sync-collection report
    // (RFC 6578) which KDAV doesn't expose in the current API surface.
    // Phase E revisits; Phase D returns empty (no deletion tracking).
    Q_UNUSED(collectionId)
    Q_UNUSED(since)
    return {};
}

} // namespace Kalburator::Sync
