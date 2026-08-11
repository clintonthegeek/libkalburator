// src/calendar/syncconflictstore.cpp
#include "syncconflictstore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QFile>

namespace Kalburator::Sync {

int SyncConflictStore::s_connectionCounter = 0;

SyncConflictStore::SyncConflictStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_connectionName(QStringLiteral("SyncConflictStore_%1").arg(++s_connectionCounter))
{
    m_isOpen = initDatabase();
}

SyncConflictStore::~SyncConflictStore()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool SyncConflictStore::isOpen() const
{
    return m_isOpen;
}

QString SyncConflictStore::lastError() const
{
    return m_lastError;
}

bool SyncConflictStore::initDatabase()
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

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        setError(QStringLiteral("Failed to open database: %1").arg(db.lastError().text()));
        return false;
    }

    QSqlQuery query(db);
    query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    query.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    return createTables();
}

bool SyncConflictStore::createTables()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

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

    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_conflicts_unresolved "
        "ON sync_conflicts(mapping_id, resolved_at)"));

    // Schema migration: add columns if they don't exist
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN conflict_type INTEGER DEFAULT 0"));
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN local_ical TEXT"));
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN remote_ical TEXT"));
    query.exec(QStringLiteral("ALTER TABLE sync_conflicts ADD COLUMN baseline_ical TEXT"));

    return true;
}

void SyncConflictStore::setError(const QString &error)
{
    m_lastError = error;
    qWarning() << "SyncConflictStore error:" << error;
}

// ============================================================================
// Conflict CRUD
// ============================================================================

QString SyncConflictStore::recordConflict(const ConflictInfo &conflict)
{
    if (!m_isOpen) return QString();

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    // The engine re-presents an unresolved conflict every sync cycle with a
    // fresh ConflictInfo (conflictId always empty), so without this lookup
    // every presentation would INSERT a new row for the same logical
    // conflict (docs/bugs/sync-conflict-store-duplicate-rows.md). Identity
    // is (mapping_id, local_uid) among still-unresolved rows; "IS" (not "=")
    // so a NULL mapping_id (empty mappingId) matches itself.
    QString conflictId;
    if (conflict.conflictId.isEmpty()) {
        QSqlQuery findQuery(db);
        findQuery.prepare(QStringLiteral(
            "SELECT id FROM sync_conflicts "
            "WHERE mapping_id IS ? AND local_uid = ? AND resolved_at IS NULL "
            "LIMIT 1"));
        findQuery.addBindValue(conflict.mappingId.isEmpty() ? QVariant() : conflict.mappingId);
        findQuery.addBindValue(conflict.sourceId);
        if (findQuery.exec() && findQuery.next()) {
            conflictId = findQuery.value(0).toString();
        }
    } else {
        conflictId = conflict.conflictId;
    }

    QSqlQuery query(db);
    if (!conflictId.isEmpty() && conflict.conflictId.isEmpty()) {
        // Refresh the existing unresolved row found above.
        query.prepare(QStringLiteral(
            "UPDATE sync_conflicts SET "
            " backend_id = ?, calendar_id = ?, remote_id = ?, "
            " conflict_type = ?, local_description = ?, remote_description = ?, "
            " local_modified = ?, remote_modified = ?, "
            " local_ical = ?, remote_ical = ?, baseline_ical = ?, "
            " detected_at = datetime('now') "
            "WHERE id = ?"));
        query.addBindValue(conflict.sourceBackendId);
        query.addBindValue(conflict.calendarId);
        query.addBindValue(conflict.targetId);
        query.addBindValue(static_cast<int>(conflict.type));
        query.addBindValue(conflict.sourceDescription);
        query.addBindValue(conflict.targetDescription);
        query.addBindValue(conflict.sourceModified.toString(Qt::ISODate));
        query.addBindValue(conflict.targetModified.toString(Qt::ISODate));
        query.addBindValue(conflict.sourceIcalData.isEmpty() ? QVariant() : conflict.sourceIcalData);
        query.addBindValue(conflict.targetIcalData.isEmpty() ? QVariant() : conflict.targetIcalData);
        query.addBindValue(conflict.baselineIcalData.isEmpty() ? QVariant() : conflict.baselineIcalData);
        query.addBindValue(conflictId);

        if (!query.exec()) {
            setError(QStringLiteral("Failed to refresh conflict: %1").arg(query.lastError().text()));
            return QString();
        }
    } else {
        if (conflictId.isEmpty())
            conflictId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        query.prepare(QStringLiteral(
            "INSERT INTO sync_conflicts "
            "(id, mapping_id, backend_id, calendar_id, local_uid, remote_id, "
            " conflict_type, local_description, remote_description, "
            " local_modified, remote_modified, "
            " local_ical, remote_ical, baseline_ical, detected_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))"));
        query.addBindValue(conflictId);
        query.addBindValue(conflict.mappingId.isEmpty() ? QVariant() : conflict.mappingId);
        query.addBindValue(conflict.sourceBackendId);
        query.addBindValue(conflict.calendarId);
        query.addBindValue(conflict.sourceId);
        query.addBindValue(conflict.targetId);
        query.addBindValue(static_cast<int>(conflict.type));
        query.addBindValue(conflict.sourceDescription);
        query.addBindValue(conflict.targetDescription);
        query.addBindValue(conflict.sourceModified.toString(Qt::ISODate));
        query.addBindValue(conflict.targetModified.toString(Qt::ISODate));
        query.addBindValue(conflict.sourceIcalData.isEmpty() ? QVariant() : conflict.sourceIcalData);
        query.addBindValue(conflict.targetIcalData.isEmpty() ? QVariant() : conflict.targetIcalData);
        query.addBindValue(conflict.baselineIcalData.isEmpty() ? QVariant() : conflict.baselineIcalData);

        if (!query.exec()) {
            setError(QStringLiteral("Failed to record conflict: %1").arg(query.lastError().text()));
            return QString();
        }
    }

    ConflictInfo conflictWithId = conflict;
    conflictWithId.conflictId = conflictId;
    emit conflictRecorded(conflictWithId);
    return conflictId;
}

