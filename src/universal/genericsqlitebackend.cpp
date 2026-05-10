#include "genericsqlitebackend.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;

namespace Kalburator::Sinks {

static int s_connCounter = 0;

GenericSqliteBackend::GenericSqliteBackend(QString dbPath, QObject *parent)
    : Kalburator::Sync::SyncBackend(parent)
    , m_dbPath(std::move(dbPath))
    , m_connectionName(QStringLiteral("generic-sqlite-%1").arg(++s_connCounter))
{
    ensureOpen();
}

GenericSqliteBackend::~GenericSqliteBackend()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool GenericSqliteBackend::isAvailable() const
{
    return m_open;
}

// ---- Collection management ----

QList<CollectionInfo> GenericSqliteBackend::availableCollections()
{
    return m_collections.values();
}

CollectionInfo GenericSqliteBackend::collectionInfo(const QString &collectionId)
{
    return m_collections.value(collectionId);
}

QString GenericSqliteBackend::createCollection(const CollectionInfo &info)
{
    if (!m_open && !ensureOpen())
        return {};
    if (!ensureTableFor(info.id))
        return {};
    if (!m_collections.contains(info.id)) {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO _shapes "
            "(shape_key, shape_name, shape_type, created_at) "
            "VALUES (?, ?, ?, datetime('now'))"));
        q.addBindValue(info.id);
        q.addBindValue(info.name);
        q.addBindValue(info.type);
        q.exec();
        m_collections[info.id] = info;
    }
    return info.id;
}

void GenericSqliteBackend::deleteCollection(const QString &collectionId)
{
    clearCollection(collectionId);
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    const QString table = tableNameFor(collectionId);
    q.exec(QStringLiteral("DROP TABLE IF EXISTS \"%1\"").arg(table));
    q.prepare(QStringLiteral("DELETE FROM _shapes WHERE shape_key = ?"));
    q.addBindValue(collectionId);
    q.exec();
    m_collections.remove(collectionId);
}

void GenericSqliteBackend::clearCollection(const QString &collectionId)
{
    if (!m_open)
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.exec(QStringLiteral("DELETE FROM \"%1\"").arg(tableNameFor(collectionId)));
}

// ---- Record I/O ----

QList<BackendRecord> GenericSqliteBackend::loadRecords(const QString &collectionId)
{
    if (!m_open)
        return {};
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT record_id, data, content_hash, last_modified "
        "FROM \"%1\"").arg(tableNameFor(collectionId)));
    if (!q.exec())
        return {};

    QList<BackendRecord> result;
    while (q.next()) {
        BackendRecord r;
        r.id = encodeRecordId(collectionId, q.value(0).toString());
        r.data = q.value(1).toByteArray();
        r.contentHash = q.value(2).toString();
        r.lastModified = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        r.type = collectionId;
        result.append(std::move(r));
    }
    return result;
}

std::optional<BackendRecord> GenericSqliteBackend::loadRecord(const QString &recordId)
{
    if (!m_open)
        return std::nullopt;
    QString collectionId, origId;
    if (!decodeRecordId(recordId, &collectionId, &origId))
        return std::nullopt;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT record_id, data, content_hash, last_modified "
        "FROM \"%1\" WHERE record_id = ?").arg(tableNameFor(collectionId)));
    q.addBindValue(origId);
    if (!q.exec() || !q.next())
        return std::nullopt;

    BackendRecord r;
    r.id = recordId;
    r.data = q.value(1).toByteArray();
    r.contentHash = q.value(2).toString();
    r.lastModified = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
    r.type = collectionId;
    return r;
}

QString GenericSqliteBackend::createRecord(const QString &collectionId,
                                            const BackendRecord &record)
{
    if (!m_open)
        return {};
    if (!ensureTableFor(collectionId))
        return {};

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    const QString origId = record.id.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : record.id;
    q.prepare(QStringLiteral(
        "INSERT INTO \"%1\" (record_id, data, content_hash, last_modified) "
        "VALUES (?, ?, ?, ?)")
        .arg(tableNameFor(collectionId)));
    q.addBindValue(origId);
    q.addBindValue(record.data);
    q.addBindValue(record.contentHash);
    q.addBindValue(record.lastModified.toString(Qt::ISODate));
    if (!q.exec())
        return {};
    return encodeRecordId(collectionId, origId);
}

bool GenericSqliteBackend::updateRecord(const BackendRecord &record)
{
    if (!m_open)
        return false;
    QString collectionId, origId;
    if (!decodeRecordId(record.id, &collectionId, &origId))
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE \"%1\" SET data=?, content_hash=?, last_modified=? "
        "WHERE record_id=?").arg(tableNameFor(collectionId)));
    q.addBindValue(record.data);
    q.addBindValue(record.contentHash);
    q.addBindValue(record.lastModified.toString(Qt::ISODate));
    q.addBindValue(origId);
    return q.exec() && q.numRowsAffected() > 0;
}

bool GenericSqliteBackend::deleteRecord(const QString &recordId)
{
    if (!m_open)
        return false;
    QString collectionId, origId;
    if (!decodeRecordId(recordId, &collectionId, &origId))
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM \"%1\" WHERE record_id=?")
        .arg(tableNameFor(collectionId)));
    q.addBindValue(origId);
    return q.exec();
}

// ---- Private helpers ----

bool GenericSqliteBackend::ensureOpen()
{
    if (m_open)
        return true;
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbPath);
    if (!db.open())
        return false;
    if (!ensureSchema())
        return false;

    // Load existing shapes into m_collections.
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT shape_key, shape_name, shape_type FROM _shapes"))) {
        while (q.next()) {
            CollectionInfo ci;
            ci.id = q.value(0).toString();
            ci.name = q.value(1).toString();
            ci.type = q.value(2).toString();
            m_collections[ci.id] = ci;
        }
    }
    m_open = true;
    return true;
}

bool GenericSqliteBackend::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    return q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS _shapes ("
        "  shape_key TEXT PRIMARY KEY,"
        "  shape_name TEXT NOT NULL,"
        "  shape_type TEXT NOT NULL,"
        "  created_at TEXT NOT NULL"
        ")"));
}

bool GenericSqliteBackend::ensureTableFor(const QString &collectionId)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    const QString table = tableNameFor(collectionId);
    return q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS \"%1\" ("
        "  record_id TEXT PRIMARY KEY,"
        "  data BLOB NOT NULL,"
        "  content_hash TEXT NOT NULL,"
        "  last_modified TEXT NOT NULL"
        ")").arg(table));
}

// static
QString GenericSqliteBackend::tableNameFor(const QString &collectionId)
{
    // Replace non-alphanumeric characters with underscores to get a valid table name.
    QString table = collectionId;
    for (QChar &c : table) {
        if (!c.isLetterOrNumber())
            c = QLatin1Char('_');
    }
    return table;
}

// static
QString GenericSqliteBackend::encodeRecordId(const QString &collectionId, const QString &id)
{
    // Separator is \x01 (SOH control char), which won't appear in normal IDs.
    return collectionId + QLatin1Char('\x01') + id;
}

// static
bool GenericSqliteBackend::decodeRecordId(const QString &recordId,
                                           QString *collectionId, QString *id)
{
    const int sep = recordId.indexOf(QLatin1Char('\x01'));
    if (sep < 0)
        return false;
    *collectionId = recordId.left(sep);
    *id = recordId.mid(sep + 1);
    return true;
}

} // namespace Kalburator::Sinks
