#include "decsynccontrollerstore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QDir>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DecSyncControllerStore::DecSyncControllerStore(const QString &dbPath)
    : m_dbPath(dbPath)
    , m_connectionName(QStringLiteral("DecSyncControllerStore_%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

DecSyncControllerStore::~DecSyncControllerStore()
{
    close();
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

bool DecSyncControllerStore::open()
{
    if (m_isOpen)
        return true;

    // Ensure the parent directory exists
    QFileInfo fi(m_dbPath);
    QDir dir = fi.dir();
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
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    if (!createTables()) {
        db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    m_isOpen = true;
    return true;
}

void DecSyncControllerStore::close()
{
    if (!m_isOpen && !QSqlDatabase::contains(m_connectionName))
        return;

    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_isOpen = false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool DecSyncControllerStore::createTables()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS baselines ("
            "  collection_id TEXT NOT NULL,"
            "  uid           TEXT NOT NULL,"
            "  ical_data     TEXT NOT NULL,"
            "  written_at    TEXT NOT NULL,"
            "  PRIMARY KEY (collection_id, uid)"
            ")"))) {
        setError(QStringLiteral("Failed to create baselines table: %1")
                     .arg(q.lastError().text()));
        return false;
    }

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS app_activity ("
            "  collection_id  TEXT NOT NULL,"
            "  app_id         TEXT NOT NULL,"
            "  last_active    TEXT NOT NULL,"
            "  last_compacted TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY (collection_id, app_id)"
            ")"))) {
        setError(QStringLiteral("Failed to create app_activity table: %1")
                     .arg(q.lastError().text()));
        return false;
    }

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS deletion_log ("
            "  collection_id TEXT NOT NULL,"
            "  uid           TEXT NOT NULL,"
            "  deleted_at    TEXT NOT NULL,"
            "  PRIMARY KEY (collection_id, uid)"
            ")"))) {
        setError(QStringLiteral("Failed to create deletion_log table: %1")
                     .arg(q.lastError().text()));
        return false;
    }

    return true;
}

void DecSyncControllerStore::setError(const QString &error)
{
    m_lastError = error;
    qWarning() << "DecSyncControllerStore:" << error;
}

// ---------------------------------------------------------------------------
// Baselines
// ---------------------------------------------------------------------------

std::optional<BaselineRecord> DecSyncControllerStore::baseline(const QString &collectionId,
                                                                const QString &uid) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT ical_data, written_at FROM baselines "
        "WHERE collection_id = :cid AND uid = :uid"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":uid"), uid);

    if (!q.exec() || !q.next())
        return std::nullopt;

    BaselineRecord rec;
    rec.icalData  = q.value(0).toString();
    rec.writtenAt = q.value(1).toString();
    return rec;
}

void DecSyncControllerStore::setBaseline(const QString &collectionId,
                                         const QString &uid,
                                         const QString &icalData,
                                         const QString &writtenAt)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO baselines (collection_id, uid, ical_data, written_at) "
        "VALUES (:cid, :uid, :ical, :written_at)"));
    q.bindValue(QStringLiteral(":cid"),        collectionId);
    q.bindValue(QStringLiteral(":uid"),        uid);
    q.bindValue(QStringLiteral(":ical"),       icalData);
    q.bindValue(QStringLiteral(":written_at"), writtenAt);
    if (!q.exec())
        setError(QStringLiteral("setBaseline failed: %1").arg(q.lastError().text()));
}

void DecSyncControllerStore::removeBaseline(const QString &collectionId, const QString &uid)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM baselines WHERE collection_id = :cid AND uid = :uid"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":uid"), uid);
    if (!q.exec())
        setError(QStringLiteral("removeBaseline failed: %1").arg(q.lastError().text()));
}

QMap<QString, BaselineRecord> DecSyncControllerStore::allBaselines(const QString &collectionId) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT uid, ical_data, written_at FROM baselines WHERE collection_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), collectionId);

    QMap<QString, BaselineRecord> result;
    if (!q.exec())
        return result;

    while (q.next()) {
        BaselineRecord rec;
        rec.icalData  = q.value(1).toString();
        rec.writtenAt = q.value(2).toString();
        result.insert(q.value(0).toString(), rec);
    }
    return result;
}

// ---------------------------------------------------------------------------
// App Activity
// ---------------------------------------------------------------------------

void DecSyncControllerStore::recordAppActivity(const QString &collectionId,
                                                const QString &appId,
                                                const QString &lastActive)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    // Insert new row or update only last_active on conflict
    q.prepare(QStringLiteral(
        "INSERT INTO app_activity (collection_id, app_id, last_active) "
        "VALUES (:cid, :app, :la) "
        "ON CONFLICT(collection_id, app_id) DO UPDATE SET last_active = excluded.last_active"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":app"), appId);
    q.bindValue(QStringLiteral(":la"),  lastActive);
    if (!q.exec())
        setError(QStringLiteral("recordAppActivity failed: %1").arg(q.lastError().text()));
}

