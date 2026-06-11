#include "remotecalendarbackend.h"
#include "caldavcontentcache.h"
#include "icalcodec.h"
#include "syncoperation.h"
#include "backendcapabilities.h"
#include "logicalcalendar.h"
#include "discoveredcalendar.h"
#include "backendrecord.h"
#include "collectioninfo.h"

#include <KDAV/DavCollectionsFetchJob>
#include <KDAV/DavItemsListJob>
#include <KDAV/DavItemsFetchJob>
#include <KDAV/DavItemCreateJob>
#include <KDAV/DavItemModifyJob>
#include <KDAV/DavItemDeleteJob>
#include <KCalendarCore/ICalFormat>
#include <KIO/Job>

#include <QPointer>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlQuery>
#include <QSqlError>
#include <QXmlStreamReader>
#include <QCryptographicHash>

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
        : m_backendId(backendId)
        , m_connectionName(QStringLiteral("CTagStore_%1_%2")
                               .arg(backendId)
                               .arg(reinterpret_cast<quintptr>(this)))
    {
        if (dbPath.isEmpty()) {
            qWarning() << "CTagStore: empty dbPath for backend" << backendId;
            return;
        }
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            qWarning() << "CTagStore: failed to open" << dbPath
                       << ":" << db.lastError().text();
            QSqlDatabase::removeDatabase(m_connectionName);
            m_connectionName.clear();
            return;
        }
        ensureSchema();
    }

    ~CTagStore()
    {
        if (!m_connectionName.isEmpty()) {
            if (QSqlDatabase::contains(m_connectionName)) {
                QSqlDatabase::database(m_connectionName).close();
                QSqlDatabase::removeDatabase(m_connectionName);
            }
        }
    }

    bool isValid() const { return !m_connectionName.isEmpty(); }

    QString get(const QString &calendarId) const
    {
        if (!isValid()) return QString();
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

    bool set(const QString &calendarId, const QString &ctag)
    {
        if (!isValid()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO remote_ctags "
            "(backend_id, calendar_id, ctag) VALUES (?, ?, ?)"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        q.addBindValue(ctag);
        if (!q.exec()) {
            qWarning() << "CTagStore::set failed:" << q.lastError().text();
            return false;
        }
        return true;
    }

    bool clear(const QString &calendarId)
    {
        if (!isValid()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "DELETE FROM remote_ctags "
            "WHERE backend_id = ? AND calendar_id = ?"));
        q.addBindValue(m_backendId);
        q.addBindValue(calendarId);
        return q.exec();
    }

private:
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
        if (!ok)
            qWarning() << "CTagStore::ensureSchema failed:" << q.lastError().text();
        return ok;
    }

    QString m_backendId;
    QString m_connectionName;
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

DavResponse davSyncRequest(const QUrl &url, const QByteArray &verb,
                           const QString &username, const QString &password,
                           const QByteArray &body = {},
                           const QList<std::pair<QByteArray, QByteArray>> &rawHeaders = {},
                           const QByteArray &contentType =
                               QByteArrayLiteral("application/xml; charset=utf-8"))
{
    QUrl cleanUrl = url;
    cleanUrl.setUserInfo(QString());

    QNetworkRequest request(cleanUrl);
    if (!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QString::fromLatin1(contentType));
    }
    const QString credentials = username + QLatin1Char(':') + password;
    request.setRawHeader("Authorization",
                         "Basic " + credentials.toUtf8().toBase64());
    for (const auto &h : rawHeaders) {
        request.setRawHeader(h.first, h.second);
    }

    QNetworkAccessManager nam;
    QEventLoop loop;
    DavResponse resp;

    QNetworkReply *reply = nam.sendCustomRequest(request, verb, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
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
        reply->deleteLater();
        loop.quit();
    });
    loop.exec();
    return resp;
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

// Block on a SyncOperation's finished signal (the worker-thread blob-view
// adapters). Returns true iff the operation Succeeded.
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
            }
        }

        // Signal that loadCalendars has finished
        emit loadCalendarsFinished(collectionId, true);
    });

    fetchJob->start();
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
    // Reports only what discovery / registration actually recorded — NOT the
    // derive-on-miss fallback davUrlFor() applies so operations can proceed
    // before discovery completes. Callers (and tests) use this as an
    // "is this calendar registered?" predicate, e.g. after deleteCalendar
    // unregisters a URL.
    const auto it = m_calendars.constFind(calendarId);
    if (it != m_calendars.constEnd() && !it->davUrl.url().isEmpty()) {
        return it->davUrl.url().toString();
    }
    return QString();
}

