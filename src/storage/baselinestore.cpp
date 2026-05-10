#include "baselinestore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace Kalburator::Storage {

namespace {
constexpr int kSchemaVersion = 5;  // K.5: collection_baselines table.
} // namespace

int BaselineStore::s_connectionCounter = 0;

BaselineStore::BaselineStore(const QString &dbPath)
    : m_dbPath(dbPath)
    , m_connName(QStringLiteral("KalburatorBaselineStore_%1")
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

BaselineStore::~BaselineStore()
{
    if (QSqlDatabase::contains(m_connName)) {
        QSqlDatabase::database(m_connName).close();
        QSqlDatabase::removeDatabase(m_connName);
    }
}

bool BaselineStore::isOpen() const { return m_isOpen; }
QString BaselineStore::lastError() const { return m_lastError; }
QString BaselineStore::databasePath() const { return m_dbPath; }

void BaselineStore::setError(const QString &message) const
{
    m_lastError = message;
}

bool BaselineStore::ensureSchemaAndVersion(bool dbFileExistedBefore)
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

    // G.4: ensure v3 mapping-keyed table exists and flag if data migration is needed.
    if (!ensureSchemaV3()) {
        return false;
    }

    // K.5: ensure collection_baselines table exists.
    if (!ensureSchemaV5()) {
        return false;
    }

    return true;
}

bool BaselineStore::ensureSchemaV3()
{
    // Migration discipline (per FINDINGS): gate on PRAGMA user_version.
    // v2 DBs (stamped 3 in F1) need data migration; fresh v4 DBs skip it.
    // Migration is idempotent — CREATE TABLE IF NOT EXISTS + user_version gate.
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    // Always ensure the v3 table exists (idempotent).
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS blob_baselines_v3 ("
            "  mapping_id              TEXT NOT NULL,"
            "  record_id               TEXT NOT NULL,"
            "  canonical_shape_domain  TEXT NOT NULL,"
            "  canonical_shape_encoding TEXT NOT NULL,"
            "  canonical_bytes          BLOB NOT NULL,"
            "  updated_at               INTEGER NOT NULL,"
            "  PRIMARY KEY (mapping_id, record_id)"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE blob_baselines_v3 failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_baselines_v3_mapping "
        "ON blob_baselines_v3 (mapping_id)"));

    // Check whether a v2→v3 data migration is still needed.
    // Gate on the G.4 schema stamp (version 4 specifically), not the current
    // kSchemaVersion, so that bumping kSchemaVersion for later table additions
    // does not re-trigger this migration on already-migrated databases.
    QSqlQuery vq(db);
    vq.exec(QStringLiteral("PRAGMA user_version"));
    const int userVersion = vq.next() ? vq.value(0).toInt() : 0;
    static constexpr int kSchemaV3Introduced = 4;
    if (userVersion < kSchemaV3Introduced) {
        // Flag for deferred data migration (requires mapping resolver from engine).
        m_needsV3Migration = true;
        // Stamp the G.4 schema version so re-opens skip this branch.
        q.exec(QStringLiteral("PRAGMA user_version = %1")
                   .arg(kSchemaV3Introduced));
    }

    return true;
}

bool BaselineStore::ensureSchemaV5()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS collection_baselines ("
            "  mapping_id      TEXT NOT NULL,"
            "  collection_id   TEXT NOT NULL,"
            "  properties_json BLOB NOT NULL,"
            "  updated_at      INTEGER NOT NULL,"
            "  PRIMARY KEY (mapping_id, collection_id)"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE collection_baselines failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_collection_baselines_mapping "
        "ON collection_baselines (mapping_id)"));

    return true;
}

// ===========================================================================
// Collection-baseline API (K.5, schema v5)
// ===========================================================================

