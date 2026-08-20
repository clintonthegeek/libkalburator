# libkalburator — Claude instructions

This repo is the in-flight extraction of PlanStan's sync library into a
standalone project shared with Wild Palms. The source of truth for the
overall plan lives in PlanStan at
`~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

## Consumer coordination — cross-repo status index (updated 2026-07-19)

Current release **v0.94**. Both consumers (PlanStan, WildPalms) pin **v0.94**;
WildPalms is mid-port on its v0.77→v0.94 leap. The single "where do the three
repos stand" page is **`docs/2026-07-19-consumer-coordination-status.md`** —
consult it (and update it) whenever a consumer files an RFC/handoff, an inbound
item resolves, or a pin moves. **Open inbound items** (both low-severity,
non-blocking; logged in `docs/campaign/FINDINGS.md`): **O46** — surface the
read-only write-skip in `SyncResult` (WildPalms RFC 2026-07-18); **O47** —
`MockBlobBackend` never computes `contentHash`, manufacturing spurious conflicts
post-v0.93 (WildPalms handoff 2026-07-19). Ready to close: **WP-A1 calendarsOnly**
(both consumers signed off 2026-07-18 — flip the `MultiProtocolDavProvider` ctor
default to `false`). Historical note: the **calendar per-kind VTODO/VJOURNAL
canon dispatch** shipped as **v0.80** (spec/plan under
`docs/superpowers/{specs,plans}/2026-06-28-calendar-per-kind-canon-dispatch*`;
resolved the 2026-06-28 PlanStan handoff).

## Parallel-sync campaign — START HERE if on branch `parallel-sync`

Tasks 0-10 of 16 are DONE on branch `parallel-sync` (cut from `main@3fcb842`),
closing sync-excellence's §16-parked "parallel mapping execution" residual
(see `docs/campaign/FINDINGS.md`'s resolved entry for the full reversal
rationale and defect list). `SyncEngine` can now run multiple sync mappings
concurrently: `setMaxConcurrentMappings(int)` (default 1, bit-identical to
every existing consumer), an endpoint-collision scheduler (`pumpQueue()`)
that never lets two mappings diff/apply against the same (backend,
calendar) at once, and `phaseChanged`/`progressUpdated` semantics redefined
for concurrency (`Complete` describes the RUN, not one mapping — WildPalms'
`shouldPauseTickle()` depends on this).

**Suite: 179 total, 177 passing, identical at N=1 and N=4** (three
consecutive `KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS=4` sweeps) — the same
two pre-existing failures throughout (`tst_remotecalendarbackend`: broken
local Radicale test-server auth; `tst_calendar_canon_roundtrip`:
pre-existing on `main`). Tag **v0.95** lands with Task 10's docs commit.

`KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS` is a **test-only** env knob
(read once into a `static` in `resolveEffectiveCap()`, memoized for the
whole process): forces every Queue-mode run's concurrency to the given
value regardless of what the host requested, except Monitored runs, which
stay pinned to 1 unconditionally. Never consulted unless set — production
and every real consumer are unaffected. Because the `static` is memoized
per-process (= per test binary, since QTest runs all slots in one
process), a test cannot override the sweep back down via
`setMaxConcurrentMappings()` once any earlier test in that binary has read
it; a test whose contract is genuinely concurrency-1-only must instead
guard the sweep-invalidated assertion behind
`!qEnvironmentVariableIsSet("KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS")` —
see `tst_syncengine_unification.cpp` and `tst_engine_cancellation.cpp` for
the pattern.

**Remaining:** Task 11 (thread-per-backend) and Task 12 (pin bump +
concurrency setting) are PlanStan-side. Tasks 13+14 are USER-RUN (live
Radicale gate). Task 15 is docs closeout in both repos. Full task detail:
`~/dev/PlanStan/docs/superpowers/plans/2026-08-12-parallel-sync.md` (the
only task source; its Execution Order section is authoritative and NOT
sequential).

## Architectural-redress campaign — START HERE if on a branch `feature/redress-N-*`

If your CWD is on any branch matching `feature/redress-N-*` (N = 1..11), you are
working the campaign opened 2026-05-29 from a fresh-eyes audit of the post-canon
codebase. The audit found the canon-upgrade convergence (below) succeeded but the
underlying layering, encapsulation, and naming grew leaks no one stopped to name.
The redress is the next sustained body of work.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/architectural-redress/INVARIANTS.md` — the rules you accept by
   working here. Non-optional.
