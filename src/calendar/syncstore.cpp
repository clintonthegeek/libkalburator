#include "syncstore.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDebug>
#include <QFileInfo>
#include <QDir>

namespace Kalburator::Sync {

// Split a compound sync key (uid or uid\0recurrenceId) into uid and recurrence_id parts.
// Matches the syncRecordKey() format from syncdiff.cpp.
static void splitSyncKey(const QString &key, QString &uid, QString &recurrenceId)
{
    int sep = key.indexOf(QChar(0));
    if (sep >= 0) {
        uid = key.left(sep);
        recurrenceId = key.mid(sep + 1);
    } else {
        uid = key;
        recurrenceId = QString();
    }
}

// Build a compound sync key from uid and recurrence_id.
// Matches the syncRecordKey() format from syncdiff.cpp.
static QString buildSyncKey(const QString &uid, const QString &recurrenceId)
{
    if (!recurrenceId.isEmpty())
        return uid + QChar(0) + recurrenceId;
    return uid;
}

int SyncStore::s_connectionCounter = 0;

SyncStore::SyncStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_connectionName(QStringLiteral("SyncStore_%1").arg(++s_connectionCounter))
{
    m_isOpen = initDatabase();
}

SyncStore::~SyncStore()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool SyncStore::isOpen() const
{
    return m_isOpen;
}

QString SyncStore::lastError() const
{
    return m_lastError;
}

bool SyncStore::initDatabase()
{
    // Ensure parent directory exists
    QFileInfo fileInfo(m_dbPath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            setError(QStringLiteral("Failed to create directory: %1").arg(dir.path()));
            return false;
        }
    }

    // Check schema version. If outdated, delete DB file and start fresh.
    // This avoids DROP TABLE + CREATE TABLE races on WAL-mode databases.
    static constexpr int EXPECTED_SCHEMA_VERSION = 6;  // v6: local-fingerprint storage moved to FingerprintStore (LocalBackend-private)

    if (QFile::exists(m_dbPath)) {
        int currentVersion = 0;
        {
            // Use a temporary connection for the version check so we can
            // close it cleanly before deleting the file.
            const QString checkConn = m_connectionName + QStringLiteral("_schema_check");
            {
                QSqlDatabase checkDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), checkConn);
                checkDb.setDatabaseName(m_dbPath);
                if (checkDb.open()) {
                    QSqlQuery q(checkDb);
                    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next()) {
                        currentVersion = q.value(0).toInt();
                    }
                    checkDb.close();
                }
            }
            QSqlDatabase::removeDatabase(checkConn);
        }

        if (currentVersion != EXPECTED_SCHEMA_VERSION) {
            qDebug() << "SyncStore: Schema version" << currentVersion
                     << "!=" << EXPECTED_SCHEMA_VERSION << "- recreating database";
            QFile::remove(m_dbPath);
            QFile::remove(m_dbPath + QStringLiteral("-wal"));
            QFile::remove(m_dbPath + QStringLiteral("-shm"));
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        setError(QStringLiteral("Failed to open database: %1").arg(db.lastError().text()));
        return false;
    }

    // Enable foreign keys and WAL mode for better performance
    QSqlQuery query(db);
    query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    query.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    if (!createTables()) {
        return false;
    }

    // Stamp the schema version
    query.exec(QStringLiteral("PRAGMA user_version = %1").arg(EXPECTED_SCHEMA_VERSION));

    return true;
}

