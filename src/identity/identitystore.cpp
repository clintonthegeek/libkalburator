#include "identitystore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace Kalburator::Identity {

namespace {

constexpr int kSchemaVersion = 2;

}  // namespace

int IdentityStoreConnectionCounter = 0;  // unique QSqlDatabase connection names

IdentityStore::IdentityStore(const QString& dbPath)
    : m_dbPath(dbPath)
    , m_connName(QStringLiteral("KalburatorIdentityStore_%1")
                     .arg(++IdentityStoreConnectionCounter))
{
    QFileInfo fi(m_dbPath);
    QDir parent = fi.dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        m_lastError = QStringLiteral("Failed to create directory: %1")
                          .arg(parent.path());
        return;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                m_connName);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        m_lastError = QStringLiteral("Failed to open database: %1")
                          .arg(db.lastError().text());
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

IdentityStore::~IdentityStore()
{
    if (QSqlDatabase::contains(m_connName)) {
        QSqlDatabase::database(m_connName).close();
        QSqlDatabase::removeDatabase(m_connName);
    }
}

bool IdentityStore::ensureSchemaAndVersion()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS record_links ("
        "domain TEXT NOT NULL, "
        "record_uid TEXT NOT NULL, "
        "entity_id TEXT NOT NULL, "
        "linked_at TEXT, "
        "PRIMARY KEY(domain, record_uid))"));
    // v2: display-name projection column (SQLite has no ADD COLUMN IF NOT
    // EXISTS — probe table_info like BaselineStore's ensureColumn).
    {
        QSqlQuery probe(db);
        probe.exec(QStringLiteral("PRAGMA table_info(record_links)"));
        bool hasDisplayName = false;
        while (probe.next()) {
            if (probe.value(1).toString() == QLatin1String("display_name"))
                hasDisplayName = true;
        }
        if (!hasDisplayName) {
            if (!q.exec(QStringLiteral(
                    "ALTER TABLE record_links ADD COLUMN display_name TEXT"))) {
                m_lastError = QStringLiteral("v2 migration failed: %1")
                                  .arg(q.lastError().text());
                return false;
            }
        }
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_record_links_entity "
        "ON record_links(entity_id)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS email_index ("
        "email TEXT PRIMARY KEY, "
        "entity_id TEXT NOT NULL)"));

    const int userVersion =
        q.exec(QStringLiteral("PRAGMA user_version")) && q.next()
            ? q.value(0).toInt()
            : 0;
    if (userVersion < kSchemaVersion) {
        if (!q.exec(QStringLiteral("PRAGMA user_version = %1")
                        .arg(kSchemaVersion))) {
            m_lastError = QStringLiteral("Failed to stamp schema version: %1")
                              .arg(q.lastError().text());
            return false;
        }
    }
    return true;
}

