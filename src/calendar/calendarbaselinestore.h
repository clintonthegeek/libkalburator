#ifndef KALBURATOR_CALENDARBASELINESTORE_H
#define KALBURATOR_CALENDARBASELINESTORE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QDateTime>

#include <memory>

namespace Kalburator::Storage { class BaselineStore; }

namespace Kalburator::Sync {

/**
 * @brief Phase K.5 facade over Storage::BaselineStore.
 *
 * Preserves the legacy iCal-text + property-JSON + last-sync-time
 * surface used by the engine and tests during the migration window.
 * Deleted in Task 13 once all callers move to Storage::BaselineStore.
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
                         const QHash<QString, QString> &uidToIcal);
    bool    removeBaseline(const QString &mappingId, const QString &uid);
    bool    removeBaselines(const QString &mappingId);
    QHash<QString, QString> allBaselines(const QString &mappingId) const;
    bool    clearBaselines();
    bool    hasBaselines(const QString &mappingId) const;

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
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_store;
    mutable QSet<QString>                               m_seenMappings;
};

} // namespace Kalburator::Sync

#endif
