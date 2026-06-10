#include "remotecalendarbackend.h"
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

#include <QAtomicInt>
#include <QPointer>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QCryptographicHash>

#include <KIO/Job>
#include <KIO/TransferJob>
#include <KIO/DeleteJob>
#include <KDAV/DavItemFetchJob>

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

    bool clearAll()
    {
        if (!isValid()) return false;
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "DELETE FROM remote_ctags WHERE backend_id = ?"));
        q.addBindValue(m_backendId);
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
    , m_cacheConnectionName(QStringLiteral("RemoteCalendarBackendCache_") + QUuid::createUuid().toString(QUuid::WithoutBraces))
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
    m_cacheDirOverride = dir;
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

// ============================================================================
// Content Cache for Delta Sync
// ============================================================================

namespace {
// Process-stable 64-bit hash (FNV-1a) over a string's UTF-8 bytes. Unlike
// qHash(QString), this does NOT incorporate the per-process random QHashSeed,
// so the derived cache filename is identical across launches for the same
// account — the property the content cache requires to persist.
quint64 stableContentCacheHash(const QString &s)
{
    const QByteArray bytes = s.toUtf8();
    quint64 hash = 1469598103934665603ULL;          // FNV offset basis
    for (const char ch : bytes) {
        hash ^= static_cast<quint64>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ULL;                    // FNV prime
    }
    return hash;
}
} // namespace

void RemoteCalendarBackend::initContentCache()
{
    if (m_cacheInitialized) {
        return;
    }

    // Cache directory: caller-chosen (per-collection profile folder) when set,
    // otherwise the app's shared cache location.
    QString cacheDir = m_cacheDirOverride.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        : m_cacheDirOverride;
    if (cacheDir.isEmpty()) {
        cacheDir = QDir::tempPath();
    }
    QDir().mkpath(cacheDir);

    // Create a host-specific cache file to avoid collisions between servers.
    // NOTE: must be a *stable* hash — qHash(QString) mixes in a per-process
    // random seed (QHashSeed), so it yields a different filename every launch,
    // orphaning the previous run's cache (libkalburator content-cache handoff,
    // 2026-05-27). Use a fixed FNV-1a over the UTF-8 bytes instead.
    QString hostHash = QString::number(stableContentCacheHash(m_url.host() + m_url.path()));
    QString dbPath = cacheDir + QStringLiteral("/caldav-cache-%1.db").arg(hostHash);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_cacheConnectionName);
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qWarning() << "RemoteCalendarBackend: Failed to open content cache database:" << db.lastError().text();
        return;
    }

    // Create the cache table
    QSqlQuery query(db);
    bool success = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cached_items ("
        "  url TEXT PRIMARY KEY,"
        "  etag TEXT NOT NULL,"
        "  ical_content TEXT NOT NULL,"
        "  fetched_at INTEGER NOT NULL"
        ")"));

    if (!success) {
        qWarning() << "RemoteCalendarBackend: Failed to create cache table:" << query.lastError().text();
        return;
    }

    // Create index for faster lookups
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_cached_items_etag ON cached_items(etag)"));

    m_cacheInitialized = true;
    qDebug() << "RemoteCalendarBackend: Content cache initialized at" << dbPath;
}

QString RemoteCalendarBackend::getCachedContent(const QString &itemUrl, const QString &expectedEtag) const
{
    if (!m_cacheInitialized || expectedEtag.isEmpty()) {
        return QString();
    }

    QSqlDatabase db = QSqlDatabase::database(m_cacheConnectionName);
    if (!db.isOpen()) {
        return QString();
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT ical_content FROM cached_items WHERE url = ? AND etag = ?"));
    query.addBindValue(itemUrl);
    query.addBindValue(expectedEtag);

    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }

    return QString();
}

void RemoteCalendarBackend::setCachedContent(const QString &itemUrl, const QString &etag, const QString &icalContent)
{
    if (!m_cacheInitialized || itemUrl.isEmpty() || etag.isEmpty()) {
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(m_cacheConnectionName);
    if (!db.isOpen()) {
        return;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO cached_items (url, etag, ical_content, fetched_at) "
        "VALUES (?, ?, ?, ?)"));
    query.addBindValue(itemUrl);
    query.addBindValue(etag);
    query.addBindValue(icalContent);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "RemoteCalendarBackend: Failed to cache content:" << query.lastError().text();
    }
}

