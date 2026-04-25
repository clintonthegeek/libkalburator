# Batched CTag PROPFIND + Mapping Skip — landed 2026-04-25

**Status:** Phase 1 (batched PROPFIND) and Phase 2 (coordinator-level
skip + local fingerprint, behind a `skipUnchangedMappings` config flag,
default off) both implemented and merged on `main`. Phase 2b
(StagingController hook for in-memory unsaved-change detection) deferred.

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

## Phase 2 — landed 2026-04-25

Code in this repo, on `main`:

| Commit | Scope |
|---|---|
| `e4d6997` + `c8e6450` | `SyncStore`: schema bump v3 → v4, `local_fingerprints` table + 4 accessors (`localFingerprint`, `setLocalFingerprint`, `clearLocalFingerprint`, `clearLocalFingerprints`). The `c8e6450` follow-up adds the missing CREATE-error check and the timestamp-type-divergence comment per code review. |
| `4c2f9a9` + `873ddb5` | `LocalBackend::calendarFingerprint(calId) const`: sha256 hex digest over the sorted list of `(filename \| mtime \| size)` tuples for `*.ics` files. The `873ddb5` follow-up replaces deprecated `addData(const char*, int)` with the Qt 6 `QByteArrayView` overload (hash bytes unchanged). |
| `0a93461` + `e43e0a4` | `SyncCoordinator`: rename `primeBatchedCtags` → `prepareSyncFastPath`, extend pre-pass to also collect Local fingerprints and decide per-mapping skip eligibility; new `setSkipUnchangedMappings(bool)` runtime setter; `processNextMapping` consults `m_skippedMappingIds` and emits a successful no-op `syncCompleted` for skipped mappings; `onWorkerSyncCompleted` persists fresh ctags / fingerprints to `SyncStore` on success. The `e43e0a4` follow-up clears `m_freshState` and `m_skippedMappingIds` at the start of single-mapping `runSync(mappingId, ...)` to prevent stale-baseline writes per code review. |

Tests in PlanStan, on `master`: `f8efb581` (SyncStore round-trip) and `c9d2f097` (LocalBackend determinism). PlanStan-side wiring: `d88b5f0e` (`AppSettings::syncSkipUnchanged` + KConfig + coordinator setter call + settings-dialog checkbox) and `0bba93e1` (live-toggle via `AppSettings::settingsChanged` signal connection per code review).

### Behavior

`SyncCoordinator::prepareSyncFastPath` now extends the Phase 1 batched-CTag pre-pass with two further responsibilities:

1. For each enabled mapping endpoint that's a `LocalBackend`, compute `calendarFingerprint(calId)` and cache it.
2. Per mapping: if both endpoints are covered (each is a `RemoteBackend` with a fresh primed CTag matching `SyncStore::ctag(...)`, or a `LocalBackend` with a fresh fingerprint matching `SyncStore::localFingerprint(...)`), the mapping is **eligible** to skip. If `m_skipUnchangedMappings` is true, the mapping ID enters `m_skippedMappingIds` and `processNextMapping` emits `syncCompleted` with `success=true` without dispatching the worker. If the flag is false (default), a `qInfo` log line records "would skip" — telemetry-only.

After a non-skipped mapping completes successfully, `onWorkerSyncCompleted` persists `m_freshState[mappingId]`'s ctags / fingerprints back to `SyncStore` so the next sync's pre-pass has up-to-date baselines.

Default flag state: **off**. Coordinator runs in telemetry-only mode until a release window of feedback motivates flipping the default.

### Out of scope, deferred to Phase 2b

- `StagingController::hasStagedChanges(backendId, calId)` hook for in-memory unsaved-change detection (the file-mtime fingerprint catches everything that's been written through to disk).
- UI surface for skip telemetry (e.g., a sync-summary line "5 of 11 calendars skipped").
- Backends other than `LocalBackend` and `RemoteBackend` (DecSync, Org, Mock) — they're treated as "always changed" and never participate in the skip path until they get their own fingerprint mechanism.
