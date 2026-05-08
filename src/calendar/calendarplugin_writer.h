#pragma once

#include "irecordwriter.h"
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
/// **Required setup contract (Task 13 / engine wiring).** The writer
/// needs an `ICalendarCollection*` to translate `calendarId` ->
/// `MemoryCalendar*` for the SyncTransaction items. The engine is
/// responsible for calling `setCollection()` after constructing the
/// writer (via `DomainPlugin::createWriter()`) and before invoking
/// `apply()`. Without a collection set, `apply()` returns false.
///
/// **Skipped for v1 (revisit when wired into the engine).**
/// 1. **Cancellation oracle.** The existing `applyChangesToBackend`
///    threads `m_cancelOracle()` through the per-record loop to
///    short-circuit on cancellation. The unified engine's cancel path
///    will reach the writer differently; v1 commits straight through.
/// 2. **`useTargetRecord` / `mappingId` parameters.** With
///    `BackendRecord`-typed creates/updates the engine has already
///    picked the side; the writer just applies what it's given. The
///    transaction id is therefore a simpler
///    `"calendar-writer-<collectionId>"`.
/// 3. **`TranscodingPlan`.** Defaulted to `TranscodingPlan{}` for v1.
///    The engine context that holds the real plan can flow through
///    later (or via a setter analogous to `setCollection`).
/// 4. **`transcodingWarning` connect/disconnect dance.** That belongs
///    at the call site (engine), not in the writer body — same as
///    documented on `CalendarDomainAdapter::applyChangesToBackend`
///    (header comment). Engine is responsible.
class CalendarPluginWriter : public Kalburator::Shape::IRecordWriter {
public:
    explicit CalendarPluginWriter(Kalburator::Sync::SyncBackend *backend);
    ~CalendarPluginWriter() override;

    /// Set the calendar collection lookup. The writer needs this to
    /// resolve calendarId -> MemoryCalendar* for the SyncTransaction
    /// items. (CalendarDomainAdapter has the same dependency at
    /// `calendardomainadapter.h:147`; the engine sets it before
    /// dispatch.)
    void setCollection(Kalburator::Sync::ICalendarCollection *collection);
    void setTranscodingPlan(const Kalburator::Sync::TranscodingPlan &plan);

    bool apply(
        const QString &collectionId,
        const QList<Kalburator::Sync::BackendRecord> &creates,
        const QList<Kalburator::Sync::BackendRecord> &updates,
        const QStringList &deletes) override;

private:
    Kalburator::Sync::SyncBackend         *m_backend    = nullptr;
    Kalburator::Sync::ICalendarCollection *m_collection = nullptr;
    Kalburator::Sync::TranscodingPlan      m_plan;
};

} // namespace Kalburator::Calendar
