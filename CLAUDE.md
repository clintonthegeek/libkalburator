# libkalburator — Claude instructions

## 🚨 READ THIS FIRST — critical open bug, not yet fixed (filed 2026-08-21)

`RemoteCalendarBackend` assumes every item's server URL is
`<calendar>/<uid>.ics`. False for any item created by another CalDAV
client (which keeps its original server-assigned filename forever) — the
first edit-and-sync of such an item fails permanently with a SabreDAV
"uid already exists" 400. No conflict needed to trigger it; this is more
severe than anything the conflict-resolution-repair work below fixed, and
unrelated to it. **Fix this before anything else.** Full analysis, exact
root-cause location, live confirmation, and the recommended fix shape:
`docs/2026-08-21-remotecalendarbackend-uid-url-assumption-critical-bug.md`
(also logged as `docs/campaign/FINDINGS.md` **O54**).

---

This repo is the in-flight extraction of PlanStan's sync library into a
standalone project shared with Wild Palms. The source of truth for the
overall plan lives in PlanStan at
`~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`.

## Consumer coordination — cross-repo status index (updated 2026-08-21)

Current release **v0.97**, on `main` (the parallel-sync campaign merged
2026-08-21 — see below). PlanStan pins **v0.97**. WildPalms is still on an
older pin (mid-port from v0.77 as of the last check) and needs zero changes
for this campaign — it never calls `setMaxConcurrentMappings()`, so it stays
at the library's default concurrency of 1, bit-identical to before. The
single "where do the three
repos stand" page is **`docs/2026-07-19-consumer-coordination-status.md`** —
consult it (and update it) whenever a consumer files an RFC/handoff, an inbound
item resolves, or a pin moves. **Open inbound items** (logged in
`docs/campaign/FINDINGS.md`, full index in §2c of the status page): **O54**
— CRITICAL, urgent — `RemoteCalendarBackend` guesses every item's write URL
as `<calendar>/<uid>.ics`, false for any item another CalDAV client created;
first edit-and-sync of an adopted calendar's pre-existing items fails
permanently (PlanStan, live session 2026-08-21 — see the top of this file);
**O55** — non-blocking, can wait — TwoWay sync between a bare-id backend and
the `GenericSqliteBackend` hub churns and silently empties the hub from pass
2 on, regression v0.77→v0.93+ (WildPalms handoff 2026-08-21, catching up past
a dormant pin). O46, O47, and **WP-A1 calendarsOnly** are all resolved/closed
(§3 of the status page). Historical note: the **calendar per-kind VTODO/VJOURNAL
canon dispatch** shipped as **v0.80** (spec/plan under
`docs/superpowers/{specs,plans}/2026-06-28-calendar-per-kind-canon-dispatch*`;
resolved the 2026-06-28 PlanStan handoff).

## Conflict-resolution repair — code COMPLETE 2026-08-21, branch NOT yet merged

Branch **`feature/conflict-resolution-repair`** (`3902f40` → `79d5d77`), off
`main` @ `b0bf3d5`. **Not merged, not tagged, not pushed.** Answers PlanStan's
`docs/2026-08-21-conflict-info-canonical-data-and-unmonitored-resolution-handoff.md`.

**Read `docs/2026-08-21-conflict-resolution-repair-response.md` first** — it is
the authoritative summary for any consumer question about conflicts. Plan and
locked decisions: `docs/2026-08-21-conflict-resolution-repair-plan.md`.

Four defects, one root cause (the canon-upgrade campaign promoted
`BackendRecord::data` to canonical Shape JSON inside `dispatchSync()`, and the
conflict code was never taught):

- **A** — `ConflictInfo::source/targetIcalData`, documented "Full iCal", always
  carried canonical JSON. Both construction sites collapsed into one
  `SyncEngineWorker::buildConflictInfo(op)` that demotes through the reverse
  pipelines (`m_unifiedCanonToSrc`/`m_unifiedCanonToTgt`, stashed beside
  `m_unifiedCanonical`).
- **B** (critical) — a resolution chosen in `SyncBehavior::Unmonitored` wrote
  one `SyncConflictStore` column and never touched data, so the same conflict
  re-presented forever. Fixed by **resolution injection**: the resolution is
  persisted, rehydrated at run start, carried on `SyncEngineWorker::Request`,
  and replayed in `unifiedHandleConflicts()` through
  `applyConflictResolution(op, resolution, mergedNative)` — the helper
  extracted from `resumeAfterConflict()`'s `switch`. **The existing write path
  is reused verbatim; `SyncEngine` never got its own backend write access**
  (INVARIANTS §1). A bounded follow-up pass (`kMaxResolutionPasses = 2`) rides
  `pumpQueue()`'s L2 re-prime so the resolution lands in the same `runSync`.
  Stale resolutions are discarded, not applied; applied ones are consumed once,
  only on the successful-write branch.
