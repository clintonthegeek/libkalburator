# Batched CTag PROPFIND (Phase 1) — landed 2026-04-25

**Status:** Implemented and merged on `main`. Phase 2 (coordinator-level
skip + local fingerprint, behind a `skipUnchangedMappings` config flag)
deferred to a separate plan.

## Source of truth

- Proposal: PlanStan `docs/proposals/2026-04-25-sequential-sync-skip-unchanged.md`
- Implementation plan: PlanStan `docs/superpowers/plans/2026-04-25-batched-ctag-propfind.md`

## What landed

Code in this repo, on `main`:

| Commit | Scope |
|---|---|
| `240f0d0` | Scaffold: declarations + `PrimedCtag` struct + `m_primedCtags` member + `primeCtagCache` impl. |
| `db91ae2` | Wire `RemoteBackend::fetchItems` PROPFIND fast-path to consult primed cache before the per-call PROPFIND. |
| `058d46f` | Implement `RemoteBackend::fetchAllCtags` — group calIds by parent URL, one Depth:1 PROPFIND per group, parse multistatus with `QXmlStreamReader`. |
| `1dd95af` | `SyncCoordinator::primeBatchedCtags()` pre-pass called from `runSync(SyncBehavior)` before the worker thread starts. |

Test in PlanStan, on `master`: `ba9fb909` — `testFetchAllCtagsBatched` exercises one batched PROPFIND against the local Radicale.

## Behavior

Before: each enabled mapping that hit the CTag fast-path inside
`fetchItems` issued its own Depth:0 PROPFIND. With N mappings against
the same server, that's N round-trips just to ask "did anything change?"

After: `SyncCoordinator::runSync` runs a synchronous pre-pass that
groups all mapping calIds per `RemoteBackend` instance, calls
`fetchAllCtags(ids)` (one Depth:1 PROPFIND per parent URL — typically
one parent per server), and primes each backend's `m_primedCtags`
cache with a 60-second freshness window. `fetchItems` then short-circuits
its own PROPFIND when a fresh primed CTag is on hand.

Failure modes are graceful: any error in `fetchAllCtags` returns an
empty result, the cache stays empty, and `fetchItems` falls back to
the existing per-call PROPFIND path. No semantic change in any case.

## What's left for Phase 2

- `LocalBackend::calendarFingerprint(calId)` — cheap fingerprint (filename + mtime + size hash).
- `SyncCoordinator` mapping-skip decision (drop unchanged mappings from the queue pre-flight).
- `skipUnchangedMappings` config flag (location TBD: KAlb config / PlanStan AppSettings / libkalburator runtime knob).
- Telemetry / log-every-skip mode for the first release window.
- Staging-aware skip via PlanStan's `StagingController::hasStagedChanges` hook.

These will be planned in a separate doc once Phase 1 has been observable in real syncs for a while.