bool SyncStore::createTables()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    // Version tracking table
    if (!query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sync_versions ("
        "  backend_id TEXT NOT NULL,"
        "  calendar_id TEXT NOT NULL,"
        "  uid TEXT NOT NULL,"
        "  recurrence_id TEXT DEFAULT '',"
        "  version_hash TEXT NOT NULL,"
        "  modified_at TEXT DEFAULT (datetime('now')),"
        "  PRIMARY KEY (backend_id, calendar_id, uid, recurrence_id)"
        ")"))) {
        setError(QStringLiteral("Failed to create sync_versions table: %1")
                 .arg(query.lastError().text()));
        return false;
    }

    // Baselines table for 3-way merge
    if (!query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sync_baselines ("
        "  mapping_id TEXT NOT NULL,"
        "  uid TEXT NOT NULL,"
        "  recurrence_id TEXT DEFAULT '',"
        "  ical_data TEXT NOT NULL,"
        "  synced_at TEXT DEFAULT (datetime('now')),"
        "  PRIMARY KEY (mapping_id, uid, recurrence_id)"
        ")"))) {
        setError(QStringLiteral("Failed to create sync_baselines table: %1")
                 .arg(query.lastError().text()));
        return false;
    }

    // Property baselines table for calendar-level properties (color, description)
    if (!query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS property_baselines ("
        "  mapping_id TEXT NOT NULL,"
        "  calendar_id TEXT NOT NULL,"
        "  properties TEXT NOT NULL,"
        "  synced_at TEXT DEFAULT (datetime('now')),"
        "  PRIMARY KEY (mapping_id, calendar_id)"
        ")"))) {
        setError(QStringLiteral("Failed to create property_baselines table: %1")
                 .arg(query.lastError().text()));
        return false;
    }

    // Sync metadata table (last sync times, etc.)
    if (!query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sync_metadata ("
        "  mapping_id TEXT PRIMARY KEY,"
        "  last_sync_time TEXT,"
        "  sync_token TEXT,"
        "  extra_data TEXT"
        ")"))) {
        setError(QStringLiteral("Failed to create sync_metadata table: %1")
                 .arg(query.lastError().text()));
        return false;
    }

    // Conflicts table
    if (!query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sync_conflicts ("
        "  id TEXT PRIMARY KEY,"
        "  mapping_id TEXT,"
        "  backend_id TEXT,"
        "  calendar_id TEXT,"
        "  local_uid TEXT NOT NULL,"
        "  remote_id TEXT,"
        "  conflict_type INTEGER DEFAULT 0,"
        "  local_description TEXT,"
        "  remote_description TEXT,"
        "  local_modified TEXT,"
        "  remote_modified TEXT,"
        "  local_ical TEXT,"
        "  remote_ical TEXT,"
        "  baseline_ical TEXT,"
        "  detected_at TEXT DEFAULT (datetime('now')),"
        "  resolved_at TEXT,"
        "  resolution TEXT"
        ")"))) {
        setError(QStringLiteral("Failed to create sync_conflicts table: %1")
                 .arg(query.lastError().text()));
        return false;
    }

    // Index for unresolved conflicts query
    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_conflicts_unresolved "
        "ON sync_conflicts(mapping_id, resolved_at)"));

    // Schema migration: add new columns if they don't exist (for existing databases)
    // SQLite ignores errors for already-existing columns
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN conflict_type INTEGER DEFAULT 0"));
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN local_ical TEXT"));
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN remote_ical TEXT"));
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN baseline_ical TEXT"));

    return true;
}

void SyncStore::setError(const QString &error)
{
    m_lastError = error;
    qWarning() << "SyncStore error:" << error;
}

// ============================================================================
// Version Tracking
// ============================================================================

QString SyncStore::versionHash(const QString &backendId,
                                const QString &calendarId,
                                const QString &uid) const
{
    if (!m_isOpen) return QString();

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT version_hash FROM sync_versions "
        "WHERE backend_id = ? AND calendar_id = ? AND uid = ? AND recurrence_id = ''"));
    query.addBindValue(backendId);
    query.addBindValue(calendarId);
    query.addBindValue(uid);

    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}

void SyncStore::setVersionHash(const QString &backendId,
                                const QString &calendarId,
                                const QString &uid,
                                const QString &hash)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sync_versions "
        "(backend_id, calendar_id, uid, recurrence_id, version_hash, modified_at) "
        "VALUES (?, ?, ?, '', ?, datetime('now'))"));
    query.addBindValue(backendId);
    query.addBindValue(calendarId);
    query.addBindValue(uid);
    query.addBindValue(hash);

    if (!query.exec()) {
        setError(QStringLiteral("Failed to set version hash: %1").arg(query.lastError().text()));
    }
}

