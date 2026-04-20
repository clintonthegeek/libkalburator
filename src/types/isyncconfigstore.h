#ifndef ISYNCCONFIGSTORE_H
#define ISYNCCONFIGSTORE_H

#include <QList>
#include <QString>
#include <QVariantMap>

struct LogicalCalendar;
struct SyncMapping;

/**
 * @brief Host-supplied persistence layer for sync configuration.
 *
 * The sync engine (currently `libs/sync/`, destined for
 * `libkalburator`) needs to read and write three kinds of config:
 *   - `LogicalCalendar` entries (one-logical-over-many-physical bindings)
 *   - backend-specific config blobs (CalDAV URL, local dir, etc.)
 *   - sync mappings (source + target + policy tuples)
 *
 * PlanStan's `KalbConfigManager` implements the full interface over
 * the on-disk `.kalb` file. A reuse host (e.g. Wild Palms in Full
 * Sync Mode) provides its own implementation — typically backed by
 * QSettings, a profile JSON, or whatever its config story is.
 *
 * Surface matches the calls `SyncCoordinator` + `CalendarManager`
 * actually make, audited 2026-04-20.
 */
class ISyncConfigStore
{
public:
    virtual ~ISyncConfigStore() = default;

    // Logical-calendar CRUD
    virtual void addLogicalCalendar(const LogicalCalendar &logCal) = 0;
    virtual void updateLogicalCalendar(const LogicalCalendar &logCal) = 0;
    virtual void removeLogicalCalendar(const QString &logicalCalendarId) = 0;
    virtual LogicalCalendar logicalCalendar(const QString &logicalCalendarId) const = 0;

    // Backend-config read (host owns the schema of the QVariantMap)
    virtual QVariantMap backendConfig(const QString &backendId) const = 0;

    // Sync-mapping read
    virtual bool hasSyncMappings() const = 0;
    virtual QList<SyncMapping> syncMappings() const = 0;

    // Persistence hook — host decides where/how to flush
    virtual void save() = 0;
};

#endif // ISYNCCONFIGSTORE_H