void RemoteCalendarBackend::removeCachedContent(const QString &itemUrl)
{
    if (!m_cacheInitialized) {
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(m_cacheConnectionName);
    if (!db.isOpen()) {
        return;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM cached_items WHERE url = ?"));
    query.addBindValue(itemUrl);

    if (!query.exec()) {
        qWarning() << "RemoteCalendarBackend: Failed to remove cached content:" << query.lastError().text();
    }
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

                m_davUrls[calId] = configuredUrl;

                // Store discovered color (from apple:calendar-color property)
                QColor calColor = col.color();
                if (calColor.isValid()) {
                    m_calendarColors[calId] = calColor;
                    qDebug() << "RemoteCalendarBackend: discovered calendar:" << calId
                             << "with color:" << calColor.name();
                } else {
                    qDebug() << "RemoteCalendarBackend: discovered calendar:" << calId
                             << "(no color set)";
                }

                // Store discovered content types (VEVENT, VTODO support)
                m_calendarContentTypes[calId] = col.contentTypes();

                // Cache CTag for sync optimization
                QString ctag = col.CTag();
                if (!ctag.isEmpty()) {
                    m_calendarCtags[calId] = ctag;
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
    m_davUrls[calendarId] = configuredUrl;

    qDebug() << "RemoteCalendarBackend::registerCalendarUrl: Registered calendar" << calendarId
             << "with URL:" << configuredUrl.url().toString(QUrl::RemovePassword);
}

QString RemoteCalendarBackend::discoveredUrl(const QString &calendarId) const
{
    if (m_davUrls.contains(calendarId)) {
        return m_davUrls[calendarId].url().toString();
    }
    return QString();
}

QColor RemoteCalendarBackend::discoveredColor(const QString &calendarId) const
{
    return m_calendarColors.value(calendarId);
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
        if (!c.davUrl.isEmpty()) {
            m_davUrls[c.calendarId] = configuredDavUrl(c.davUrl);
        }
        if (c.color.isValid()) {
            m_calendarColors[c.calendarId] = c.color;
        }
        m_calendarContentTypes[c.calendarId] = c.contentTypes;
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
        if (!m_davUrls.contains(calId)) continue;
        const QUrl url = m_davUrls.value(calId).url();
        QUrl parent = parentUrl(url);
        parent.setUserName(QString());
        parent.setPassword(QString());
        groups[parent].append(calId);
        hrefByCalId[calId] = url.path();
    }

    if (groups.isEmpty()) return result;

    const QByteArray body =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:propfind xmlns:D=\"DAV:\" xmlns:CS=\"http://calendarserver.org/ns/\">"
        "  <D:prop><CS:getctag/></D:prop>"
        "</D:propfind>";

    const QString credentials = m_username + QLatin1Char(':') + m_password;
    const QByteArray authHeader = "Basic " + credentials.toUtf8().toBase64();

    QNetworkAccessManager nam;

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        QNetworkRequest request(it.key());
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/xml; charset=utf-8"));
        request.setRawHeader("Depth", "1");
        request.setRawHeader("Authorization", authHeader);

        QEventLoop loop;
        QByteArray responseData;
        bool ok = false;

        QNetworkReply *reply = nam.sendCustomRequest(request, "PROPFIND", body);
        connect(reply, &QNetworkReply::finished, &loop,
                [reply, &loop, &responseData, &ok]() {
            if (reply->error() == QNetworkReply::NoError) {
                responseData = reply->readAll();
                ok = true;
            } else {
                qWarning() << "RemoteCalendarBackend::fetchAllCtags: PROPFIND failed for"
                           << reply->url() << ":" << reply->errorString();
            }
            reply->deleteLater();
            loop.quit();
        });
        loop.exec();

        if (!ok) continue;

        // Parse multistatus: each <D:response> has a <D:href> and a <CS:getctag>.
        // QXmlStreamReader returns the local name without prefix, so comparisons
        // against "response", "href", and "getctag" are correct regardless of
        // namespace prefix used by the server.
        QXmlStreamReader xml(responseData);
        QString currentHref;
        QString currentCtag;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == u"response") {
                    currentHref.clear();
                    currentCtag.clear();
                } else if (xml.name() == u"href") {
                    currentHref = xml.readElementText();
                } else if (xml.name() == u"getctag") {
                    currentCtag = xml.readElementText();
                }
            } else if (xml.isEndElement() && xml.name() == u"response") {
                if (!currentHref.isEmpty() && !currentCtag.isEmpty()) {
                    // Match href back to calId by path comparison.
                    // TODO: if the server returns URL-encoded hrefs for calendars
                    // with non-ASCII characters, this comparison may fail; normalize
                    // both sides with QUrl::fromPercentEncoding if that becomes a concern.
                    for (const QString &calId : it.value()) {
                        if (hrefByCalId.value(calId) == currentHref) {
                            result[calId] = currentCtag;
                            break;
                        }
                    }
                }
            }
        }
        if (xml.hasError()) {
            qWarning() << "RemoteCalendarBackend::fetchAllCtags: XML parse error for"
                       << it.key() << ":" << xml.errorString();
        }
    }

    qDebug() << "RemoteCalendarBackend::fetchAllCtags: requested" << calendarIds.size()
             << "calendars across" << groups.size() << "parent URLs, got"
             << result.size() << "ctags";
    return result;
}

QColor RemoteCalendarBackend::calendarColor(const QString &calendarId) const
{
    // Return from cache (populated during discovery or after updateCalendar)
    return m_calendarColors.value(calendarId);
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
    if (!m_calendarContentTypes.contains(calendarId)) {
        return true;  // Default to true if not discovered
    }
    auto types = m_calendarContentTypes.value(calendarId);
    return (types & KDAV::DavCollection::Events) || (types & KDAV::DavCollection::Calendar);
}

bool RemoteCalendarBackend::discoveredSupportsTodos(const QString &calendarId) const
{
    if (!m_calendarContentTypes.contains(calendarId)) {
        return true;  // Default to true if not discovered
    }
    auto types = m_calendarContentTypes.value(calendarId);
    return (types & KDAV::DavCollection::Todos) || (types & KDAV::DavCollection::Calendar);
}

