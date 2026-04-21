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

bool BlobBaselineStore::setBaseline(const QString &mappingId,
                                    const QString &recordId,
                                    const QString &contentHash)
{
    if (!m_isOpen) {
        setError(QStringLiteral("setBaseline: store not open"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blob_baselines "
        "(mapping_id, record_id, content_hash, updated_at) "
        "VALUES (?, ?, ?, datetime('now'))"));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);
    q.addBindValue(contentHash);

    if (!q.exec()) {
        setError(QStringLiteral("setBaseline: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    return true;
}

QString BlobBaselineStore::baselineHash(const QString &mappingId,
                                        const QString &recordId) const
{
    if (!m_isOpen) {
        return {};
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT content_hash FROM blob_baselines "
        "WHERE mapping_id = ? AND record_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);

    if (!q.exec() || !q.next()) {
        return {};
    }
    return q.value(0).toString();
}

bool BlobBaselineStore::commitBaselines(
    const QString &mappingId,
    const QMap<QString, QString> &recordIdToHash)
{
    if (!m_isOpen) {
        setError(QStringLiteral("commitBaselines: store not open"));
        return false;
    }
    if (recordIdToHash.isEmpty()) {
        return true;  // Nothing to do; not an error.
    }

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.transaction()) {
        setError(QStringLiteral("commitBaselines: BEGIN failed: %1")
                     .arg(db.lastError().text()));
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blob_baselines "
        "(mapping_id, record_id, content_hash, updated_at) "
        "VALUES (?, ?, ?, datetime('now'))"));

    for (auto it = recordIdToHash.constBegin();
         it != recordIdToHash.constEnd(); ++it) {
        q.addBindValue(mappingId);
        q.addBindValue(it.key());
        q.addBindValue(it.value());
        if (!q.exec()) {
            setError(QStringLiteral("commitBaselines: insert failed: %1")
                         .arg(q.lastError().text()));
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        setError(QStringLiteral("commitBaselines: COMMIT failed: %1")
                     .arg(db.lastError().text()));
        db.rollback();
        return false;
    }
    return true;
}

QStringList BlobBaselineStore::baselineRecordIds(
    const QString &mappingId) const
{
    if (!m_isOpen) {
        return {};
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT record_id FROM blob_baselines WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (!q.exec()) {
        setError(QStringLiteral("baselineRecordIds: %1")
                     .arg(q.lastError().text()));
        return {};
    }

    QStringList out;
    while (q.next()) {
        out.append(q.value(0).toString());
    }
    return out;
}

bool BlobBaselineStore::clearMapping(const QString &)
{
    setError(QStringLiteral("clearMapping: not implemented yet"));
    return false;
}

} // namespace Kalburator::Sync
