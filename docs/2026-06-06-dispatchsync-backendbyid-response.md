# Response: dispatchSync `backendById` RFC landed as v0.66

**Date:** 2026-06-06
**To:** WildPalms (`feature/three-tier-sync`) — re:
`WildPalms/docs/2026-06-06-libkalburator-dispatchsync-backendbyid-regression.md`
**Cc:** PlanStan (green-gate co-consumer)
**Status:** CLOSED — fix landed, regression test added, tag cut.

> Delivery note: as with v0.65, this doc is the canonical notification.

---

## Pin details

- **GIT_TAG:** `v0.66`
- annotated tag object: `6ed87d2f8aaada0b078ec217a16fb7ed6824e7b2`
- resolves to merge commit: `fa4f070b6fb84483386a82e7550ee0a9b5ea974e` (== `origin/main`)

WildPalms can bump its pin `v0.65 → v0.66` in the standard dedicated commit.
**No WP-side code change is required** — your `PalmSyncHost::backendById`
`dynamic_cast` stays exactly as it is (it is the type-correct implementation;
the engine simply no longer calls it on the dispatch path).

## What landed (your §3 primary proposal, not the alternative)

All `m_controller->backendById()` dispatch-path lookups now go through
`BackendRegistry::backendInstance()` with `SyncBackendBase*` locals
(`16afeb0`):

- `processSync` loss-profile prep (your `:1526`)
- `dispatchFirstSync` (your `:1709`)
- `harvestBaselinesAfterFirstSync` (your `:1868`)
- `dispatchSync` (your `:1931` — the failing-test site)
- `unifiedContinueAfterConflicts` (your `:2592`)

`ISyncHost::backendById` itself is untouched (your "keep it for legacy
calendar paths" suggestion) — `CalendarManager` and the calendar-path
consumers still use it legitimately. Its eventual neutralization remains an
open item in our redress-campaign FINDINGS (Plan-8-adjacent).

## Corrections to the RFC (verified against source before landing)

Your diagnosis was confirmed end-to-end, with four refinements found during
verification — none change your conclusion:

1. **There were SIX sites, not five.** `SyncEngine::advanceQueue`'s
   ResourceLost skip (`syncengine.cpp:911-912` at v0.65) also routed through
   `backendById` (via `auto*`). For base-only backends the null result made
   `srcLost`/`tgtLost` silently evaluate false — the skip-on-lost-resource
   protection never engaged for them. Fixed with the same pattern.
2. **The worker had no registry.** Your diff sketch assumed `m_registry` was
   reachable in `dispatchSync`; the five worker sites live in
   `SyncEngineWorker`, which only held `m_controller`. `setDependencies()`
   gained a `BackendRegistry*` parameter (threading semantics unchanged —
   your host already read the registry from the worker thread, so this is
   the same access pattern minus the vtable detour).
3. **`runPropertyPhase` was also calendar-typed** (`SyncBackend*` params) and
   would not compile against the narrowed pointers. Its params are now
   `SyncBackendBase*`; the `DomainOperations` property hooks it forwards to
   were already neutral, so nothing else moved.
4. **`pushItems` (your §3 prose) is never called on these pointers** — the
   verified call surface is `shapeFor` / `fetchItems` / `discoveredWritable`
   / `resourceId` plus `IBlobBackend` record CRUD via `asBlob()` (which now
   takes `SyncBackendBase*`). Your §2 grep list was accurate.

## Regression coverage added

`tests/blob/tst_engine_baseonly_backend.cpp` — two tests using
`SyncBackendBase`-only backends and a **type-correct host** that
`dynamic_cast`s exactly like `PalmSyncHost::backendById` (returning nullptr
for base-only backends). Both failed with `dispatchSync: backend not found`
before the fix and pass after. This closes the coverage hole that let the
regression ship: every other in-tree engine harness registers
calendar-derived stubs behind an unchecked `static_cast`, so the engine's
typed lookups were never exercised against a base-only backend.

## Acceptance criteria (your §4) — all verified 2026-06-06

| Criterion | Result |
|---|---|
| Five sites adjusted (+ the 6th we found) | done (`16afeb0`) |
| `tests/calendar/` stub-`ISyncHost` harness still passes | 137/137 in-tree (full suite incl. all calendar integration tests) |
| PlanStan ctest baseline green | 98/118 — failed-set **identical** to the pre-change baseline (20 headless `Not Run` GUI tests; environment, not code) |
| WP's three tests turn green | **Pre-verified on our side**: temp clone of WildPalms `f826612`+ built against v0.66 → **120/120**, including `tst_palm_runtime_route_first_sync`, `tst_palm_runtime_route_recategorization`, `tst_runtime_carddav_e2e` |

Your pin-bump verification should be a formality.

## Two WildPalms-side observations from our gate runs (FYI, not blockers)

1. `tst_palm_mass_delete_guard_e2e` has a **pre-existing nondeterministic
   heap-teardown abort** ("corrupted double-linked list" after all 4 subtests
   pass, post-`cleanupTestCase()`): ~5/15 isolated-run failures against
   pre-Plan-6 libkalburator and ~4/15 against Plan 6 — i.e. unrelated to
   library changes. Likely a double-free in a fixture/runtime teardown path.
2. `tests/runtime/CMakeLists.txt` hardcodes
   `${CMAKE_SOURCE_DIR}/../libkalburator/tests/sync/fakecaldavserver.cpp` —
   it assumes the flat sibling layout even when WildPalms is cloned
   elsewhere. Consider resolving the helper through
   `WILDPALMS_LIBKALBURATOR_SOURCE_DIR` instead.
