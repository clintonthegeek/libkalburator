#ifndef KALBURATOR_SYNC_WRITEOPERATION_H
#define KALBURATOR_SYNC_WRITEOPERATION_H

#include <QStringList>

#include "syncoperation.h"

namespace Kalburator::Sync {

/**
 * @brief Operation that applies a classified WriterBatch (creates/updates/
 * deletes) to a backend.
 *
 * E5.3 (sync-excellence campaign, audit B7 / CP-A): the return type of
 * `SyncBackendBase::applyRecords()`, the neutral entry point that replaces
 * the engine's old thread-blocking `RecordWriter::apply()` dispatch. Unlike
 * `PushOperation`/`DeleteOperation` (calendar/syncoperation.h), which are
 * `KCalendarCore::Incidence::Ptr`-typed, `WriteOperation` is domain-neutral
 * (`BackendRecord`-based) — it belongs in `sync/` alongside `SyncBackendBase`
 * itself, not `calendar/`.
 *
 * Per-record success/failure is tracked the same way as
 * `PushOperation`/`DeleteOperation` (`addSucceededUid`/`addFailedUid`), but
 * over the union of the batch's creates+updates+deletes ids, since
 * `applyRecords()` dispatches all three kinds from one call. Terminal-state
 * contract matches that precedent: `complete()` (state Succeeded) as long as
 * at least one record succeeded (or the batch was empty); `fail()` only when
 * every attempted record failed.
 */
class WriteOperation : public SyncOperation
{
    Q_OBJECT

public:
    explicit WriteOperation(const QString &calendarId, QObject *parent = nullptr);

    /// Ids (BackendRecord::id, over creates+updates, or the delete id) that
    /// were applied successfully.
    QStringList succeededUids() const { return m_succeededUids; }

    /// Ids that failed to apply (network/backend error, or a watchdog
    /// timeout — see RemoteCalendarBackend::applyRecords()).
    QStringList failedUids() const { return m_failedUids; }

    /// E9.2 (sync-excellence campaign, O34): the backend's own expected
    /// post-write collection revision/fingerprint, computed incrementally
    /// from its fetch-time snapshot plus exactly the files THIS call wrote
    /// or deleted (no full re-scan, no foreign-edit absorption). Empty for
    /// backends that don't compute one (the default; remote CalDAV always
    /// stays empty here — no server-side CTag guessing). The engine, not
    /// the backend, decides whether/where to persist this as a
    /// sync-progress token (see SyncEngineWorker::applyBatch /
    /// SyncEngine::FreshSyncState) — engine ownership of sync-progress
    /// tokens (the two-token architecture) is unchanged; this only
    /// supplies a fresher VALUE than the pre-fetch snapshot would.
    QString resultRevision() const { return m_resultRevision; }
    void setResultRevision(const QString &revision) { m_resultRevision = revision; }

    // Modification methods (called by backends)
    void addSucceededUid(const QString &uid);
    void addFailedUid(const QString &uid);

private:
    QStringList m_succeededUids;
    QStringList m_failedUids;
    QString m_resultRevision;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_WRITEOPERATION_H
