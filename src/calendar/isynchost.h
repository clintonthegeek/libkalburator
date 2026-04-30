#ifndef ISYNCHOST_H
#define ISYNCHOST_H

#include <QString>
#include <QHash>
#include <QDateTime>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

class SyncBackend;
class ICalendarCollection;
class IIncidenceSource;
class IIncidenceRegistry;
class ISyncConfigStore;

/**
 * @brief Abstract interface decoupling sync engine from CollectionController.
 *
 * SyncEngine and CalendarManager use this interface instead of
 * depending on CollectionController directly.
 * The app shell implements this interface in CollectionController.
 *
 * Narrowed 2026-04-20 (Phase 1.2 of libkalburator extraction):
 *   - `collection()` returns `ICalendarCollection*` (was `Collection*`)
 *   - `kalbConfigManager()` replaced with `configStore()` returning
 *     `ISyncConfigStore*`
 *   - Removed `syncCoordinator()` — circular; CalendarManager now
 *     fires `generateSyncMappingsFromLogicalCalendars()` unconditionally
 */
class ISyncHost
{
public:
    virtual ~ISyncHost() = default;

    // Backend lifecycle
    virtual SyncBackend* backendById(const QString &id) = 0;
    virtual QHash<QString, SyncBackend*> backends() = 0;

    // Incidence propagation (sync -> model)
    virtual bool applyIncidenceAddition(const QString &calendarId,
                                        const KCalendarCore::Incidence::Ptr &inc,
                                        bool stageForSync = true) = 0;
    virtual bool applyIncidenceRemoval(const QString &calendarId,
                                       const QString &uid,
                                       bool stageForSync = true,
                                       const QDateTime &recurrenceId = {}) = 0;
    virtual bool applyIncidenceUpdate(const QString &calendarId,
                                      const KCalendarCore::Incidence::Ptr &inc,
                                      bool stageForSync = true) = 0;

    // Calendar discovery (narrow host-side view of the collection)
    virtual ICalendarCollection* collection() = 0;

    // Subsystem access (for SyncEngine and CalendarManager)
    virtual IIncidenceSource* incidenceSource() = 0;
    virtual IIncidenceRegistry* incidenceRegistry() = 0;
    virtual ISyncConfigStore* configStore() = 0;

    // Calendar lifecycle
    virtual void unloadCalendar(const QString &calendarId) = 0;

    // Sync mapping regeneration (called after calendar CRUD). Hosts
    // that have no sync engine configured should implement as a no-op.
    virtual void generateSyncMappingsFromLogicalCalendars() = 0;
};

} // namespace Kalburator::Sync

#endif // ISYNCHOST_H
