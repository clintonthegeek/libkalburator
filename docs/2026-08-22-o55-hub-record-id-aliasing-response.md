# Response: O55 hub record-id join churn — RESOLVED (engine-side id aliasing + fail-loud guard)

- **From:** libkalburator, branch `fix/o55-hub-record-id-aliasing`
- **To:** WildPalms (re: your `docs/2026-08-21-libkalburator-hub-record-id-join-churn-handoff.md`)
- **Date:** 2026-08-22
- **Status:** Fixed. All changes additive — **bump the pin when convenient; no
  WildPalms code change is needed**, and your two failing runtime tests
  (`tst_palm_runtime_route_first_sync`, `tst_palm_runtime_route_recategorization`)
  should pass unmodified.

## Verdict on your handoff

Correct in every checkable particular, and thank you for the repro-without-Palm:
it turned a two-day forensic exercise into an afternoon. We accepted it as a
genuine lib regression (FINDINGS **O55**, now closed). One correction to the
record: the "id-prefix machinery" deleted during fanout-collapse was DAV
collection-id prefixes (`multiproto-dav:<id>:cal:`), unrelated to your bug.

## What was actually wrong

Two facts coexisted harmlessly since G.8/v0.44:

- `GenericSqliteBackend::loadRecords()` presents `<collectionId>\x01<origId>`
  while storing whatever id a create carried;
- `perRecordDiff()` joins strictly by raw `BackendRecord::id`.

**v0.77 never converged correctly here — it converged by accident.** Pass 2
re-created the already-stored id, the duplicate-key INSERT failed, and the
v0.77-era engine aborted the mapping before its destructive ops ran. Phase B4's
per-side baseline hashing (v0.82) fixed change detection for real but removed
that accidental abort: pass 2 then read each side as "tracked here, vanished
there", emitted symmetric deletes, and churned both sides empty **while
reporting success**. That silent-success aspect violated our own stated
"fail loud, never silently-empty" rule; it is fixed alongside the aliasing.

## The fix (your Direction 1)

1. **Capture** — `WriteOperation` now reports `idAliases()` (requested
   create-id → backend-assigned id). The default `applyRecords()` populates it
   whenever `createRecord()` returns a different id; every backend on the
   default write path (including the sqlite hub) is covered.
2. **Persist** — `BaselineStore` schema v8 adds per-mapping
   `blob_id_aliases(native_id → canonical_id)`, wiped with the mapping's other
   engine state.
3. **Join** — `perRecordDiff()` resolves ids through the alias map at index
   time; emitted ops still carry native ids so writes route correctly.
   Post-write refetch hashes resolve through the same aliases, so each side's
   baseline carries its own read-back hash.
4. **Fail loud** — if the diff ever again sees canonically-equal records under
   unjoined ids (the churn signature — canonical equality includes the payload
   uid, so independent records can't trip it), the mapping now FAILS with an
   "identity conflict" error instead of cross-creating toward an empty peer.

## Two things to know

- **Already-poisoned profiles:** state churned by pre-fix runs carries orphan
  baseline rows under both id forms and no alias row. Those mappings recover
  by clearing the mapping's sync state once (re-first-sync). Fresh profiles —
  including your failing tests, which build fresh state — converge as-is.
- **Route legs:** your observation about `FilteredCollectionBackend` inheriting
  prefixed ids is covered — aliases are recorded for any create whose returned
  id differs from requested, regardless of which backend wrapper produced it,
  so route and direct hub siblings join consistently.

## Verification

New regression gate `tests/engine/tst_engine_id_aliasing.cpp` — both slots RED
on v0.99, GREEN with the fix: (a) bare-id mock ↔ real `GenericSqliteBackend`
TwoWay converges across three consecutive runs (one hub row, zero steady-state
writes after run 1, exactly one baseline row); (b) unjoined equal twins fail
loudly with both records intact. This also closes the coverage gap you flagged
— `GenericSqliteBackend` is now a real mapping endpoint in our suite. Full
suite 177/180; all three failures pre-existing (verified on the pristine tree)
and unrelated.

PlanStan is unaffected either way (no sqlite-hub endpoints; engine-stable ids).
