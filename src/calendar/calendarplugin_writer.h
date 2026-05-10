#pragma once

#include "recordwriter.h"
#include "transcodingplan.h"

namespace Kalburator::Sync {
class SyncBackend;
class ICalendarCollection;
}

namespace Kalburator::Calendar {

/// IRecordWriter for the calendar domain. Wraps the existing
/// SyncTransaction machinery — under the hood drives
/// Create/Update/DeleteIncidenceItem against the SyncBackend, then
/// commits the transaction on the backend's own thread via
/// BlockingQueuedConnection (mirroring the pattern in
/// `CalendarDomainAdapter::applyChangesToBackend`, which this lifts
/// from for Phase Ia.5).
///
/// **Phase K.4 (threading + setup contract)**:
///
/// - `threading()` returns `WorkerThread`: the engine MUST call
///   `apply()` from a thread that is NOT the backend's thread, because
///   the writer uses `Qt::BlockingQueuedConnection` to the backend
///   thread internally for the transaction commit.
///
/// - `prepareForApply(ctx)` injects the host `MemoryCalendar*` (when
///   the engine has one) and the per-write `TranscodingPlan`.
///   Replaces the K.3-and-earlier `dynamic_cast<CalendarPluginWriter>`
///   + `setCollection()` / `setTranscodingPlan()` engine-side dance.
///
/// - When `ctx.calendarCollection` is null (e.g. for
///   RemoteCalendarBackend used purely via the blob path), `apply()`
///   degrades gracefully: it parses the incidence from the raw iCal
///   bytes in `BackendRecord::data` and pushes through the backend's
///   IBlobBackend surface (createRecord / updateRecord / deleteRecord)
///   on the backend thread.
class CalendarPluginWriter : public Kalburator::Shape::RecordWriter {
public:
    explicit CalendarPluginWriter(Kalburator::Sync::SyncBackend *backend);
    ~CalendarPluginWriter() override;

    // ---- IRecordWriter threading + setup ------------------------------------
    Threading threading() const override
        { return Threading::WorkerThread; }

    void prepareForApply(const ApplyContext &ctx) override;

    // ---- Legacy direct setters (kept for tests; engine no longer uses) ------
    void setCollection(Kalburator::Sync::ICalendarCollection *collection);
    void setTranscodingPlan(const Kalburator::Sync::TranscodingPlan &plan);

    bool apply(
        const QString &collectionId,
        const QList<Kalburator::Sync::BackendRecord> &creates,
        const QList<Kalburator::Sync::BackendRecord> &updates,
        const QStringList &deletes) override;

private:
    Kalburator::Sync::SyncBackend         *m_backend    = nullptr;
    /// Optional ICalendarCollection set via setCollection() — used
    /// only when prepareForApply() did NOT supply a calendarCollection.
    Kalburator::Sync::ICalendarCollection *m_collection = nullptr;
    Kalburator::Sync::TranscodingPlan      m_plan;
    /// Direct per-call MemoryCalendar from prepareForApply(); takes
    /// precedence over `m_collection->calendar()` when set. May be
    /// null (legitimate: blob-only target).
    KCalendarCore::MemoryCalendar         *m_directCalendar = nullptr;
    /// True after prepareForApply() has been called at least once.
    /// Used to distinguish "engine deliberately said: no host
    /// MemoryCalendar, use blob path" (allow blob fallback) from
    /// "legacy caller forgot to call setCollection()" (return false).
    bool                                   m_prepared = false;
};

} // namespace Kalburator::Calendar