CalendarType RemoteCalendarBackend::discoveredCalendarType(const QString &calendarId) const
{
    if (!m_calendarContentTypes.contains(calendarId)) {
        return CalendarType::Hybrid;  // Default if not discovered
    }

    auto types = m_calendarContentTypes.value(calendarId);
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

void RemoteCalendarBackend::removeItem(const QString &calId, const QString &itemUid)
{
    if (!m_davUrls.contains(calId)) {
        qWarning() << "RemoteCalendarBackend::removeItem: Unknown calendar DAV URL for" << calId;
        return;
    }
    if (itemUid.isEmpty()) {
        qWarning() << "RemoteCalendarBackend::removeItem: Empty item UID";
        return;
    }

    KDAV::DavUrl davUrl = m_davUrls.value(calId);

    QUrl itemUrl = generateItemUrl(davUrl, itemUid);
    KDAV::DavUrl itemDavUrl(itemUrl, davUrl.protocol());

    QString oldEtag = cachedEtag(itemUrl.toString());

    KDAV::DavItem davItem;
    davItem.setUrl(itemDavUrl);
    davItem.setContentType(QStringLiteral("text/calendar"));
    davItem.setData(QByteArray());
    davItem.setEtag(oldEtag);

    auto *deleteJob = new KDAV::DavItemDeleteJob(davItem, this);

    connect(deleteJob, &KDAV::DavItemDeleteJob::result, this, [this, deleteJob, calId, itemUid, itemUrl](KJob *job) {
        if (job->error()) {
            qWarning() << "RemoteCalendarBackend::removeItem: Failed to delete item:" << job->errorString();
            return;
        }

        QString urlKey = normalizeUrlKey(itemUrl.toString());
        if (m_etagCache) {
            m_etagCache->removeEtag(urlKey);
        }
        m_localEtags.remove(urlKey);

        qDebug() << "RemoteCalendarBackend::removeItem: Deleted incidence UID:" << itemUid << "from calendar" << calId;

        emit itemRemoved(calId, itemUid);
    });

    deleteJob->start();
}


// ... other includes if needed...

KDAV::DavUrl RemoteCalendarBackend::configuredDavUrl(const QString &rawUrl)
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

    if (!m_davUrls.contains(calId)) {
        qWarning() << "RemoteCalendarBackend::startSync: No DAV URL for calendar" << calId;
        emit syncCompleted(collectionId);
        return;
    }

    const QList<KCalendarCore::Incidence::Ptr> &finalCreations = stagedCreations;
    const QList<KCalendarCore::Incidence::Ptr> &finalUpdates = stagedUpdates;

    const KDAV::DavUrl baseDavUrl = m_davUrls.value(calId);

    // Count total jobs for completion tracking
    const int totalJobs = finalCreations.size() + finalUpdates.size() + stagedDeletions.size();
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

    // IMPORTANT: Create ICalFormat inside the lambda to avoid use-after-free.
    // This lambda is called from async job callbacks that run after startSync returns,
    // so any captured-by-reference local variables would be dangling references.
    auto serializeIncidence = [](const KCalendarCore::Incidence::Ptr &inc) -> QByteArray {
        KCalendarCore::ICalFormat icalFormat;
        auto tmpCalRaw = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        tmpCalRaw->addIncidence(inc);
        QSharedPointer<KCalendarCore::Calendar> tmpCal(tmpCalRaw, [](KCalendarCore::Calendar*){});
        QString icalData = icalFormat.toString(tmpCal);
        delete tmpCalRaw;
        return icalData.toUtf8();
    };

    // Helper to force-update an incidence using If-Match: * (bypasses ETag check)
    // Used when user explicitly resolved a conflict and chose to overwrite server version.
    // Per RFC 7232, If-Match: * succeeds if the resource exists, regardless of its current ETag.
    auto forceUpdateIncidence = [this, &serializeIncidence, &checkDone, calId, calendar](KCalendarCore::Incidence::Ptr inc) {
        QUrl itemUrl = generateItemUrl(m_davUrls[calId], inc->uid());

        KDAV::DavItem davItem;
        davItem.setUrl(KDAV::DavUrl(itemUrl, m_davUrls[calId].protocol()));
        davItem.setContentType(QStringLiteral("text/calendar"));
        davItem.setData(serializeIncidence(inc));
        davItem.setEtag(QStringLiteral("*"));  // If-Match: * - force update if resource exists

        auto *modifyJob = new KDAV::DavItemModifyJob(davItem, this);
        modifyJob->setProperty("incidenceUid", inc->uid());
        modifyJob->setProperty("calendarId", calId);

        connect(modifyJob, &KDAV::DavItemModifyJob::result, this,
                [this, modifyJob, inc, calendar, checkDone, serializeIncidence](KJob *job) {
                    if (job->error()) {
                        qWarning() << "Force update job failed for" << inc->uid() << ":" << job->errorString();
                        checkDone();
                        return;
                    }
                    auto updatedItem = modifyJob->item();
                    QString url = normalizeUrlKey(updatedItem.url().url().toString());

                    if (m_etagCache)
                        m_etagCache->setEtag(url, updatedItem.etag());
                    m_localEtags[url] = updatedItem.etag();

                    // Update content cache
                    if (!updatedItem.etag().isEmpty()) {
                        QString icalData = QString::fromUtf8(serializeIncidence(inc));
                        setCachedContent(url, updatedItem.etag(), icalData);
                    }

                    qDebug() << "Force update succeeded for" << inc->uid() << "new ETag:" << updatedItem.etag();
                    emit itemLoaded(calendar, inc, updatedItem.etag());
                    checkDone();
                });

        modifyJob->start();
    };

    // Helper to start update job after failed create (412)
    auto startUpdateJobForIncidence = [this, &serializeIncidence, &checkDone, calId, calendar](KCalendarCore::Incidence::Ptr inc) {
        QUrl itemUrl = generateItemUrl(m_davUrls[calId], inc->uid());
        QString etag = m_localEtags.value(normalizeUrlKey(itemUrl.toString()));

        KDAV::DavItem davItem;
        davItem.setUrl(KDAV::DavUrl(itemUrl, m_davUrls[calId].protocol()));
        davItem.setContentType(QStringLiteral("text/calendar"));
        davItem.setData(serializeIncidence(inc));
        davItem.setEtag(etag);

        auto *modifyJob = new KDAV::DavItemModifyJob(davItem, this);
        modifyJob->setProperty("incidenceUid", inc->uid());
        modifyJob->setProperty("calendarId", calId);

        connect(modifyJob, &KDAV::DavItemModifyJob::result, this,
                [this, modifyJob, inc, calendar, checkDone, serializeIncidence](KJob *job) {
                    if (job->error()) {
                        qWarning() << "Update job (retry after 412) failed for" << inc->uid() << ":" << job->errorString();
                        checkDone();
                        return;
                    }
                    auto updatedItem = modifyJob->item();
                    QString url = normalizeUrlKey(updatedItem.url().url().toString());

                    if (m_etagCache)
                        m_etagCache->setEtag(url, updatedItem.etag());
                    m_localEtags[url] = updatedItem.etag();

                    // Update content cache
                    if (!updatedItem.etag().isEmpty()) {
                        QString icalData = QString::fromUtf8(serializeIncidence(inc));
                        setCachedContent(url, updatedItem.etag(), icalData);
                    }

                    emit itemLoaded(calendar, inc, updatedItem.etag());

                    checkDone();
                });

        modifyJob->start();
    };

    // Launch create jobs
    for (const auto &inc : finalCreations) {
        if (!inc) {
            checkDone();
            continue;
        }

        QUrl itemUrl = generateItemUrl(baseDavUrl, inc->uid());

        KDAV::DavItem davItem;
        davItem.setUrl(KDAV::DavUrl(itemUrl, baseDavUrl.protocol()));
        davItem.setContentType(QStringLiteral("text/calendar"));
        // Create PUT without ETag
        davItem.setData(serializeIncidence(inc));

        auto *createJob = new KDAV::DavItemCreateJob(davItem, this);
        createJob->setProperty("incidenceUid", inc->uid());
        createJob->setProperty("calendarId", calId);

        connect(createJob, &KDAV::DavItemCreateJob::result, this,
                [this, createJob, inc, calendar, checkDone, startUpdateJobForIncidence, serializeIncidence](KJob *job) {
            if (job->error()) {
                int httpStatus = getHttpStatusCode(job);
                if (httpStatus == 412) {
                    qDebug() << "Create job 412 Precondition Failed, switching to update for" << inc->uid();
                    startUpdateJobForIncidence(inc);
                    return;
                }
                qWarning() << "Create job failed for" << inc->uid() << ":" << job->errorString();
                checkDone();
                return;
            }

                    auto createdItem = createJob->item();
                    QString remoteUrl = normalizeUrlKey(createdItem.url().url().toString());

                    if (!createdItem.etag().isEmpty()) {
                        if (m_etagCache) m_etagCache->setEtag(remoteUrl, createdItem.etag());
                        m_localEtags[remoteUrl] = createdItem.etag();

                        // Update content cache with the data we just sent
                        QString icalData = QString::fromUtf8(serializeIncidence(inc));
                        setCachedContent(remoteUrl, createdItem.etag(), icalData);
                    }

                    emit itemLoaded(calendar, inc, createdItem.etag());

                    checkDone();
                });

        createJob->start();
    }

    // Launch update jobs
    for (const auto &inc : finalUpdates) {
        if (!inc) {
            checkDone();
            continue;
        }

        QUrl itemUrl = generateItemUrl(baseDavUrl, inc->uid());
        QString etag = m_localEtags.value(normalizeUrlKey(itemUrl.toString()));

        KDAV::DavItem davItem;
        davItem.setUrl(KDAV::DavUrl(itemUrl, baseDavUrl.protocol()));
        davItem.setContentType(QStringLiteral("text/calendar"));
        davItem.setData(serializeIncidence(inc));
        davItem.setEtag(etag);

        auto *modifyJob = new KDAV::DavItemModifyJob(davItem, this);
        modifyJob->setProperty("incidenceUid", inc->uid());
        modifyJob->setProperty("calendarId", calId);

        connect(modifyJob, &KDAV::DavItemModifyJob::result, this,
                [this, modifyJob, inc, calendar, checkDone, forceUpdateIncidence, serializeIncidence](KJob *job) {
                    if (job->error()) {
                        int httpStatus = getHttpStatusCode(job);
                        if (httpStatus == 412) {
                            // 412 Precondition Failed - ETag mismatch (server has newer version)
                            // This happens during sync when user resolved a conflict to push local.
                            // Retry with If-Match: * to force the update (user's explicit intent).
                            qDebug() << "Update job 412 Precondition Failed for" << inc->uid()
                                     << "- retrying with force update (If-Match: *)";
                            forceUpdateIncidence(inc);
                            return;  // forceUpdateIncidence will call checkDone when complete
                        }
                        qWarning() << "Update job failed for" << inc->uid() << ":" << job->errorString();
                        checkDone();
                        return;
                    }

                    auto updatedItem = modifyJob->item();
                    QString url = normalizeUrlKey(updatedItem.url().url().toString());

                    if (m_etagCache) m_etagCache->setEtag(url, updatedItem.etag());
                    m_localEtags[url] = updatedItem.etag();

                    // Update content cache with the data we just sent
                    if (!updatedItem.etag().isEmpty()) {
                        QString icalData = QString::fromUtf8(serializeIncidence(inc));
                        setCachedContent(url, updatedItem.etag(), icalData);
                    }

                    emit itemLoaded(calendar, inc, updatedItem.etag());

                    checkDone();
                });

        modifyJob->start();
    }

    // Launch delete jobs
    for (auto it = stagedDeletions.constBegin(); it != stagedDeletions.constEnd(); ++it) {
        const QString &uid = it.key();
        const QString &etag = it.value();

        QUrl itemUrl = generateItemUrl(baseDavUrl, uid);

        KDAV::DavItem davItem;
        davItem.setUrl(KDAV::DavUrl(itemUrl, baseDavUrl.protocol()));
        davItem.setContentType(QStringLiteral("text/calendar"));
        davItem.setData(QByteArray());
        davItem.setEtag(etag);

        auto *deleteJob = new KDAV::DavItemDeleteJob(davItem, this);
        deleteJob->setProperty("incidenceUid", uid);
        deleteJob->setProperty("calendarId", calId);

        connect(deleteJob, &KDAV::DavItemDeleteJob::result, this,
                [this, deleteJob, uid, itemUrl, checkDone](KJob *job) {
                    if (job->error()) {
                        qWarning() << "Delete job failed for" << uid << ":" << job->errorString();
                        checkDone();
                        return;
                    }

                    qDebug() << "Delete job succeeded for" << uid;

                    QString calendarId = deleteJob->property("calendarId").toString();
                    QString urlKey = normalizeUrlKey(itemUrl.toString());

                    m_localEtags.remove(urlKey);
                    if (m_etagCache) {
                        m_etagCache->removeEtag(urlKey);
                    }

                    // Remove from content cache
                    removeCachedContent(urlKey);

                    emit itemRemoved(calendarId, uid);

                    checkDone();
                });

        deleteJob->start();
    }
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
    // Build calendar URL: principal URL + calendar slug
    // For Radicale-style servers, the username must be in the path: /username/calendar/
    // The URL may have credentials in userinfo (user@host) that need to be moved to path
    QUrl calendarUrl = m_url;
    QString path = calendarUrl.path();

    // If path is empty or just "/", and we have a username, prepend username to path
    // This handles URLs like "http://user@host:port/" -> path becomes "/user/"
    if ((path.isEmpty() || path == QLatin1String("/")) && !m_username.isEmpty()) {
        path = QLatin1Char('/') + m_username + QLatin1Char('/');
    }

    // Remove credentials from URL - they'll be passed via Basic Auth header
    calendarUrl.setUserName(QString());
    calendarUrl.setPassword(QString());

    if (!path.endsWith('/')) {
        path += '/';
    }
    path += calendarId + '/';
    calendarUrl.setPath(path);

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

    // Use QNetworkAccessManager for custom HTTP method (MKCALENDAR)
    // KIO doesn't properly support custom HTTP methods like MKCALENDAR
    QNetworkAccessManager manager;
    QNetworkRequest request(calendarUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));

    // Set up Basic Authentication
    QString credentials = m_username + ':' + m_password;
    QByteArray authData = credentials.toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + authData);

    // Use sendCustomRequest for MKCALENDAR method
    QEventLoop loop;
    bool success = false;
    QString errorMessage;

    QNetworkReply *reply = manager.sendCustomRequest(request, "MKCALENDAR", requestBody.toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, &loop, &success, &errorMessage, calendarId, calendarUrl, collectionId, type]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "RemoteCalendarBackend::createCalendar: HTTP status" << statusCode;

        // Register the calendar URL helper (used on success)
        auto registerCalendar = [this, calendarId, calendarUrl, collectionId, type]() {
            QUrl davCalendarUrl = calendarUrl;
            davCalendarUrl.setUserName(m_username);
            davCalendarUrl.setPassword(m_password);
            KDAV::DavUrl davUrl = configuredDavUrl(davCalendarUrl.toString());
            m_davUrls[calendarId] = davUrl;

            // Store the calendar type so discoveredCalendarType() returns correct value
            KDAV::DavCollection::ContentTypes contentTypes;
            if (type == CalendarType::Event) {
                contentTypes = KDAV::DavCollection::Events;
            } else if (type == CalendarType::Todo) {
                contentTypes = KDAV::DavCollection::Todos;
            } else {
                contentTypes = KDAV::DavCollection::Events | KDAV::DavCollection::Todos;
            }
            m_calendarContentTypes[calendarId] = contentTypes;
        };

        if (statusCode == 201) {
            qDebug() << "RemoteCalendarBackend::createCalendar: Calendar created successfully:" << calendarId;
            registerCalendar();
            emit calendarCreated(collectionId, calendarId);
            emit calendarDiscovered(collectionId, calendarId);
            success = true;
        } else if (statusCode == 405 || statusCode == 409) {
            // Idempotent: 405 Method Not Allowed or 409 Conflict means calendar already exists
            qDebug() << "RemoteCalendarBackend::createCalendar: Calendar already exists:" << calendarId << "(HTTP" << statusCode << ")";
            registerCalendar();
            success = true;
        } else if (reply->error() != QNetworkReply::NoError) {
            errorMessage = reply->errorString();
            qWarning() << "RemoteCalendarBackend::createCalendar: Failed:" << errorMessage << "HTTP status:" << statusCode;
            emit calendarOperationError(calendarId, errorMessage);
            success = false;
        } else {
            errorMessage = QStringLiteral("Unexpected HTTP status: %1").arg(statusCode);
            qWarning() << "RemoteCalendarBackend::createCalendar: Failed:" << errorMessage;
            emit calendarOperationError(calendarId, errorMessage);
            success = false;
        }
        reply->deleteLater();
        loop.quit();
    });

    loop.exec();

    return success;
}

