# Phase Ia.5 — Engine unification (status)

**Status:** landed 2026-05-08
**Tag:** `v0.26-phase-ia.5-engine-unification`
**Spec:** `~/dev/refactor-engine-merger/2026-05-08-phase-ia.5-engine-unification-design.md`
**Plan:** `~/dev/refactor-engine-merger/2026-05-08-phase-ia.5-engine-unification-plan.md`

## What landed

All 22 tasks of the Phase Ia.5 plan executed. The engine entry
point is unified: `processSync` always calls `dispatchSync`, the
calendar/blob router (`syncengine.cpp:1457-1465`) is gone, and
non-calendar domains now route through plugin-driven differ /
merger / writer + a compiled `Pipeline` from
`TransformationRegistry::compile(srcShape, tgtShape)`.

Calendar internals are preserved verbatim as a
`dispatchCalendarLegacy(request)` helper inside `dispatchSync`
(per design § 3 minimal-scope boundary). Full generalization of
the calendar-typed signal path (itemReady/itemFetched/ConflictInfo
calendar-typedness, KCalendarCore in the engine TU) is deferred
to Phase Ib.5.

### Tasks (1–22) summary

- **Tasks 1–8.** Foundation: status doc, IRecordWriter +
  DefaultBlobWriter, DomainPlugin::createWriter, collection-property
  hooks (interface + calendar plugin impl), generic property-phase
  helper, Pipeline compilation in dispatchSync.
- **Task 9.** dispatchSync lifts plugin lookup out of homogeneous /
  heterogeneous branches and consults `plugin->createCanonicalMerger()`
  for conflict resolution. Homogeneous short-circuit removed
  (`compile(X, X)` returns identity). Precursor: 11 test CMakeLists
  wrap `Kalburator::Sync` with
  `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`.
- **Task 10.** Honor `mapping.conflictPolicy` on the unified path;
  consolidated `pickByPolicy`. AskUser pause/resume hooked into the
  same conflict-yield pattern used by the calendar legacy branch.
- **Task 11.** dispatchSync uses plugin's IRecordWriter (via
  `DomainPlugin::createWriter`).
- **Task 12.** Generic property phase wired into dispatchSync via
  `plugin->collectionProperties()` /
  `plugin->applyCollectionProperties()`.
- **Task 13.** **The big swap.** Calendar/blob router deleted.
  `processSync` always calls `dispatchSync`. Calendar branch's
  former in-line stages wrapped as `dispatchCalendarLegacy(request)`
  helper, called from `dispatchSync` when the request's domain is
  `calendar`. Non-calendar domains take the unified plugin-driven
  path.
- **Task 14.** Removed dead `m_blobAdapter` member and refreshed
  stale router comments in `syncengine.{h,cpp}`. **Did NOT migrate
  remaining calendar-typed stages out of `processSync`** — those
  are now in `dispatchCalendarLegacy` and remain calendar-typed
  (deferred to Phase Ib.5; see "Carry-forwards" below).
- **Tasks 15, 17.** **DEFERRED to Phase Ib.5.** `IDomainAdapter`
  and `CalendarDomainAdapter` remain in the tree. The plan
  proposed deleting them in Ia.5 alongside `BlobDomainAdapter`,
  but `CalendarDomainAdapter` is still referenced by the calendar
  legacy helper, and `IDomainAdapter` is its base. Deleting them
  requires generalising the calendar-typed signal path (itemReady
  carries `IncidencePtr`, ConflictInfo is calendar-typed, etc.) —
  larger than Ia.5's stated minimal-scope boundary. Both deletions
  carry forward to Phase Ib.5 alongside the calendar-typed signal
  generalization.
- **Task 16.** `BlobDomainAdapter` deleted. Its still-live methods
  (`diff`, `mergeWithPlugin`) lifted to free functions
  `blobBatchDiff()` and `blobBatchMergeWithPlugin()` in
  `src/blob/blobbatchdiff.{h,cpp}`. dispatchSync calls the free
  functions directly. The rest of the adapter API was already
  dead after Tasks 11 / 13 / 14.
- **Task 18.** Adapter-deletion stragglers swept
  (`tests/blob/tst_engine_mirror_direction.cpp`'s stub
  `nativeShapes()` corrected from `(memo, raw)` to
  `(memo, text)` so it routes through the now-required edge
  graph).