void SyncStore::removeVersionHash(const QString &backendId,
                                   const QString &calendarId,
                                   const QString &uid)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    // Removes all recurrence variants for this uid
    query.prepare(QStringLiteral(
        "DELETE FROM sync_versions "
        "WHERE backend_id = ? AND calendar_id = ? AND uid = ?"));
    query.addBindValue(backendId);
    query.addBindValue(calendarId);
    query.addBindValue(uid);
    if (!query.exec()) {
        qWarning() << "SyncStore::removeVersionHash - failed:" << query.lastError().text();
    }
}

QMap<QString, QString> SyncStore::allVersionHashes(const QString &backendId,
                                                    const QString &calendarId) const
{
    QMap<QString, QString> result;
    if (!m_isOpen) return result;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT uid, version_hash FROM sync_versions "
        "WHERE backend_id = ? AND calendar_id = ?"));
    query.addBindValue(backendId);
    query.addBindValue(calendarId);

    if (query.exec()) {
        while (query.next()) {
            result.insert(query.value(0).toString(), query.value(1).toString());
        }
    }
    return result;
}

void SyncStore::clearVersionHashes(const QString &backendId, const QString &calendarId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    if (calendarId.isEmpty()) {
        query.prepare(QStringLiteral("DELETE FROM sync_versions WHERE backend_id = ?"));
        query.addBindValue(backendId);
    } else {
        query.prepare(QStringLiteral(
            "DELETE FROM sync_versions WHERE backend_id = ? AND calendar_id = ?"));
        query.addBindValue(backendId);
        query.addBindValue(calendarId);
    }
    if (!query.exec()) {
        qWarning() << "SyncStore::clearVersionHashes - failed:" << query.lastError().text();
    }
}

// ============================================================================
// Baseline Storage
// ============================================================================

QString SyncStore::baseline(const QString &mappingId, const QString &uid) const
{
    if (!m_isOpen) return QString();

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT ical_data FROM sync_baselines "
        "WHERE mapping_id = ? AND uid = ? AND recurrence_id = ''"));
    query.addBindValue(mappingId);
    query.addBindValue(uid);

    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}

void SyncStore::setBaseline(const QString &mappingId,
                             const QString &uid,
                             const QString &icalData)
{
    if (!m_isOpen) return;

    // uid parameter may be a compound key (uid\0recurrenceId)
    QString actualUid, recurrenceId;
    splitSyncKey(uid, actualUid, recurrenceId);

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sync_baselines "
        "(mapping_id, uid, recurrence_id, ical_data, synced_at) "
        "VALUES (?, ?, ?, ?, datetime('now'))"));
    query.addBindValue(mappingId);
    query.addBindValue(actualUid);
    query.addBindValue(recurrenceId.isEmpty() ? QStringLiteral("") : recurrenceId);
    query.addBindValue(icalData);

    if (!query.exec()) {
        setError(QStringLiteral("Failed to set baseline: %1").arg(query.lastError().text()));
    }
}

void SyncStore::setBaselines(const QString &mappingId,
                              const QMap<QString, QString> &baselines)
{
    if (!m_isOpen || baselines.isEmpty()) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!db.transaction()) {
        qWarning() << "SyncStore::setBaselines - failed to start transaction";
        return;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sync_baselines "
        "(mapping_id, uid, recurrence_id, ical_data, synced_at) "
        "VALUES (?, ?, ?, ?, datetime('now'))"));

    bool success = true;
    for (auto it = baselines.constBegin(); it != baselines.constEnd(); ++it) {
        // Key may be a compound key (uid\0recurrenceId)
        QString uid, recurrenceId;
        splitSyncKey(it.key(), uid, recurrenceId);

        query.bindValue(0, mappingId);
        query.bindValue(1, uid);
        query.bindValue(2, recurrenceId.isEmpty() ? QStringLiteral("") : recurrenceId);
        query.bindValue(3, it.value());

        if (!query.exec()) {
            qWarning() << "SyncStore::setBaselines - failed to set baseline for"
                       << it.key() << ":" << query.lastError().text();
            success = false;
            break;
        }
    }

    if (success) {
        db.commit();
    } else {
        db.rollback();
    }
}

