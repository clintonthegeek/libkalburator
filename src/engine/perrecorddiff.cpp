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

QHash<QString, BaselineEntry> indexBaselineById(const QList<BaselineEntry>& entries)
{
    QHash<QString, BaselineEntry> out;
    out.reserve(entries.size());
    for (const auto& e : entries) out.insert(e.id, e);
    return out;
}

// EngineDiffOp::baselineRecord is a BackendRecord for historical/API-
// compatibility reasons (CustomMerge/Duplicate resolution and the doomed-
// record delete path only need `.id`, and CustomMerge reads `.data`, which
// baseline entries never carried even before B4). Build a minimal shell:
// `.contentHash` carries the baseline's sourceHash so pre-B4 call sites and
// tests that read a single baseline hash keep working (source == target in
// every case that predates the per-side split).
BackendRecord baselineShell(const BaselineEntry& e)
{
    BackendRecord r;
    r.id = e.id;
    r.contentHash = e.sourceHash;
    return r;
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
                         const QList<BaselineEntry>& baseline,
                         const Kalburator::Shape::Shape& canonical,
                         const RecordDiffer& differ)
{
    EngineDiff result;

    const QHash<QString, BackendRecord>  sById = indexById(source);
    const QHash<QString, BackendRecord>  tById = indexById(target);
    const QHash<QString, BaselineEntry>  bById = indexBaselineById(baseline);

    QSet<QString> allIds;
    for (auto it = sById.constBegin(); it != sById.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = tById.constBegin(); it != tById.constEnd(); ++it) allIds.insert(it.key());
    for (auto it = bById.constBegin(); it != bById.constEnd(); ++it) allIds.insert(it.key());

    // Phase B4 (N2 fix): each side is compared against its OWN baseline
    // hash, never against the other side's native bytes — two backends
    // never serialize the same logical record identically (PRODID,
    // property order, folding, server normalization), so a cross-side
    // hash compare would read "changed" forever after any cross-backend
    // write. Prefer hash equality (baseline entries store no data bytes,
    // only hashes, so a semantic differ has nothing to compare against on
    // that side); fall back to the differ only when a side's baseline hash
    // is absent, in which case we compare against the OTHER side's current
    // record as the closest available reference — still never bytewise
    // cross-side hash-equating.
    auto sourceChanged = [&](const BackendRecord& s, const BaselineEntry& b) -> bool {
        if (!s.contentHash.isEmpty() && !b.sourceHash.isEmpty())
            return s.contentHash != b.sourceHash;
        // No hash to compare: no baseline bytes exist to diff against
        // either (BaselineEntry carries no payload) — report "changed" so
        // an untracked side never silently reads as unchanged (fail loud,
        // never silently-empty).
        return true;
    };
    auto targetChanged = [&](const BackendRecord& t, const BaselineEntry& b) -> bool {
        if (!t.contentHash.isEmpty() && !b.targetHash.isEmpty())
            return t.contentHash != b.targetHash;
        return true;
    };
    // Only used where there is genuinely no baseline at all (!hasB): the
    // one place a cross-side comparison is legitimate, since there is no
    // per-side reference yet. Semantic (canonical) equality, not hash.
    auto semanticallyEqual = [&differ, &canonical](const BackendRecord& a,
                                                    const BackendRecord& b) -> bool {
        return differ.equal(toCanonical(a, canonical), toCanonical(b, canonical));
    };

    for (const QString& id : allIds) {
        const bool hasS = sById.contains(id);
        const bool hasT = tById.contains(id);
        const bool hasB = bById.contains(id);

        const BackendRecord  sRec   = hasS ? sById.value(id) : BackendRecord{};
        const BackendRecord  tRec   = hasT ? tById.value(id) : BackendRecord{};
        const BaselineEntry  bEntry = hasB ? bById.value(id) : BaselineEntry{};
        const BackendRecord  bRec   = hasB ? baselineShell(bEntry) : BackendRecord{};

        if (hasS && hasT && hasB) {
            const bool sChanged = sourceChanged(sRec, bEntry);
            const bool tChanged = targetChanged(tRec, bEntry);
            if (!sChanged && !tChanged) continue;
            if (sChanged && !tChanged)
                result.toTarget.append(makeUpdate(sRec, bRec, tRec));
            else if (!sChanged && tChanged)
                result.toSource.append(makeUpdate(tRec, bRec));
            else
                result.toTarget.append(makeConflict(sRec, tRec, bRec));
        } else if (!hasS && hasT && hasB) {
            if (targetChanged(tRec, bEntry))
                result.toTarget.append(makeConflict(BackendRecord{}, tRec, bRec));
            else
                result.toTarget.append(makeDelete(bRec, bRec));
        } else if (hasS && !hasT && hasB) {
            if (sourceChanged(sRec, bEntry))
                result.toTarget.append(makeConflict(sRec, BackendRecord{}, bRec));
            else
                result.toSource.append(makeDelete(bRec, bRec));
        } else if (hasS && !hasT && !hasB) {
            result.toTarget.append(makeCreate(sRec));
        } else if (!hasS && hasT && !hasB) {
            result.toSource.append(makeCreate(tRec));
        } else if (hasS && hasT && !hasB) {
            // No baseline for this id at all yet (e.g. immediately after a
            // first-sync mirror, before either side's steady-state baseline
            // has been written): the only reference available is the other
            // side's current record, so this is the one legitimate
            // cross-side comparison — and it must be semantic, not a raw
            // native-bytes hash compare (which would never match across
            // backends and would manufacture a spurious conflict on every
            // record on the very next sync).
            if (!semanticallyEqual(sRec, tRec))
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