2. `docs/campaign/architectural-redress/AUDIT.md` — the fresh-eyes findings this
   campaign exists to redress. The audit wins if it disagrees with a plan.
3. `docs/campaign/architectural-redress/STATUS.md` — campaign state, the 11-plan
   sequence, locked decisions, your next action.
4. `docs/campaign/architectural-redress/FINDINGS.md` — the discipline log; append
   to it (invariant 9) when you walk past a smell.
5. The current plan in `docs/campaign/architectural-redress/plans/`.

New smells go in `FINDINGS.md`; update `STATUS.md` in the same commit that
changes plan state.

## Canon-upgrade / convergence campaign — START HERE if on branch `feature/canon-upgrade-convergence`

If your CWD is on branch `feature/canon-upgrade-convergence`, you are working
the campaign that (a) retires `src/transcoding/` into the shape graph and
(b) upgrades the calendar/contacts/todo canons to rich JSON superset encodings
behind a **versioned canonical spine** with a four-kind loss model.

**Status (2026-05-24): the campaign is COMPLETE — all four plans landed; `src/transcoding/`
is deleted and the shape graph is the sole transformation mechanism (invariant 1).**
Downstream port (FINDINGS O7/O12) DONE; O7 resolved 2026-05-27, O12 effectively closed;
branch merged to `main`. See `docs/campaign/STATUS.md` for the full history.

**Before your first non-trivial change, read in this order:**
1. `docs/campaign/INVARIANTS.md` — the rules you accept by working here. Non-optional.
2. `docs/campaign/STATUS.md` — campaign state (now: converged), the 4-plan sequence, locked
   decisions, and the remaining downstream next actions.
3. `docs/campaign/FINDINGS.md` — open watch items (esp. O9) and the discipline log.
4. The plans, all complete: `docs/2026-05-23-plan-1-shape-core-foundations.md`,
   `docs/2026-05-23-plan-2-per-engine-registries.md`, `docs/2026-05-24-plan-3-canon-encodings.md`,
   `docs/2026-05-24-plan-4-calendar-convergence.md`.
5. Design set (as needed): `docs/2026-05-23-canon-upgrade-and-convergence-design.md`,
   `docs/2026-05-23-canon-schema-design.md`, `docs/2026-05-23-vendor-api-shapes-reference.md`.

The one-paragraph why: libkalburator grew **two** parallel conversion mechanisms;
this campaign collapses them into one (the shape graph) and modernizes the canons.
The deepest invariant (INVARIANTS §1): extend the shape graph, never fork a third
mechanism. New issues/smells go in `docs/campaign/FINDINGS.md`; update
`docs/campaign/STATUS.md` in the same commit that changes plan state.

## Sync-graph-redesign campaign (PlanStan-originated) — Phase 1 CLOSED 2026-07-16; current release v0.94

**This repo's Phase 1 is complete — do not redo its work.** The full
cross-repo plan (this repo, Graffodil, PlanStan) lives in PlanStan at
`~/dev/PlanStan/docs/superpowers/plans/2026-07-15-sync-graph-redesign.md`
(status tracked there — see PlanStan's own `CLAUDE.md` for the current
cross-repo campaign summary). This repo's Phase 1 (Tasks 1-5, branch
`sync-graph-engine`, merged to `main` and deleted) added engine-level
sync convergence fixes and per-LC wiring: **L1** un-freezes the
once-per-run fast-path skip set when an earlier mapping in the same
Queue run writes a shared endpoint (`SyncEngine::invalidateSkipsTouching`);
**L2** adds fixpoint passes — a Queue run re-primes over dirtied mappings
(up to `kMaxSyncPasses = 3`, `syncPassStarted(int,int)` signal) so
convergence no longer depends on mapping list order; per-LC
`WiringPolicy` (`CollectionDefault`/`Hub`/`Mesh`/`Chain`/`Manual`) lets
individual logical calendars override the collection's default sync
topology, with `Manual` meaning the compiler skips that LC entirely;
providers (`CalDavProvider`/`MultiProtocolDavProvider`/`CardDavProvider`)
now reliably emit `Connecting`/`Connected`/`Error` connection states with
populated `lastError()` — this required relocating `ProviderConnectionState`
from `providermanager.h` to `iprovider.h` and making
`connectionStateChanged` a genuine C++ signal overload
(`(bool)` and `(ProviderConnectionState)` coexist; disambiguate with
`qOverload<...>(&IProvider::connectionStateChanged)`). Tag: **v0.94**.
Full suite 170/172 at close (the pre-existing `tst_remotecalendarbackend`
Radicale-state flake, plus a pre-existing-but-newly-surfaced
`tst_calendar_canon_roundtrip` failure independently confirmed unrelated
to this campaign — canon/iCal classification encoding, no code-path
overlap). Known gap carried forward for PlanStan's later tasks:
`ProviderManager`'s aggregate `providerStateChanged` surface does not
forward the new Connecting/Error granularity (only Connected/Disconnected)
— PlanStan must bind directly to `IProvider::connectionStateChanged`.

