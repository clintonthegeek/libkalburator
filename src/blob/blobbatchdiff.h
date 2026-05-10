#ifndef KALBURATOR_BLOB_BLOBBATCHDIFF_H
#define KALBURATOR_BLOB_BLOBBATCHDIFF_H

#include "backendcapabilities.h"
#include "backendrecord.h"
#include "enginediff.h"
#include "shape.h"
#include "synctypes.h"

#include <QList>

namespace Kalburator::Shape { class RecordMerger; }

namespace Kalburator::Sync {

/// Phase Ia.5 Task 16: free-function home for the batch-shaped blob
/// diff/merge logic that previously lived on `BlobDomainAdapter`.
///
/// Phase Ib.5 (or later) is expected to replace these batch helpers
/// with a per-record `IRecordDiffer` / `IRecordMerger`-driven walk in
/// `dispatchSync`, mirroring the design's true intent. Until then,
/// these helpers are the engine's default blob diff/merge: hash-equality
/// detection, three-way merge with `ConflictResolution` policy, and
/// optional `IRecordMerger` delegation for `CustomMerge` resolution.
///
/// Callers: `dispatchSync` in `src/engine/syncengine.cpp` and the
/// `tst_blob_domain_adapter` integration test.

/// Hash-equality diff over `BackendRecord`. Treats `contentHash` as the
/// equality oracle. Returns toSource / toTarget op lists; conflicts are
/// emitted into toTarget.
EngineDiff blobBatchDiff(const QList<BackendRecord>& source,
                         const QList<BackendRecord>& target,
                         const QList<BackendRecord>& baseline,
                         const BackendCapabilities& sourceCaps,
                         const BackendCapabilities& targetCaps);

/// Resolve conflicts via `policy`; for `CustomMerge` policy, delegate
/// to the supplied per-record `IRecordMerger` (the domain plugin's
/// canonical merger). Mirror-direction overrides bypass conflict
/// resolution and produce a one-way copy.
///
/// `customMerger` may be null — falls back to `policy`-driven
/// resolution exactly. `canonical` is the canonical shape used when
/// promoting `BackendRecord` payloads to `CanonicalRecord` for the
/// custom merger.
EngineMerge blobBatchMergeWithPlugin(const EngineDiff& diff,
                                     ConflictResolution policy,
                                     const ExecutionOverride& executionOverride,
                                     Kalburator::Shape::RecordMerger* customMerger,
                                     const Kalburator::Shape::Shape& canonical);

} // namespace Kalburator::Sync

#endif // KALBURATOR_BLOB_BLOBBATCHDIFF_H
