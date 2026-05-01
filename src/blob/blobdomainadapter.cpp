#include "blobdomainadapter.h"

#include "blobbaselinestore.h"
#include "canonicalrecord.h"
#include "syncbackend.h"  // SyncBackend definition for IBlobBackend dispatch
#include "iblobbackend.h"
#include "synctypes.h"

#include <QHash>
#include <QMap>
#include <QSet>

namespace Kalburator::Sync {

namespace {

QHash<QString, BackendRecord> indexById(const QList<BackendRecord> &records)
{
    QHash<QString, BackendRecord> out;
    out.reserve(records.size());
    for (const auto &r : records) {
        out.insert(r.id, r);
    }
    return out;
}

EngineDiffOp makeUpdate(const BackendRecord &newState,
                        const BackendRecord &baseline)
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Update;
    op.record = newState;
    op.baselineRecord = baseline;
    return op;
}

EngineDiffOp makeCreate(const BackendRecord &newState)
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Create;
    op.record = newState;
    return op;
}

EngineDiffOp makeDelete(const BackendRecord &doomed,
                        const BackendRecord &baseline)
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Delete;
    op.record = doomed;
    op.baselineRecord = baseline;
    return op;
}

EngineDiffOp makeConflict(const BackendRecord &source,
                          const BackendRecord &target,
                          const BackendRecord &baseline)
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Conflict;
    op.record = source;
    op.targetRecord = target;
    op.baselineRecord = baseline;
    return op;
}

bool resolvePolicy(ConflictResolution policy,
                   const BackendRecord &source,
                   const BackendRecord &target,
                   bool *sourceWins)
{
    switch (policy) {
        case ConflictResolution::SourceWins:
            *sourceWins = true;
            return true;
        case ConflictResolution::TargetWins:
            *sourceWins = false;
            return true;
        case ConflictResolution::LastWriteWins:
            *sourceWins = source.lastModified >= target.lastModified;
            return true;
        case ConflictResolution::Skip:
        case ConflictResolution::AskUser:
        case ConflictResolution::Duplicate:
        case ConflictResolution::CustomMerge:
            return false;
    }
    return false;
}

} // namespace

BlobDomainAdapter::BlobDomainAdapter() = default;
BlobDomainAdapter::~BlobDomainAdapter() = default;

void BlobDomainAdapter::setBaselineStore(BlobBaselineStore *store) noexcept
{
    m_baselineStore = store;
}

void BlobDomainAdapter::setBackendId(const QString &backendId)
{
    m_backendId = backendId;
}

void BlobDomainAdapter::setCollectionId(const QString &collectionId)
{
    m_collectionId = collectionId;
}

QList<BackendRecord> BlobDomainAdapter::fetchRecords(
    SyncBackend *backend, const QString &collectionId)
{
    return fetchRecordsBlob(backend, collectionId);
}

QList<BackendRecord> BlobDomainAdapter::fetchRecordsBlob(
    IBlobBackend *backend, const QString &collectionId) const
{
    if (!backend) {
        return {};
    }
    return backend->loadRecords(collectionId);
}

EngineDiff BlobDomainAdapter::diff(
    const QList<BackendRecord> &source,
    const QList<BackendRecord> &target,
    const QList<BackendRecord> &baseline,
    const BackendCapabilities & /*sourceCaps*/,
    const BackendCapabilities & /*targetCaps*/) const
{
    EngineDiff result;

    const QHash<QString, BackendRecord> sById = indexById(source);
    const QHash<QString, BackendRecord> tById = indexById(target);
    const QHash<QString, BackendRecord> bById = indexById(baseline);

    QSet<QString> allIds;
    for (auto it = sById.constBegin(); it != sById.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = tById.constBegin(); it != tById.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = bById.constBegin(); it != bById.constEnd(); ++it) allIds.insert(it.key());

    for (const QString &id : allIds) {
        const bool hasS = sById.contains(id);
        const bool hasT = tById.contains(id);
        const bool hasB = bById.contains(id);

        const BackendRecord sRec = hasS ? sById.value(id) : BackendRecord{};
        const BackendRecord tRec = hasT ? tById.value(id) : BackendRecord{};
        const BackendRecord bRec = hasB ? bById.value(id) : BackendRecord{};

        if (hasS && hasT && hasB) {
            const bool sChanged = (sRec.contentHash != bRec.contentHash);
            const bool tChanged = (tRec.contentHash != bRec.contentHash);
            if (!sChanged && !tChanged) {
                continue;
            }
            if (sChanged && !tChanged) {
                result.toTarget.append(makeUpdate(sRec, bRec));
            } else if (!sChanged && tChanged) {
                result.toSource.append(makeUpdate(tRec, bRec));
            } else {
                result.toTarget.append(makeConflict(sRec, tRec, bRec));
            }
        } else if (!hasS && hasT && hasB) {
            result.toTarget.append(makeDelete(bRec, bRec));
        } else if (hasS && !hasT && hasB) {
            result.toSource.append(makeDelete(bRec, bRec));
        } else if (hasS && !hasT && !hasB) {
            result.toTarget.append(makeCreate(sRec));
        } else if (!hasS && hasT && !hasB) {
            result.toSource.append(makeCreate(tRec));
        } else if (hasS && hasT && !hasB) {
            // Both sides have the record, no baseline. The legacy
            // BlobSyncEngine (deleted F1 Task 10) historically fell through
            // this case; preserve "in sync if hashes match, otherwise treat
            // as a both-modified conflict".
            if (sRec.contentHash != tRec.contentHash) {
                result.toTarget.append(makeConflict(sRec, tRec, BackendRecord{}));
            }
        }
        // (!hasS && !hasT && hasB) is a vestigial baseline; nothing to do.
    }

    return result;
}

