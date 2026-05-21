#ifndef ISYNCHOST_H
#define ISYNCHOST_H

#include <QHash>
#include <QString>

#include "canonicalrecord.h"
#include "lossprofile.h"
#include "synctypes.h"

namespace Kalburator::Sync {

class SyncBackend;
class ISyncConfigStore;

/**
 * @brief Abstract interface decoupling sync engine from the application shell.
 *
 * G.9.a narrowed this interface to ~7 generic methods; the calendar-typed
 * methods were deleted in Phase G Task 67. New code should implement only
 * the generic lifecycle events below.
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

};

} // namespace Kalburator::Sync

#endif // ISYNCHOST_H
