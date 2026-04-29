#ifndef KALBURATOR_CALENDAR_CALENDARDOMAINADAPTER_H
#define KALBURATOR_CALENDAR_CALENDARDOMAINADAPTER_H

#include "idomainadapter.h"

#include <QString>

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
/// existing SyncWorker today; Task 5 collapses it into this adapter
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
                      ConflictResolution policy) const override;

    EngineApplyResult applyChanges(const EngineMerge& merge,
                                   SyncBackend* destination,
                                   const QString& collectionId,
                                   const TranscodingPlan& plan = TranscodingPlan{}) override;

    QList<BackendRecord> loadBaselines(const QString& mappingId) const override;
    bool                 saveBaselines(const QString& mappingId,
                                       const QList<BackendRecord>& baselines) override;

private:
    const TranscodingRouter& m_router;
    CalendarBaselineStore*   m_baselineStore = nullptr;
    ICalendarCollection*     m_collection    = nullptr;
    SyncMode                 m_syncMode      = SyncMode::TwoWay;
    bool                     m_useQuickPath  = false;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_CALENDAR_CALENDARDOMAINADAPTER_H
