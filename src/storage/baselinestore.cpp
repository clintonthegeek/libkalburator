#include "baselinestore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
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
constexpr int kSchemaVersion = 7;  // H3: sync_tokens table (engine-owned sync-progress tokens).
} // namespace

int BaselineStore::s_connectionCounter = 0;

BaselineStore::BaselineStore(const QString &dbPath)
    : m_dbPath(dbPath)
    , m_connName(QStringLiteral("KalburatorBaselineStore_%1")
                     .arg(++s_connectionCounter))
{
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

    if (!ensureSchemaAndVersion()) {
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

bool BaselineStore::ensureSchemaAndVersion()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    // Schema bootstrap (post-fanout-collapse Task 3.1):
    //   - The pre-collapse triple-keyed blob_baselines table was deleted
    //     along with its public API; there is nothing to rename here.
    //   - The mapping-keyed blob_baselines_v3 table + sync_tokens +
    //     collection_baselines + mapping_metadata + per-side hash
    //     columns are created idempotently below (all CREATE TABLE /
    //     COLUMN IF NOT EXISTS).
    //   - Any pre-collapse DB with blob_baselines (triple-keyed) is
    //     intentionally NOT migrated: the campaign's locked
    //     "break + recreate" decision removes the migration reach
    //     (spec §A/B). Already-migrated (v3-only) DBs are unchanged.

    // G.4: ensure v3 mapping-keyed table exists.
    if (!ensureSchemaV3()) {
        return false;
    }

    // K.5: ensure collection_baselines + mapping_metadata tables exist.
    if (!ensureSchemaV5()) {
        return false;
    }

    // B4: ensure blob_baselines_v3 has the per-side hash columns.
    if (!ensureSchemaV6()) {
        return false;
    }

    // H3: ensure sync_tokens table exists.
    if (!ensureSchemaV7()) {
        return false;
    }

    // Single final user_version stamp for the full migration arc.
    // ensureSchemaV3/V5/V6 only create tables/columns; they do not stamp
    // user_version. This stamp covers everything up to kSchemaVersion.
    {
        QSqlQuery sq(db);
        sq.exec(QStringLiteral("PRAGMA user_version"));
        const int userVersion = sq.next() ? sq.value(0).toInt() : 0;
        if (userVersion < kSchemaVersion) {
            QSqlQuery stampQ(db);
            stampQ.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
        }
    }

    return true;
}

bool BaselineStore::ensureSchemaV3()
{
    // Idempotent table creation (CREATE TABLE IF NOT EXISTS). No data
    // migration code path remains — the pre-collapse v2→v3 migration
    // was deleted along with the triple-keyed API in fanout-collapse
    // Task 3.1 (spec §B).
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

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS mapping_metadata ("
            "  mapping_id      TEXT NOT NULL PRIMARY KEY,"
            "  last_sync_at    INTEGER"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE mapping_metadata failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }

    return true;
}

bool BaselineStore::ensureSchemaV6()
{
    // B4 (N2 fix): add nullable source_hash/target_hash columns to
    // blob_baselines_v3. SQLite has no "ADD COLUMN IF NOT EXISTS", so probe
    // the column set via PRAGMA table_info first — idempotent, safe on every
    // open. Existing rows keep NULL in the new columns; the fallback that
    // treats a legacy single-hash row as "both sides equal" lives in
    // baselineHashesV4()/baselineHashesForMappingV4(), not here.
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    bool hasSourceHash = false;
    bool hasTargetHash = false;
    {
        QSqlQuery info(db);
        if (!info.exec(QStringLiteral("PRAGMA table_info(blob_baselines_v3)"))) {
            setError(QStringLiteral("ensureSchemaV6: table_info failed: %1")
                         .arg(info.lastError().text()));
            return false;
        }
        while (info.next()) {
            const QString colName = info.value(1).toString();
            if (colName == QLatin1String("source_hash")) hasSourceHash = true;
            if (colName == QLatin1String("target_hash")) hasTargetHash = true;
        }
    }

    QSqlQuery q(db);
    if (!hasSourceHash) {
        if (!q.exec(QStringLiteral(
                "ALTER TABLE blob_baselines_v3 ADD COLUMN source_hash TEXT"))) {
            setError(QStringLiteral("ensureSchemaV6: ADD COLUMN source_hash failed: %1")
                         .arg(q.lastError().text()));
            return false;
        }
    }
    if (!hasTargetHash) {
        if (!q.exec(QStringLiteral(
                "ALTER TABLE blob_baselines_v3 ADD COLUMN target_hash TEXT"))) {
            setError(QStringLiteral("ensureSchemaV6: ADD COLUMN target_hash failed: %1")
                         .arg(q.lastError().text()));
            return false;
        }
    }

    return true;
}

bool BaselineStore::ensureSchemaV7()
{
    // H3: engine-owned sync-progress tokens, keyed per (mappingId, side).
    // Additive table, idempotent CREATE TABLE IF NOT EXISTS — no data
    // migration needed (a missing row just means "no skip", same as
    // today's absent-baseline behavior).
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sync_tokens ("
            "  mapping_id TEXT NOT NULL,"
            "  side       TEXT NOT NULL CHECK(side IN ('source','target')),"
            "  token      TEXT NOT NULL,"
            "  PRIMARY KEY (mapping_id, side)"
            ")"))) {
        setError(QStringLiteral("ensureSchemaV7: CREATE TABLE sync_tokens failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    return true;
}

// ===========================================================================
// Sync-progress tokens (H3, schema v7)
// ===========================================================================

QString BaselineStore::syncToken(const QString &mappingId, const QString &side) const
{
    if (!m_isOpen) return {};
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT token FROM sync_tokens WHERE mapping_id = ? AND side = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(side);
    if (!q.exec() || !q.next()) return {};
    return q.value(0).toString();
}

void BaselineStore::setSyncToken(const QString &mappingId, const QString &side,
                                 const QString &token)
{
    if (!m_isOpen) {
        setError(QStringLiteral("setSyncToken: store not open"));
        return;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sync_tokens (mapping_id, side, token) "
        "VALUES (?, ?, ?)"));
    q.addBindValue(mappingId);
    q.addBindValue(side);
    q.addBindValue(token);
    if (!q.exec()) {
        setError(QStringLiteral("setSyncToken: %1").arg(q.lastError().text()));
    }
}

void BaselineStore::clearSyncTokens(const QString &mappingId)
{
    if (!m_isOpen) {
        setError(QStringLiteral("clearSyncTokens: store not open"));
        return;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM sync_tokens WHERE mapping_id = ?"));
    q.addBindValue(mappingId);
    if (!q.exec()) {
        setError(QStringLiteral("clearSyncTokens: %1").arg(q.lastError().text()));
    }
}

// ===========================================================================
// Per-side baseline hashes (Phase B4 / N2 fix, schema v6)
// ===========================================================================

bool BaselineStore::setBaselineHashesV4(const QString &mappingId,
                                        const QString &recordId,
                                        const QString &sourceHash,
                                        const QString &targetHash)
{
    if (!m_isOpen) {
        setError(QStringLiteral("setBaselineHashesV4: store not open"));
        return false;
    }
    // Shares blob_baselines_v3 with setBaselineV3()/baselineV3(); the
    // domain/encoding/canonical_bytes columns are NOT NULL so we stamp them
    // with the same "blob"/"raw" marker the unified engine's steady-state
    // save path has always used for hash-only baseline rows (see
    // syncengine.cpp) — canonical_bytes is left empty since the real payload
    // now lives in source_hash/target_hash.
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blob_baselines_v3 "
        "(mapping_id, record_id, canonical_shape_domain, canonical_shape_encoding, "
        " canonical_bytes, source_hash, target_hash, updated_at) "
        "VALUES (?, ?, 'blob', 'raw', ?, ?, ?, ?)"));
    // A default-constructed QByteArray() is *null*, which Qt's SQLite
    // driver binds as SQL NULL — violating canonical_bytes's NOT NULL
    // constraint. Bind a non-null, empty byte array instead (the real
    // payload for these rows lives in source_hash/target_hash).
    static const QByteArray kEmptyNotNull(QByteArrayLiteral(""));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);
    q.addBindValue(kEmptyNotNull);
    q.addBindValue(sourceHash);
    q.addBindValue(targetHash);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) {
        setError(QStringLiteral("setBaselineHashesV4: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

std::optional<BaselineStore::BaselineHashes>
BaselineStore::baselineHashesV4(const QString &mappingId, const QString &recordId) const
{
    if (!m_isOpen) {
        return std::nullopt;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT canonical_shape_domain, canonical_bytes, source_hash, target_hash "
        "FROM blob_baselines_v3 WHERE mapping_id = ? AND record_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(recordId);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    const QString domain     = q.value(0).toString();
    const QByteArray bytes   = q.value(1).toByteArray();
    const QVariant sourceVal = q.value(2);
    const QVariant targetVal = q.value(3);

    if (sourceVal.isNull() || targetVal.isNull()) {
        // Legacy pre-B4 row: only a single hash was ever stored (in
        // canonical_bytes, under domain "blob"). Treat it as both sides'
        // hash so the next diff behaves exactly as it did before this
        // migration; the next successful sync overwrites this row with a
        // proper per-side pair. Non-"blob" legacy rows (e.g. the
        // pre-unified calendar/ical baseline shape) are not hash rows at
        // all — report "no baseline" rather than misreading ical text as
        // a hash.
        if (domain != QLatin1String("blob")) {
            return std::nullopt;
        }
        BaselineHashes h;
        h.recordId   = recordId;
        h.sourceHash = QString::fromUtf8(bytes);
        h.targetHash = h.sourceHash;
        return h;
    }

    BaselineHashes h;
    h.recordId   = recordId;
    h.sourceHash = sourceVal.toString();
    h.targetHash = targetVal.toString();
    return h;
}

QList<BaselineStore::BaselineHashes>
BaselineStore::baselineHashesForMappingV4(const QString &mappingId) const
{
    QList<BaselineHashes> out;
    if (!m_isOpen) {
        return out;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT record_id, canonical_shape_domain, canonical_bytes, source_hash, target_hash "
        "FROM blob_baselines_v3 WHERE mapping_id = ?"));
    q.addBindValue(mappingId);
    if (!q.exec()) {
        setError(QStringLiteral("baselineHashesForMappingV4: %1").arg(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        const QString recordId   = q.value(0).toString();
        const QString domain     = q.value(1).toString();
        const QByteArray bytes   = q.value(2).toByteArray();
        const QVariant sourceVal = q.value(3);
        const QVariant targetVal = q.value(4);

        if (sourceVal.isNull() || targetVal.isNull()) {
            if (domain != QLatin1String("blob")) {
                continue;  // legacy non-hash row (e.g. calendar/ical) — not ours
            }
            BaselineHashes h;
            h.recordId   = recordId;
            h.sourceHash = QString::fromUtf8(bytes);
            h.targetHash = h.sourceHash;
            out.append(h);
            continue;
        }

        BaselineHashes h;
        h.recordId   = recordId;
        h.sourceHash = sourceVal.toString();
        h.targetHash = targetVal.toString();
        out.append(h);
    }
    return out;
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
// Mapping-metadata API (K.5, schema v5)
// ===========================================================================

bool BaselineStore::setLastSyncTime(const QString &mappingId, const QDateTime &when) {
    if (!m_isOpen) {
        setError(QStringLiteral("setLastSyncTime: store not open"));
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO mapping_metadata (mapping_id, last_sync_at) "
        "VALUES (?, ?)"));
    q.addBindValue(mappingId);
    q.addBindValue(when.toSecsSinceEpoch());
    if (!q.exec()) {
        setError(QStringLiteral("setLastSyncTime: %1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

QDateTime BaselineStore::lastSyncTime(const QString &mappingId) const {
    if (!m_isOpen) return {};
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT last_sync_at FROM mapping_metadata WHERE mapping_id = ?"));
    q.addBindValue(mappingId);
    if (!q.exec() || !q.next()) return {};
    const QVariant v = q.value(0);
    if (v.isNull()) return {};
    return QDateTime::fromSecsSinceEpoch(v.toLongLong());
}

// ===========================================================================
// v3 mapping-keyed API (the only persistent API post fanout-collapse Task 3.1)
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
    // H3/CP-A: a mapping-scoped baseline wipe must also drop its sync
    // tokens — otherwise a surviving token could let the next sync skip
    // a mapping with no baselines at all (the exact hole H3 closes).
    clearSyncTokens(mappingId);
    return true;
}

} // namespace Kalburator::Storage