QList<ConflictInfo> SyncConflictStore::unresolvedConflicts(const QString &mappingId) const
{
    QList<ConflictInfo> result;
    if (!m_isOpen) return result;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

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
            info.conflictId        = query.value(0).toString();
            info.mappingId         = query.value(1).toString();
            info.sourceBackendId   = query.value(2).toString();
            info.calendarId        = query.value(3).toString();
            info.sourceId          = query.value(4).toString();
            info.targetId          = query.value(5).toString();
            info.type              = static_cast<ConflictType>(query.value(6).toInt());
            info.sourceDescription = query.value(7).toString();
            info.targetDescription = query.value(8).toString();
            info.sourceModified    = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
            info.targetModified    = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
            info.sourceIcalData    = query.value(11).toString();
            info.targetIcalData    = query.value(12).toString();
            info.baselineIcalData  = query.value(13).toString();
            info.detectedAt        = QDateTime::fromString(query.value(14).toString(), Qt::ISODate);
            result.append(info);
        }
    }
    return result;
}

ConflictInfo SyncConflictStore::conflict(const QString &conflictId) const
{
    ConflictInfo info;
    if (!m_isOpen) return info;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, mapping_id, backend_id, calendar_id, local_uid, remote_id, "
        "       conflict_type, local_description, remote_description, "
        "       local_modified, remote_modified, "
        "       local_ical, remote_ical, baseline_ical, detected_at "
        "FROM sync_conflicts WHERE id = ?"));
    query.addBindValue(conflictId);

    if (query.exec() && query.next()) {
        info.conflictId        = query.value(0).toString();
        info.mappingId         = query.value(1).toString();
        info.sourceBackendId   = query.value(2).toString();
        info.calendarId        = query.value(3).toString();
        info.sourceId          = query.value(4).toString();
        info.targetId          = query.value(5).toString();
        info.type              = static_cast<ConflictType>(query.value(6).toInt());
        info.sourceDescription = query.value(7).toString();
        info.targetDescription = query.value(8).toString();
        info.sourceModified    = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
        info.targetModified    = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
        info.sourceIcalData    = query.value(11).toString();
        info.targetIcalData    = query.value(12).toString();
        info.baselineIcalData  = query.value(13).toString();
        info.detectedAt        = QDateTime::fromString(query.value(14).toString(), Qt::ISODate);
    }
    return info;
}

void SyncConflictStore::resolveConflict(const QString &conflictId, ConflictResolution resolution)
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

void SyncConflictStore::removeConflict(const QString &conflictId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM sync_conflicts WHERE id = ?"));
    query.addBindValue(conflictId);
    if (!query.exec()) {
        qWarning() << "SyncConflictStore::removeConflict - failed:" << query.lastError().text();
    }
}

int SyncConflictStore::unresolvedConflictCount(const QString &mappingId) const
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

void SyncConflictStore::clearBackendData(const QString &backendId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM sync_conflicts WHERE backend_id = ?"));
    query.addBindValue(backendId);
    if (!query.exec()) {
        qWarning() << "SyncConflictStore::clearBackendData - failed:" << query.lastError().text();
    }
}

void SyncConflictStore::clearMappingData(const QString &mappingId)
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM sync_conflicts WHERE mapping_id = ?"));
    query.addBindValue(mappingId);
    if (!query.exec()) {
        qWarning() << "SyncConflictStore::clearMappingData - failed:" << query.lastError().text();
    }
}

void SyncConflictStore::vacuum()
{
    if (!m_isOpen) return;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("VACUUM"))) {
        qWarning() << "SyncConflictStore::vacuum - failed:" << query.lastError().text();
    }
}

} // namespace Kalburator::Sync