- **C** — `resumeAfterConflict(resolution, mergedIcal)` never read `mergedIcal`;
  `CustomMerge` silently ran the automatic merger instead. Now promoted through
  `m_unifiedSrcToCanon`, with guarded fallbacks.
- **D** — `Duplicate` rewrote the clone's uid via `data.replace("UID:"…)`,
  which never matches canonical JSON (`"uid"`), so "Keep Both" emitted a
  colliding clone. Now rewritten through `CanonEnvelope`. Closes PlanStan's
  `docs/bugs/sync-dialog-keepboth-duplicate-not-created.md`.

**Suite: 179 total, 177 passing** — exactly the pre-existing baseline; the same
two failures (`tst_remotecalendarbackend` Radicale auth,
`tst_calendar_canon_roundtrip`). `tst_syncengine_unification` grew from 4 test slots to 14 (runner totals 6 → 16, counting fixtures).

`tests/calendar/tst_calendar_conflict.cpp`'s contract was deliberately
**flipped**: it asserted an Unmonitored run left the target unwritten, which was
pinning defect B in place. It now asserts the resolution lands.

New FINDINGS: **O48** (baseline *bytes* are stored nowhere → `baselineIcalData`
always empty → PlanStan's 3-way diff unreachable; needs a storage decision),
**O49**/**O50** (fixed), **O51** (staleness guard is second-granular and
unprotective for backends reporting no `lastModified`), **O52** (a rehydrated
`CustomMerge` loses the user's payload), **O53** (pre-existing: the batch
conflict dialog is modal inside `onWorkerSyncCompleted` while other mappings are
in flight — live because PlanStan defaults to 4 concurrent mappings).

**Still explicitly USER-RUN:** live verification against a real CalDAV account
that a resolved conflict lands on the server, stops re-presenting, and that
"Keep Both" now yields two items. All coverage above is stub-backend.

## Parallel-sync campaign — CLOSED 2026-08-21, merged to `main` at v0.97

`parallel-sync` fast-forwarded onto `main` (was a strict superset — zero
divergence) and has been deleted. `SyncEngine` now runs multiple sync
mappings concurrently: `setMaxConcurrentMappings(int)` (library default
**1**, bit-identical to every pre-campaign consumer — concurrency is
opt-in per host), an endpoint-collision scheduler (`pumpQueue()`) that
never lets two mappings diff/apply against the same (backend, calendar) at
once, source/target fetch overlap within a mapping, chunked `LocalBackend`
I/O, and a per-backend `maxConcurrentOperations()` veto
(`RemoteCalendarBackend` declares 4). `phaseChanged`/`progressUpdated`
semantics were redefined for concurrency — `Complete` describes the RUN,
not one mapping (WildPalms' `shouldPauseTickle()` depends on this, though
WildPalms itself is unaffected since it never raises concurrency above 1).

**Suite: 179 total, 177 passing** — the same two pre-existing failures
throughout campaign and after merge (`tst_remotecalendarbackend`: broken
local Radicale test-server auth; `tst_calendar_canon_roundtrip`:
pre-existing on `main`, uncatalogued, still needs triage). Verified
identical at N=1 and N=4 (`KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS=4`)
during the campaign; re-verified on `main` post-merge at default N=1.

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

**PlanStan-side (thread-per-backend + pin bump + concurrency setting) is
also DONE and merged**, pinning **v0.97** (carries the `BackendRegistry::
m_instances` QMutex fix — a genuine pre-existing write-path race, guarded
the same way as `TransformationRegistry::m_frozenDomainsMutex`). PlanStan's
own `AppSettings::syncMaxConcurrentMappings()` defaults to **4** — that's a
host-side choice, not a library default change.

**Still open, explicitly USER-RUN, not attempted by any agent session:**
re-measuring the original ~76s/11-mapping table against a real
multi-calendar CalDAV account with concurrency on, and a live Radicale gate
exercising real concurrent sync end-to-end with a display. Full history:
`~/dev/PlanStan/docs/superpowers/plans/2026-08-12-parallel-sync.md` (task
source, STATUS header marked complete).

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
