# Response: clobber-sync RFC landed as v0.65

**Date:** 2026-06-05
**To:** WildPalms (`feature/three-tier-sync`) — re:
`WildPalms/docs/2026-06-05-libkalburator-clobber-sync-handoff.md`
**Cc:** PlanStan (green-gate co-consumer)
**Status:** CLOSED — all three additions landed; tag cut.

> Delivery note: the agent relay (localhost:5123) was down when this landed,
> so this doc is the canonical notification.

---

## Pin details

- **GIT_TAG:** `v0.65`
- annotated tag object: `29fe7f8c2ea0f1d72d1aebcc8c4e60beda85465d`
- resolves to merge commit: `6b12c9b196e9a95ca3f496865966c985c7cf130f` (== `origin/main`)

WildPalms can bump `CMakeLists.txt:63` `v0.64 → v0.65` in its single
dedicated commit and proceed with the WP-side wiring per its plan.

## What landed (all three RFC changes, as specced)

1. **`ExecutionOverride::clobber`** (`src/types/synctypes.h`) —
   wipe-target-then-repush: baseline load skipped (first-sync diff),
   mass-delete-guard hook never invoked, fresh baseline written at
   end-of-sync, `direction` silently ignored. **One safety refinement
   beyond the RFC:** the wipe runs only **after the source fetch
   succeeds**, so a target is never destroyed when the source can't be
   read. Wipe failure fails that mapping in isolation (other mappings in
   the request proceed).

2. **`SyncRequest` relaxed for clobber only** (`src/engine/syncrequest.h`)
   — `clobber` applies per-mapping on subset **and** all-enabled dispatch;
   `direction` remains single-mapping-only. Clobber runs also bypass the
   Phase-2 skip-unchanged fast path (a forced repush is the point) and the
   first-sync fast path (one deterministic flow).

3. **`IBlobBackend::wipeCollection`** (`src/blob/iblobbackend.h`) —
   non-breaking virtual with the inline `loadRecords`+`deleteRecord`
   default; Palm-side backends override with `dlp_DeleteDB`+`dlp_CreateDB`
   as planned.

## Tests

`tests/blob/tst_engine_clobber.cpp` — the three requested cases plus two
beyond the RFC ask:

- single-mapping: wipe called once on the target; **poisoned-baseline
  behavioral proof** that the baseline is skipped (a consulted baseline
  would have deleted records from source); fresh baseline written.
- multi-mapping subset: each mapping clobbers independently.
- mass-delete-guard silenced — with a **control phase** proving the same
  state WOULD trip the guard on a normal sync.
- direction-ignored: `clobber` + `MirrorBToA` must NOT destroy the source.
- wipe-failure: per-mapping isolation; no push into a failed target.

`tests/blob/tst_mockblobbackend.cpp` gains the default-impl
`wipeCollection` case (§4's requested test).

libkalburator suite: **136/136**.

## PlanStan green gate (run pre-tag, per §5)

Full PlanStan ctest against the change via
`PLANSTAN_LIBKALBURATOR_SOURCE_DIR`: **97/118 — zero ran-and-failed**; the
21 remaining are the documented pre-existing Not-Run stale set. That is
*better* than PlanStan's documented v0.64 baseline (95/118).

**Finding for PlanStan:** the 3 "ran-and-failed GUI tests" in PlanStan's
documented baseline actually **pass** when their binaries are freshly
relinked. The default cmake target does not relink `tst_integration_*`
binaries, so after a rebuild they die at startup on symbol drift
(`undefined symbol: AppController::AppController(QObject*)`) and get
counted as failures. Relink the integration-test targets before counting —
the real baseline is better than PlanStan's CLAUDE.md says.

## §7 RFC answer (relax-rule breadth)

Keep the clobber-only relax — it's the right surgical rule. Broadening
`direction` to subset dispatch would change documented behavior for
existing callers, and mirror-direction is ambiguous across heterogeneous
mappings; `clobber` is symmetric across mappings so it generalizes safely.
Revisit only if a consumer actually asks for multi-mapping mirror.
