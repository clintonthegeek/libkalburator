#ifndef KALBURATOR_BLOB_BLOBDOMAINADAPTER_H
#define KALBURATOR_BLOB_BLOBDOMAINADAPTER_H

#include "idomainadapter.h"

#include <QString>

namespace Kalburator::Sync {

class IBlobBackend;
class BlobBaselineStore;

/// Concrete IDomainAdapter for the blob domain.
///
/// Hash-equality diff over BackendRecord. Identity (de)serialization —
/// the adapter does not parse `data`. Conflict resolution per
/// ConflictResolution policy.
///
/// Phase F1 absorbs the body of BlobSyncEngine::twoWayWithBaseline into
/// this adapter's diff/merge/applyChanges pipeline. The free-function
/// blob engine remains in place until Group 4 deletes it.
///
/// **Baseline keying.** The IDomainAdapter contract takes only
/// `mappingId` for load/saveBaselines, but the design pins blob baselines
/// to BlobBaselineStore's triple-keyed (backendId, collectionId,
/// recordId) table. The adapter holds backendId/collectionId via setters
/// and uses them when persisting. `mappingId` is currently unused for
/// blob; reserved for future per-mapping bookkeeping.
///
/// **Backend type.** IDomainAdapter::fetchRecords / applyChanges take
/// `SyncBackend*` because the calendar adapter needs the QObject signals.
/// SyncBackend IS-A IBlobBackend, so the calendar-engine path works
/// unchanged. For the blob one-shot path (WildPalms's syncrunner_wp.cpp
/// migration in Group 4 / Task 9), callers hold IBlobBackend* and use
/// the non-virtual helpers below.
///
/// **Encoding deletes in EngineMerge.** finalSource / finalTarget
/// carry both writes and deletes; `BackendRecord::isDeleted == true`
/// flags a deletion. applyChanges() inspects the flag and routes to
/// IBlobBackend::deleteRecord vs createRecord/updateRecord.
class BlobDomainAdapter final : public IDomainAdapter
{
public:
    BlobDomainAdapter();
    ~BlobDomainAdapter() override;

    BlobDomainAdapter(const BlobDomainAdapter&) = delete;
    BlobDomainAdapter& operator=(const BlobDomainAdapter&) = delete;

    // --- Configuration (set per-sync by engine / one-shot caller) ---

    void setBaselineStore(BlobBaselineStore* store) noexcept;
    void setBackendId(const QString& backendId);
    void setCollectionId(const QString& collectionId);

    BlobBaselineStore* baselineStore() const noexcept { return m_baselineStore; }
    QString backendId() const noexcept { return m_backendId; }
    QString collectionId() const noexcept { return m_collectionId; }

    // --- IDomainAdapter ---

    QString domainType() const override { return QStringLiteral("blob"); }

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

    // --- IBlobBackend* helpers (one-shot path, used by SyncEngine::runBlob*) ---

    /// IBlobBackend-typed equivalent of fetchRecords. Identical body.
    QList<BackendRecord> fetchRecordsBlob(IBlobBackend* backend,
                                          const QString& collectionId) const;

    /// IBlobBackend-typed equivalent of applyChanges. Used by SyncEngine's
    /// one-shot blob facade where the destination is plain IBlobBackend.
    EngineApplyResult applyChangesBlob(const EngineMerge& merge,
                                       IBlobBackend* destination,
                                       const QString& collectionId);

private:
    BlobBaselineStore* m_baselineStore = nullptr;
    QString            m_backendId;
    QString            m_collectionId;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_BLOB_BLOBDOMAINADAPTER_H
