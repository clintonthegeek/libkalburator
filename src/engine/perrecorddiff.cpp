#include "perrecorddiff.h"

#include "canonicalrecord.h"
#include "recorddiffer.h"

#include <QHash>
#include <QSet>

namespace Kalburator::Engine {

using Kalburator::Sync::BackendRecord;
using Kalburator::Shape::CanonicalRecord;
using Kalburator::Shape::RecordDiffer;
using KShape = Kalburator::Shape::Shape;

namespace {

QHash<QString, BackendRecord> indexById(const QList<BackendRecord>& records)
{
    QHash<QString, BackendRecord> out;
    out.reserve(records.size());
    for (const auto& r : records) out.insert(r.id, r);
    return out;
}

CanonicalRecord toCanonical(const BackendRecord& r, const KShape& shape)
{
    CanonicalRecord c;
    c.recordId = r.id;
    c.shape    = shape;
    c.data     = r.data;
    return c;
}

EngineDiffOp makeUpdate(const BackendRecord& newState,
                        const BackendRecord& baseline,
                        const BackendRecord& otherSide = {})
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Update;
    op.record = newState;
    op.baselineRecord = baseline;
    op.targetRecord = otherSide;
    return op;
}

EngineDiffOp makeCreate(const BackendRecord& newState)
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Create;
    op.record = newState;
    return op;
}

EngineDiffOp makeDelete(const BackendRecord& doomed,
                        const BackendRecord& baseline)
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Delete;
    op.record = doomed;
    op.baselineRecord = baseline;
    return op;
}

EngineDiffOp makeConflict(const BackendRecord& source,
                          const BackendRecord& target,
                          const BackendRecord& baseline)
{
    EngineDiffOp op;
    op.kind = EngineDiffOp::Kind::Conflict;
    op.record = source;
    op.targetRecord = target;
    op.baselineRecord = baseline;
    return op;
}

} // namespace

EngineDiff perRecordDiff(const QList<BackendRecord>& source,
                         const QList<BackendRecord>& target,
                         const QList<BackendRecord>& baseline,
                         const Kalburator::Shape::Shape& canonical,
                         const RecordDiffer& differ)
{
    EngineDiff result;

    const QHash<QString, BackendRecord> sById = indexById(source);
    const QHash<QString, BackendRecord> tById = indexById(target);
    const QHash<QString, BackendRecord> bById = indexById(baseline);

    QSet<QString> allIds;
    for (auto it = sById.constBegin(); it != sById.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = tById.constBegin(); it != tById.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = bById.constBegin(); it != bById.constEnd(); ++it) allIds.insert(it.key());

    for (const QString& id : allIds) {
        const bool hasS = sById.contains(id);
        const bool hasT = tById.contains(id);
        const bool hasB = bById.contains(id);

        const BackendRecord sRec = hasS ? sById.value(id) : BackendRecord{};
        const BackendRecord tRec = hasT ? tById.value(id) : BackendRecord{};
        const BackendRecord bRec = hasB ? bById.value(id) : BackendRecord{};

        const auto sCanon = toCanonical(sRec, canonical);
        const auto tCanon = toCanonical(tRec, canonical);
        const auto bCanon = toCanonical(bRec, canonical);

        if (hasS && hasT && hasB) {
            const bool sChanged = !differ.equal(sCanon, bCanon);
            const bool tChanged = !differ.equal(tCanon, bCanon);
            if (!sChanged && !tChanged) continue;
            if (sChanged && !tChanged)
                result.toTarget.append(makeUpdate(sRec, bRec, tRec));
            else if (!sChanged && tChanged)
                result.toSource.append(makeUpdate(tRec, bRec));
            else
                result.toTarget.append(makeConflict(sRec, tRec, bRec));
        } else if (!hasS && hasT && hasB) {
            if (!differ.equal(tCanon, bCanon))
                result.toTarget.append(makeConflict(BackendRecord{}, tRec, bRec));
            else
                result.toTarget.append(makeDelete(bRec, bRec));
        } else if (hasS && !hasT && hasB) {
            if (!differ.equal(sCanon, bCanon))
                result.toTarget.append(makeConflict(sRec, BackendRecord{}, bRec));
            else
                result.toSource.append(makeDelete(bRec, bRec));
        } else if (hasS && !hasT && !hasB) {
            result.toTarget.append(makeCreate(sRec));
        } else if (!hasS && hasT && !hasB) {
            result.toSource.append(makeCreate(tRec));
        } else if (hasS && hasT && !hasB) {
            if (!differ.equal(sCanon, tCanon))
                result.toTarget.append(makeConflict(sRec, tRec, BackendRecord{}));
        }
        // (!hasS && !hasT && hasB) is vestigial; ignore.
    }

    return result;
}

EngineMerge mergeMirrorAToB(const EngineDiff& d)
{
    EngineMerge m;
    for (const auto& op : d.toTarget) {
        if (op.kind == EngineDiffOp::Kind::Create
            || op.kind == EngineDiffOp::Kind::Update) {
            m.finalTarget.append(op.record);
            m.updatedBaselines.append(op.record);
        } else if (op.kind == EngineDiffOp::Kind::Conflict) {
            m.finalTarget.append(op.record);
            m.updatedBaselines.append(op.record);
            ++m.conflictsResolved;
        } else if (op.kind == EngineDiffOp::Kind::Delete) {
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalTarget.append(doomed);
        }
    }
    for (const auto& op : d.toSource) {
        if (op.kind == EngineDiffOp::Kind::Create) {
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalTarget.append(doomed);
        }
    }
    return m;
}

EngineMerge mergeMirrorBToA(const EngineDiff& d)
{
    EngineMerge m;
    for (const auto& op : d.toSource) {
        if (op.kind == EngineDiffOp::Kind::Create
            || op.kind == EngineDiffOp::Kind::Update) {
            m.finalSource.append(op.record);
            m.updatedBaselines.append(op.record);
        } else if (op.kind == EngineDiffOp::Kind::Delete) {
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalSource.append(doomed);
        }
    }
    for (const auto& op : d.toTarget) {
        if (op.kind == EngineDiffOp::Kind::Conflict) {
            m.finalSource.append(op.targetRecord);
            m.updatedBaselines.append(op.targetRecord);
            ++m.conflictsResolved;
        } else if (op.kind == EngineDiffOp::Kind::Create) {
            BackendRecord doomed = op.record;
            doomed.isDeleted = true;
            m.finalSource.append(doomed);
        } else if (op.kind == EngineDiffOp::Kind::Update) {
            m.finalSource.append(op.targetRecord);
            m.updatedBaselines.append(op.targetRecord);
        }
    }
    return m;
}

} // namespace Kalburator::Engine
