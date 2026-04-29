#include "idmappingstore.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace Kalburator::Sync {

namespace {

// DB schema version for sync_id_mappings table.
constexpr int kSchemaVersion = 3;

QString encodeCategories(const QStringList &categories)
{
    QJsonArray arr;
    for (const QString &c : categories) {
        arr.append(c);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QStringList decodeCategories(const QString &encoded)
{
    if (encoded.isEmpty()) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    if (!doc.isArray()) {
        return {};
    }
    QStringList out;
    const QJsonArray arr = doc.array();
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        out.append(v.toString());
    }
    return out;
}

QDateTime decodeDateTime(const QVariant &v)
{
    if (v.isNull() || !v.isValid()) {
        return {};
    }
    return QDateTime::fromString(v.toString(), Qt::ISODate);
}

// SQLite's handling of NULL in PK columns and WHERE equality is a
// landmine: a null QString binds as SQL NULL, and `col = NULL` never
// matches (SQL three-valued logic). The existing sync_id_mappings
// schema has `recurrence_id TEXT DEFAULT ''`; to match on the empty
// case we must bind an empty-but-not-null string rather than a null
// QString (null vs empty distinction).
inline QString normRec(const QString &r)
{
    return r.isNull() ? QStringLiteral("") : r;
}

} // namespace

int IDMappingStore::s_connectionCounter = 0;

IDMappingStore::IDMappingStore(const QString &dbPath)
    : m_dbPath(dbPath)
    , m_connName(QStringLiteral("IDMappingStore_%1")
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

IDMappingStore::~IDMappingStore()
{
    if (QSqlDatabase::contains(m_connName)) {
        QSqlDatabase::database(m_connName).close();
        QSqlDatabase::removeDatabase(m_connName);
    }
}

bool IDMappingStore::isOpen() const
{
    return m_isOpen;
}

QString IDMappingStore::lastError() const
{
    return m_lastError;
}

QString IDMappingStore::databasePath() const
{
    return m_dbPath;
}

void IDMappingStore::setError(const QString &message) const
{
    m_lastError = message;
}

bool IDMappingStore::ensureSchemaAndVersion(bool dbFileExistedBefore)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    // Base table.
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sync_id_mappings ("
            "  backend_id TEXT NOT NULL,"
            "  local_uid TEXT NOT NULL,"
            "  recurrence_id TEXT DEFAULT '',"
            "  remote_id TEXT NOT NULL,"
            "  calendar_id TEXT,"
            "  created_at TEXT DEFAULT (datetime('now')),"
            "  PRIMARY KEY (backend_id, local_uid, recurrence_id)"
            ")"))) {
        setError(QStringLiteral("CREATE TABLE failed: %1")
                     .arg(q.lastError().text()));
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_id_mappings_remote "
        "ON sync_id_mappings(backend_id, remote_id)"));

    // Four additive columns for the merged Audit 2 schema.
    if (!ensureColumn(QStringLiteral("last_synced"),
                      QStringLiteral("TEXT"))) return false;
    if (!ensureColumn(QStringLiteral("source_category"),
                      QStringLiteral("TEXT"))) return false;
    if (!ensureColumn(QStringLiteral("target_categories"),
                      QStringLiteral("TEXT"))) return false;
    if (!ensureColumn(QStringLiteral("archived"),
                      QStringLiteral("INTEGER DEFAULT 0"))) return false;

    // Stamp PRAGMA user_version = 3 only if this is a freshly-created DB.
    // If the file already existed, leave the version to SyncStore's policy.
    if (!dbFileExistedBefore) {
        q.exec(QStringLiteral("PRAGMA user_version = %1")
                   .arg(kSchemaVersion));
    }

    return true;
}

bool IDMappingStore::ensureColumn(const QString &column,
                                  const QString &ddl)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery probe(db);

    if (!probe.exec(QStringLiteral("PRAGMA table_info(sync_id_mappings)"))) {
        setError(QStringLiteral("PRAGMA table_info failed: %1")
                     .arg(probe.lastError().text()));
        return false;
    }
    while (probe.next()) {
        if (probe.value(1).toString() == column) {
            return true; // already present
        }
    }

    QSqlQuery alter(db);
    const QString sql = QStringLiteral(
        "ALTER TABLE sync_id_mappings ADD COLUMN %1 %2")
            .arg(column, ddl);
    if (!alter.exec(sql)) {
        setError(QStringLiteral("ALTER TABLE failed for %1: %2")
                     .arg(column, alter.lastError().text()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// Primary lookup API
// ---------------------------------------------------------------------

QString IDMappingStore::targetIdForSourceUid(const QString &backendId,
                                             const QString &sourceUid,
                                             const QString &recurrenceId) const
{
    if (!m_isOpen) return {};

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT remote_id FROM sync_id_mappings "
        "WHERE backend_id = ? AND local_uid = ? AND recurrence_id = ?"));
    q.addBindValue(backendId);
    q.addBindValue(sourceUid);
    q.addBindValue(normRec(recurrenceId));

    if (!q.exec()) {
        setError(QStringLiteral("targetIdForSourceUid failed: %1")
                     .arg(q.lastError().text()));
        return {};
    }
    if (q.next()) {
        return q.value(0).toString();
    }
    return {};
}

QString IDMappingStore::sourceUidForTargetId(const QString &backendId,
                                             const QString &targetId) const
{
    if (!m_isOpen) return {};

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT local_uid FROM sync_id_mappings "
        "WHERE backend_id = ? AND remote_id = ? "
        "LIMIT 1"));
    q.addBindValue(backendId);
    q.addBindValue(targetId);

    if (!q.exec()) {
        setError(QStringLiteral("sourceUidForTargetId failed: %1")
                     .arg(q.lastError().text()));
        return {};
    }
    if (q.next()) {
        return q.value(0).toString();
    }
    return {};
}