QColor RemoteCalendarBackend::discoveredColor(const QString &calendarId) const
{
    return m_calendars.value(calendarId).color;
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
            it.key(), QByteArrayLiteral("PROPFIND"), m_username, m_password,
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

QString RemoteCalendarBackend::fetchFreshCtag(const QString &calendarId)
{
    const auto davUrl = davUrlFor(calendarId);
    if (!davUrl) return QString();

    const DavResponse resp = davSyncRequest(
        davUrl->url(), QByteArrayLiteral("PROPFIND"),
        m_username, m_password, kCtagPropfindBody,
        {{QByteArrayLiteral("Depth"), QByteArrayLiteral("0")}});
    if (!resp.transportOk()) return QString();

    const QMap<QString, QString> ctags = parseCtagMultistatus(resp.body);
    return ctags.isEmpty() ? QString() : ctags.first();
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

QString RemoteCalendarBackend::cachedCollectionRevision(const QString &collectionId) const
{
    return ctag(collectionId);
}

void RemoteCalendarBackend::primeRevisionCache(const QMap<QString, QString> &cache)
{
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
        setCtag(it.key(), it.value());
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
    const CalendarFacts facts = m_calendars.value(calendarId);
    if (!facts.hasContentTypes) {
        return true;  // Default to true if not discovered
    }
    return (facts.contentTypes & KDAV::DavCollection::Events)
        || (facts.contentTypes & KDAV::DavCollection::Calendar);
}

bool RemoteCalendarBackend::discoveredSupportsTodos(const QString &calendarId) const
{
    const CalendarFacts facts = m_calendars.value(calendarId);
    if (!facts.hasContentTypes) {
        return true;  // Default to true if not discovered
    }
    return (facts.contentTypes & KDAV::DavCollection::Todos)
        || (facts.contentTypes & KDAV::DavCollection::Calendar);
}

CalendarType RemoteCalendarBackend::discoveredCalendarType(const QString &calendarId) const
{
    const CalendarFacts facts = m_calendars.value(calendarId);
    if (!facts.hasContentTypes) {
        return CalendarType::Hybrid;  // Default if not discovered
    }

    const auto types = facts.contentTypes;
    bool supportsEvents = (types & KDAV::DavCollection::Events) || (types & KDAV::DavCollection::Calendar);
    bool supportsTodos = (types & KDAV::DavCollection::Todos) || (types & KDAV::DavCollection::Calendar);

    if (supportsEvents && !supportsTodos) {
        return CalendarType::Event;
    } else if (supportsTodos && !supportsEvents) {
        return CalendarType::Todo;
    } else {
        return CalendarType::Hybrid;
    }
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

    deleteJob->start();
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

        createJob->start();
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

        deleteJob->start();
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

    modifyJob->start();
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

bool RemoteCalendarBackend::createCalendar(const QString &collectionId, const QString &calendarId,
                                    const QString &name, CalendarType type)
{
    const QUrl calendarUrl = calendarUrlForCrud(calendarId);

    qDebug() << "RemoteCalendarBackend::createCalendar: Creating calendar at" << safeUrlString(calendarUrl)
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

    const DavResponse resp = davSyncRequest(calendarUrl,
                                            QByteArrayLiteral("MKCALENDAR"),
                                            m_username, m_password,
                                            requestBody.toUtf8());
    qDebug() << "RemoteCalendarBackend::createCalendar: HTTP status" << resp.status;

    // Register the calendar URL helper (used on success)
    auto registerCalendar = [this, calendarId, calendarUrl, type]() {
        QUrl davCalendarUrl = calendarUrl;
        davCalendarUrl.setUserName(m_username);
        davCalendarUrl.setPassword(m_password);
        CalendarFacts &facts = m_calendars[calendarId];
        facts.davUrl = configuredDavUrl(davCalendarUrl.toString());

        // Store the calendar type so discoveredCalendarType() returns correct value
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
        qDebug() << "RemoteCalendarBackend::createCalendar: Calendar created successfully:" << calendarId;
        registerCalendar();
        emit calendarCreated(collectionId, calendarId);
        emit calendarDiscovered(collectionId, calendarId);
        return true;
    }
    if (resp.status == 405 || resp.status == 409) {
        // Idempotent: 405 Method Not Allowed or 409 Conflict means calendar already exists
        qDebug() << "RemoteCalendarBackend::createCalendar: Calendar already exists:" << calendarId << "(HTTP" << resp.status << ")";
        registerCalendar();
        return true;
    }
    const QString errorMessage = !resp.transportOk()
        ? resp.errorString
        : QStringLiteral("Unexpected HTTP status: %1").arg(resp.status);
    qWarning() << "RemoteCalendarBackend::createCalendar: Failed:" << errorMessage
               << "HTTP status:" << resp.status;
    emit calendarOperationError(calendarId, errorMessage);
    return false;
}

bool RemoteCalendarBackend::updateCalendar(const QString &collectionId, const QString &calendarId,
                                    const QVariantMap &properties)
{
    const QUrl calendarUrl = calendarUrlForCrud(calendarId);

    qDebug() << "RemoteCalendarBackend::updateCalendar: Updating calendar at" << safeUrlString(calendarUrl);

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
        qDebug() << "RemoteCalendarBackend::updateCalendar: No supported properties to update";
        return true;
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

    const DavResponse resp = davSyncRequest(calendarUrl,
                                            QByteArrayLiteral("PROPPATCH"),
                                            m_username, m_password,
                                            requestBody.toUtf8());
    qDebug() << "RemoteCalendarBackend::updateCalendar: HTTP status" << resp.status;

    // 207 Multi-Status is the expected response for PROPPATCH
    if (resp.status == 207 || resp.status == 200 || resp.status == 204) {
        qDebug() << "RemoteCalendarBackend::updateCalendar: Calendar updated successfully:" << calendarId;

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
        return true;
    }
    const QString errorMessage = !resp.transportOk()
        ? resp.errorString
        : QStringLiteral("Unexpected HTTP status: %1").arg(resp.status);
    qWarning() << "RemoteCalendarBackend::updateCalendar: Failed:" << errorMessage
               << "HTTP status:" << resp.status;
    emit calendarOperationError(calendarId, errorMessage);
    return false;
}

bool RemoteCalendarBackend::deleteCalendar(const QString &collectionId, const QString &calendarId)
{
    QUrl calendarUrl;

    // Try to use the stored URL from discovery (has correct path case)
    if (const auto davUrl = davUrlFor(calendarId)) {
        calendarUrl = davUrl->url();
        calendarUrl.setUserName(QString());
        calendarUrl.setPassword(QString());
        qDebug() << "RemoteCalendarBackend::deleteCalendar: Using discovered URL for" << calendarId;
    } else {
        calendarUrl = calendarUrlForCrud(calendarId);
        qDebug() << "RemoteCalendarBackend::deleteCalendar: Using constructed URL for" << calendarId;
    }

    qDebug() << "RemoteCalendarBackend::deleteCalendar: Deleting calendar at" << safeUrlString(calendarUrl);

    const DavResponse resp = davSyncRequest(calendarUrl,
                                            QByteArrayLiteral("DELETE"),
                                            m_username, m_password);
    qDebug() << "RemoteCalendarBackend::deleteCalendar: HTTP status" << resp.status;

    // 200 OK or 204 No Content are both valid DELETE responses
    if (resp.status == 200 || resp.status == 204) {
        qDebug() << "RemoteCalendarBackend::deleteCalendar: Calendar deleted successfully:" << calendarId;
        m_calendars[calendarId].davUrl = KDAV::DavUrl();  // keep other facts (old per-map remove)
        emit calendarDeleted(collectionId, calendarId);
        return true;
    }
    if (resp.status == 404) {
        // Calendar doesn't exist - return false to indicate it wasn't deleted
        qDebug() << "RemoteCalendarBackend::deleteCalendar: Calendar not found:" << calendarId;
        m_calendars[calendarId].davUrl = KDAV::DavUrl();  // keep other facts (old per-map remove)
        return false;
    }
    QString errorMessage = resp.errorString;
    if (errorMessage.isEmpty()) {
        errorMessage = QStringLiteral("HTTP status: %1").arg(resp.status);
    }
    qWarning() << "RemoteCalendarBackend::deleteCalendar: Failed:" << errorMessage;
    emit calendarOperationError(calendarId, errorMessage);
    return false;
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
            cachedIncidences.append(incidence);
            emit itemFetched(calendarId, incidence);
        }
    }
    return cachedIncidences;
}

FetchOperation* RemoteCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = onOwnerThread(new FetchOperation(calendarId), this);
    registerOperation(op);

    if (!davUrlFor(calendarId)) {
        qWarning() << "RemoteCalendarBackend::fetchItems: No DAV URL for calendar:" << calendarId;
        // Use QTimer to defer the failure so caller can connect to signals
        QTimer::singleShot(0, op, [op, calendarId, this]() {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
            emit fetchFinished(calendarId, false, QStringLiteral("No DAV URL registered"));
        });
        return op;
    }

    // Initialize content cache on first fetch (lazy initialization)
    m_contentCache->ensureOpen();

    KDAV::DavUrl davUrl = *davUrlFor(calendarId);

    // Start the operation
    QMetaObject::invokeMethod(this, [this, op, davUrl, calendarId]() {
        // Mark operation as running
        op->setState(SyncOperation::Running);

        // CTag optimization: raw PROPFIND for CS:getctag on the calendar URL.
        // KDAV's DavCollectionsFetchJob doesn't return CTag for individual calendar
        // URLs, so we do a lightweight Depth:0 PROPFIND ourselves.
        if (davUrlFor(calendarId)) {
            QString storedCtag = ctag(calendarId);
            QString freshCtag;

            if (!storedCtag.isEmpty()) {
                freshCtag = fetchFreshCtag(calendarId);
            }

            if (!storedCtag.isEmpty() && !freshCtag.isEmpty() && freshCtag == storedCtag) {
                qDebug() << "RemoteCalendarBackend::fetchItems: CTag unchanged for" << calendarId
                         << "(" << freshCtag << ") - serving from cache";

                auto cachedIncidences = serveCachedItems(calendarId, davUrl);

                emit fetchStarted(calendarId, cachedIncidences.size());
                qDebug() << "RemoteCalendarBackend::fetchItems: Served" << cachedIncidences.size()
                         << "incidences from cache (CTag match) for" << calendarId;

                op->setFetchedItems(cachedIncidences);
                op->complete();
                emit fetchFinished(calendarId, true);
                return;
            }

            // CTag changed or unavailable — update in-memory cache for storage after full fetch
            if (!freshCtag.isEmpty()) {
                m_calendars[calendarId].pendingCtag = freshCtag;
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
                op->setFetchedItems({});
                op->complete();
                emit fetchFinished(calendarId, true);
                return;
            }

            // Emit fetchStarted with total items (cached + to-fetch)
            emit fetchStarted(calendarId, allItems.size());

            // If no items to fetch, serve everything from cache
            if (urlsToFetch.isEmpty()) {
                QList<KCalendarCore::Incidence::Ptr> fetchedIncidences;
                int currentItem = 0;

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
                        currentItem++;
                        emit fetchProgressChanged(calendarId, currentItem, allItems.size());
                        continue;
                    }

                    const auto incidences = incidencesFromIcal(cachedIcal);
                    if (incidences.isEmpty()) {
                        qWarning() << "RemoteCalendarBackend::fetchItems: Could not parse cached iCal for:" << urlKey;
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
                        fetchedIncidences.append(incidence);
                        emit itemFetched(calendarId, incidence);
                    }

                    currentItem++;
                    emit fetchProgressChanged(calendarId, currentItem, allItems.size());
                }

                qDebug() << "RemoteCalendarBackend::fetchItems: Served" << fetchedIncidences.size()
                         << "incidences from cache for calendar" << calendarId;

                // Update stored CTag after successful full fetch
                const QString pendingCtag = m_calendars.value(calendarId).pendingCtag;
                if (!pendingCtag.isEmpty()) {
                    setCtag(calendarId, pendingCtag);
                }

                op->setFetchedItems(fetchedIncidences);
                op->complete();
                emit fetchFinished(calendarId, true);
                return;
            }

            // Fetch only the changed items from the server via MULTIGET
            KDAV::DavItemsFetchJob *fetchJob = new KDAV::DavItemsFetchJob(davUrl, urlsToFetch, this);

            connect(fetchJob, &KDAV::DavItemsFetchJob::result, this,
                    [this, op, calendarId, fetchJob, allItems, serverEtags, urlsToFetch](KJob *fj) {
                if (op->state() == SyncOperation::Cancelled) {
                    emit fetchFinished(calendarId, false, QStringLiteral("Cancelled"));
                    return;
                }

                if (fj->error()) {
                    QString errorMsg = QStringLiteral("Failed to fetch items: %1").arg(fj->errorString());
                    op->fail(errorMsg);
                    emit fetchFinished(calendarId, false, errorMsg);
                    return;
                }

                QList<KCalendarCore::Incidence::Ptr> fetchedIncidences;

                // Build a map of fetched items for quick lookup.
                // Use normalizeUrlKey (strips credentials) so the map key matches
                // regardless of whether the multiget response URL includes user-info
                // or not. Discovery URLs carry credentials (http://user@host/...);
                // multiget response URLs typically don't (http://host/...) — using
                // a raw toDisplayString() key here caused a systematic lookup miss
                // (FINDINGS 2026-05-09 "FakeCalDavServer multiget REPORT").
                QMap<QString, KDAV::DavItem> fetchedItemsMap;
                for (const auto &davItem : fetchJob->items()) {
                    fetchedItemsMap[normalizeUrlKey(davItem.url().url().toString())] = davItem;
                }

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
                        fetchedIncidences.append(incidence);
                        emit itemFetched(calendarId, incidence);
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

                // Update stored CTag after successful full fetch
                const QString pendingCtag = m_calendars.value(calendarId).pendingCtag;
                if (!pendingCtag.isEmpty()) {
                    setCtag(calendarId, pendingCtag);
                }

                op->setFetchedItems(fetchedIncidences);
                op->complete();
                emit fetchFinished(calendarId, true);
            });

            fetchJob->start();
        });

        listJob->start();
    }, Qt::QueuedConnection);

    return op;
}

