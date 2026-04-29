#include "calendarbaselinestore.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace Kalburator::Sync {

static int s_connectionCounter = 0;

CalendarBaselineStore::CalendarBaselineStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("CalendarBaselineStore_%1").arg(++s_connectionCounter))
{
    // Ensure parent directory exists
    QFileInfo fileInfo(dbPath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "CalendarBaselineStore: failed to open database:"
                   << m_db.lastError().text();
        return;
    }

    // Enable WAL mode for better concurrent performance
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    if (!ensureSchema()) {
        qWarning() << "CalendarBaselineStore: ensureSchema() failed";
        m_db.close();
    }
}

CalendarBaselineStore::~CalendarBaselineStore()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool CalendarBaselineStore::isValid() const
{
    return m_db.isOpen();
}

bool CalendarBaselineStore::ensureSchema()
{
    QSqlQuery q(m_db);

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS calendar_baseline_ical ("
            "  mapping_id TEXT NOT NULL,"
            "  uid TEXT NOT NULL,"
            "  ical_text TEXT NOT NULL,"
            "  PRIMARY KEY (mapping_id, uid)"
            ")"))) {
        qWarning() << "CalendarBaselineStore: failed to create calendar_baseline_ical:"
                   << q.lastError().text();
        return false;
    }

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS calendar_baseline_property ("
            "  mapping_id TEXT NOT NULL,"
            "  calendar_id TEXT NOT NULL,"
            "  property_json TEXT NOT NULL,"
            "  PRIMARY KEY (mapping_id, calendar_id)"
            ")"))) {
        qWarning() << "CalendarBaselineStore: failed to create calendar_baseline_property:"
                   << q.lastError().text();
        return false;
    }

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS calendar_baseline_lastsync ("
            "  mapping_id TEXT PRIMARY KEY,"
            "  last_sync_iso TEXT NOT NULL"
            ")"))) {
        qWarning() << "CalendarBaselineStore: failed to create calendar_baseline_lastsync:"
                   << q.lastError().text();
        return false;
    }

    return true;
}

// ============================================================================
// iCal-text baselines
// ============================================================================

QString CalendarBaselineStore::baseline(const QString &mappingId, const QString &uid) const
{
    if (!m_db.isOpen()) return QString();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT ical_text FROM calendar_baseline_ical "
        "WHERE mapping_id = ? AND uid = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(uid);

    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

bool CalendarBaselineStore::setBaseline(const QString &mappingId, const QString &uid,
                                        const QString &icalText)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO calendar_baseline_ical (mapping_id, uid, ical_text) "
        "VALUES (?, ?, ?)"));
    q.addBindValue(mappingId);
    q.addBindValue(uid);
    q.addBindValue(icalText);

    if (!q.exec()) {
        qWarning() << "CalendarBaselineStore::setBaseline - failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool CalendarBaselineStore::setBaselines(const QString &mappingId,
                                         const QHash<QString, QString> &uidToIcal)
{
    if (!m_db.isOpen() || uidToIcal.isEmpty()) return false;

    if (!m_db.transaction()) {
        qWarning() << "CalendarBaselineStore::setBaselines - failed to start transaction";
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO calendar_baseline_ical (mapping_id, uid, ical_text) "
        "VALUES (?, ?, ?)"));

    bool success = true;
    for (auto it = uidToIcal.constBegin(); it != uidToIcal.constEnd(); ++it) {
        q.bindValue(0, mappingId);
        q.bindValue(1, it.key());
        q.bindValue(2, it.value());

        if (!q.exec()) {
            qWarning() << "CalendarBaselineStore::setBaselines - failed for uid"
                       << it.key() << ":" << q.lastError().text();
            success = false;
            break;
        }
    }

    if (success) {
        m_db.commit();
    } else {
        m_db.rollback();
    }
    return success;
}

