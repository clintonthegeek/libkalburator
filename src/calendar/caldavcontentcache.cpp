#include "caldavcontentcache.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace Kalburator::Sync {

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

CalDavContentCache::CalDavContentCache(const QString &accountSeed)
    : m_seed(accountSeed)
    , m_connectionName(QStringLiteral("CalDavContentCache_")
                       + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

CalDavContentCache::~CalDavContentCache()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

void CalDavContentCache::setCacheDir(const QString &dir)
{
    m_dirOverride = dir;
}

bool CalDavContentCache::ensureOpen()
{
    if (m_open) {
        return true;
    }

    // Cache directory: caller-chosen (per-collection profile folder) when set,
    // otherwise the app's shared cache location.
    QString cacheDir = m_dirOverride.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        : m_dirOverride;
    if (cacheDir.isEmpty()) {
        cacheDir = QDir::tempPath();
    }
    QDir().mkpath(cacheDir);

    const QString hostHash = QString::number(stableContentCacheHash(m_seed));
    const QString dbPath =
        cacheDir + QStringLiteral("/caldav-cache-%1.db").arg(hostHash);

    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qWarning() << "CalDavContentCache: failed to open" << dbPath
                   << ":" << db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    QSqlQuery query(db);
    const bool success = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cached_items ("
        "  url TEXT PRIMARY KEY,"
        "  etag TEXT NOT NULL,"
        "  ical_content TEXT NOT NULL,"
        "  fetched_at INTEGER NOT NULL"
        ")"));
    if (!success) {
        qWarning() << "CalDavContentCache: failed to create cache table:"
                   << query.lastError().text();
        return false;
    }

    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_cached_items_etag ON cached_items(etag)"));

    m_open = true;
    qDebug() << "CalDavContentCache: initialized at" << dbPath;
    return true;
}

QString CalDavContentCache::content(const QString &itemUrl,
                                    const QString &expectedEtag) const
{
    if (!m_open || expectedEtag.isEmpty()) {
        return QString();
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
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

void CalDavContentCache::store(const QString &itemUrl, const QString &etag,
                               const QString &icalContent)
{
    if (!m_open || itemUrl.isEmpty() || etag.isEmpty()) {
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
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
        qWarning() << "CalDavContentCache: failed to cache content:"
                   << query.lastError().text();
    }
}

void CalDavContentCache::remove(const QString &itemUrl)
{
    if (!m_open) {
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        return;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM cached_items WHERE url = ?"));
    query.addBindValue(itemUrl);

    if (!query.exec()) {
        qWarning() << "CalDavContentCache: failed to remove cached content:"
                   << query.lastError().text();
    }
}

QList<CalDavContentCache::Row>
CalDavContentCache::rowsByPathFragment(const QString &pathFragment) const
{
    QList<Row> rows;
    if (!m_open) {
        return rows;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        return rows;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT url, ical_content FROM cached_items WHERE url LIKE ?"));
    query.addBindValue(QLatin1Char('%') + pathFragment + QLatin1Char('%'));

    if (query.exec()) {
        while (query.next()) {
            rows.append(Row{query.value(0).toString(), query.value(1).toString()});
        }
    }
    return rows;
}

} // namespace Kalburator::Sync
