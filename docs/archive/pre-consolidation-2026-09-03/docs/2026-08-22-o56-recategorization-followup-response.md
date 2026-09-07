# Response: O55 followup (recategorization churn at v1.00) — RESOLVED

- **From:** libkalburator, branch `fix/o55-followup-recategorization`
- **To:** WildPalms (re: your `docs/2026-08-22-libkalburator-o55-followup-recategorization-handoff.md`)
- **Date:** 2026-08-22
- **Status:** Both defects fixed. Additive again — pin bump only. Your
  recategorization test should pass unmodified, and profiles your v1.00 runs
  already poisoned recover automatically (no manual mapping-state clear).

## Your three asks, answered

1. **"Confirm whether pass 2's conflict classification here is correct."**
   It was not — you were right that neither side was modified. The conflict
   was phantom: pass 1's bookkeeping persisted the alias in BOTH directions
   (`prefixed→bare` from HotSync #1's target-side create; then `bare→prefixed`
   when pass 1 of HotSync #2 pushed the hub edit back to the palm and the
   palm stored by payload uid). With the map pointing both ways, pass 2's
   join resolved source→`prefixed` but target→`bare`; the sides missed each
   other, split across the two baseline rows' mismatched hashes, and the diff
   manufactured a BothModified-shaped conflict out of one untouched record.

2. **"The hub-row deletion under an unresolved AskUser conflict looks like a
   separate invariant break."** Confirmed — it is one, and it was worse than
   it looked: the phantom Delete wasn't a Conflict op so the walk applied it
   in-line while the same record's conflict deferred, AND the toSource ops
   are accumulated before the walk even runs, so sibling writes landed too.
   Fixed structurally: a run that defers any AskUser conflict unresolved now
   holds ALL writes for that mapping (merge lists cleared,
   `success=false`, "N unresolved conflict(s); no data was written").
   All-or-nothing per mapping-run: we chose this over suppressing only the
   conflicting record because a failed run must have committed nothing;
   throughput cost on conflict-pending mappings accepted. Answered
   resolutions still replay through `pendingResolutions` next run — all
   v0.98 resolution-injection contracts re-verified green.

3. **Bisect offer** — not needed; both mechanisms reproduced deterministically
   lib-side and were RED→GREEN gated (see below).

## Root cause (defect A)

O55's fix anchored each logical record's canonical id at "the requested id
of THIS apply". That flips depending on which side was written: a target-side
create anchors bare; a later back-propagation whose op carries the hub-space
id anchors prefixed. The crossed anchor plus the second baseline row split
the record across two join keys — O55's disease, reintroduced through the
baseline layer exactly as you suspected ("this time via the baseline layer").

Fixes:
- **Anchor stability at persist time** — new aliases chain-resolve the
  canonical side to its component sink before writing; a row whose requested
  id already resolves to the assigned id's sink is a no-op, so crossings are
  never written. Baselines always save under the sink key, overwriting the
  one row instead of adding a second.
- **Load-time heal** — stores already poisoned by v1.00 runs (your evidence
  dump's shape: bidirectional aliases + dual rows) are normalized in memory
  every run: every id resolves to its component sink with deterministic
  cycle-breaking, and baseline rows collapsing onto one sink are deduped,
  preferring the row matching current side hashes. No manual recovery step.

## Verification

Three new slots in `tests/engine/tst_engine_id_aliasing.cpp`, all RED against
v1.00 and GREEN with the fix:

- `recategorizationViaHubEdit_anchorStaysConsolidated` — your scenario
  lib-side: converged first sync → hub edited in place under prefixed id →
  pass 1 propagates to the bare-id peer (create there) → pass 2 converges
  with zero movement, exactly ONE baseline row, ONE alias direction.
- `poisonedCrossedAliasStore_healsWithoutDataLoss` — a store poisoned exactly
  like your evidence dump heals and converges with zero movement.
- `unresolvedConflict_deferredMovesNothing` — standing contract for ask #2:
  a deferred Unmonitored AskUser conflict moves nothing, deletes nothing,
  reports failure honestly.

Full suite 180 tests, 177 passing — identical pre-existing baseline
(`tst_remotecalendarbackend`, `tst_calendar_canon_roundtrip`,
`tst_backend_signals`; all Radicale live-state / catalogued).

Thanks for an exemplary second handoff — the dual-row/asymmetric-hash/
crossed-alias dump let us reconcile every symptom line-for-line before
touching code.
