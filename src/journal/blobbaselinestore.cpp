#include "blobbaselinestore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace Kalburator::Sync {

namespace {
constexpr int kSchemaVersion = 3;  // Matches IDMappingStore / SyncStore.
} // namespace

int BlobBaselineStore::s_connectionCounter = 0;

BlobBaselineStore::BlobBaselineStore(const QString &dbPath)
    : m_dbPath(dbPath)
    , m_connName(QStringLiteral("BlobBaselineStore_%1")
                     .arg(++s_connectionCounter))
{
    const bool dbFileExistedBefore = QFile::exists(m_dbPath);

    QFileInfo fi(m_dbPath);
    QDir parent = fi.dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        setError(QStringLiteral("Failed to create directory: %1")
                     .arg(parent.path()));
        return;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                m_connName);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        setError(QStringLiteral("Failed to open database: %1")
                     .arg(db.lastError().text()));
        return;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    if (!ensureSchemaAndVersion(dbFileExistedBefore)) {
        return;
    }

    m_isOpen = true;
}

BlobBaselineStore::~BlobBaselineStore()
{
    if (QSqlDatabase::contains(m_connName)) {
        QSqlDatabase::database(m_connName).close();
        QSqlDatabase::removeDatabase(m_connName);
    }
}

bool BlobBaselineStore::isOpen() const { return m_isOpen; }
QString BlobBaselineStore::lastError() const { return m_lastError; }
QString BlobBaselineStore::databasePath() const { return m_dbPath; }

void BlobBaselineStore::setError(const QString &message) const
{
    m_lastError = message;
}

bool BlobBaselineStore::ensureSchemaAndVersion(bool dbFileExistedBefore)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS blob_baselines ("
            "  mapping_id   TEXT NOT NULL,"
            "  record_id    TEXT NOT NULL,"
            "  content_hash TEXT NOT NULL,"
            "  updated_at   TEXT DEFAULT (datetime('now')),"
            "  PRIMARY KEY (mapping_id, record_id)"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_blob_baselines_mapping "
        "ON blob_baselines(mapping_id)"));

    if (!dbFileExistedBefore) {
        q.exec(QStringLiteral("PRAGMA user_version = %1")
                   .arg(kSchemaVersion));
    }

    return true;
}

// --- Method stubs: implementations land in subsequent tasks ---

bool BlobBaselineStore::setBaseline(const QString &, const QString &,
                                    const QString &)
{
    setError(QStringLiteral("setBaseline: not implemented yet"));
    return false;
}

QString BlobBaselineStore::baselineHash(const QString &,
                                        const QString &) const
{
    return {};
}

bool BlobBaselineStore::commitBaselines(const QString &,
                                        const QMap<QString, QString> &)
{
    setError(QStringLiteral("commitBaselines: not implemented yet"));
    return false;
}

QStringList BlobBaselineStore::baselineRecordIds(const QString &) const
{
    return {};
}

bool BlobBaselineStore::clearMapping(const QString &)
{
    setError(QStringLiteral("clearMapping: not implemented yet"));
    return false;
}

} // namespace Kalburator::Sync