void IDMappingStore::setIdMapping(const QString &backendId,
                                  const QString &sourceUid,
                                  const QString &recurrenceId,
                                  const QString &targetId,
                                  const QString &calendarId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sync_id_mappings "
        "(backend_id, local_uid, recurrence_id, remote_id, calendar_id, "
        " created_at, last_synced) "
        "VALUES (?, ?, ?, ?, ?, datetime('now'), ?)"));
    q.addBindValue(backendId);
    q.addBindValue(sourceUid);
    q.addBindValue(normRec(recurrenceId));
    q.addBindValue(targetId);
    q.addBindValue(calendarId.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                        : QVariant(calendarId));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!q.exec()) {
        setError(QStringLiteral("setIdMapping failed: %1")
                     .arg(q.lastError().text()));
    }
}

void IDMappingStore::removeIdMapping(const QString &backendId,
                                     const QString &sourceUid,
                                     const QString &recurrenceId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM sync_id_mappings "
        "WHERE backend_id = ? AND local_uid = ? AND recurrence_id = ?"));
    q.addBindValue(backendId);
    q.addBindValue(sourceUid);
    q.addBindValue(normRec(recurrenceId));

    if (!q.exec()) {
        setError(QStringLiteral("removeIdMapping failed: %1")
                     .arg(q.lastError().text()));
    }
}

void IDMappingStore::clearIdMappings(const QString &backendId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM sync_id_mappings WHERE backend_id = ?"));
    q.addBindValue(backendId);

    if (!q.exec()) {
        setError(QStringLiteral("clearIdMappings failed: %1")
                     .arg(q.lastError().text()));
    }
}

QList<IDMapping> IDMappingStore::allMappings(const QString &backendId) const
{
    QList<IDMapping> out;
    if (!m_isOpen) return out;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT backend_id, local_uid, recurrence_id, remote_id, "
        "       calendar_id, last_synced, source_category, "
        "       target_categories, archived "
        "FROM sync_id_mappings WHERE backend_id = ?"));
    q.addBindValue(backendId);

    if (!q.exec()) {
        setError(QStringLiteral("allMappings failed: %1")
                     .arg(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        IDMapping m;
        m.backendId        = q.value(0).toString();
        m.sourceUid        = q.value(1).toString();
        m.recurrenceId     = q.value(2).toString();
        m.targetId         = q.value(3).toString();
        m.calendarId       = q.value(4).toString();
        m.lastSynced       = decodeDateTime(q.value(5));
        m.sourceCategory   = q.value(6).toString();
        m.targetCategories = decodeCategories(q.value(7).toString());
        m.archived         = q.value(8).toInt() != 0;
        out.append(m);
    }
    return out;
}

IDMapping IDMappingStore::getMapping(const QString &backendId,
                                     const QString &sourceUid,
                                     const QString &recurrenceId) const
{
    IDMapping m;
    if (!m_isOpen) return m;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT backend_id, local_uid, recurrence_id, remote_id, "
        "       calendar_id, last_synced, source_category, "
        "       target_categories, archived "
        "FROM sync_id_mappings "
        "WHERE backend_id = ? AND local_uid = ? AND recurrence_id = ?"));
    q.addBindValue(backendId);
    q.addBindValue(sourceUid);
    q.addBindValue(normRec(recurrenceId));

    if (!q.exec()) {
        setError(QStringLiteral("getMapping failed: %1")
                     .arg(q.lastError().text()));
        return m;
    }
    if (q.next()) {
        m.backendId        = q.value(0).toString();
        m.sourceUid        = q.value(1).toString();
        m.recurrenceId     = q.value(2).toString();
        m.targetId         = q.value(3).toString();
        m.calendarId       = q.value(4).toString();
        m.lastSynced       = decodeDateTime(q.value(5));
        m.sourceCategory   = q.value(6).toString();
        m.targetCategories = decodeCategories(q.value(7).toString());
        m.archived         = q.value(8).toInt() != 0;
    }
    return m;
}

void IDMappingStore::updateCategories(const QString &backendId,
                                      const QString &sourceUid,
                                      const QString &recurrenceId,
                                      const QString &sourceCategory,
                                      const QStringList &targetCategories)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_id_mappings "
        "SET source_category = ?, target_categories = ? "
        "WHERE backend_id = ? AND local_uid = ? AND recurrence_id = ?"));
    q.addBindValue(sourceCategory);
    q.addBindValue(encodeCategories(targetCategories));
    q.addBindValue(backendId);
    q.addBindValue(sourceUid);
    q.addBindValue(normRec(recurrenceId));

    if (!q.exec()) {
        setError(QStringLiteral("updateCategories failed: %1")
                     .arg(q.lastError().text()));
    }
}

void IDMappingStore::setArchived(const QString &backendId,
                                 const QString &sourceUid,
                                 const QString &recurrenceId,
                                 bool archived)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_id_mappings SET archived = ? "
        "WHERE backend_id = ? AND local_uid = ? AND recurrence_id = ?"));
    q.addBindValue(archived ? 1 : 0);
    q.addBindValue(backendId);
    q.addBindValue(sourceUid);
    q.addBindValue(normRec(recurrenceId));

    if (!q.exec()) {
        setError(QStringLiteral("setArchived failed: %1")
                     .arg(q.lastError().text()));
    }
}

} // namespace Kalburator::Sync
