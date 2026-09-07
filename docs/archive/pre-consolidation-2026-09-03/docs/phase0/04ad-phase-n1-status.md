# Phase N.1 — Per-record diff/merge loop

**Status:** landed 2026-05-17
**Tag:** `v0.44-phase-n1-perrecord-diff-merge`
**Plan:** `~/dev/refactor-engine-merger/2026-05-17-phase-n1-perrecord-diff-merge-plan.md`
**Spec:** `~/dev/refactor-engine-merger/2026-05-17-phase-n1-perrecord-diff-merge-design.md`

## What landed

- `src/engine/perrecorddiff.{h,cpp}` — new free functions:
  - `perRecordDiff(source, target, baseline, canonical, differ)` — per-record diff
    using `RecordDiffer::equal()` as the equality oracle.
  - `mergeMirrorAToB`, `mergeMirrorBToA` — mirror semantics lifted from
    `blobbatchdiff.cpp`'s anonymous namespace.
- `SyncEngineWorker` gains `m_unifiedDiffer` / `m_unifiedMerger` (unique_ptr<RecordDiffer/RecordMerger>);
  acquired once per `dispatchSync` from the domain plugin, retained across AskUser yield/resume,
  cleared in `unifiedContinueAfterConflicts` and on cancel.
- `unifiedHandleConflicts` and `resumeAfterConflict` resolve `ConflictResolution::CustomMerge`
  via `m_unifiedMerger->merge()` instead of falling through to the defer branch.
- `src/blob/blobbatchdiff.{h,cpp}` deleted (all callers migrated).
- Semantic-equality fixes in iCal, VCard, and TextDiffer: when both inputs fail
  to parse but are byte-inequal, these differs now correctly return "changed"
  instead of "equal". This is the correct behavior for two distinct corrupted
  records and fixes the hash-as-data false-equality bug.
- Test count: libkalburator 99 → 101.
  - New: `tests/engine/tst_perrecorddiff.cpp` (6 slots).
  - New: `tests/contacts/tst_unified_custom_merge.cpp` (2 slots).
  - Rewritten: `tests/blob/tst_blob_domain_adapter.cpp` (4 slots; same cases, switched to `perRecordDiff`).

## Closes (04w deferred-work)

- A.4 (per-record loop) — full.
- A.5 CustomMerge half — full. (Duplicate was already implemented in the engine
  pre-N.1; A.5 remains open only for Duplicate test coverage.)

## What did not land

- A.5 Duplicate test — separate test-only follow-up.
- A.6 baseline-aware calendar property diff — separate phase.