void SyncStore::removeBaseline(const QString &mappingId, const QString &uid)
{
    if (!m_isOpen) return;

    // uid parameter may be a compound key (uid\0recurrenceId)
    QString actualUid, recurrenceId;
    splitSyncKey(uid, actualUid, recurrenceId);

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "DELETE FROM sync_baselines "
        "WHERE mapping_id = ? AND uid = ? AND recurrence_id = ?"));
    query.addBindValue(mappingId);
    query.addBindValue(actualUid);
    query.addBindValue(recurrenceId.isEmpty() ? QStringLiteral("") : recurrenceId);
    if (!query.exec()) {
        qWarning() << "SyncStore::removeBaseline - failed:" << query.lastError().text();
    }
}

void SyncStore::removeBaselines(const QString &mappingId, const QStringList &uids)
{
    if (!m_isOpen || uids.isEmpty()) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!db.transaction()) {
        qWarning() << "SyncStore::removeBaselines - failed to start transaction";
        return;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "DELETE FROM sync_baselines "
        "WHERE mapping_id = ? AND uid = ? AND recurrence_id = ?"));

    bool success = true;
    for (const QString &key : uids) {
        // key may be a compound key (uid\0recurrenceId)
        QString uid, recurrenceId;
        splitSyncKey(key, uid, recurrenceId);

        query.bindValue(0, mappingId);
        query.bindValue(1, uid);
        query.bindValue(2, recurrenceId.isEmpty() ? QStringLiteral("") : recurrenceId);

        if (!query.exec()) {
            qWarning() << "SyncStore::removeBaselines - failed to remove baseline for"
                       << key << ":" << query.lastError().text();
            success = false;
            break;
        }
    }

    if (success) {
        db.commit();
    } else {
        db.rollback();
    }
}

QMap<QString, QString> SyncStore::allBaselines(const QString &mappingId) const
{
    QMap<QString, QString> result;
    if (!m_isOpen) return result;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT uid, recurrence_id, ical_data FROM sync_baselines WHERE mapping_id = ?"));
    query.addBindValue(mappingId);

    if (query.exec()) {
        while (query.next()) {
            QString uid = query.value(0).toString();
            QString recurrenceId = query.value(1).toString();
            QString key = buildSyncKey(uid, recurrenceId);
            result.insert(key, query.value(2).toString());
        }
    }
    return result;
}

void SyncStore::clearBaselines(const QString &mappingId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM sync_baselines WHERE mapping_id = ?"));
    query.addBindValue(mappingId);
    if (!query.exec()) {
        qWarning() << "SyncStore::clearBaselines - failed:" << query.lastError().text();
    }
}

QDateTime SyncStore::lastSyncTime(const QString &mappingId) const
{
    if (!m_isOpen) return QDateTime();

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT last_sync_time FROM sync_metadata WHERE mapping_id = ?"));
    query.addBindValue(mappingId);

    if (query.exec() && query.next()) {
        QString timeStr = query.value(0).toString();
        if (!timeStr.isEmpty()) {
            return QDateTime::fromString(timeStr, Qt::ISODate);
        }
    }
    return QDateTime();
}

void SyncStore::setLastSyncTime(const QString &mappingId, const QDateTime &time)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sync_metadata (mapping_id, last_sync_time) "
        "VALUES (?, ?)"));
    query.addBindValue(mappingId);
    query.addBindValue(time.toString(Qt::ISODate));

    if (!query.exec()) {
        setError(QStringLiteral("Failed to set last sync time: %1").arg(query.lastError().text()));
    }
}