- **Task 19.** Phase Ia Task 19's negative-assertion diagnostic
  test in `WildPalms/tests/plugins/contacts/tst_contacts_palm_engine_sync.cpp`
  flipped to positive: target bytes now contain `BEGIN:VCARD` and
  `VERSION:4.0`, palm-bytes round-trip via `PalmRecord::fromWireBytes`
  fails (proves the engine ran the registered Pipeline).
- **Task 20.** New test
  `libkalburator/tests/calendar/tst_engine_unified_routing.cpp`
  pins the unified-path contracts: single dispatch entry point,
  plugin lookup happens for every domain, registered Pipelines
  run for cross-shape mappings, identity Pipelines run for
  same-shape mappings.
- **Task 21.** `verify-all.sh` exits 0; baselines refreshed.
  libkalburator went 66 → 70 (added: `tst_default_blob_writer`,
  `tst_calendar_plugin_writer`, `tst_property_phase`,
  `tst_engine_unified_routing`). PlanStan unchanged at 82 / 106
  (24 pre-existing env failures). WildPalms 77 → 78 (Task 19
  flip is in-place; new shape was Task 1's `--whole-archive`
  precursor).
- **Task 22.** This commit. Status doc landed; CURRENT-STATUS,
  ROADMAP, FINDINGS updated; tag
  `v0.26-phase-ia.5-engine-unification` applied to libkalburator
  + WildPalms.

### Test posture (post-Ia.5)

- libkalburator: 70 / 70 pass. New tests:
  `tst_default_blob_writer`, `tst_calendar_plugin_writer`,
  `tst_property_phase`, `tst_engine_unified_routing`,
  `tst_vcard3_vcard4_edge` (the last one was added in Phase Ia
  Task 9 but is post-pivot under the unified path now).
- PlanStan: 82 / 106 pass (24 pre-existing env failures, unchanged).
- WildPalms: 78 / 78 pass.

`scripts/verify-all.sh` exit 0.

## Carry-forwards (deferred to Phase Ib.5)

Per design § 4.3, the original Ia.5 plan proposed deleting
`IDomainAdapter`, `BlobDomainAdapter`, and `CalendarDomainAdapter`
in this phase. Only `BlobDomainAdapter` was actually deleted.
The other two are still in the tree, reachable only from
`dispatchCalendarLegacy`. Deletion is gated on Phase Ib.5's
calendar-typed signal generalization:

1. **`itemReady` / `itemFetched` carry `IncidencePtr`.** These
   need to become domain-generic (probably opaque + a typed
   side-channel). The calendar UI (PlanStan / WildPalms calendar)
   still expects calendar-typed payloads — so the consumer
   signal handlers need updating in lockstep.
2. **`ConflictInfo`** is calendar-typed in the engine's existing
   `IConflictHandler`. Same treatment.
3. **KCalendarCore in the engine TU.** Once 1 and 2 are domain-
   generic, the engine no longer needs to include
   `<KCalendarCore/...>`. The calendar-specific types move
   wholly into `KalburatorDomainCalendar`'s plugin TU.
4. Only after 1–3 can `CalendarDomainAdapter` and
   `IDomainAdapter` be deleted; their last reachable callers
   (the calendar legacy helper) will already have moved to the
   plugin form.

These four bullets are the body of Phase Ib.5's design when it
opens; they are tracked at the ROADMAP level and not duplicated
into a Ib.5 doc until the phase begins.

## Findings landed during Ia.5

See `~/dev/refactor-engine-merger/FINDINGS.md`. The four
substantive findings from this phase:

1. Static-link visibility for plugin registrars (every consumer
   of `Kalburator::Sync` MUST link with
   `$<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>`).
2. Phase Ia.5 transitional split (calendar internals preserved
   as `dispatchCalendarLegacy`; full generalization deferred).
3. `BlobBackendAdapter` default-shape gotcha (every backend's
   `nativeShapes()` must return shapes the registered plugin's
   edge graph can route).
4. Adapter deletion deferred (only `BlobDomainAdapter` actually
   deleted; the other two carry forward to Phase Ib.5).