bool BaselineStore::setCollectionBaseline(const QString &mappingId,
                                          const QString &collectionId,
                                          const QVariantMap &props)
{
    if (!m_isOpen) {
        setError(QStringLiteral("setCollectionBaseline: store not open"));
        return false;
    }
    QJsonDocument doc(QJsonObject::fromVariantMap(props));
    const QByteArray bytes = doc.toJson(QJsonDocument::Compact);

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO collection_baselines "
        "(mapping_id, collection_id, properties_json, updated_at) "
        "VALUES (?, ?, ?, strftime('%s','now'))"));
    q.addBindValue(mappingId);
    q.addBindValue(collectionId);
    q.addBindValue(bytes);
    if (!q.exec()) {
        setError(QStringLiteral("setCollectionBaseline: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

QVariantMap BaselineStore::collectionBaseline(const QString &mappingId,
                                              const QString &collectionId) const
{
    if (!m_isOpen) return {};
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT properties_json FROM collection_baselines "
        "WHERE mapping_id = ? AND collection_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(collectionId);
    if (!q.exec() || !q.next()) return {};
    const QByteArray bytes = q.value(0).toByteArray();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object().toVariantMap();
}

bool BaselineStore::removeCollectionBaseline(const QString &mappingId,
                                             const QString &collectionId)
{
    if (!m_isOpen) {
        setError(QStringLiteral("removeCollectionBaseline: store not open"));
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM collection_baselines "
        "WHERE mapping_id = ? AND collection_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(collectionId);
    if (!q.exec()) {
        setError(QStringLiteral("removeCollectionBaseline: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

// ===========================================================================
// Triple-keyed API — stored in the canonical blob_baselines table.
// ===========================================================================

bool BaselineStore::setBaseline(const QString &backendId,
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

QString BaselineStore::baselineHash(const QString &backendId,
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

bool BaselineStore::commitBaselines(
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

QStringList BaselineStore::baselineRecordIds(
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

bool BaselineStore::clearCollection(const QString &backendId,
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

// ===========================================================================
// Mapping resolver and v2→v3 data migration
// ===========================================================================

void BaselineStore::setMappingResolver(MappingResolver fn)
{
    m_mappingResolver = std::move(fn);
}

bool BaselineStore::migrateV3()
{
    if (!m_isOpen) {
        return false;
    }
    if (!m_needsV3Migration) {
        return true;  // already migrated
    }
    if (!m_mappingResolver) {
        // No resolver — can't map old rows to mapping IDs.
        // Accept the loss: old blob baselines are dropped; first sync
        // after upgrade falls into "first sync" semantics.
        m_needsV3Migration = false;
        return true;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connName);

    // Fetch all rows from the v2 triple-keyed table.
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT backend_id, collection_id, record_id, content_hash "
            "FROM blob_baselines"))) {
        setError(QStringLiteral("migrateV3: SELECT failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }

    struct V2Row {
        QString backendId, collectionId, recordId, contentHash;
    };
    QList<V2Row> rows;
    while (q.next()) {
        rows.append({q.value(0).toString(), q.value(1).toString(),
                     q.value(2).toString(), q.value(3).toString()});
    }

    if (rows.isEmpty()) {
        m_needsV3Migration = false;
        return true;
    }

    if (!db.transaction()) {
        setError(QStringLiteral("migrateV3: BEGIN failed"));
        return false;
    }

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO blob_baselines_v3 "
        "(mapping_id, record_id, canonical_shape_domain, canonical_shape_encoding, "
        " canonical_bytes, updated_at) "
        "VALUES (?, ?, '', '', ?, ?)"));

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const auto &row : rows) {
        const QStringList mappingIds =
            m_mappingResolver(row.backendId, row.collectionId);
        for (const QString &mid : mappingIds) {
            ins.addBindValue(mid);
            ins.addBindValue(row.recordId);
            ins.addBindValue(row.contentHash.toUtf8());
            ins.addBindValue(now);
            if (!ins.exec()) {
                db.rollback();
                setError(QStringLiteral("migrateV3: insert failed: %1")
                             .arg(ins.lastError().text()));
                return false;
            }
        }
        // Rows with no mapping are orphaned — skip (logged below).
        if (mappingIds.isEmpty()) {
            qDebug("BaselineStore::migrateV3: orphan row "
                   "(backendId=%s collectionId=%s recordId=%s) — skipped",
                   qUtf8Printable(row.backendId),
                   qUtf8Printable(row.collectionId),
                   qUtf8Printable(row.recordId));
        }
    }

    if (!db.commit()) {
        setError(QStringLiteral("migrateV3: COMMIT failed"));
        db.rollback();
        return false;
    }

    m_needsV3Migration = false;
    return true;
}

// ===========================================================================
// v3 mapping-keyed API
// ===========================================================================

bool BaselineStore::setBaselineV3(const QString &mappingId,
                                       const Kalburator::Shape::CanonicalRecord &rec)
{
    if (!m_isOpen) {
        setError(QStringLiteral("setBaselineV3: store not open"));
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blob_baselines_v3 "
        "(mapping_id, record_id, canonical_shape_domain, canonical_shape_encoding, "
        " canonical_bytes, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(mappingId);
    q.addBindValue(rec.recordId);
    q.addBindValue(rec.shape.domain.toString());
    q.addBindValue(rec.shape.encoding.toString());
    q.addBindValue(rec.data);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) {
        setError(QStringLiteral("setBaselineV3: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

std::optional<Kalburator::Shape::CanonicalRecord>
BaselineStore::baselineV3(const QString &mappingId,
                               const QString &recordId) const
{
    if (!m_isOpen) {
        return std::nullopt;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT canonical_shape_domain, canonical_shape_encoding, canonical_bytes "
        "FROM blob_baselines_v3 "
        "WHERE mapping_id = ? AND record_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = recordId;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{q.value(0).toString()},
        Kalburator::Shape::EncodingId{q.value(1).toString()}};
    rec.data = q.value(2).toByteArray();
    return rec;
}

QList<Kalburator::Shape::CanonicalRecord>
BaselineStore::baselinesForMappingV3(const QString &mappingId) const
{
    if (!m_isOpen) {
        return {};
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT record_id, canonical_shape_domain, canonical_shape_encoding, canonical_bytes "
        "FROM blob_baselines_v3 WHERE mapping_id = ?"));
    q.addBindValue(mappingId);
    if (!q.exec()) {
        setError(QStringLiteral("baselinesForMappingV3: %1")
                     .arg(q.lastError().text()));
        return {};
    }
    QList<Kalburator::Shape::CanonicalRecord> out;
    while (q.next()) {
        Kalburator::Shape::CanonicalRecord rec;
        rec.recordId = q.value(0).toString();
        rec.shape    = Kalburator::Shape::Shape{
            Kalburator::Shape::DomainId{q.value(1).toString()},
            Kalburator::Shape::EncodingId{q.value(2).toString()}};
        rec.data = q.value(3).toByteArray();
        out.append(rec);
    }
    return out;
}

bool BaselineStore::removeBaselineV3(const QString &mappingId,
                                          const QString &recordId)
{
    if (!m_isOpen) {
        setError(QStringLiteral("removeBaselineV3: store not open"));
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM blob_baselines_v3 WHERE mapping_id = ? AND record_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);
    if (!q.exec()) {
        setError(QStringLiteral("removeBaselineV3: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

bool BaselineStore::clearMappingV3(const QString &mappingId)
{
    if (!m_isOpen) {
        setError(QStringLiteral("clearMappingV3: store not open"));
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM blob_baselines_v3 WHERE mapping_id = ?"));
    q.addBindValue(mappingId);
    if (!q.exec()) {
        setError(QStringLiteral("clearMappingV3: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

} // namespace Kalburator::Storage
