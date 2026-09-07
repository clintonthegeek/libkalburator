# LocalBlobBackend cross-id-space mapping — suspected duplicate-on-second-sync

**Status:** suspected bug; **not reproduced**, no failing test in the suite.
**Origin:** WildPalms Phase E.16 deferral (d), raised 2026-04-28.
**Affects:** any consumer that uses `Kalburator::Storage::BaselineStore` +
`LocalBlobBackend` together — at minimum WildPalms (for legacy / fallback
paths), potentially PlanStan if it uses LocalBlobBackend anywhere.

## What we think is happening

When the blob sync engine pairs a source backend (e.g. `PalmBackend`) with
`LocalBlobBackend` as target, the two sides use different record-ID spaces:

- Source-side record IDs look like `palm:address:42` (semantic, stable across
  syncs, assigned by the device or the source backend).
- Target-side record IDs returned by `LocalBlobBackend::createRecord` are
  *absolute filesystem paths* (`/some/path/contacts/alice.vcf`).

`BlobBaselineStore` (v3, `blob_baselines_v3` keyed by `mapping_id, record_id`)
stores baseline content per side. The engine writes a baseline entry per
record after each successful sync so the next sync can diff against it.

The concern: if the baseline store only records *one* of the two id-spaces per
record (or fails to record the cross-id mapping at all), the next sync sees
the local-side record as "new" and the engine creates a duplicate file rather
than recognising it as the existing partner of the source-side record.

Symptom prediction: run a Palm↔LocalBlobBackend sync against an empty target,
let it write N files. Run a second sync with the source unchanged. Expected:
zero writes / no duplicates. Suspected: N more files appear with new names.

## Why this is suspected, not confirmed

- No test in libkalburator's ctest suite uses `LocalBlobBackend` as a sync
  target. All `BlobSyncEngine::twoWayWithBaseline` tests use
  `MockBlobBackend`, which assigns synthetic ids that don't expose the
  cross-id-space problem (the mock keeps mapping state in process memory).
- All of WildPalms' per-plugin e2e tests (`tst_calendar_v2`, `tst_todo_v2`,
  `tst_contacts_v2`, `tst_memo_v2`) also target `MockBlobBackend`, for the
  same reason.
- WildPalms' new `SyncRunner`-driven sync (E.16) does use `LocalBlobBackend`
  in some Tools-menu paths against a live Palm — first-sync record motion
  works (621-record first HotSync verified on a Palm m505), but no one has
  yet run a *second* sync to compare deltas and check for duplicates.

So this is "an architectural concern with a credible failure mode" rather
than "we've seen it happen and reproduced it."

## Suggested investigation

The smallest experiment that would confirm or rule it out:

1. Write a new ctest in `libkalburator/tests/blob/` (or wherever
   `tst_blobsyncengine.cpp` lives) that exercises:
   - `MockBlobBackend` (source, deterministic ids like `src:001`, `src:002`, …)
   - `LocalBlobBackend` (target, tmpdir, file ids = absolute paths)
   - `BlobBaselineStore` opened on a tmp sqlite file
   - `BlobSyncEngine::twoWayWithBaseline` invoked once with N records on
     the source side, target empty
   - Then invoked a second time with the source unchanged
2. Assert: after sync 2, the target tmpdir contains exactly N files (no
   duplicates, no growth, no churn).

If that test passes, the concern is unfounded and 04w §C can mark this
✅ "verified, no fix needed." If it fails, the failure mode reveals which
mapping path is broken — likely either:

- `BlobBaselineStore::recordBaselineV3` not writing both id-spaces, or
- The mapping resolver (set via `BlobBaselineStore::setMappingResolver`)
  not being consulted on the second sync, or
- `LocalBlobBackend::loadRecords` not returning the same record-ids on the
  second sync (filesystem-order non-determinism, or path normalisation drift).

The fix lands in libkalburator. Per the coordination policy
(`docs/2026-04-28-honest-assessment.md` + PlanStan's pretest discipline), the
change ships with a libkalburator ctest demonstrating the bug pre-fix and
green post-fix, and PlanStan ctest holds across the change.

## What both consumers should do today

- **WildPalms:** don't promote `LocalBlobBackend` to a primary sync target
  until this is resolved. The current Tools-menu sync flows that use it
  (Backup, CopyPCToPalm in certain configurations) work for first-sync but
  should not be assumed safe across multiple syncs.
- **PlanStan:** if any code path pairs `LocalBlobBackend` with another
  backend through `BlobSyncEngine::twoWayWithBaseline`, please test the
  multi-sync case and report findings here (a new test in your repo is
  fine — it doesn't have to live upstream until the fix lands).

## Cross-references

- WildPalms Phase E.16 spec: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (E.16 row, deferral (d))
- WildPalms integration plan: `docs/plans/2026-04-20-libkalburator-integration.md` (Phase E section, E.16 (d))
- 04w deferred-work catalog: this note should also be summarised under §C
  ("Backends") if/when it lands as a tracked work item.

## Open questions for PlanStan devs

1. Does PlanStan use `LocalBlobBackend` at all? If yes, in what paths?
2. If yes, has anyone observed duplicate records on second sync, or unexpected
   churn in a local sync target between identical syncs?
3. Any preference on test scaffolding location? I.e., should the reproducer
   live in `libkalburator/tests/blob/` or in a consumer-side test?

If the answer to (1) is "no, PlanStan only uses LocalBackend / CalDAV /
Akonadi / Org," then this is a WildPalms-driven concern and a WP-side
investigation is appropriate.