bool RemoteCalendarBackend::updateCalendar(const QString &collectionId, const QString &calendarId,
                                    const QVariantMap &properties)
{
    // Build calendar URL (similar to createCalendar)
    QUrl calendarUrl = m_url;
    QString path = calendarUrl.path();

    if ((path.isEmpty() || path == QLatin1String("/")) && !m_username.isEmpty()) {
        path = QLatin1Char('/') + m_username + QLatin1Char('/');
    }

    calendarUrl.setUserName(QString());
    calendarUrl.setPassword(QString());

    if (!path.endsWith('/')) {
        path += '/';
    }
    path += calendarId + '/';
    calendarUrl.setPath(path);

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

    // Use QNetworkAccessManager for PROPPATCH
    QNetworkAccessManager manager;
    QNetworkRequest request(calendarUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));

    QString credentials = m_username + ':' + m_password;
    QByteArray authData = credentials.toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + authData);

    QEventLoop loop;
    bool success = false;
    QString errorMessage;

    QNetworkReply *reply = manager.sendCustomRequest(request, "PROPPATCH", requestBody.toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, &loop, &success, &errorMessage, calendarId, collectionId, properties]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "RemoteCalendarBackend::updateCalendar: HTTP status" << statusCode;

        // 207 Multi-Status is the expected response for PROPPATCH
        if (statusCode == 207 || statusCode == 200 || statusCode == 204) {
            qDebug() << "RemoteCalendarBackend::updateCalendar: Calendar updated successfully:" << calendarId;

            // Update local cache to reflect the change
            if (properties.contains(QStringLiteral("color"))) {
                QColor color = properties.value(QStringLiteral("color")).value<QColor>();
                if (!color.isValid()) {
                    color = QColor(properties.value(QStringLiteral("color")).toString());
                }
                if (color.isValid()) {
                    m_calendarColors[calendarId] = color;
                }
            }

            emit calendarUpdated(collectionId, calendarId);
            success = true;
        } else if (reply->error() != QNetworkReply::NoError) {
            errorMessage = reply->errorString();
            qWarning() << "RemoteCalendarBackend::updateCalendar: Failed:" << errorMessage << "HTTP status:" << statusCode;
            emit calendarOperationError(calendarId, errorMessage);
            success = false;
        } else {
            errorMessage = QStringLiteral("Unexpected HTTP status: %1").arg(statusCode);
            qWarning() << "RemoteCalendarBackend::updateCalendar: Failed:" << errorMessage;
            emit calendarOperationError(calendarId, errorMessage);
            success = false;
        }
        reply->deleteLater();
        loop.quit();
    });

    loop.exec();

    return success;
}