bool CalendarBaselineStore::removeBaseline(const QString &mappingId, const QString &uid)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM calendar_baseline_ical WHERE mapping_id = ? AND uid = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(uid);

    if (!q.exec()) {
        qWarning() << "CalendarBaselineStore::removeBaseline - failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool CalendarBaselineStore::removeBaselines(const QString &mappingId)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM calendar_baseline_ical WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (!q.exec()) {
        qWarning() << "CalendarBaselineStore::removeBaselines - failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QHash<QString, QString> CalendarBaselineStore::allBaselines(const QString &mappingId) const
{
    QHash<QString, QString> result;
    if (!m_db.isOpen()) return result;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT uid, ical_text FROM calendar_baseline_ical WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (q.exec()) {
        while (q.next())
            result.insert(q.value(0).toString(), q.value(1).toString());
    }
    return result;
}

bool CalendarBaselineStore::clearBaselines()
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM calendar_baseline_ical"))) {
        qWarning() << "CalendarBaselineStore::clearBaselines - failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool CalendarBaselineStore::hasBaselines(const QString &mappingId) const
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM calendar_baseline_ical WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (q.exec() && q.next())
        return q.value(0).toInt() > 0;
    return false;
}

// ============================================================================
// Property-JSON baselines
// ============================================================================

QString CalendarBaselineStore::propertyBaseline(const QString &mappingId,
                                                 const QString &calendarId) const
{
    if (!m_db.isOpen()) return QString();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT property_json FROM calendar_baseline_property "
        "WHERE mapping_id = ? AND calendar_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(calendarId);

    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

bool CalendarBaselineStore::setPropertyBaseline(const QString &mappingId,
                                                 const QString &calendarId,
                                                 const QString &propertyJson)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO calendar_baseline_property "
        "(mapping_id, calendar_id, property_json) VALUES (?, ?, ?)"));
    q.addBindValue(mappingId);
    q.addBindValue(calendarId);
    q.addBindValue(propertyJson);

    if (!q.exec()) {
        qWarning() << "CalendarBaselineStore::setPropertyBaseline - failed:"
                   << q.lastError().text();
        return false;
    }
    return true;
}

bool CalendarBaselineStore::removePropertyBaseline(const QString &mappingId,
                                                    const QString &calendarId)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM calendar_baseline_property "
        "WHERE mapping_id = ? AND calendar_id = ?"));
    q.addBindValue(mappingId);
    q.addBindValue(calendarId);

    if (!q.exec()) {
        qWarning() << "CalendarBaselineStore::removePropertyBaseline - failed:"
                   << q.lastError().text();
        return false;
    }
    return true;
}

QHash<QString, QString> CalendarBaselineStore::allPropertyBaselines(
    const QString &mappingId) const
{
    QHash<QString, QString> result;
    if (!m_db.isOpen()) return result;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT calendar_id, property_json FROM calendar_baseline_property "
        "WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (q.exec()) {
        while (q.next())
            result.insert(q.value(0).toString(), q.value(1).toString());
    }
    return result;
}

// ============================================================================
// Last-sync timestamp
// ============================================================================

QDateTime CalendarBaselineStore::lastSyncTime(const QString &mappingId) const
{
    if (!m_db.isOpen()) return QDateTime();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT last_sync_iso FROM calendar_baseline_lastsync WHERE mapping_id = ?"));
    q.addBindValue(mappingId);

    if (q.exec() && q.next()) {
        const QString iso = q.value(0).toString();
        if (!iso.isEmpty())
            return QDateTime::fromString(iso, Qt::ISODate);
    }
    return QDateTime();
}

bool CalendarBaselineStore::setLastSyncTime(const QString &mappingId, const QDateTime &when)
{
    if (!m_db.isOpen()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO calendar_baseline_lastsync (mapping_id, last_sync_iso) "
        "VALUES (?, ?)"));
    q.addBindValue(mappingId);
    q.addBindValue(when.toString(Qt::ISODate));

    if (!q.exec()) {
        qWarning() << "CalendarBaselineStore::setLastSyncTime - failed:" << q.lastError().text();
        return false;
    }
    return true;
}

} // namespace Kalburator::Sync