// ============================================================================
// Property Baselines
// ============================================================================

QString SyncStore::propertyBaseline(const QString &mappingId, const QString &calendarId) const
{
    if (!m_isOpen) return QString();

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT properties FROM property_baselines "
        "WHERE mapping_id = ? AND calendar_id = ?"));
    query.addBindValue(mappingId);
    query.addBindValue(calendarId);

    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}

void SyncStore::setPropertyBaseline(const QString &mappingId,
                                    const QString &calendarId,
                                    const QString &propertiesJson)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO property_baselines "
        "(mapping_id, calendar_id, properties, synced_at) "
        "VALUES (?, ?, ?, datetime('now'))"));
    query.addBindValue(mappingId);
    query.addBindValue(calendarId);
    query.addBindValue(propertiesJson);

    if (!query.exec()) {
        setError(QStringLiteral("Failed to set property baseline: %1").arg(query.lastError().text()));
    }
}

void SyncStore::removePropertyBaseline(const QString &mappingId, const QString &calendarId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "DELETE FROM property_baselines WHERE mapping_id = ? AND calendar_id = ?"));
    query.addBindValue(mappingId);
    query.addBindValue(calendarId);

    if (!query.exec()) {
        qWarning() << "SyncStore::removePropertyBaseline - failed:" << query.lastError().text();
    }
}

QMap<QString, QString> SyncStore::allPropertyBaselines(const QString &mappingId) const
{
    QMap<QString, QString> result;
    if (!m_isOpen) return result;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT calendar_id, properties FROM property_baselines WHERE mapping_id = ?"));
    query.addBindValue(mappingId);

    if (query.exec()) {
        while (query.next()) {
            result.insert(query.value(0).toString(), query.value(1).toString());
        }
    }
    return result;
}

// ============================================================================
// Conflict Tracking
// ============================================================================

QString SyncStore::recordConflict(const ConflictInfo &conflict)
{
    if (!m_isOpen) return QString();

    QString conflictId = conflict.conflictId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : conflict.conflictId;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    // DB columns use local_/remote_ naming for backward compat with existing DBs
    // ConflictInfo fields use source_/target_ naming
    query.prepare(QStringLiteral(
        "INSERT INTO sync_conflicts "
        "(id, mapping_id, backend_id, calendar_id, local_uid, remote_id, "
        " conflict_type, local_description, remote_description, "
        " local_modified, remote_modified, "
        " local_ical, remote_ical, baseline_ical, detected_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))"));
    query.addBindValue(conflictId);
    query.addBindValue(conflict.mappingId.isEmpty() ? QVariant() : conflict.mappingId);
    query.addBindValue(conflict.sourceBackendId);  // Store source backend in backend_id column
    query.addBindValue(conflict.calendarId);
    query.addBindValue(conflict.sourceId);         // local_uid = sourceId
    query.addBindValue(conflict.targetId);         // remote_id = targetId
    query.addBindValue(static_cast<int>(conflict.type));
    query.addBindValue(conflict.sourceDescription);  // local_description = sourceDescription
    query.addBindValue(conflict.targetDescription);  // remote_description = targetDescription
    query.addBindValue(conflict.sourceModified.toString(Qt::ISODate));
    query.addBindValue(conflict.targetModified.toString(Qt::ISODate));
    query.addBindValue(conflict.sourceIcalData.isEmpty() ? QVariant() : conflict.sourceIcalData);
    query.addBindValue(conflict.targetIcalData.isEmpty() ? QVariant() : conflict.targetIcalData);
    query.addBindValue(conflict.baselineIcalData.isEmpty() ? QVariant() : conflict.baselineIcalData);

    if (!query.exec()) {
        setError(QStringLiteral("Failed to record conflict: %1").arg(query.lastError().text()));
        return QString();
    }

    // Create a copy with the generated ID
    ConflictInfo conflictWithId = conflict;
    conflictWithId.conflictId = conflictId;

    emit conflictRecorded(conflictWithId);
    return conflictId;
}