bool RemoteCalendarBackend::deleteCalendar(const QString &collectionId, const QString &calendarId)
{
    QUrl calendarUrl;

    // Try to use the stored URL from discovery (has correct path case)
    if (m_davUrls.contains(calendarId)) {
        calendarUrl = m_davUrls[calendarId].url();
        calendarUrl.setUserName(QString());
        calendarUrl.setPassword(QString());
        qDebug() << "RemoteCalendarBackend::deleteCalendar: Using discovered URL for" << calendarId;
    } else {
        // Fallback: Build calendar URL from calendarId
        // For Radicale-style servers, the username must be in the path: /username/calendar/
        calendarUrl = m_url;
        QString path = calendarUrl.path();

        if ((path.isEmpty() || path == QLatin1String("/")) && !m_username.isEmpty()) {
            path = QLatin1Char('/') + m_username + QLatin1Char('/');
        }

        calendarUrl.setUserName(QString());
        calendarUrl.setPassword(QString());

        if (!path.endsWith('/')) {
            path += '/';
        }
        path += calendarId + '/';
        calendarUrl.setPath(path);
        qDebug() << "RemoteCalendarBackend::deleteCalendar: Using constructed URL for" << calendarId;
    }

    qDebug() << "RemoteCalendarBackend::deleteCalendar: Deleting calendar at" << safeUrlString(calendarUrl);

    // Use QNetworkAccessManager for DELETE (consistent with createCalendar)
    QNetworkAccessManager manager;
    QNetworkRequest request(calendarUrl);

    // Set up Basic Authentication
    QString credentials = m_username + ':' + m_password;
    QByteArray authData = credentials.toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + authData);

    QEventLoop loop;
    bool success = false;
    QString errorMessage;

    QNetworkReply *reply = manager.deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, &loop, &success, &errorMessage, calendarId, collectionId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "RemoteCalendarBackend::deleteCalendar: HTTP status" << statusCode;

        // 200 OK or 204 No Content are both valid DELETE responses
        if (statusCode == 200 || statusCode == 204) {
            qDebug() << "RemoteCalendarBackend::deleteCalendar: Calendar deleted successfully:" << calendarId;

            // Remove from our URL cache
            m_davUrls.remove(calendarId);

            emit calendarDeleted(collectionId, calendarId);
            success = true;
        } else if (statusCode == 404) {
            // Calendar doesn't exist - return false to indicate it wasn't deleted
            qDebug() << "RemoteCalendarBackend::deleteCalendar: Calendar not found:" << calendarId;
            m_davUrls.remove(calendarId);
            success = false;
        } else {
            errorMessage = reply->errorString();
            if (errorMessage.isEmpty()) {
                errorMessage = QStringLiteral("HTTP status: %1").arg(statusCode);
            }
            qWarning() << "RemoteCalendarBackend::deleteCalendar: Failed:" << errorMessage;
            emit calendarOperationError(calendarId, errorMessage);
            success = false;
        }
        reply->deleteLater();
        loop.quit();
    });

    loop.exec();

    return success;
}

// ============================================================================
// Operation-Based API Implementation
// ============================================================================

QList<KCalendarCore::Incidence::Ptr> RemoteCalendarBackend::serveCachedItems(
    const QString &calendarId, const KDAV::DavUrl &davUrl)
{
    QList<KCalendarCore::Incidence::Ptr> cachedIncidences;
    KCalendarCore::ICalFormat format;

    QSqlDatabase db = QSqlDatabase::database(m_cacheConnectionName);
    if (!db.isValid()) {
        return cachedIncidences;
    }

    QString calendarPath = davUrl.url().path();
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT url, ical_content FROM cached_items WHERE url LIKE ?"));
    query.addBindValue(QLatin1Char('%') + calendarPath + QLatin1Char('%'));

    if (query.exec()) {
        while (query.next()) {
            QString icalData = query.value(1).toString();
            auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
            QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});

            if (format.fromString(tmpCalPtr, icalData)) {
                for (const auto &incidence : tmpCal->incidences()) {
                    cachedIncidences.append(incidence);
                    emit itemFetched(calendarId, incidence);
                }
            }
            delete tmpCal;
        }
    }

    return cachedIncidences;
}

