#ifndef ISYNCHOST_H
#define ISYNCHOST_H

#include <QDateTime>
#include <QHash>
#include <QString>
#include <KCalendarCore/Incidence>

#include "canonicalrecord.h"
#include "lossprofile.h"
#include "synctypes.h"

namespace Kalburator::Sync {

class SyncBackend;
class ICalendarCollection;
class IIncidenceSource;
class IIncidenceRegistry;
class ISyncConfigStore;

/**
 * @brief Abstract interface decoupling sync engine from the application shell.
 *
 * G.9.a narrows this interface to ~7 generic methods. Calendar-typed methods
 * are deprecated and will be deleted in Task 67. New code should implement
 * only the generic lifecycle events.
 */
class ISyncHost
{
public:
    virtual ~ISyncHost() = default;

    // ---- Registry access (kept) ----
    virtual SyncBackend* backendById(const QString &id) = 0;
    virtual QHash<QString, SyncBackend*> backends() = 0;
    virtual ISyncConfigStore* configStore() = 0;

    // ---- Generic lifecycle events (G.9.a — new in Task 63) ----

    enum class ChangeKind { Created, Updated, Deleted };

    virtual void syncStarted(const QString &mappingId,
                             const Kalburator::Shape::LossProfile &pipelineLoss) {}

    virtual void syncFinished(const QString &mappingId,
                              const Kalburator::Sync::SyncResult &result) {}

    virtual void recordChanged(const QString &mappingId,
                               const QString &recordId,
                               ChangeKind kind) {}

    virtual ConflictResolution resolveConflict(const QString &mappingId,
                                               const QString &recordId,
                                               const Kalburator::Shape::CanonicalRecord &source,
                                               const Kalburator::Shape::CanonicalRecord &target,
                                               const Kalburator::Shape::CanonicalRecord &baseline)
    {
        Q_UNUSED(mappingId) Q_UNUSED(recordId)
        Q_UNUSED(source) Q_UNUSED(target) Q_UNUSED(baseline)
        return ConflictResolution::SourceWins;
    }

    virtual void progressChanged(const QString &mappingId,
                                 int current, int total,
                                 const QString &msg) {}

    virtual void phaseChanged(const QString &mappingId, int phase) {}

    virtual void errorOccurred(const QString &mappingId, const QString &msg) {}

    // ---- Deprecated calendar-typed methods (deleted in Task 67) ----

    [[deprecated("Use recordChanged() — deleted in G.9 Task 67")]]
    virtual bool applyIncidenceAddition(const QString &calendarId,
                                        const KCalendarCore::Incidence::Ptr &inc,
                                        bool stageForSync = true)
    { Q_UNUSED(calendarId) Q_UNUSED(inc) Q_UNUSED(stageForSync) return false; }

    [[deprecated("Use recordChanged() — deleted in G.9 Task 67")]]
    virtual bool applyIncidenceRemoval(const QString &calendarId,
                                       const QString &uid,
                                       bool stageForSync = true,
                                       const QDateTime &recurrenceId = {})
    { Q_UNUSED(calendarId) Q_UNUSED(uid) Q_UNUSED(stageForSync) Q_UNUSED(recurrenceId) return false; }

    [[deprecated("Use recordChanged() — deleted in G.9 Task 67")]]
    virtual bool applyIncidenceUpdate(const QString &calendarId,
                                      const KCalendarCore::Incidence::Ptr &inc,
                                      bool stageForSync = true)
    { Q_UNUSED(calendarId) Q_UNUSED(inc) Q_UNUSED(stageForSync) return false; }

    [[deprecated("Consumer-side responsibility — deleted in G.9 Task 67")]]
    virtual ICalendarCollection* collection() { return nullptr; }

    [[deprecated("Consumer-side responsibility — deleted in G.9 Task 67")]]
    virtual IIncidenceSource* incidenceSource() { return nullptr; }

    [[deprecated("Consumer-side responsibility — deleted in G.9 Task 67")]]
    virtual IIncidenceRegistry* incidenceRegistry() { return nullptr; }

    [[deprecated("Consumer-side responsibility — deleted in G.9 Task 67")]]
    virtual void unloadCalendar(const QString &calendarId)
    { Q_UNUSED(calendarId) }

    [[deprecated("Consumer-side responsibility — deleted in G.9 Task 67")]]
    virtual void generateSyncMappingsFromLogicalCalendars() {}
};

} // namespace Kalburator::Sync

#endif // ISYNCHOST_H