QList<ConflictInfo> SyncStore::unresolvedConflicts(const QString &mappingId) const
{
    QList<ConflictInfo> result;
    if (!m_isOpen) return result;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    // DB columns use local_/remote_ naming for backward compat
    if (mappingId.isEmpty()) {
        query.prepare(QStringLiteral(
            "SELECT id, mapping_id, backend_id, calendar_id, local_uid, remote_id, "
            "       conflict_type, local_description, remote_description, "
            "       local_modified, remote_modified, "
            "       local_ical, remote_ical, baseline_ical, detected_at "
            "FROM sync_conflicts WHERE resolved_at IS NULL "
            "ORDER BY detected_at DESC"));
    } else {
        query.prepare(QStringLiteral(
            "SELECT id, mapping_id, backend_id, calendar_id, local_uid, remote_id, "
            "       conflict_type, local_description, remote_description, "
            "       local_modified, remote_modified, "
            "       local_ical, remote_ical, baseline_ical, detected_at "
            "FROM sync_conflicts WHERE mapping_id = ? AND resolved_at IS NULL "
            "ORDER BY detected_at DESC"));
        query.addBindValue(mappingId);
    }

    if (query.exec()) {
        while (query.next()) {
            ConflictInfo info;
            info.conflictId = query.value(0).toString();
            info.mappingId = query.value(1).toString();
            info.sourceBackendId = query.value(2).toString();  // backend_id = sourceBackendId
            info.calendarId = query.value(3).toString();
            info.sourceId = query.value(4).toString();         // local_uid = sourceId
            info.targetId = query.value(5).toString();         // remote_id = targetId
            info.type = static_cast<ConflictType>(query.value(6).toInt());
            info.sourceDescription = query.value(7).toString();  // local_description = sourceDescription
            info.targetDescription = query.value(8).toString();  // remote_description = targetDescription
            info.sourceModified = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
            info.targetModified = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
            info.sourceIcalData = query.value(11).toString();
            info.targetIcalData = query.value(12).toString();
            info.baselineIcalData = query.value(13).toString();
            info.detectedAt = QDateTime::fromString(query.value(14).toString(), Qt::ISODate);
            result.append(info);
        }
    }
    return result;
}

ConflictInfo SyncStore::conflict(const QString &conflictId) const
{
    ConflictInfo info;
    if (!m_isOpen) return info;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    // DB columns use local_/remote_ naming for backward compat
    query.prepare(QStringLiteral(
        "SELECT id, mapping_id, backend_id, calendar_id, local_uid, remote_id, "
        "       conflict_type, local_description, remote_description, "
        "       local_modified, remote_modified, "
        "       local_ical, remote_ical, baseline_ical, detected_at "
        "FROM sync_conflicts WHERE id = ?"));
    query.addBindValue(conflictId);

    if (query.exec() && query.next()) {
        info.conflictId = query.value(0).toString();
        info.mappingId = query.value(1).toString();
        info.sourceBackendId = query.value(2).toString();  // backend_id = sourceBackendId
        info.calendarId = query.value(3).toString();
        info.sourceId = query.value(4).toString();         // local_uid = sourceId
        info.targetId = query.value(5).toString();         // remote_id = targetId
        info.type = static_cast<ConflictType>(query.value(6).toInt());
        info.sourceDescription = query.value(7).toString();  // local_description = sourceDescription
        info.targetDescription = query.value(8).toString();  // remote_description = targetDescription
        info.sourceModified = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
        info.targetModified = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
        info.sourceIcalData = query.value(11).toString();
        info.targetIcalData = query.value(12).toString();
        info.baselineIcalData = query.value(13).toString();
        info.detectedAt = QDateTime::fromString(query.value(14).toString(), Qt::ISODate);
    }
    return info;
}