EngineMerge BlobDomainAdapter::merge(const EngineDiff &d,
                                     ConflictResolution policy) const
{
    EngineMerge m;

    auto routeOp = [&m](const EngineDiffOp &op, bool toTarget) {
        QList<BackendRecord> &bucket = toTarget ? m.finalTarget : m.finalSource;
        switch (op.kind) {
            case EngineDiffOp::Kind::Create:
            case EngineDiffOp::Kind::Update:
                bucket.append(op.record);
                m.updatedBaselines.append(op.record);
                return;
            case EngineDiffOp::Kind::Delete: {
                BackendRecord doomed = op.record;
                doomed.isDeleted = true;
                bucket.append(doomed);
                return;
            }
            case EngineDiffOp::Kind::Conflict:
                Q_UNREACHABLE();
                return;
        }
    };

    for (const auto &op : d.toSource) {
        if (op.kind == EngineDiffOp::Kind::Conflict) {
            // Conflicts are emitted to toTarget by diff(); guard anyway.
            continue;
        }
        routeOp(op, /*toTarget=*/false);
    }

    for (const auto &op : d.toTarget) {
        if (op.kind != EngineDiffOp::Kind::Conflict) {
            routeOp(op, /*toTarget=*/true);
            continue;
        }
        bool sourceWins = false;
        if (resolvePolicy(policy, op.record, op.targetRecord, &sourceWins)) {
            EngineDiffOp resolved;
            resolved.kind = EngineDiffOp::Kind::Update;
            resolved.baselineRecord = op.baselineRecord;
            if (sourceWins) {
                resolved.record = op.record;
                routeOp(resolved, /*toTarget=*/true);
            } else {
                resolved.record = op.targetRecord;
                routeOp(resolved, /*toTarget=*/false);
            }
            ++m.conflictsResolved;
        } else {
            ++m.conflictsDeferred;
        }
    }

    return m;
}

EngineApplyResult BlobDomainAdapter::applyChanges(
    const EngineMerge &m, SyncBackend *destination,
    const QString &collectionId, const TranscodingPlan & /*plan*/)
{
    return applyChangesBlob(m, destination, collectionId);
}

EngineApplyResult BlobDomainAdapter::applyChangesBlob(
    const EngineMerge &m, IBlobBackend *destination,
    const QString &collectionId)
{
    EngineApplyResult r;
    if (!destination) {
        r.success = false;
        r.errorMessage = QStringLiteral(
            "BlobDomainAdapter::applyChanges: null destination");
        return r;
    }

    const QHash<QString, BackendRecord> currentById =
        indexById(destination->loadRecords(collectionId));

    for (const auto &rec : m.finalTarget) {
        if (rec.isDeleted) {
            if (currentById.contains(rec.id)) {
                if (destination->deleteRecord(rec.id)) {
                    ++r.deleted;
                } else {
                    r.success = false;
                    r.errorMessage =
                        QStringLiteral("deleteRecord failed: %1").arg(rec.id);
                }
            }
            // else: already absent — no-op, treated as success.
        } else if (currentById.contains(rec.id)) {
            if (destination->updateRecord(rec)) {
                ++r.updated;
                r.appliedBaselines.append(rec);
            } else {
                r.success = false;
                r.errorMessage =
                    QStringLiteral("updateRecord failed: %1").arg(rec.id);
            }
        } else {
            const QString newId = destination->createRecord(collectionId, rec);
            if (!newId.isEmpty()) {
                ++r.created;
                BackendRecord baseline = rec;
                baseline.id = newId;
                r.appliedBaselines.append(baseline);
            } else {
                r.success = false;
                r.errorMessage =
                    QStringLiteral("createRecord failed: %1").arg(rec.id);
            }
        }
    }

    return r;
}

namespace {
// Shape used for blob-path canonical records (G.4).
const Kalburator::Shape::Shape kBlobShape{
    Kalburator::Shape::DomainId{QStringLiteral("blob")},
    Kalburator::Shape::EncodingId{QStringLiteral("raw")}};
} // namespace

QList<BackendRecord> BlobDomainAdapter::loadBaselines(
    const QString &mappingId) const
{
    if (!m_baselineStore || mappingId.isEmpty()) {
        return {};
    }
    QList<BackendRecord> out;
    for (const auto &canonical : m_baselineStore->baselinesForMappingV3(mappingId)) {
        BackendRecord rec;
        rec.id          = canonical.recordId;
        rec.contentHash = QString::fromUtf8(canonical.data);
        out.append(rec);
    }
    return out;
}

bool BlobDomainAdapter::saveBaselines(
    const QString &mappingId, const QList<BackendRecord> &baselines)
{
    if (!m_baselineStore || mappingId.isEmpty()) {
        return false;
    }
    bool ok = true;
    for (const auto &rec : baselines) {
        if (rec.id.isEmpty() || rec.isDeleted) {
            continue;
        }
        Kalburator::Shape::CanonicalRecord canonical;
        canonical.recordId = rec.id;
        canonical.shape    = kBlobShape;
        canonical.data     = rec.contentHash.toUtf8();
        if (!m_baselineStore->setBaselineV3(mappingId, canonical)) {
            ok = false;
        }
    }
    return ok;
}

} // namespace Kalburator::Sync
