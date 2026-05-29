#include "genericsqlitebackend.h"

#include <QDebug>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;

namespace Kalburator::Sinks {

static int s_connCounter = 0;

GenericSqliteBackend::GenericSqliteBackend(QString dbPath, QObject *parent)
    : Kalburator::Sync::SyncBackendBase(parent)
    , m_dbPath(std::move(dbPath))
    , m_baseConnectionName(QStringLiteral("generic-sqlite-%1").arg(++s_connCounter))
{
    ensureOpen();
}

GenericSqliteBackend::~GenericSqliteBackend()
{
    // Close the per-thread connection for the current (typically main) thread.
    // Other per-thread connections (e.g. SyncEngine worker thread) are
    // removed by name only — calling QSqlDatabase::database() on a connection
    // that belongs to a different thread emits a Qt warning, so we skip the
    // close() call and let Qt's internal cleanup handle the underlying handle.
    const QString thisThreadConnName = m_baseConnectionName
        + QLatin1Char('_')
        + QString::number(reinterpret_cast<quintptr>(QThread::currentThread()), 16);

    QMutexLocker lock(&m_connMutex);
    for (const QString &name : std::as_const(m_openConnections)) {
        if (name == thisThreadConnName) {
            QSqlDatabase db = QSqlDatabase::database(name);
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
}

bool GenericSqliteBackend::isAvailable() const
{
    return m_open;
}

// ---- Collection management ----

QList<CollectionInfo> GenericSqliteBackend::availableCollections()
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_collections.values();
}

CollectionInfo GenericSqliteBackend::collectionInfo(const QString &collectionId)
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_collections.value(collectionId);
}

QString GenericSqliteBackend::createCollection(const CollectionInfo &info,
                                               const Kalburator::Shape::Shape &shape)
{
    if (!m_open && !ensureOpen())
        return {};
    if (!ensureTableFor(info.id))
        return {};
    bool needShapeRow = false;
    {
        QMutexLocker lock(&m_collectionsMutex);
        if (!m_collections.contains(info.id)) {
            m_collections[info.id] = info;
            needShapeRow = true;
        }
        m_shapeByCollection.insert(info.id, shape);
    }
    if (needShapeRow) {
        // DB write outside the hash lock: threadDb() may take m_connMutex on first
        // use per thread, and m_collectionsMutex must never be held across it.
        QSqlDatabase db = threadDb();
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO _shapes "
            "(shape_key, shape_name, shape_type, created_at) "
            "VALUES (?, ?, ?, datetime('now'))"));
        q.addBindValue(info.id);
        q.addBindValue(info.name);
        q.addBindValue(info.type);
        q.exec();
    }
    return info.id;
}

QList<Kalburator::Shape::Shape> GenericSqliteBackend::nativeShapes() const
{
    QMutexLocker lock(&m_collectionsMutex);
    QList<Kalburator::Shape::Shape> out;
    for (auto it = m_shapeByCollection.constBegin();
         it != m_shapeByCollection.constEnd(); ++it) {
        if (!out.contains(it.value()))
            out.append(it.value());
    }
    return out;
}

Kalburator::Shape::Shape GenericSqliteBackend::shapeFor(const QString &collectionId) const
{
    QMutexLocker lock(&m_collectionsMutex);
    return m_shapeByCollection.value(collectionId, Kalburator::Shape::Shape::Any());
}

bool GenericSqliteBackend::deleteCollection(const QString &collectionId)
{
    if (!m_open)
        return false;
    bool ok = clearCollection(collectionId);
    QSqlDatabase db = threadDb();
    QSqlQuery q(db);
    const QString table = tableNameFor(collectionId);
    if (!q.exec(QStringLiteral("DROP TABLE IF EXISTS \"%1\"").arg(table))) {
        qWarning() << "GenericSqliteBackend::deleteCollection: DROP failed for"
                   << collectionId << ":" << q.lastError().text();
        ok = false;
    }
    q.prepare(QStringLiteral("DELETE FROM _shapes WHERE shape_key = ?"));
    q.addBindValue(collectionId);
    if (!q.exec()) {
        qWarning() << "GenericSqliteBackend::deleteCollection: _shapes cleanup failed for"
                   << collectionId << ":" << q.lastError().text();
        ok = false;
    }
    // Best-effort eviction: drop the in-memory entry even on partial DB-cleanup
    // failure so the cache reflects the caller's delete intent. The bool return
    // signals partial failure; on false the on-disk _shapes row or table may
    // persist (named in architectural-redress FINDINGS, P4.T2 review).
    {
        QMutexLocker lock(&m_collectionsMutex);
        m_collections.remove(collectionId);
    }
    return ok;
}

bool GenericSqliteBackend::clearCollection(const QString &collectionId)
{
    if (!m_open)
        return false;  // not open = not operable; caller treats as failure
    QSqlDatabase db = threadDb();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("DELETE FROM \"%1\"").arg(tableNameFor(collectionId)))) {
        qWarning() << "GenericSqliteBackend::clearCollection: DELETE failed for"
                   << collectionId << ":" << q.lastError().text();
        return false;
    }
    return true;
}

// ---- Record I/O ----

QList<BackendRecord> GenericSqliteBackend::loadRecords(const QString &collectionId)
{
    if (!m_open)
        return {};
    QSqlDatabase db = threadDb();
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

    QSqlDatabase db = threadDb();
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

    QSqlDatabase db = threadDb();
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

    QSqlDatabase db = threadDb();
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

    QSqlDatabase db = threadDb();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM \"%1\" WHERE record_id=?")
        .arg(tableNameFor(collectionId)));
    q.addBindValue(origId);
    return q.exec();
}

// ---- Private helpers ----

/// Returns the per-thread SQLite connection, opening it lazily if needed.
/// Each QThread gets its own connection (name = base + "_" + thread-address)
/// so that multiple threads (e.g. main + SyncEngine worker) can use the
/// backend concurrently without triggering Qt's "database does not belong
/// to the calling thread" warning.
QSqlDatabase GenericSqliteBackend::threadDb()
{
    const QString name = m_baseConnectionName
        + QLatin1Char('_')
        + QString::number(reinterpret_cast<quintptr>(QThread::currentThread()),
                          16);

    if (QSqlDatabase::contains(name))
        return QSqlDatabase::database(name);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(m_dbPath);
    if (!db.open())
        return db; // isValid() but !isOpen() — callers check via QSqlQuery::exec()

    // Ensure schema on this connection. Do it here so every per-thread
    // connection sees the same table structure even if it opens after
    // createCollection() already ran on another thread's connection.
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS _shapes ("
        "  shape_key TEXT PRIMARY KEY,"
        "  shape_name TEXT NOT NULL,"
        "  shape_type TEXT NOT NULL,"
        "  created_at TEXT NOT NULL"
        ")"));

    QMutexLocker lock(&m_connMutex);
    m_openConnections.append(name);
    return db;
}

bool GenericSqliteBackend::ensureOpen()
{
    if (m_open)
        return true;
    QSqlDatabase db = threadDb();
    if (!db.isOpen())
        return false;
    if (!ensureSchema(db))
        return false;

    // Load existing shapes into m_collections.
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT shape_key, shape_name, shape_type FROM _shapes"))) {
        QMutexLocker lock(&m_collectionsMutex);
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

bool GenericSqliteBackend::ensureSchema(QSqlDatabase &db)
{
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
    QSqlDatabase db = threadDb();
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
