// src/calendar/calendarbaselinestore.h
#ifndef KALBURATOR_CALENDARBASELINESTORE_H
#define KALBURATOR_CALENDARBASELINESTORE_H

#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QDateTime>

namespace Kalburator::Sync {

/**
 * @brief 3-way merge baseline for calendar-typed sync.
 *
 * Owns:
 *   - per-(mappingId, uid) iCal text baselines for incidence merge
 *   - per-(mappingId, calendarId) JSON property baselines for calendar
 *     property merge
 *   - per-mappingId last-sync timestamps
 *
 * Carved out of the dissolving `SyncStore` during Phase D. SQLite-backed,
 * lives in the same `.kalburator-sync.db` file as the blob stores.
 */
class CalendarBaselineStore : public QObject
{
    Q_OBJECT
public:
    explicit CalendarBaselineStore(const QString &dbPath, QObject *parent = nullptr);
    ~CalendarBaselineStore() override;

    bool isValid() const;

    // ---- iCal-text baselines ----
    QString baseline(const QString &mappingId, const QString &uid) const;
    bool    setBaseline(const QString &mappingId, const QString &uid,
                        const QString &icalText);
    bool    setBaselines(const QString &mappingId,
                         const QHash<QString, QString> &uidToIcal);   // bulk
    bool    removeBaseline(const QString &mappingId, const QString &uid);
    bool    removeBaselines(const QString &mappingId);                // per-mapping
    QHash<QString, QString> allBaselines(const QString &mappingId) const;
    bool    clearBaselines();

    bool hasBaselines(const QString &mappingId) const;

    // ---- property-JSON baselines ----
    QString propertyBaseline(const QString &mappingId, const QString &calendarId) const;
    bool    setPropertyBaseline(const QString &mappingId, const QString &calendarId,
                                const QString &propertyJson);
    bool    removePropertyBaseline(const QString &mappingId, const QString &calendarId);
    QHash<QString, QString> allPropertyBaselines(const QString &mappingId) const;

    // ---- last-sync timestamp ----
    QDateTime lastSyncTime(const QString &mappingId) const;
    bool      setLastSyncTime(const QString &mappingId, const QDateTime &when);

private:
    bool ensureSchema();
    QSqlDatabase m_db;
    QString m_connectionName;
};

} // namespace Kalburator::Sync

#endif