FetchOperation* RemoteCalendarBackend::fetchItems(const QString &calendarId)
{
    auto *op = new FetchOperation(calendarId, this);
    registerOperation(op);

    if (!m_davUrls.contains(calendarId)) {
        qWarning() << "RemoteCalendarBackend::fetchItems: No DAV URL for calendar:" << calendarId;
        // Use QTimer to defer the failure so caller can connect to signals
        QTimer::singleShot(0, op, [op, calendarId, this]() {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
            emit fetchFinished(calendarId, false, QStringLiteral("No DAV URL registered"));
        });
        return op;
    }

    // Initialize content cache on first fetch (lazy initialization)
    initContentCache();

    KDAV::DavUrl davUrl = m_davUrls.value(calendarId);

    // Start the operation
    QMetaObject::invokeMethod(this, [this, op, davUrl, calendarId]() {
        // Mark operation as running
        op->setState(SyncOperation::Running);

        // CTag optimization: raw PROPFIND for CS:getctag on the calendar URL.
        // KDAV's DavCollectionsFetchJob doesn't return CTag for individual calendar
        // URLs, so we do a lightweight Depth:0 PROPFIND ourselves.
        if (m_davUrls.contains(calendarId)) {
            QString storedCtag = ctag(calendarId);
            QString freshCtag;

            if (!storedCtag.isEmpty()) {
                QUrl propfindUrl = davUrl.url();
                propfindUrl.setUserName(QString());
                propfindUrl.setPassword(QString());

                QNetworkAccessManager nam;
                QNetworkRequest request(propfindUrl);
                request.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("application/xml; charset=utf-8"));
                request.setRawHeader("Depth", "0");

                QString credentials = m_username + QLatin1Char(':') + m_password;
                request.setRawHeader("Authorization",
                                     "Basic " + credentials.toUtf8().toBase64());

                QByteArray body =
                    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
                    "<D:propfind xmlns:D=\"DAV:\" xmlns:CS=\"http://calendarserver.org/ns/\">"
                    "  <D:prop><CS:getctag/></D:prop>"
                    "</D:propfind>";

                QEventLoop loop;

                QNetworkReply *reply = nam.sendCustomRequest(request, "PROPFIND", body);
                connect(reply, &QNetworkReply::finished, this, [reply, &loop, &freshCtag]() {
                    if (reply->error() == QNetworkReply::NoError) {
                        QByteArray responseData = reply->readAll();
                        // Parse CS:getctag from XML response
                        // Format: <CS:getctag>"hash-value"</CS:getctag>
                        QString response = QString::fromUtf8(responseData);
                        QRegularExpression re(QStringLiteral("<[^>]*:getctag[^>]*>([^<]+)</[^>]*:getctag>"));
                        auto match = re.match(response);
                        if (match.hasMatch()) {
                            freshCtag = match.captured(1);
                        }
                    }
                    reply->deleteLater();
                    loop.quit();
                });
                loop.exec();
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
                m_calendarCtags[calendarId] = freshCtag;
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
                removeCachedContent(urlStr);
                m_localEtags.remove(urlStr);
                if (m_etagCache) {
                    m_etagCache->removeEtag(urlStr);
                }
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
                KCalendarCore::ICalFormat format;
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
                    QString cachedIcal = getCachedContent(urlKey, etag);
                    if (cachedIcal.isEmpty()) {
                        // Cache miss - shouldn't happen if item wasn't in changedItems
                        // but handle gracefully by skipping
                        qWarning() << "RemoteCalendarBackend::fetchItems: Cache miss for unchanged item:" << urlKey;
                        currentItem++;
                        emit fetchProgressChanged(calendarId, currentItem, allItems.size());
                        continue;
                    }

                    auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
                    QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});

                    if (!format.fromString(tmpCalPtr, cachedIcal)) {
                        qWarning() << "RemoteCalendarBackend::fetchItems: Could not parse cached iCal for:" << urlKey;
                        delete tmpCal;
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

                    for (const auto &incidence : tmpCal->incidences()) {
                        fetchedIncidences.append(incidence);
                        emit itemFetched(calendarId, incidence);
                    }

                    delete tmpCal;
                    currentItem++;
                    emit fetchProgressChanged(calendarId, currentItem, allItems.size());
                }

                qDebug() << "RemoteCalendarBackend::fetchItems: Served" << fetchedIncidences.size()
                         << "incidences from cache for calendar" << calendarId;

                // Update stored CTag after successful full fetch
                if (m_calendarCtags.contains(calendarId)) {
                    setCtag(calendarId, m_calendarCtags.value(calendarId));
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
                KCalendarCore::ICalFormat format;

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
                            setCachedContent(urlKey, etag, icalData);
                        }
                    } else {
                        // Serve from cache
                        icalData = getCachedContent(urlKey, etag);
                        if (icalData.isEmpty()) {
                            qWarning() << "RemoteCalendarBackend::fetchItems: Cache miss for item:" << urlKey;
                            countSkipped++;
                            currentItem++;
                            emit fetchProgressChanged(calendarId, currentItem, totalItems);
                            continue;
                        }
                    }

                    auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
                    QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});

                    if (!format.fromString(tmpCalPtr, icalData)) {
                        qWarning() << "RemoteCalendarBackend::fetchItems: Could not parse iCal data for item:"
                                   << urlKey << (fromNetwork ? "(from network)" : "(from cache)");
                        delete tmpCal;
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

                    for (const auto &incidence : tmpCal->incidences()) {
                        fetchedIncidences.append(incidence);
                        emit itemFetched(calendarId, incidence);
                    }

                    delete tmpCal;
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
                if (m_calendarCtags.contains(calendarId)) {
                    setCtag(calendarId, m_calendarCtags.value(calendarId));
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
    auto *op = new PushOperation(calendarId, items, this);
    registerOperation(op);

    if (items.isEmpty()) {
        QTimer::singleShot(0, op, [op]() {
            op->complete();
        });
        return op;
    }

    if (!m_davUrls.contains(calendarId)) {
        QTimer::singleShot(0, op, [op, calendarId]() {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
        });
        return op;
    }

    KDAV::DavUrl davUrl = m_davUrls.value(calendarId);

    // Use shared counter to track completion
    auto remaining = std::make_shared<int>(items.size());
    auto anyError = std::make_shared<bool>(false);

    QMetaObject::invokeMethod(this, [this, op, davUrl, items, remaining, anyError]() mutable {
        op->setState(SyncOperation::Running);

        // Initialize content cache so we can store pushed items for subsequent fetches
        initContentCache();

        KCalendarCore::ICalFormat icalFormat;  // Create inside lambda (non-copyable)

        for (const auto &incidence : items) {
            if (incidence.isNull()) {
                (*remaining)--;
                continue;
            }

            auto tempCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
            tempCal->addIncidence(incidence);
            QSharedPointer<KCalendarCore::Calendar> tempCalPtr(tempCal, [](KCalendarCore::Calendar*){});
            QString icalData = icalFormat.toString(tempCalPtr);

            if (icalData.isEmpty()) {
                qWarning() << "RemoteCalendarBackend::pushItems: Failed to convert incidence to iCal:" << incidence->uid();
                op->addFailedUid(incidence->uid());
                (*remaining)--;
                if (*remaining == 0) {
                    if (*anyError || !op->failedUids().isEmpty()) {
                        op->fail(QStringLiteral("Some items failed to push"));
                    } else {
                        op->complete();
                    }
                }
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
                    [this, op, createJob, uid, remaining, anyError, icalData](KJob *job) {
                if (op->state() == SyncOperation::Cancelled) {
                    return;
                }

                if (job->error()) {
                    qWarning() << "RemoteCalendarBackend::pushItems: Failed to create item:" << job->errorString();
                    op->addFailedUid(uid);
                    *anyError = true;
                } else {
                    KDAV::DavItem createdItem = createJob->item();
                    QString remoteUrl = normalizeUrlKey(createdItem.url().url().toString());
                    if (m_etagCache) {
                        m_etagCache->setEtag(remoteUrl, createdItem.etag());
                    }
                    m_localEtags[remoteUrl] = createdItem.etag();

                    // Store content in cache so subsequent fetches can use delta sync
                    setCachedContent(remoteUrl, createdItem.etag(), icalData);

                    op->addSucceededUid(uid);
                    qDebug() << "RemoteCalendarBackend::pushItems: Created" << uid << "ETag:" << createdItem.etag();
                }

                (*remaining)--;
                if (*remaining == 0) {
                    // Invalidate stored CTag — the server's CTag changed due to our push
                    if (!op->succeededUids().isEmpty()) {
                        clearCtag(op->calendarId());
                    }

                    if (*anyError || !op->failedUids().isEmpty()) {
                        if (op->succeededUids().isEmpty()) {
                            op->fail(QStringLiteral("All items failed to push"));
                        } else {
                            // Partial success - still complete but with failed UIDs tracked
                            op->complete();
                        }
                    } else {
                        op->complete();
                    }
                }
            });

            createJob->start();
        }
    }, Qt::QueuedConnection);

    return op;
}


DeleteOperation* RemoteCalendarBackend::deleteItems(const QString &calendarId,
                                            const QStringList &uids)
{
    auto *op = new DeleteOperation(calendarId, uids, this);
    registerOperation(op);

    if (uids.isEmpty()) {
        QTimer::singleShot(0, op, [op]() {
            op->complete();
        });
        return op;
    }

    if (!m_davUrls.contains(calendarId)) {
        QTimer::singleShot(0, op, [op, calendarId]() {
            op->fail(QStringLiteral("No DAV URL registered for calendar: %1").arg(calendarId));
        });
        return op;
    }

    KDAV::DavUrl davUrl = m_davUrls.value(calendarId);

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
                    QString urlKey = normalizeUrlKey(itemUrl.toString());
                    if (m_etagCache) {
                        m_etagCache->removeEtag(urlKey);
                    }
                    m_localEtags.remove(urlKey);
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

    if (!m_davUrls.contains(calendarId)) {
        qWarning() << "RemoteCalendarBackend::getRawIcs: No DAV URL for calendar:" << calendarId;
        return QString();
    }

    KDAV::DavUrl davUrl = m_davUrls.value(calendarId);
    QUrl itemUrl = const_cast<RemoteCalendarBackend*>(this)->generateItemUrl(davUrl, uid);

    // Remove credentials from URL - they'll be passed via Basic Auth header
    QUrl cleanUrl = itemUrl;
    cleanUrl.setUserName(QString());
    cleanUrl.setPassword(QString());

    QNetworkAccessManager manager;
    QNetworkRequest request(cleanUrl);

    // Set up Basic Authentication
    QString credentials = m_username + ':' + m_password;
    QByteArray authData = credentials.toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + authData);

    QEventLoop loop;
    QString content;

    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, [reply, &loop, &content]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (statusCode == 200 && reply->error() == QNetworkReply::NoError) {
            content = QString::fromUtf8(reply->readAll());
        } else {
            qWarning() << "RemoteCalendarBackend::getRawIcs: Failed to fetch, HTTP status:" << statusCode
                       << "error:" << reply->errorString();
        }

        reply->deleteLater();
        loop.quit();
    });

    loop.exec();

    return content;
}

