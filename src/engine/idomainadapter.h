#ifndef KALBURATOR_ENGINE_IDOMAINADAPTER_H
#define KALBURATOR_ENGINE_IDOMAINADAPTER_H

#include "backendrecord.h"
#include "enginediff.h"
#include "synctypes.h"            // ConflictResolution
#include "transcodingplan.h"      // TranscodingPlan
#include "backendcapabilities.h"  // BackendCapabilities

#include <QList>
#include <QString>

namespace Kalburator::Sync {

class SyncBackend;

/// Per-domain glue between the unified SyncEngine and its backends.
///
/// Phase F1 introduces this seam to collapse the two parallel sync
/// engines (calendar SyncEngine + worker, plus the now-deleted blob engine)
/// into a single SyncEngine that drives an IDomainAdapter*. Calendar
/// adapter parses iCal text + uses IncidenceDiff; blob adapter is
/// identity serde + hash-equality diff.
///
/// **Boundary:** pure BackendRecord. Engine never sees parsed
/// Incidence::Ptr or domain-typed payload objects. Adapter
/// internally caches parsed forms if profiling warrants.
///
/// **Ownership:** the engine borrows IDomainAdapter*; adapters are
/// constructed and owned by the consumer (PlanStan / WildPalms /
/// libkalburator tests).
///
/// **Threading:** all methods run on the SyncEngine's worker thread
/// during a sync. Adapters must be thread-safe with respect to
/// their own state but do not need to support concurrent calls
/// from multiple SyncEngines (one engine ↔ one adapter set).
class IDomainAdapter
{
public:
    virtual ~IDomainAdapter() = default;

    /// Discriminator. "calendar", "blob", or future "vcard". Used
    /// for diagnostics and Phase G's plugin lookup. Mappings carry
    /// a domain string that the engine matches against this.
    virtual QString domainType() const = 0;

    /// Fetch records from a backend's collection. The adapter knows
    /// whether to use the calendar-shaped or blob-shaped backend
    /// methods; the engine never has to choose.
    virtual QList<BackendRecord> fetchRecords(
        SyncBackend* backend,
        const QString& collectionId) = 0;

    /// Compute the diff between source / target / baseline. Each
    /// list is the full current state; the adapter reconstructs
    /// baseline absence / change correctly. Capabilities feed
    /// transcoding decisions (calendar adapter only — blob ignores).
    virtual EngineDiff diff(const QList<BackendRecord>& source,
                            const QList<BackendRecord>& target,
                            const QList<BackendRecord>& baseline,
                            const BackendCapabilities& sourceCaps,
                            const BackendCapabilities& targetCaps) const = 0;

    /// Resolve conflicts in the diff according to policy and produce
    /// the merged record set. The optional override lets callers
    /// request mirror-direction semantics (MirrorAToB / MirrorBToA)
    /// without persisting that direction on the mapping. Defaults to
    /// ExecutionOverride{} (Direction::Default = bidirectional merge).
    virtual EngineMerge merge(const EngineDiff& diff,
                              ConflictResolution policy,
                              const ExecutionOverride& executionOverride = {}) const = 0;

    /// Apply the merged result to the destination backend. Returns
    /// the new baseline records (one per applied write) on success;
    /// on partial failure, the result contains what was written
    /// before the error so the engine can update baselines for
    /// successful writes only.
    ///
    /// `plan` carries Phase E's transcoding decision; calendar
    /// adapter consumes it, blob adapter ignores.
    virtual EngineApplyResult applyChanges(
        const EngineMerge& merge,
        SyncBackend* destination,
        const QString& collectionId,
        const TranscodingPlan& plan = TranscodingPlan{}) = 0;

    /// Load and save baselines. Adapter holds its own baseline
    /// store internally (CalendarBaselineStore for calendar,
    /// BlobBaselineStore for blob); engine calls these around the
    /// diff/apply flow.
    virtual QList<BackendRecord> loadBaselines(
        const QString& mappingId) const = 0;

    virtual bool saveBaselines(
        const QString& mappingId,
        const QList<BackendRecord>& baselines) = 0;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_ENGINE_IDOMAINADAPTER_H