QString IdentityStore::linkRecord(const QString& domain,
                                  const QString& recordUid,
                                  const QStringList& emails,
                                  const QString& displayName)
{
    if (!m_isOpen || domain.isEmpty() || recordUid.isEmpty())
        return {};

    QSqlQuery q(QSqlDatabase::database(m_connName));

    // 1. Email evidence first (sorted → deterministic adoption).
    QString entityId;
    QStringList sorted = emails;
    sorted.removeAll(QString());
    sorted.removeDuplicates();
    std::sort(sorted.begin(), sorted.end());
    for (const QString& email : sorted) {
        q.prepare(QStringLiteral(
            "SELECT entity_id FROM email_index WHERE email = ?"));
        q.addBindValue(email.toLower());
        if (q.exec() && q.next()) {
            entityId = q.value(0).toString();
            break;
        }
    }

    // 2. Existing link on this record (and its stored projection).
    QString existingDisplayName;
    if (entityId.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT entity_id, display_name FROM record_links "
            "WHERE domain = ? AND record_uid = ?"));
        q.addBindValue(domain);
        q.addBindValue(recordUid);
        if (q.exec() && q.next()) {
            entityId = q.value(0).toString();
            existingDisplayName = q.value(1).toString();
        }
    } else {
        q.prepare(QStringLiteral(
            "SELECT display_name FROM record_links "
            "WHERE domain = ? AND record_uid = ?"));
        q.addBindValue(domain);
        q.addBindValue(recordUid);
        if (q.exec() && q.next())
            existingDisplayName = q.value(0).toString();
    }

    // 3. Mint.
    if (entityId.isEmpty())
        entityId = QStringLiteral("ent-%1")
                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    // Empty never overwrites an existing projection.
    const QString nameToStore =
        displayName.isEmpty() ? existingDisplayName : displayName;

    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO record_links "
        "(domain, record_uid, entity_id, linked_at, display_name) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(domain);
    q.addBindValue(recordUid);
    q.addBindValue(entityId);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!nameToStore.isEmpty())
        q.addBindValue(nameToStore);
    else
        q.bindValue(4, QVariant());  // SQL NULL, not '' — keeps empty distinct
    if (!q.exec()) {
        m_lastError = QStringLiteral("linkRecord failed: %1")
                          .arg(q.lastError().text());
        return {};
    }

    for (const QString& email : sorted) {
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO email_index (email, entity_id) "
            "VALUES (?, ?)"));
        q.addBindValue(email.toLower());
        q.addBindValue(entityId);
        if (!q.exec()) {
            m_lastError = QStringLiteral("email index failed: %1")
                              .arg(q.lastError().text());
            return {};
        }
    }
    return entityId;
}

QString IdentityStore::entityIdFor(const QString& domain,
                                   const QString& recordUid) const
{
    if (!m_isOpen)
        return {};
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "SELECT entity_id FROM record_links WHERE domain = ? AND record_uid = ?"));
    q.addBindValue(domain);
    q.addBindValue(recordUid);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

QString IdentityStore::entityIdForEmail(const QString& email) const
{
    if (!m_isOpen || email.isEmpty())
        return {};
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "SELECT entity_id FROM email_index WHERE email = ?"));
    q.addBindValue(email.toLower());
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

QString IdentityStore::displayNameFor(const QString& domain,
                                      const QString& recordUid) const
{
    if (!m_isOpen)
        return {};
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "SELECT display_name FROM record_links "
        "WHERE domain = ? AND record_uid = ?"));
    q.addBindValue(domain);
    q.addBindValue(recordUid);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

QList<EntityLink> IdentityStore::recordsForEntity(const QString& entityId) const
{
    QList<EntityLink> out;
    if (!m_isOpen || entityId.isEmpty())
        return out;
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "SELECT domain, record_uid FROM record_links WHERE entity_id = ? "
        "ORDER BY domain, record_uid"));
    q.addBindValue(entityId);
    if (q.exec()) {
        while (q.next()) {
            out.append(EntityLink{ q.value(0).toString(),
                                   q.value(1).toString(), entityId });
        }
    }
    return out;
}

QStringList IdentityStore::emailsForEntity(const QString& entityId) const
{
    QStringList out;
    if (!m_isOpen || entityId.isEmpty())
        return out;
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "SELECT email FROM email_index WHERE entity_id = ? ORDER BY email"));
    q.addBindValue(entityId);
    if (q.exec()) {
        while (q.next())
            out.append(q.value(0).toString());
    }
    return out;
}

void IdentityStore::unlinkRecord(const QString& domain, const QString& recordUid)
{
    if (!m_isOpen)
        return;
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "DELETE FROM record_links WHERE domain = ? AND record_uid = ?"));
    q.addBindValue(domain);
    q.addBindValue(recordUid);
    q.exec();

    // Prune email rows whose entity lost its last record — a later
    // same-email record must mint fresh, not resurrect the dead entity.
    q.exec(QStringLiteral(
        "DELETE FROM email_index WHERE entity_id NOT IN "
        "(SELECT DISTINCT entity_id FROM record_links)"));
}

}  // namespace Kalburator::Identity