bool RemoteCalendarBackend::setRawIcs(const QString &calendarId, const QString &uid,
                               const QString &icsContent)
{
    if (calendarId.isEmpty() || uid.isEmpty() || icsContent.isEmpty()) {
        return false;
    }

    if (!m_davUrls.contains(calendarId)) {
        qWarning() << "RemoteCalendarBackend::setRawIcs: No DAV URL for calendar:" << calendarId;
        return false;
    }

    KDAV::DavUrl davUrl = m_davUrls.value(calendarId);
    QUrl itemUrl = generateItemUrl(davUrl, uid);

    // Remove credentials from URL - they'll be passed via Basic Auth header
    QUrl cleanUrl = itemUrl;
    cleanUrl.setUserName(QString());
    cleanUrl.setPassword(QString());

    QNetworkAccessManager manager;
    QNetworkRequest request(cleanUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/calendar; charset=utf-8"));

    // Set up Basic Authentication
    QString credentials = m_username + ':' + m_password;
    QByteArray authData = credentials.toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + authData);

    // Get cached ETag and set If-Match header to prevent overwriting concurrent changes
    QString oldEtag = cachedEtag(itemUrl.toString());
    if (!oldEtag.isEmpty()) {
        request.setRawHeader("If-Match", oldEtag.toUtf8());
        qDebug() << "RemoteCalendarBackend::setRawIcs: Using ETag:" << oldEtag;
    }

    QEventLoop loop;
    bool success = false;

    QNetworkReply *reply = manager.put(request, icsContent.toUtf8());
    QObject::connect(reply, &QNetworkReply::finished, [this, reply, &loop, &success, itemUrl]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 200 OK, 201 Created, or 204 No Content are valid PUT responses
        if (statusCode == 200 || statusCode == 201 || statusCode == 204) {
            success = true;

            QString urlKey = normalizeUrlKey(itemUrl.toString());

            // Capture and cache the new ETag from the response
            QString newEtag = QString::fromUtf8(reply->rawHeader("ETag"));
            if (!newEtag.isEmpty()) {
                // Remove quotes if present (ETags may be quoted per RFC 7232)
                if (newEtag.startsWith('"') && newEtag.endsWith('"')) {
                    newEtag = newEtag.mid(1, newEtag.length() - 2);
                }
                m_localEtags[urlKey] = newEtag;
                if (m_etagCache) {
                    m_etagCache->setEtag(urlKey, newEtag);
                }
                qDebug() << "RemoteCalendarBackend::setRawIcs: Updated ETag to:" << newEtag;
            } else {
                // If server doesn't return ETag, clear the cached one to force refresh
                qWarning() << "RemoteCalendarBackend::setRawIcs: Server didn't return ETag, clearing cache";
                m_localEtags.remove(urlKey);
                if (m_etagCache) {
                    m_etagCache->removeEtag(urlKey);
                }
            }

            qDebug() << "RemoteCalendarBackend::setRawIcs: Successfully updated, HTTP status:" << statusCode;
        } else {
            qWarning() << "RemoteCalendarBackend::setRawIcs: Failed, HTTP status:" << statusCode
                       << "error:" << reply->errorString();
            success = false;
        }

        reply->deleteLater();
        loop.quit();
    });

    loop.exec();

    // Invalidate stored CTag — the server's CTag changed due to our push
    if (success) {
        clearCtag(calendarId);
    }

    return success;
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
    for (auto it = m_davUrls.constBegin(); it != m_davUrls.constEnd(); ++it) {
        CollectionInfo info;
        info.id   = it.key();
        info.name = it.key();
        info.path = it.value().url().toString(QUrl::RemovePassword);
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
    if (m_davUrls.contains(collectionId)) {
        info.path = m_davUrls.value(collectionId).url().toString(QUrl::RemovePassword);
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

    // Block until the operation finishes.
    if (!op->isFinished()) {
        QEventLoop loop;
        QObject::connect(op, &SyncOperation::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }

    QList<BackendRecord> result;
    if (op->state() != SyncOperation::Succeeded) {
        qWarning() << "RemoteCalendarBackend::loadRecords: fetchItems failed for" << collectionId
                   << ":" << op->errorString();
        op->deleteLater();
        return result;
    }

    KCalendarCore::ICalFormat fmt;
    for (const auto &incidence : op->fetchedItems()) {
        if (incidence.isNull()) continue;

        // Serialize the incidence back to iCal bytes.
        auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
        tmpCal->addIncidence(incidence);
        QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});
        const QString icalStr = fmt.toString(tmpCalPtr);
        const QByteArray icalBytes = icalStr.toUtf8();
        result.append(blobRecordFromIcal(incidence->uid(), icalBytes));
    }

    op->deleteLater();
    return result;
}

