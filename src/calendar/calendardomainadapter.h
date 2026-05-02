#ifndef KALBURATOR_CALENDAR_CALENDARDOMAINADAPTER_H
#define KALBURATOR_CALENDAR_CALENDARDOMAINADAPTER_H

#include "idomainadapter.h"
#include "syncdiff.h"

#include <QList>
#include <QMap>
#include <QString>

#include <functional>

namespace Kalburator::Sync {

class CalendarBaselineStore;
class ICalendarCollection;
class TranscodingRouter;

/// Concrete IDomainAdapter for the calendar domain.
///
/// Wraps the existing calendar-typed diff engine
/// (`computeSyncDiff` / `computeQuickDiff` from
/// `src/transcoding/syncdiff.h`) so the unified `SyncEngine` can drive
/// it through the BackendRecord-shaped IDomainAdapter contract. The
/// adapter parses iCal text from `BackendRecord::data` into
/// `KCalendarCore::Incidence::Ptr` for the diff, then translates the
/// resulting calendar-typed `SyncDiff` back to BackendRecord-shaped
/// `EngineDiff` for the engine.
///
/// **F1 scope:** diff/merge/loadBaselines/saveBaselines are operational.
/// `applyChanges()` is a deferred stub for F1 Task 3 — Task 5 wires the
/// full applyChangesToBackend body (SyncTransaction + Phase E
/// writeFinished-capture wrappers + cross-thread commit marshalling)
/// into adapter scope. The four diff/merge unit tests
/// (`tst_calendar_domain_adapter`) cover the operational surface.
///
/// **Conflict resolution.** `merge()` handles SourceWins, TargetWins,
/// and LastWriteWins for BothModified conflicts. The richer
/// resolveConflictAutomatically logic (ModifyDelete fixups, Duplicate
/// clone-and-rename, Skip / AskUser deferral) is preserved by the
/// engine's inner worker today; Task 5 collapses it into this adapter
/// alongside the wrapper integration.
///
/// **Encoding deletes.** Following the convention established by
/// BlobDomainAdapter (FINDINGS 2026-04-29), deletes in
/// `EngineMerge::finalSource/finalTarget` are flagged via
/// `BackendRecord::isDeleted == true`.
class CalendarDomainAdapter final : public IDomainAdapter
{
public:
    explicit CalendarDomainAdapter(const TranscodingRouter& router);
    ~CalendarDomainAdapter() override;

    CalendarDomainAdapter(const CalendarDomainAdapter&) = delete;
    CalendarDomainAdapter& operator=(const CalendarDomainAdapter&) = delete;

    // --- Configuration (set per-sync by engine) ---

    void setBaselineStore(CalendarBaselineStore* store) noexcept;
    void setCollection(ICalendarCollection* collection) noexcept;

    /// Engine sets this so loadBaselines/saveBaselines and quick-vs-full
    /// diff selection know which mapping is in flight.
    void setSyncMode(SyncMode mode) noexcept;

    /// Force the quick (no-baseline 2-way) diff path. Engine sets this
    /// when there are no baselines for the mapping. Default: full
    /// 3-way `computeSyncDiff`.
    void setUseQuickPath(bool quick) noexcept;

    /// F2 Task 19: lock-free oracle for cancellation observation.
    /// Installed by the engine at registration; called from the
    /// per-record loop in applyChangesToBackend to short-circuit
    /// the apply phase when cancellation arrives. Returns true iff
    /// cancellation has been observed.
    using CancelOracle = std::function<bool()>;
    void setCancelOracle(CancelOracle oracle) { m_cancelOracle = std::move(oracle); }

    CalendarBaselineStore* baselineStore() const noexcept { return m_baselineStore; }
    ICalendarCollection*   collection()    const noexcept { return m_collection; }
    SyncMode               syncMode()      const noexcept { return m_syncMode; }

    // --- IDomainAdapter ---

    QString domainType() const override { return QStringLiteral("calendar"); }

    QList<BackendRecord> fetchRecords(SyncBackend* backend,
                                      const QString& collectionId) override;

    EngineDiff diff(const QList<BackendRecord>& source,
                    const QList<BackendRecord>& target,
                    const QList<BackendRecord>& baseline,
                    const BackendCapabilities& sourceCaps,
                    const BackendCapabilities& targetCaps) const override;

    EngineMerge merge(const EngineDiff& diff,
                      ConflictResolution policy,
                      const ExecutionOverride& executionOverride = {}) const override;

    EngineApplyResult applyChanges(const EngineMerge& merge,
                                   SyncBackend* destination,
                                   const QString& collectionId,
                                   const TranscodingPlan& plan = TranscodingPlan{}) override;

    QList<BackendRecord> loadBaselines(const QString& mappingId) const override;
    bool                 saveBaselines(const QString& mappingId,
                                       const QList<BackendRecord>& baselines) override;

    // --- Calendar-typed convenience entry points (F1 Task 5) ---
    //
    // These mirror the eventual BackendRecord-typed IDomainAdapter
    // contract but take parsed calendar types so SyncEngine's inner
    // worker doesn't pay an iCal re-parse round-trip for every record.
    // Internals route to the same `computeSyncDiff` / `computeQuickDiff`
    // and `SyncTransaction` machinery as the BackendRecord-typed
    // overloads.

    /// Calendar-typed diff: dispatches to `computeQuickDiff` (when
    /// `useQuickPath` or `baselines` empty) or `computeSyncDiff`. The
    /// SyncDiff result drives the engine worker's conflict-resolution
    /// and apply-changes loops directly — no BackendRecord round-trip.
    SyncDiff diffCalendarRecords(const QList<SyncRecord>& source,
                                 const QList<SyncRecord>& target,
                                 const QMap<QString, QString>& baselines,
                                 SyncMode mode,
                                 bool useQuickPath) const;

    /// Calendar-typed apply: builds a SyncTransaction populated with
    /// CreateIncidenceItem / UpdateIncidenceItem / DeleteIncidenceItem
    /// wrappers and commits it on the main thread (BlockingQueuedConnection).
    /// Returns true on success; on failure, fills `*errorMessage` with the
    /// concatenated per-item errors. Caller is responsible for connecting
    /// the backend's `transcodingWarning` signal to its preferred sink
    /// around the call (the connection is per-apply and best left at the
    /// call site for now — adapter promotion to QObject is F2 territory).
    bool applyChangesToBackend(SyncBackend* backend,
                               const QString& calendarId,
                               const QList<SyncChange>& changes,
                               bool useTargetRecord,
                               const QString& mappingId,
                               const TranscodingPlan& plan,
                               QString* errorMessage);

private:
    const TranscodingRouter& m_router;
    CalendarBaselineStore*   m_baselineStore = nullptr;
    ICalendarCollection*     m_collection    = nullptr;
    SyncMode                 m_syncMode      = SyncMode::TwoWay;
    bool                     m_useQuickPath  = false;
    CancelOracle             m_cancelOracle;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_CALENDAR_CALENDARDOMAINADAPTER_H
