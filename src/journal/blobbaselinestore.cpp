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

    // --- Phase F1 Task 11 migration ----------------------------------------
    // Pre-Task-11 schema: a flat-keyed `blob_baselines` table
    //   (mapping_id, record_id, content_hash) plus a triple-keyed
    //   `blob_baselines_triple` table
    //   (backend_id, collection_id, record_id, content_hash).
    //
    // Post-Task-11 schema: a single triple-keyed `blob_baselines` table
    //   (backend_id, collection_id, record_id, content_hash).
    //
    // Migration sequence (idempotent — safe on every open):
    //   1. Probe sqlite_master to determine the current shape.
    //   2. If a legacy flat-keyed `blob_baselines` exists (its row in
    //      sqlite_master mentions `mapping_id`), drop it. The
    //      triple-keyed data is preserved on the side in
    //      `blob_baselines_triple`.
    //   3. If `blob_baselines_triple` exists, rename it to
    //      `blob_baselines` (the canonical name).
    //   4. Fall through to CREATE TABLE IF NOT EXISTS
    //      blob_baselines(...) — no-op on already-migrated and renamed
    //      DBs; creates fresh on never-opened DBs.
    //
    // Crucially, step 2 must NOT run on already-migrated DBs (where
    // `blob_baselines` is the renamed triple table) — that would
    // obliterate user baseline data on every reopen. We discriminate
    // by inspecting the table's column set in sqlite_master.

    bool legacyFlatExists  = false;
    bool tripleExists      = false;
    {
        QSqlQuery probe(db);
        probe.prepare(QStringLiteral(
            "SELECT name, sql FROM sqlite_master "
            "WHERE type = 'table' "
            "AND name IN ('blob_baselines', 'blob_baselines_triple')"));
        if (!probe.exec()) {
            setError(QStringLiteral("schema probe failed: %1")
                         .arg(probe.lastError().text()));
            return false;
        }
        while (probe.next()) {
            const QString name = probe.value(0).toString();
            const QString sql  = probe.value(1).toString();
            if (name == QLatin1String("blob_baselines_triple")) {
                tripleExists = true;
            } else if (name == QLatin1String("blob_baselines")) {
                // Discriminate flat-keyed vs already-migrated triple-keyed:
                // flat schema has `mapping_id` column, triple schema does
                // not. CREATE TABLE statements stored in sqlite_master are
                // verbatim, so a substring check is sufficient and stable.
                if (sql.contains(QLatin1String("mapping_id"))) {
                    legacyFlatExists = true;
                }
            }
        }
    }

    if (legacyFlatExists) {
        if (!q.exec(QStringLiteral("DROP TABLE blob_baselines"))) {
            setError(QStringLiteral("DROP TABLE blob_baselines failed: %1")
                         .arg(q.lastError().text()));
            return false;
        }
    }

    if (tripleExists) {
        if (!q.exec(QStringLiteral(
                "ALTER TABLE blob_baselines_triple "
                "RENAME TO blob_baselines"))) {
            setError(QStringLiteral(
                         "ALTER TABLE blob_baselines_triple "
                         "RENAME TO blob_baselines failed: %1")
                         .arg(q.lastError().text()));
            return false;
        }
    }

    // Step 3: ensure the canonical triple-keyed table exists. No-op on
    // DBs that have just been migrated from the renamed triple table;
    // creates the table fresh on never-opened DBs.
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS blob_baselines ("
            "  backend_id    TEXT NOT NULL,"
            "  collection_id TEXT NOT NULL,"
            "  record_id     TEXT NOT NULL,"
            "  content_hash  TEXT NOT NULL,"
            "  updated_at    TEXT DEFAULT (datetime('now')),"
            "  PRIMARY KEY (backend_id, collection_id, record_id)"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE blob_baselines failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_blob_baselines_collection "
        "ON blob_baselines(backend_id, collection_id)"));

    if (!dbFileExistedBefore) {
        q.exec(QStringLiteral("PRAGMA user_version = %1")
                   .arg(kSchemaVersion));
    }

    return true;
}

// ===========================================================================
// Triple-keyed API — stored in the canonical blob_baselines table.
// ===========================================================================

bool BlobBaselineStore::setBaseline(const QString &backendId,
                                    const QString &collectionId,
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
        "(backend_id, collection_id, record_id, content_hash, updated_at) "
        "VALUES (?, ?, ?, ?, datetime('now'))"));
    q.addBindValue(backendId);
    q.addBindValue(collectionId);
    q.addBindValue(recordId);
    q.addBindValue(contentHash);

    if (!q.exec()) {
        setError(QStringLiteral("setBaseline: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    return true;
}

QString BlobBaselineStore::baselineHash(const QString &backendId,
                                        const QString &collectionId,
                                        const QString &recordId) const
{
    if (!m_isOpen) {
        return {};
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT content_hash FROM blob_baselines "
        "WHERE backend_id = ? AND collection_id = ? AND record_id = ?"));
    q.addBindValue(backendId);
    q.addBindValue(collectionId);
    q.addBindValue(recordId);

    if (!q.exec() || !q.next()) {
        return {};
    }
    return q.value(0).toString();
}

bool BlobBaselineStore::commitBaselines(
    const QString &backendId,
    const QString &collectionId,
    const QMap<QString, QString> &recordIdToHash)
{
    if (!m_isOpen) {
        setError(QStringLiteral("commitBaselines: store not open"));
        return false;
    }
    if (recordIdToHash.isEmpty()) {
        return true;
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
        "(backend_id, collection_id, record_id, content_hash, updated_at) "
        "VALUES (?, ?, ?, ?, datetime('now'))"));

    for (auto it = recordIdToHash.constBegin();
         it != recordIdToHash.constEnd(); ++it) {
        q.addBindValue(backendId);
        q.addBindValue(collectionId);
        q.addBindValue(it.key());
        q.addBindValue(it.value());
        if (!q.exec()) {
            setError(
                QStringLiteral("commitBaselines: insert failed: %1")
                    .arg(q.lastError().text()));
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        setError(
            QStringLiteral("commitBaselines: COMMIT failed: %1")
                .arg(db.lastError().text()));
        db.rollback();
        return false;
    }
    return true;
}

QStringList BlobBaselineStore::baselineRecordIds(
    const QString &backendId,
    const QString &collectionId) const
{
    if (!m_isOpen) {
        return {};
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT record_id FROM blob_baselines "
        "WHERE backend_id = ? AND collection_id = ?"));
    q.addBindValue(backendId);
    q.addBindValue(collectionId);

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

bool BlobBaselineStore::clearCollection(const QString &backendId,
                                        const QString &collectionId)
{
    if (!m_isOpen) {
        setError(QStringLiteral("clearCollection: store not open"));
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM blob_baselines "
        "WHERE backend_id = ? AND collection_id = ?"));
    q.addBindValue(backendId);
    q.addBindValue(collectionId);

    if (!q.exec()) {
        setError(QStringLiteral("clearCollection: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    return true;
}

} // namespace Kalburator::Sync