std::optional<BackendRecord> RemoteCalendarBackend::loadRecord(const QString &recordId)
{
    // recordId == uid; search all registered calendars.
    for (auto it = m_davUrls.constBegin(); it != m_davUrls.constEnd(); ++it) {
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

    // Parse the iCal to get an Incidence::Ptr for pushItems.
    KCalendarCore::ICalFormat fmt;
    auto tmpCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    QSharedPointer<KCalendarCore::Calendar> tmpCalPtr(tmpCal, [](KCalendarCore::Calendar*){});
    if (!fmt.fromRawString(tmpCalPtr, record.data)) {
        qWarning() << "RemoteCalendarBackend::createRecord: cannot parse iCal for uid" << record.id;
        return {};
    }
    const auto incidences = tmpCal->incidences();
    if (incidences.isEmpty()) {
        qWarning() << "RemoteCalendarBackend::createRecord: no incidences in iCal for uid" << record.id;
        return {};
    }

    PushOperation *op = pushItems(collectionId, incidences);
    if (!op) return {};

    if (!op->isFinished()) {
        QEventLoop loop;
        QObject::connect(op, &SyncOperation::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }

    const bool ok = (op->state() == SyncOperation::Succeeded) &&
                    op->failedUids().isEmpty();
    op->deleteLater();
    return ok ? record.id : QString{};
}

bool RemoteCalendarBackend::updateRecord(const BackendRecord &record)
{
    if (record.id.isEmpty() || record.data.isEmpty()) return false;

    // Find which calendar this uid lives in.
    for (auto it = m_davUrls.constBegin(); it != m_davUrls.constEnd(); ++it) {
        KDAV::DavUrl davUrl = it.value();
        QUrl itemUrl = generateItemUrl(davUrl, record.id);
        QString urlKey = normalizeUrlKey(itemUrl.toString());

        // Check if we have an ETag for this item (proxy for "this calendar owns it").
        if (!m_localEtags.contains(urlKey)) continue;

        return setRawIcs(it.key(), record.id, QString::fromUtf8(record.data));
    }

    // Fallback: try all registered calendars (first success wins).
    for (auto it = m_davUrls.constBegin(); it != m_davUrls.constEnd(); ++it) {
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
    for (auto it = m_davUrls.constBegin(); it != m_davUrls.constEnd(); ++it) {
        DeleteOperation *op = deleteItems(it.key(), QStringList{recordId});
        if (!op) continue;

        if (!op->isFinished()) {
            QEventLoop loop;
            QObject::connect(op, &SyncOperation::finished, &loop, &QEventLoop::quit);
            loop.exec();
        }

        const bool ok = (op->state() == SyncOperation::Succeeded) &&
                        op->failedUids().isEmpty();
        op->deleteLater();
        if (ok) return true;
    }
    return false;
}

// --- Change detection -------------------------------------------------------

QList<BackendRecord> RemoteCalendarBackend::modifiedSince(const QString &collectionId,
                                                   const QDateTime &since)
{
    // CTag short-circuit: if the stored CTag matches what the server has, nothing changed.
    // The freshCtag is fetched inside fetchItems via PROPFIND; we replicate just the check here.
    // If the CTag matches, return empty — caller will skip a full fetch.
    const QString storedCtag = ctag(collectionId);
    if (!storedCtag.isEmpty() && m_davUrls.contains(collectionId)) {
        // Do a lightweight Depth:0 PROPFIND to get the current CTag.
        KDAV::DavUrl davUrl = m_davUrls.value(collectionId);
        QUrl propfindUrl = davUrl.url();
        propfindUrl.setUserName(QString());
        propfindUrl.setPassword(QString());

        QNetworkAccessManager nam;
        QNetworkRequest request(propfindUrl);
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/xml; charset=utf-8"));
        request.setRawHeader("Depth", "0");
        request.setRawHeader("Authorization",
                             "Basic " + (m_username + QLatin1Char(':') + m_password).toUtf8().toBase64());

        const QByteArray body =
            "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
            "<D:propfind xmlns:D=\"DAV:\" xmlns:CS=\"http://calendarserver.org/ns/\">"
            "  <D:prop><CS:getctag/></D:prop>"
            "</D:propfind>";

        QEventLoop loop;
        QString freshCtag;

        QNetworkReply *reply = nam.sendCustomRequest(request, "PROPFIND", body);
        QObject::connect(reply, &QNetworkReply::finished, &loop,
                         [reply, &loop, &freshCtag]() {
            if (reply->error() == QNetworkReply::NoError) {
                const QString response = QString::fromUtf8(reply->readAll());
                QRegularExpression re(QStringLiteral("<[^>]*:getctag[^>]*>([^<]+)</[^>]*:getctag>"));
                auto m = re.match(response);
                if (m.hasMatch()) freshCtag = m.captured(1);
            }
            reply->deleteLater();
            loop.quit();
        });
        loop.exec();

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