## Sync-excellence campaign — CLOSED 2026-07-09 (CP-C); current release v0.91

**The campaign is complete — do not redo its work.** The full phase plan
and per-phase evidence are archived at
**`docs/campaign/archive/2026-07-07-sync-excellence-phases.md`** (see its
§17 CP-C entry for the closing soak/adversarial/efficiency evidence).
Phases E1–E13 + checkpoints CP-A/B/C landed: honest stats + dead code
(E1), the O26 flake (E2), cancellation/teardown honesty (E3), CalDAV
write-path pins (E4), the async-backend rework deleting the
nested-event-loop re-entrancy (E5/audit B7), EtagCache seeding (E6),
RFC 6578 `sync-collection` (E7), phantom-conflict adoption (E8),
signal/fingerprint polish (E9), PlanStan adoption (E10), the
CalendarManager async API (E11/O39), canon timestamp-stamping (E12/O41),
the PlanStan presentation-freeze fix (E13/O44), and the CP-C deferral
fixes (O42 first-fetch sync-collection amnesia; O45 bounded write-dispatch
window). Tags: v0.85, v0.90, v0.90.1, **v0.91** (close). FINDINGS O26,
O28–O36, O39, O41–O45 all Resolved; the §16 residual inventory was PARKED
at CP-C with rationale. New sync issues get a new O-number in
`docs/campaign/FINDINGS.md`; any future campaign should reuse the §0
session-protocol + strong-model-checkpoint discipline — both prior
campaigns' live checkpoints each caught a blocking bug the green suite
missed (O25, O27), and CP-C caught two more (O42, O45 rulings).

Lineage (context only, all CLOSED): sync-convergence campaign (Tracks A–C,
tags v0.80–v0.82; roadmap `docs/campaign/2026-07-03-sync-convergence-roadmap.md`,
now closed end-to-end) → sync-hardening campaign (D1 threading + O16–O27,
tags v0.83/v0.84; plan archived at
`docs/campaign/archive/2026-07-05-sync-hardening-phases.md`). The
architectural reference both campaigns and this one build on is the
first-principles audit
(`docs/campaign/archive/2026-07-05-first-principles-sync-architecture-audit.md`)
— its §1 target model is what E5 finishes implementing.

## Phase-status docs are living documents

All phase progress is tracked under `docs/phase0/`. When a phase
completes, fails, or pauses, update the corresponding status file in
the same commit that lands the code change. In particular:

- `04b-phase3-status.md` — Phase 3 status. Keep the **Status** line at
  the top accurate ("Phase 3a done", "Phase 3b in progress", etc.) and
  update the "What exists now" and "Next" sections as work lands.
- Any new phase doc should follow the same pattern: Status line at top,
  "What exists" / "What remains" sections, updated every time the phase
  state changes.

Do not leave a status doc saying "paused" after work has resumed, or
"WIP" after it has landed. Future sessions start from these docs — if
they lie, work gets redone or skipped.

## Build

Standalone build:

```
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

Default profile: `KALBURATOR_HAVE_ORG_IO=OFF`, `KALBURATOR_HAVE_AKONADI=OFF`.

## Calendar-layer integration tests (since Phase D.0, 2026-04-28)

`tests/calendar/` contains stub-`ISyncHost` integration tests that
pin `SyncEngine` behavior. They are the contract the engine-merger
refactor (Phases D / E / F / G) preserves. Phase F1 (2026-04-30,
tag `v0.13-phase-f1-unify`) collapsed `SyncCoordinator` +
`SyncWorker` + `BlobSyncEngine` into the unified `SyncEngine` at
`src/engine/syncengine.{h,cpp}` — historical references to those
old class names appear in commit messages and FINDINGS but should
not be used in new code or comments.

When writing or modifying tests in this directory:

- Use the four reusable stubs at `tests/calendar/stubs/`:
  `StubSyncHost`, `StubCalendarCollection`, `StubIncidenceRegistry`,
  `StubSyncConfigStore`. Compiled into static lib
  `kalburator_calendar_test_stubs`. Link via the helper function
  `kalburator_add_calendar_integration_test()` in
  `tests/calendar/CMakeLists.txt`.

- **Canonical engine entry: `SyncEngine::runSync(SyncRequest)`**
  returning `QFuture<QList<SyncResult>>` (redress Plan 1). This is the
  **sole** sync entry — the four `runSyncFuture(...)` overloads were
  DELETED in redress Plan 8 step 3 (2026-06-10), along with
  `dispatchSingleNative` and the dual `m_currentSingleIface`/
  `m_currentMultiIface` interface; the engine now holds one
  `m_currentIface` + one `m_currentWatcher` wired by `beginRun()`.
  Build a `SyncRequest` (`mappingIds` empty ⇒ all enabled; size 1 ⇒
  single mapping; size >1 ⇒ subset). Wait via
  `QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000)` (NOT
  `waitForFinished` — Qt6's `waitForFinished` does NOT spin the test
  event loop). Read results via `future.resultAt(0)` — a
  `QList<SyncResult>` (NOT `future.results()`, empty after cancel due
  to a Qt6 quirk). The void `runSync` overloads, `cancelSync`, and the
  `syncCompleted`/`allSyncsCompleted` signals were deleted in
  F2 Task 42.

- **Single-mapping cancel is now native** (Plan 8 step 3): a canceled
  single-mapping `runSync(SyncRequest)` future preserves the F2 Task 23
  contract — `resultCount()==1`, `resultAt(0).first().cancelled==true` —
  with **no `.then()` wrap** and **no `resultCount()>0` guard** needed.
  (Pre-collapse the canonical single path lost this; only the deleted
  shims preserved it. Pinned by `tst_engine_single_mapping_cancel`.)

- **Cancellation** — call `future.cancel()`. The cancellation
  channel propagates through
  `QFutureWatcher::canceled → SyncEngine::onCancelObserved →
  SyncEngineWorker::observeCancel` and wakes the nested `QEventLoop`s
  that gate cancellation: `dispatchSync`'s two fetch-gate loops
  (source/target, H1.1) and the conflict-pause slot. (The `await<Op>`
  template that used to be the shared idiom for this was dead code —
  zero call sites — and was deleted in H1.4.)

- **Write path** — `SyncBackend::storeItems()` / `updateItem()` /
  `writeFinished` were DELETED (canon-upgrade campaign; only stale
  comments mention them). The write API is the 2-arg
  `pushItems(calendarId, items)` returning a `PushOperation*`;
  read `op->state()` / `op->errorString()` for error reporting
  (per the F2 SyncOperation contract). `TranscodingPlan` no longer
  exists — transformation flows through the shape graph.

- **Conflict tests** — set `mapping.conflictPolicy = AskUser` AND
  seed a baseline via `BaselineStore::setBaselineV3()` (the
  mapping-keyed v3 API in `storage/baselinestore.h`; there is no
  `SyncStore` class). Other policies resolve silently without
  signals; the quick-path (no baseline) downgrades AskUser to
  SourceWins.

- **`StubCalendarCollection`** must hold a `MemoryCalendar` with
  `setId(calendarId)` matching the `SyncMapping`'s calendar id, or
  `applyChangesToBackend` can't find it and writes get dropped.

See `docs/phase0/04l-phase-d0-test-harness-design.md` and
`04l-phase-d0-test-harness-plan.md` for the full pattern, including
test-execution model and gotchas.