PushOperation* RemoteCalendarBackend::pushItems(const QString &calendarId,
                                        const QList<KCalendarCore::Incidence::Ptr> &items)
{
    auto *op = onOwnerThread(new PushOperation(calendarId, items), this);
    registerOperation(op);

    if (items.isEmpty()) {
        QTimer::singleShot(0, op, [op]() {
            op->complete();
        });
        return op;
    }

    if (!davUrlFor(calendarId)) {
        QTimer::singleShot(0, op, [op, calendarId]() {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
        });
        return op;
    }

    KDAV::DavUrl davUrl = *davUrlFor(calendarId);

    // Use shared counter to track completion
    auto remaining = std::make_shared<int>(items.size());
    auto anyError = std::make_shared<bool>(false);

    QMetaObject::invokeMethod(this, [this, op, davUrl, items, remaining, anyError]() mutable {
        op->setState(SyncOperation::Running);

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

            createJob->start();
        }
    }, Qt::QueuedConnection);

    return op;
}


DeleteOperation* RemoteCalendarBackend::deleteItems(const QString &calendarId,
                                            const QStringList &uids)
{
    auto *op = onOwnerThread(new DeleteOperation(calendarId, uids), this);
    registerOperation(op);

    if (uids.isEmpty()) {
        QTimer::singleShot(0, op, [op]() {
            op->complete();
        });
        return op;
    }

    if (!davUrlFor(calendarId)) {
        QTimer::singleShot(0, op, [op, calendarId]() {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
        });
        return op;
    }

    KDAV::DavUrl davUrl = *davUrlFor(calendarId);

    auto remaining = std::make_shared<int>(uids.size());
    auto anyError = std::make_shared<bool>(false);

    QMetaObject::invokeMethod(this, [this, op, davUrl, uids, remaining, anyError]() {
        op->setState(SyncOperation::Running);

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
                    [this, op, uid, itemUrl, remaining, anyError](KJob *job) {
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

                (*remaining)--;
                if (*remaining == 0) {
                    // Invalidate stored CTag — the server's CTag changed due to our push
                    if (!op->succeededUids().isEmpty()) {
                        clearCtag(op->calendarId());
                    }

                    if (*anyError || !op->failedUids().isEmpty()) {
                        if (op->succeededUids().isEmpty()) {
                            op->fail(QStringLiteral("All items failed to delete"));
                        } else {
                            op->complete();  // Partial success
                        }
                    } else {
                        op->complete();
                    }
                }
            });

            deleteJob->start();
        }
    }, Qt::QueuedConnection);

    return op;
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

    const DavResponse resp = davSyncRequest(itemUrl, QByteArrayLiteral("GET"),
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
        itemUrl, QByteArrayLiteral("PUT"), m_username, m_password,
        icsContent.toUtf8(), headers,
        QByteArrayLiteral("text/calendar; charset=utf-8"));

    // 200 OK, 201 Created, or 204 No Content are valid PUT responses
    if (resp.status != 200 && resp.status != 201 && resp.status != 204) {
        qWarning() << "RemoteCalendarBackend::setRawIcs: Failed, HTTP status:" << resp.status
                   << "error:" << resp.errorString;
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
// lastModified = current UTC time (ETag-opaque; CalDAV doesn't reliably
//                expose getlastmodified; Phase E can improve this)
// ============================================================================

namespace {

/// Build a BackendRecord from raw iCal bytes and a uid.
static Kalburator::Sync::BackendRecord blobRecordFromIcal(
    const QString &uid,
    const QByteArray &icalBytes)
{
    Kalburator::Sync::BackendRecord rec;
    rec.id          = uid;
    rec.type        = QStringLiteral("calendar");
    rec.data        = icalBytes;
    rec.contentHash = QString::fromLatin1(
        QCryptographicHash::hash(icalBytes, QCryptographicHash::Sha256).toHex());
    rec.lastModified = QDateTime::currentDateTimeUtc();
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
        result.append(blobRecordFromIcal(incidence->uid(), icalFromIncidence(incidence)));
    }

    op->deleteLater();
    return result;
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
    if (collectionId.isEmpty() || record.id.isEmpty() || record.data.isEmpty())
        return {};

    // Parse the iCal to get Incidence::Ptrs for pushItems.
    const auto incidences = incidencesFromIcal(record.data);
    if (incidences.isEmpty()) {
        qWarning() << "RemoteCalendarBackend::createRecord: no parseable incidences in iCal for uid" << record.id;
        return {};
    }

    PushOperation *op = pushItems(collectionId, incidences);
    if (!op) return {};

    const bool ok = awaitOperation(op) && op->failedUids().isEmpty();
    op->deleteLater();
    return ok ? record.id : QString{};
}

bool RemoteCalendarBackend::updateRecord(const BackendRecord &record)
{
    if (record.id.isEmpty() || record.data.isEmpty()) return false;

    // Find which calendar this uid lives in.
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        if (it->davUrl.url().isEmpty()) continue;
        QUrl itemUrl = generateItemUrl(it->davUrl, record.id);
        QString urlKey = normalizeUrlKey(itemUrl.toString());

        // Check if we have an ETag for this item (proxy for "this calendar owns it").
        if (!m_localEtags.contains(urlKey)) continue;

        return setRawIcs(it.key(), record.id, QString::fromUtf8(record.data));
    }

    // Fallback: try all registered calendars (first success wins).
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        if (it->davUrl.url().isEmpty()) continue;
        if (setRawIcs(it.key(), record.id, QString::fromUtf8(record.data)))
            return true;
    }
    qWarning() << "RemoteCalendarBackend::updateRecord: uid not found in any calendar:" << record.id;
    return false;
}

bool RemoteCalendarBackend::deleteRecord(const QString &recordId)
{
    if (recordId.isEmpty()) return false;

    // Try all registered calendars; deleteItems returns success if the uid exists.
    for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it) {
        if (it->davUrl.url().isEmpty()) continue;
        DeleteOperation *op = deleteItems(it.key(), QStringList{recordId});
        if (!op) continue;

        const bool ok = awaitOperation(op) && op->failedUids().isEmpty();
        op->deleteLater();
        if (ok) return true;
    }
    return false;
}

// --- Change detection -------------------------------------------------------

QList<BackendRecord> RemoteCalendarBackend::modifiedSince(const QString &collectionId,
                                                   const QDateTime &since)
{
    // CTag short-circuit: if the stored CTag matches what the server has,
    // nothing changed — return empty and the caller skips the full fetch.
    const QString storedCtag = ctag(collectionId);
    if (!storedCtag.isEmpty() && davUrlFor(collectionId)) {
        const QString freshCtag = fetchFreshCtag(collectionId);
        if (!freshCtag.isEmpty() && freshCtag == storedCtag) {
            return {};  // CTag unchanged — nothing modified
        }
    }

    // CTag changed or unavailable — full load and filter by since.
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
