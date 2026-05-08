# Phase Ia.5 — Engine unification (status)

**Status:** in progress (started 2026-05-08)
**Tag (planned):** `v0.26-phase-ia.5-engine-unification`
**Spec:** `~/dev/refactor-engine-merger/2026-05-08-phase-ia.5-engine-unification-design.md`
**Plan:** `~/dev/refactor-engine-merger/2026-05-08-phase-ia.5-engine-unification-plan.md`

## What exists
- Tasks 1–8 landed (status doc, IRecordWriter, plugin createWriter,
  collection-property hooks, calendar plugin IRecordWriter, calendar
  plugin collection-property hooks, generic property-phase helper,
  Pipeline compilation in dispatchSync).
- Task 9 landed (2026-05-08): dispatchSync now lifts plugin lookup
  out of the homogeneous/heterogeneous branches and consults
  `plugin->createCanonicalMerger()` for conflict resolution. The
  Task 8 homogeneous short-circuit was removed; `compile(X, X)`
  returns identity, so the unified path handles both cases.
  `BlobDomainAdapter::mergeWithPlugin` mirrors
  `CalendarDomainAdapter`'s existing CustomMerge delegation pattern.
  Precursor: 11 test CMakeLists wrap `Kalburator::Sync` with
  `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` so plugin registrars reach
  test binaries (matches Task 1's WildPalms-side fix).

## What remains
- Tasks 10–22 (per the plan): conflict-policy honoring + AskUser
  pause/resume, IRecordWriter wiring, generic property-phase wiring,
  router deletion, adapter deletion, contacts assertion flip,
  unified-routing test, verify-all + baseline refresh, doc + tag.

## Discoveries
- Task 9: lifting plugin lookup surfaced that
  `tst_engine_mirror_direction`'s stub backend declared its native
  shape as `(memo, raw)`, while the memo plugin's canonical is
  `(memo, text)` with no edge between them. The "raw" encoding was
  a pre-Ia.5 placeholder that worked only because dispatchBlobSync
  bypassed the registry. Fix: stub now declares the canonical shape
  directly. Other test stubs that declare custom shapes will need
  the same audit when their domains are exercised through the
  unified path.
- Task 9 (precursor): static-link visibility — libkalburator's
  plugin TUs are pure-side-effect static-init objects, dropped by
  the linker when test executables don't reference their symbols.
  `$<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>` retains them.
  This had been done WildPalms-side in Task 1 of the original
  Phase Ia plan; libkalburator's own test binaries needed the same
  treatment before dispatchSync's unified plugin lookup could rely
  on registrars being present.
- Task 9 (architectural): the IRecordDiffer/IRecordMerger contract
  is per-canonical-record while BlobDomainAdapter is batch-level.
  v1 keeps BlobDomainAdapter::diff for its hash-equality semantics
  (which match KalburatorDomainBlob's IRecordDifferBlob exactly)
  and only swaps the merge path. A full per-record diff is a
  candidate for Phase Ib.5 but proved disproportionate for v1.
