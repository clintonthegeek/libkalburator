#include "blobbatchdiff.h"

#include "canonicalrecord.h"
#include "conflictpolicy.h"
#include "irecordmerger.h"

#include <QHash>
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
                        const BackendRecord &baseline,
                        const BackendRecord &otherSide = {})
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Update;
    op.record = newState;
    op.baselineRecord = baseline;
    op.targetRecord = otherSide;  // Carries the unchanged side's record.
                                   // For toTarget Updates: the target's
                                   // current (unchanged) state. Used by
                                   // MirrorBToA to push target's version
                                   // back to source.
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

EngineMerge mergeMirrorAToB(const EngineDiff &d)
{
    // Push everything that originated on source to target; delete
    // everything that is target-only (appears in toSource as Create).
    EngineMerge m;

    for (const auto &op : d.toTarget) {
        if (op.kind == EngineDiffOp::Kind::Create
            || op.kind == EngineDiffOp::Kind::Update) {
            // Source record must be written to target.
            m.finalTarget.append(op.record);
            m.updatedBaselines.append(op.record);
        } else if (op.kind == EngineDiffOp::Kind::Conflict) {
            // Conflict: source wins — push source record to target.
            m.finalTarget.append(op.record);
            m.updatedBaselines.append(op.record);
            ++m.conflictsResolved;
        }
        // Delete ops in toTarget mean "source deleted a record that
        // was in baseline" — carry through to target.
        else if (op.kind == EngineDiffOp::Kind::Delete) {
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalTarget.append(doomed);
        }
    }

    // Target-only records appear in toSource as Create ops (no
    // baseline, not in source). Mirror semantics: delete them from
    // target.
    for (const auto &op : d.toSource) {
        if (op.kind == EngineDiffOp::Kind::Create) {
            // Record exists only on target; delete it from target.
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalTarget.append(doomed);
        }
        // Update / Delete ops in toSource mean target-changed or
        // target-deleted records. Mirror ignores target changes.
    }

    return m;
}

EngineMerge mergeMirrorBToA(const EngineDiff &d)
{
    // Symmetric: push everything from target to source; delete
    // everything that is source-only.
    EngineMerge m;

    for (const auto &op : d.toSource) {
        if (op.kind == EngineDiffOp::Kind::Create
            || op.kind == EngineDiffOp::Kind::Update) {
            // Target record must be written to source.
            m.finalSource.append(op.record);
            m.updatedBaselines.append(op.record);
        } else if (op.kind == EngineDiffOp::Kind::Delete) {
            // Target deleted a record that was in baseline — carry
            // through to source.
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalSource.append(doomed);
        }
    }

    // Conflict ops land in toTarget. For MirrorBToA target wins — push
    // the target record (op.targetRecord) to source.
    for (const auto &op : d.toTarget) {
        if (op.kind == EngineDiffOp::Kind::Conflict) {
            m.finalSource.append(op.targetRecord);
            m.updatedBaselines.append(op.targetRecord);
            ++m.conflictsResolved;
        }
        // Source-only records appear in toTarget as Create ops — delete
        // them from source.
        else if (op.kind == EngineDiffOp::Kind::Create) {
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalSource.append(doomed);
        }
        else if (op.kind == EngineDiffOp::Kind::Update) {
            // Source changed vs baseline; target unchanged.
            // MirrorBToA semantics: target wins — push target's current
            // (unchanged) version to source. op.targetRecord carries
            // target's full record (set by diff() for the sChanged &&
            // !tChanged case).
            m.finalSource.append(op.targetRecord);
            m.updatedBaselines.append(op.targetRecord);
        }
    }

    return m;
}

} // namespace

EngineDiff blobBatchDiff(const QList<BackendRecord> &source,
                         const QList<BackendRecord> &target,
                         const QList<BackendRecord> &baseline,
                         const BackendCapabilities & /*sourceCaps*/,
                         const BackendCapabilities & /*targetCaps*/)
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
                // Pass tRec as otherSide so MirrorBToA can push target's
                // current (unchanged) version to source without a second
                // lookup.
                result.toTarget.append(makeUpdate(sRec, bRec, tRec));
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

EngineMerge blobBatchMergeWithPlugin(
    const EngineDiff &d,
    ConflictResolution policy,
    const ExecutionOverride &executionOverride,
    Kalburator::Shape::IRecordMerger *customMerger,
    const Kalburator::Shape::Shape &canonical)
{
    // Phase Ia.5 Task 9 / Task 16: variant of the default 3-way merge that
    // consults the plugin's IRecordMerger to resolve conflicts the policy
    // can't auto-resolve. For mirror overrides and policies that pick a
    // side outright (SourceWins / TargetWins / LastWriteWins), the standard
    // routing handles everything. Custom resolution comes into play only
    // when policy doesn't pick a side AND a merger was supplied.
    using Direction = ExecutionOverride::Direction;
    if (executionOverride.direction == Direction::MirrorAToB) {
        return mergeMirrorAToB(d);
    }
    if (executionOverride.direction == Direction::MirrorBToA) {
        return mergeMirrorBToA(d);
    }

    // Default: bidirectional merge.
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
            // Conflicts are emitted to toTarget by blobBatchDiff(); guard anyway.
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
        } else if (policy == ConflictResolution::CustomMerge && customMerger) {
            // Delegate per-record 3-way merge to the domain plugin's
            // IRecordMerger. Mirrors CalendarDomainAdapter's existing
            // pattern; canonical-shape inputs because dispatchSync has
            // already promoted source/target/baseline to canonical.
            Kalburator::Shape::CanonicalRecord srcRec{
                canonical, op.record.data,         op.record.id};
            Kalburator::Shape::CanonicalRecord tgtRec{
                canonical, op.targetRecord.data,   op.record.id};
            Kalburator::Shape::CanonicalRecord baseRec{
                canonical, op.baselineRecord.data, op.record.id};

            const auto merged = customMerger->merge(
                srcRec, tgtRec, baseRec,
                Kalburator::Sync::QSyncCore::ConflictPolicy::deferAll());

            BackendRecord mergedRecord = op.record;
            mergedRecord.data = merged.data;

            EngineDiffOp resolved;
            resolved.kind           = EngineDiffOp::Kind::Update;
            resolved.record         = mergedRecord;
            resolved.baselineRecord = op.baselineRecord;
            routeOp(resolved, /*toTarget=*/true);
            ++m.conflictsResolved;
        } else {
            ++m.conflictsDeferred;
        }
    }

    return m;
}

} // namespace Kalburator::Sync