void SyncStore::resolveConflict(const QString &conflictId, ConflictResolution resolution)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE sync_conflicts SET resolved_at = datetime('now'), resolution = ? "
        "WHERE id = ?"));
    query.addBindValue(static_cast<int>(resolution));
    query.addBindValue(conflictId);

    if (query.exec()) {
        emit conflictResolved(conflictId);
    } else {
        setError(QStringLiteral("Failed to resolve conflict: %1").arg(query.lastError().text()));
    }
}

void SyncStore::removeConflict(const QString &conflictId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM sync_conflicts WHERE id = ?"));
    query.addBindValue(conflictId);
    if (!query.exec()) {
        qWarning() << "SyncStore::removeConflict - failed:" << query.lastError().text();
    }
}

int SyncStore::unresolvedConflictCount(const QString &mappingId) const
{
    if (!m_isOpen) return 0;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    if (mappingId.isEmpty()) {
        query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM sync_conflicts WHERE resolved_at IS NULL"));
    } else {
        query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM sync_conflicts "
            "WHERE mapping_id = ? AND resolved_at IS NULL"));
        query.addBindValue(mappingId);
    }

    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

// ============================================================================
// Database Maintenance
// ============================================================================

void SyncStore::vacuum()
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("VACUUM"))) {
        qWarning() << "SyncStore::vacuum - failed:" << query.lastError().text();
    }
}

void SyncStore::clearBackendData(const QString &backendId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!db.transaction()) {
        qWarning() << "SyncStore::clearBackendData - failed to start transaction";
        return;
    }

    QSqlQuery query(db);
    bool success = true;

    // Remove version hashes
    query.prepare(QStringLiteral("DELETE FROM sync_versions WHERE backend_id = ?"));
    query.addBindValue(backendId);
    if (!query.exec()) {
        qWarning() << "SyncStore::clearBackendData - failed to clear versions:" << query.lastError().text();
        success = false;
    }

    // Remove conflicts for this backend
    if (success) {
        query.prepare(QStringLiteral("DELETE FROM sync_conflicts WHERE backend_id = ?"));
        query.addBindValue(backendId);
        if (!query.exec()) {
            qWarning() << "SyncStore::clearBackendData - failed to clear conflicts:" << query.lastError().text();
            success = false;
        }
    }

    if (success) {
        db.commit();
    } else {
        db.rollback();
    }
}

void SyncStore::clearMappingData(const QString &mappingId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!db.transaction()) {
        qWarning() << "SyncStore::clearMappingData - failed to start transaction";
        return;
    }

    QSqlQuery query(db);
    bool success = true;

    // Remove baselines
    query.prepare(QStringLiteral("DELETE FROM sync_baselines WHERE mapping_id = ?"));
    query.addBindValue(mappingId);
    if (!query.exec()) {
        qWarning() << "SyncStore::clearMappingData - failed to clear baselines:" << query.lastError().text();
        success = false;
    }

    // Remove property baselines
    if (success) {
        query.prepare(QStringLiteral("DELETE FROM property_baselines WHERE mapping_id = ?"));
        query.addBindValue(mappingId);
        if (!query.exec()) {
            qWarning() << "SyncStore::clearMappingData - failed to clear property baselines:" << query.lastError().text();
            success = false;
        }
    }

    // Remove metadata
    if (success) {
        query.prepare(QStringLiteral("DELETE FROM sync_metadata WHERE mapping_id = ?"));
        query.addBindValue(mappingId);
        if (!query.exec()) {
            qWarning() << "SyncStore::clearMappingData - failed to clear metadata:" << query.lastError().text();
            success = false;
        }
    }

    // Remove conflicts for this mapping
    if (success) {
        query.prepare(QStringLiteral("DELETE FROM sync_conflicts WHERE mapping_id = ?"));
        query.addBindValue(mappingId);
        if (!query.exec()) {
            qWarning() << "SyncStore::clearMappingData - failed to clear conflicts:" << query.lastError().text();
            success = false;
        }
    }

    if (success) {
        db.commit();
    } else {
        db.rollback();
    }
}


} // namespace Kalburator::Sync