void DecSyncControllerStore::recordAppCompaction(const QString &collectionId,
                                                  const QString &appId,
                                                  const QString &lastCompacted)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    // We need a sensible last_active for a brand-new row; use the compaction time.
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO app_activity "
        "(collection_id, app_id, last_active, last_compacted) "
        "VALUES ("
        "  :cid, :app,"
        "  COALESCE((SELECT last_active FROM app_activity "
        "            WHERE collection_id = :cid AND app_id = :app), :lc),"
        "  :lc"
        ")"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":app"), appId);
    q.bindValue(QStringLiteral(":lc"),  lastCompacted);
    if (!q.exec())
        setError(QStringLiteral("recordAppCompaction failed: %1").arg(q.lastError().text()));
}

std::optional<AppActivityRecord> DecSyncControllerStore::appActivity(const QString &collectionId,
                                                                       const QString &appId) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT app_id, last_active, last_compacted FROM app_activity "
        "WHERE collection_id = :cid AND app_id = :app"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":app"), appId);

    if (!q.exec() || !q.next())
        return std::nullopt;

    AppActivityRecord rec;
    rec.appId         = q.value(0).toString();
    rec.lastActive    = q.value(1).toString();
    rec.lastCompacted = q.value(2).toString();
    return rec;
}

QList<AppActivityRecord> DecSyncControllerStore::allAppActivity(const QString &collectionId) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT app_id, last_active, last_compacted FROM app_activity "
        "WHERE collection_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), collectionId);

    QList<AppActivityRecord> result;
    if (!q.exec())
        return result;

    while (q.next()) {
        AppActivityRecord rec;
        rec.appId         = q.value(0).toString();
        rec.lastActive    = q.value(1).toString();
        rec.lastCompacted = q.value(2).toString();
        result.append(rec);
    }
    return result;
}

QStringList DecSyncControllerStore::inactiveApps(const QString &collectionId, int days) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // Cutoff expressed as ISO 8601 UTC string for direct TEXT comparison
    const QString cutoff = QDateTime::currentDateTimeUtc()
                               .addDays(-days)
                               .toString(Qt::ISODate);

    q.prepare(QStringLiteral(
        "SELECT app_id FROM app_activity "
        "WHERE collection_id = :cid AND last_active < :cutoff"));
    q.bindValue(QStringLiteral(":cid"),    collectionId);
    q.bindValue(QStringLiteral(":cutoff"), cutoff);

    QStringList result;
    if (!q.exec())
        return result;

    while (q.next())
        result.append(q.value(0).toString());

    return result;
}

QStringList DecSyncControllerStore::newApps(const QString &collectionId,
                                             const QStringList &currentAppIds) const
{
    if (currentAppIds.isEmpty())
        return {};

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT app_id FROM app_activity WHERE collection_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), collectionId);

    if (!q.exec())
        return {};

    QSet<QString> known;
    while (q.next())
        known.insert(q.value(0).toString());

    QStringList result;
    for (const QString &appId : currentAppIds) {
        if (!known.contains(appId))
            result.append(appId);
    }
    return result;
}

void DecSyncControllerStore::removeAppActivity(const QString &collectionId, const QString &appId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM app_activity WHERE collection_id = :cid AND app_id = :app"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":app"), appId);
    if (!q.exec())
        setError(QStringLiteral("removeAppActivity failed: %1").arg(q.lastError().text()));
}

// ---------------------------------------------------------------------------
// Deletion Log
// ---------------------------------------------------------------------------

void DecSyncControllerStore::logDeletion(const QString &collectionId,
                                          const QString &uid,
                                          const QString &deletedAt)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO deletion_log (collection_id, uid, deleted_at) "
        "VALUES (:cid, :uid, :dat)"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":uid"), uid);
    q.bindValue(QStringLiteral(":dat"), deletedAt);
    if (!q.exec())
        setError(QStringLiteral("logDeletion failed: %1").arg(q.lastError().text()));
}

void DecSyncControllerStore::removeDeletion(const QString &collectionId, const QString &uid)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM deletion_log WHERE collection_id = :cid AND uid = :uid"));
    q.bindValue(QStringLiteral(":cid"), collectionId);
    q.bindValue(QStringLiteral(":uid"), uid);
    if (!q.exec())
        setError(QStringLiteral("removeDeletion failed: %1").arg(q.lastError().text()));
}

QList<DeletionRecord> DecSyncControllerStore::activeDeletions(const QString &collectionId) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT uid, deleted_at FROM deletion_log WHERE collection_id = :cid"));
    q.bindValue(QStringLiteral(":cid"), collectionId);

    QList<DeletionRecord> result;
    if (!q.exec())
        return result;

    while (q.next()) {
        DeletionRecord rec;
        rec.uid       = q.value(0).toString();
        rec.deletedAt = q.value(1).toString();
        result.append(rec);
    }
    return result;
}
