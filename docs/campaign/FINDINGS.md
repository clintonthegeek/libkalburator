# Campaign FINDINGS — canon-upgrade / convergence

Two sections:
- **Open issues / watch items** — things discovered that a future task must handle.
  When you resolve one, note the commit and move it to "Resolved."
- **Discipline Log** — one-line smell reports per invariant 9. `file:line` + invariant
  number + one phrase. No fix required in the session you log it.

Append, don't rewrite. New issues from any task go here, even off-topic.

> **Sync-hardening campaign CLOSED 2026-07-06.** The phase plan
> (`docs/campaign/2026-07-05-sync-hardening-phases.md`) is archived at
> `docs/campaign/archive/2026-07-05-sync-hardening-phases.md`. It closed
> FINDINGS **O16–O27** (all Resolved below); tags **v0.83** (H1–H6) and
> **v0.84** (H8.5/O27).
>
> **Sync-excellence campaign CLOSED 2026-07-09 at CP-C.** The phase plan
> is archived at
> `docs/campaign/archive/2026-07-07-sync-excellence-phases.md`
> (phases E1–E13 + CP-A/B/C; tags v0.85, v0.90, v0.90.1, **v0.91** at
> close). It closed FINDINGS **O26, O28–O36, O39, O41–O45** (all Resolved
> below). The §16 residual inventory (parallel mappings, Akonadi warm
> path/O14, CardDAV REPORT extraction, contacts-backend loops, RFC 6638)
> was PARKED at CP-C with rationale — see the archived plan's §16/§17.

---

## Open issues / watch items

### O1 — `LossProfile` is surfaced to the engine layer
`tests/engine/tst_engine_unified_routing.cpp:383` reads `lastLossProfile().level`, and
`tests/engine/tst_carddav_engine_integration.cpp` imports `LossLevel`. Plan 1 Task 2
migrates these to the kinds API. **Watch:** if a stub host or engine code (not just
tests) exposes `LossProfile::level`/`.dropped`, Task 2's full build will name it — fix
with the documented substitutions, don't reintroduce the old fields. (Seeded 2026-05-23.)

### O2 — Carry-verbatim containers not yet field-specified
The non-isomorphic structures (event `RANGE=THISANDFUTURE`; todo hierarchy:
`relatedTo` tree / `parentUid` / `checklistItems`) are decided in principle (schema
§2, §4) but their exact JSON shape and differ/merger handling land in **Plan 3**.
Invariant P4 applies: specify the carrier before diffing it. (Seeded 2026-05-23.)

### O3 — Live API validation deferred, not done
The vendor-shapes reference (`docs/2026-05-23-vendor-api-shapes-reference.md`) is from
docs + MS open specs, not live calls. Edge cases (real recurrence payloads, IANA↔Windows
mappings, Graph immutable-id behavior) should be validated with live Google/Graph calls
**during Plan 3** when the concrete (de)serialization stages are written. Not a blocker
for Plans 1–2. (Seeded 2026-05-23.)

### O4 — WildPalms port is downstream, but its invariants are upstream
WildPalms moves its conduits onto the converged pipeline **after this branch merges**
(handoff §3, §7). We do not wire WildPalms. But the five WildPalms invariants
(invariant 10) must hold throughout — especially that the loss model can express
`Reversible` (X-property round-trip) before Plan 3 writes the palm/canon edges. (Seeded 2026-05-23.)
**Update 2026-05-25:** WildPalms' memo holdout now has a concrete, human-approved design — a `note`
plaintext-carrier domain + `(note, markdown)` peer (frontmatter ⟷ `providerExtras`, `Reversible`) +
a `MarkdownFilesBackend` sink. See `docs/2026-05-25-note-domain-design.md`. Additive, post-convergence;
implementation plan pending. The `Reversible` requirement is satisfied by the existing four-kind model.

### O5 — `pipeline.cpp` loss folding assumed clean
Planning grep found no `.level`/`.dropped` usage in `src/shape/pipeline.cpp`, so
`composedLoss()` should fold via `compose()` unchanged. **Watch:** confirm during Plan 1
Task 2's full build; if `composedLoss()` references the removed fields, update it the
same way. (Seeded 2026-05-23.)

### O6 — design §8 was imprecise; corrected when Plan 2 was designed
The §8 planning stub said **two** `Shape::` singletons remain (`TransformationRegistry`,
`DomainRegistry`) and that they would be "owned by the `SyncEngine`." Reading the landed call sites
showed **three** (the engine also reads `DomainOperationsRegistry` at `syncengine.cpp:1874`,`:2423`;
~40 tests `clear()` it), and that engine-ownership is wrong because `PluginManager` is an independent
*writer* of the same registries. §8 was rewritten (2026-05-24) to the resolved topology — an injected
`ShapeRegistries` bundle owned at the composition root, shared by reference with both PluginManager
(writer) and SyncEngine (reader), the OSGi `BundleContext` model. Recorded as a documented deviation
from the stub per the INVARIANTS deviation rule. (Seeded 2026-05-24, Plan 2 design.)

### O8 — Plan-3/Plan-4 boundary: calendar canon moved into Plan 3 (scope decision)
Design §10's file-change list placed "`calendarstockshapes.cpp`: add canon + `ical↔canon`
bridges" under the **Plan 4** (convergence) work, while the STATUS Plan-3 row named
`calendar+canon` as Plan-3 scope — two sources of truth disagreeing (inv 7). Surfaced while
writing Plan 3 (2026-05-24). **Decision (human, 2026-05-24):** land all three canons —
including `calendar+canon` and its `ical↔canon` bridges — in **Plan 3**; narrow **Plan 4** to
pure convergence (retire `src/transcoding/`, RRULE-as-edge, remove `ApplyContext.transcodingPlan`
+ `CalendarPluginWriter` special-casing). Safe because the engine reads canonical from
`DomainDefinition::canonicalShape()` (not the graph) and transcoding is dormant in the default
build (empty plans; `RRuleTranscoder` only fires for `orgmode` backends, not built when
`KALBURATOR_HAVE_ORG_IO=OFF`). Plan 3 Task 13 records the same in STATUS on completion.
(Seeded 2026-05-24, Plan 3 authoring.)

### O9 — pre-existing `tst_providerlifecycle` failure (unrelated, not introduced by campaign)
`tst_providerlifecycle::provisionProvider_backendsReadyEmittedAfterConnectAll` fails in
test #87 (introduced in commit `b395e5b`, before the campaign branch). The failing assertion
`ready.count() >= 1` implies a signal-timing or async issue in the `ProviderLifecycle` test
harness. The campaign suite is 111/112 green; this one excluded. **Watch:** investigate
independently; do not confuse with campaign regressions. (Seeded 2026-05-24, Plan 3 Task 13.)

### O10 — design §10 "delete src/transcoding/ in full" is wrong: incidencediff/syncdiff are load-bearing
While researching Plan 4 (2026-05-24), found that `src/transcoding/incidencediff.{h,cpp}` and
`src/transcoding/syncdiff.{h,cpp}` are **conflict/diff engines**, not transcoding, and are used widely
outside the dir: `src/engine/syncengine.{h,cpp}`, `enginediff.h`, `propertydiff.h`,
`src/calendar/icalrecorddiffer.{h,cpp}`, `icalrecordmerger.cpp`, `updateincidenceitem.cpp`,
`decsyncactivecontroller.{h,cpp}`, and tests `tst_syncdiff.cpp`/`tst_incidencediff.cpp`/
`tst_calendar_subsequent_sync_uses_blob_view.cpp`. So design §10's "Delete `src/transcoding/` in full"
(inv 7: two sources of truth) cannot be taken literally. **Plan 4 retires only the transcoding
machinery** (`transcodingregistry`, `transcodingrouter`, `transcodingplan`, `rruletranscoder`,
`propertytranscoder`) and must **keep `incidencediff`/`syncdiff`** (move them out of the dir or leave
the dir as their home — decision pending). (Seeded 2026-05-24, Plan 4 research.)

### O11 — legacy RRuleReverseTranscoder was a silent no-op (latent bug, superseded by Plan 4 Task 2)
While re-homing RRULE simplification (Plan 4 Task 2, commit 624d2f3), found that the legacy
`src/transcoding/rruletranscoder.cpp` `RRuleReverseTranscoder::transcode` (~:138-174) restored the
stashed original RRULE via `KCalendarCore::ICalFormat::fromString(RecurrenceRule*, ruleStr)` — which
**always returns false for RRULE strings** (both the `RRULE:FREQ=…` form and the value-only form),
so the reverse transcoder silently restored *nothing*. No test ever covered it. The new
`OrgICalToCanonStage` sidesteps the broken API: it stashes verbatim iCal recurrence *lines* (joined by
`|`, a separator RFC5545 never uses) and restores by byte-level line injection — strictly more
faithful (byte-for-byte) and proven by `tst_orgical_canon_roundtrip`'s round-trip slot. The legacy
class is deleted in Plan 4 Task 8 regardless. (Seeded 2026-05-24, Plan 4 Task 2.)

### O14 — Akonadi ChangeRecorder warm-path deferred (DEFERRED, 2026-05-26)

The Akonadi change-detection backbone is the payload-free id+revision digest
(`Backend::ChangeDetection`, `AkonadiRevisionStore`). A `ChangeRecorder`-based
persistent warm-path (skip even the local digest fetch using a cross-restart change
journal) was scoped but deferred: `ChangeRecorder`'s recording mode changes Monitor
signal-delivery semantics (queued, `replayNext`-driven) and would risk the live cache
the write path depends on, and an in-memory-only dirty set cannot safely account for
changes during downtime (which the digest already handles). The digest fetch is a cheap
local-DB read, so the warm-path is a marginal optimization. Revisit only if profiling
shows the digest fetch is hot. (Decided 2026-05-26.)

### O16 — `prepareSyncFastPath()` still blocks the caller's thread for network I/O after D1 relocation (RESOLVED, H4, 2026-07-05)

D1 Stage 1's T1.5 GUI-stall probe (`tests/calendar/tst_backend_thread_relocation.cpp`,
`stallProbe_relocatedBackends_stayResponsive`) FAILS as of this writing: with both mapping
backends relocated to a dedicated I/O thread and a 200ms latency-injected fake CalDAV server,
the calling ("GUI") thread still stalls ~213ms — almost exactly one network round-trip.

Root cause: `SyncEngine::driveQueue()` calls `prepareSyncFastPath()` (line ~384)
**synchronously, on whatever thread called `runSync()`**, *before* `startWorkerThread()`.
`prepareSyncFastPath()` fetches fresh ctag/fingerprint revisions per backend
(`collectionRevisions`, `cachedCollectionRevision`) to decide per-mapping skip-eligibility —
this file's T1.4 fix (see the `runOnBackendThread()` helper added to `syncengine.cpp`) made
those calls thread-*safe* (no more silent cross-thread QObject/SQLite access) by marshaling
them via `QMetaObject::invokeMethod(base, ..., Qt::BlockingQueuedConnection)` when the backend
lives on a different thread than the caller. That fix was necessary and correct, but it cannot
by itself close the stall: `Qt::BlockingQueuedConnection` is *synchronous by definition* — it
parks the calling thread until the target thread finishes, regardless of which thread actually
does the work. Relocating the backend changes *who* performs the network I/O; it does not make
the caller stop waiting for it.

This is architecturally different from the ~19 pre-existing `BlockingQueuedConnection` sites in
`syncengine.cpp`, which are all reached from `SyncEngineWorker`'s own dedicated
`m_workerThread` — a thread that, by construction, is never the GUI/caller thread, so blocking
it was always safe. `prepareSyncFastPath()` and `onWorkerSyncCompleted()`'s `persistRevision`
(the same finding, smaller impact — it runs once per mapping *after* the worker already did the
real work, so it adds a per-mapping tail stall rather than a per-sync head stall) are the two
sites that run on the *caller's* thread instead, which pre-D1 was harmless (caller thread ==
backend thread == a direct, already-fast call) and only becomes a real stall once backends
relocate.

**Scope note:** this is a materially smaller problem than the 120s bulk-transfer freeze (finding
N7) that motivated D1 — it's bounded by one ctag/fingerprint round-trip per
`ChangeDetection`-implementing backend per sync (typically the source AND target of every
mapping), not by the size of the actual data being synced. D1's core deliverable — bulk
fetch/push work no longer blocking the GUI — is proven working by
`fullEngineRun_relocatedBackends_completesAcrossThreeThreads` in the same test file. This is a
narrower, separate gap in the *pre-check* phase, not a regression in the fix's core mechanism.

**Fix directions (not yet implemented, needs a decision before Stage 1 can close):**
1. Make `prepareSyncFastPath()` run asynchronously as part of the worker thread's own sequence
   (start the worker thread first; have it run the fast-path check, then decide skip-eligibility,
   then process the queue — all before ever handing control back to the caller synchronously).
   This is the architecturally clean fix and mirrors the existing worker-thread pattern, but it
   touches `driveQueue()`'s control flow (cancellation, `m_isSyncing`, the DecSync active-
   controller loop that currently runs inline before it) and needs its own test coverage.
2. Accept the bounded stall for D1 and defer a full fix to D2, documenting the acceptance gate's
   50ms threshold as "during the worker-thread-driven bulk phase" rather than the whole cycle.
3. Disable `m_skipUnchangedMappings` (the fast-path feature) when backends are relocated, trading
   away the optimization to avoid the stall — cheapest but regresses a real, shipped perf win.

Not fixed this session. Flagging here + in the D1 execution plan's checklist so Stage 1 doesn't
get marked closed while this is open.

**H4 fix (2026-07-05):** implemented fix direction 1 above. `driveQueue()` no longer calls
`prepareSyncFastPath()` inline; it starts the worker thread early and emits
`SyncEngineWorker::fastPathRequested(mappings, storedTokens, skipEnabled)` (new engine→worker
command-channel signal, same `QueuedConnection` pattern as `processSyncRequested`). The batched
per-backend revision query — same logic as before — moved to a new worker slot,
`SyncEngineWorker::prepareFastPath()`; its `runOnBackendThread()` marshal now blocks the
**worker** thread (already expected to block on backend I/O) instead of the caller. The worker
emits `fastPathReady(skipped, freshState)` back to the engine (queued); `SyncEngine::
onFastPathReady()` stores the results and calls the same `finishDriveQueueSetup()` continuation
`driveQueue()` itself uses for the clobber/no-fast-path branch — including its cancelled-
teardown path, so a `future.cancel()` landing while the fast path is still in flight on the
worker reports canceled and dispatches no mapping (new test:
`cancelDuringFastPath_reportsCancelled`). `SyncEngine::FreshSyncState` moved from a private
nested struct to public (it now crosses the engine/worker signal boundary); its
`Q_DECLARE_METATYPE` lives in the public `syncengine.h`, not the private `syncengine_p.h`,
because moc-generated code for `SyncEngine` (built from `syncengine.h` alone) needs to see the
specialization before any implicit instantiation of the unregistered-type fallback.
`stallProbe_relocatedBackends_stayResponsive` — RED since the D1 stall-probe test landed —
is green as of this fix; full suite 160/160 green for the first time in the campaign.

### O17 — failed apply phase + fetch-time CTag commit + skip fast-path strands changes (RESOLVED, H3, 2026-07-05)

Highest-severity finding of the 2026-07-05 first-principles audit
(`docs/campaign/2026-07-05-first-principles-sync-architecture-audit.md`, §A1 — full
scenario and fix there). `RemoteCalendarBackend::fetchItems` commits the fresh CTag at
*fetch* time (correct for the content cache); `prepareSyncFastPath()` reads that same
backend-global token as if it meant "this mapping is up to date." A mapping whose fetch
succeeds but whose apply phase fails **without writing to the target** is skipped on every
subsequent cycle — the fetched-but-never-applied delta is stranded until an unrelated
server change bumps the ctag. Also bites mapping B when mapping A shares B's source
collection. No threading involved; live today with `skipUnchangedMappings` on (PlanStan
default). Fix: per-`(mappingId, side)` token consumption recorded by the engine in
BaselineStore on mapping success, token captured atomically with the fetch (audit §1.4/§6
step T1.7). Needs a RED test first.

**CP-A ruling (2026-07-05):** H3 design reviewed against post-H1/H2 code and CONFIRMED
with two amendments (recorded in the phase plan §10, edited into its §6): BaselineStore
schema bump is v6→v7 (not v5→v6 — B4 already stamped 6), and `clearMappingV3` must also
clear the mapping's sync_tokens so an API-level baseline wipe can't leave skip-enabling
tokens behind (A4). Pre-fetch-snapshot semantics, the present-but-unused ChangeDetection
methods, and the accepted one-cycle re-diff lag are all confirmed as pre-decided. H3 may
proceed.

**H3 (2026-07-05):** implemented as amended. BaselineStore gained a `sync_tokens` table
(schema v7) keyed `(mapping_id, side)`; `SyncEngine::prepareSyncFastPath`'s skip check
now compares each side's fresh revision against `BaselineStore::syncToken` instead of
the backend's own `cachedCollectionRevision`; `onWorkerSyncCompleted` persists
`m_freshState`'s pre-fetch snapshot via `setSyncToken` only on `result.success`, and
persists nothing on failure. `driveQueue`'s clobber branch now also calls
`clearSyncTokens` for every enabled mapping (and clears `m_freshState`, which a fix
found necessary — see below). `clearMappingV3` clears `sync_tokens` too, per the CP-A
amendment. Pinned by four new tests in
`tests/engine/tst_sync_token_soundness.cpp`: `applyFailure_doesNotStrandChange` (O17,
using `RemoteCalendarBackend`/`FakeCalDavServer` as source — whose `fetchItems` commits
its own CTag on a complete fetch independent of apply success — and `MockBackend` as a
target with injectable write failure), `foreignEditBetweenCycles_defeatsSkip` (O18),
`settledMapping_keepsSkipping` (skip still works), and `clobberRun_clearsTokens`.
While writing the clobber test, found and fixed a real gap this phase's own change
introduced: `driveQueue`'s clobber branch calls `clearSyncTokens` but does not run
`prepareSyncFastPath` (by design — clobber skips the fast path), so a stale
`m_freshState` entry from a PRIOR non-clobber run would survive and
`onWorkerSyncCompleted` would silently re-persist it right after the clear. Fixed by
clearing `m_freshState` in the same branch. Existing tests `tst_engine_skip_unchanged`
and `tst_sync_convergence` (`fastPathSkipsGenuinelyUnchangedMapping`) were updated to
assert against `BaselineStore::syncToken` instead of backend-side
`cachedCollectionRevision`/`primeRevisionCache` — their observed behavior (including the
two-real-cycles-before-settle characteristic) is unchanged, only the underlying
mechanism their comments describe.

### O18 — LocalBackend post-write fingerprint re-hash masks concurrent foreign edits (RESOLVED, H3, 2026-07-05)

Audit §A2. `persistRevision`'s live re-hash after a successful mapping is written
**directly** into LocalBackend's persisted FingerprintStore (`localbackend.cpp:188-192` —
no N5-style staging, unlike the remote side). The re-hash includes any foreign edit made
to the directory during the sync window, stamping it as already-synced → next cycles skip
→ the edit is invisible until a second local change. Fix folded into O17's rework
(delete `persistRevision`; LocalBackend computes its expected post-write fingerprint
incrementally from its fetch snapshot + own write set).

**H3 (2026-07-05):** `persistRevision` (and its live post-write re-query on both sides)
is deleted outright. The engine now persists the pre-fetch snapshot
(`prepareSyncFastPath`'s `m_freshState`) as each mapping+side's token in
`BaselineStore::sync_tokens`, only on a successful run. A foreign edit landing after
that snapshot is captured can never be absorbed into it, so it can't be masked — it
shows up as a mismatch on the next fresh-vs-stored comparison. Pinned by
`tst_sync_token_soundness::foreignEditBetweenCycles_defeatsSkip`.

### O19 — remote half of persistRevision is inert; B5's one-cycle-lag goal never achieved for remote (RESOLVED, H3, 2026-07-05)

Audit §A3. `RemoteCalendarBackend::primeRevisionCache` stages into in-memory
`pendingCtag` (N5 fix) which the next fetch overwrites before it can be committed, and
`cachedCollectionRevision` reads only the persisted store — so post-push cycles never
skip (the exact lag B5's live re-query was written to remove), while the engine still
pays one extra CTag PROPFIND per mapping per cycle **on the GUI thread** for the
discarded result. Delete rather than repair (O17 rework).

**H3 (2026-07-05):** deleted rather than repaired, per plan. The engine no longer calls
`cachedCollectionRevision`/`primeRevisionCache` at all (grep-verified empty under
`src/engine/`); both remain on the `ChangeDetection` interface, doc-commented as
engine-unused, for backend-internal use and external consumers (WildPalms). The
accepted cost is a one-cycle re-diff lag after any cycle that wrote (CP-A-confirmed
safe-direction trade; see `tst_sync_convergence.cpp`'s
`fastPathSkipsGenuinelyUnchangedMapping` comment for the mechanism under the new
token design).

### O20 — `runPropertyPhase` calls backends directly from the worker thread — live UB pre-D1 (RESOLVED, H2.1, 2026-07-05)

Audit §B2. `syncengine.cpp:3043-3087` invokes `collectionProperties` /
`applyCollectionProperties` with no marshaling; via `CalendarDomainOperations` these are
direct backend virtual calls — unsynchronized `m_calendars` reads and, on a
color/description diff, a PROPPATCH (`RemoteCalendarBackend::updateCalendar`) issued
from the worker thread on a foreign-thread QNAM. Already a cross-thread QObject access
in production **today** (worker → GUI-thread backend); post-D1 the T1.1 Q_ASSERT turns
it into a debug crash. Missed by the viability audit because it routes through
`DomainOperations`, not a `SyncBackendBase*` invokeMethod.

**H2.1 (2026-07-05):** all four `runPropertyPhase` call sites now wrap the
`collectionProperties`/`applyCollectionProperties` call in `runOnBackendThread(...)`.
While writing the RED test (`propertyPhase_relocatedBackends_marshaledPerBackend`,
`tst_backend_thread_relocation.cpp`) a **fifth, previously-unlisted call site** turned
up: the T9 property-baseline snapshot in `unifiedContinueAfterConflicts`
(`syncengine.cpp` ~3082, `opsUCC->collectionProperties(srcBackend, srcColId)`) — same
unmarshaled direct-call shape, just outside `runPropertyPhase` proper. It masked the
main fix (overwrote the recorded call-thread after the correctly-marshaled call ran) and
is fixed the same way. Verified with a GDB breakpoint + backtrace that the marshaled
call genuinely executes on the backend's own I/O thread (not just that the assertion
passes) before landing.

### O21 — `dispatchFirstSync` runs target-backend writes on the source backend's thread (RESOLVED, H2.2, 2026-07-05)

Audit §B3. The first-sync blob mirror (`syncengine.cpp:1786-1833`) marshals one lambda
to the **source** backend's thread and calls `tgt->loadRecordsOrError/createRecord/
updateRecord/deleteRecord` inside it. Same-thread only by coincidence (pre-D1 GUI
thread; post-D1 the shared-I/O-thread plan). Cross-thread UB the moment backends have
distinct affinities. Either split into per-backend marshals or write "all sync backends
share one I/O thread" into the D1 plan as a hard invariant.

**H2.2 (2026-07-05):** split into three steps — source-thread load, target-thread load,
then a target-thread apply of a create/update/delete list computed in between on the
worker thread (no backend I/O in that middle step). Pinned by
`firstSync_backendsOnDifferentThreads` with source and target relocated to two
genuinely different I/O threads.

### O22 — no network timeouts anywhere ⇒ one stalled request silently and permanently wedges sync (RESOLVED, H1+H5.5, 2026-07-05)

**RESOLVED (H5.5, 2026-07-05).** The last live half — the KDAV bulk-traffic
stack that H1.2's `nam()` timeout never covered — is closed by H5.5's per-job
watchdog (see O25). All wedge surfaces are now accounted: transfer timeout on
BOTH network stacks (H1.2 `nam()` + H5.5 KDAV jobs), honest busy-rejection
(H1.3), and the fetch-gate `op->cancel()` on wake (H1.1). The CP-B live
pulled-cable smoke passes end-to-end (fail-within-timeout, post-`SIGCONT`
recovery, stranded item lands). Only the `stopWorkerThread` mid-marshal
deadlock note remains parked in H9 (a distinct teardown-ordering concern, not
the runtime wedge). Original finding + H1 partial below.



Audit §B4 (with B5/B6 adjuncts). `davSyncRequest` sets no transferTimeout and no
watchdog; engine marshals/gate-awaits are unbounded; a hung request leaves
`m_isSyncing` true forever and every later `runSync()` returns a rejected future
**indistinguishable from a successful empty run**. Sync dies silently until app
restart. Also: cancellation is queued and cannot interrupt blocking marshals; the gate
never calls `op->cancel()`; `stopWorkerThread` deadlocks pre-D1 if called mid-marshal.

**H1 (2026-07-05) closed two of the four halves:** (a) H1.2 gives
`RemoteCalendarBackend`'s lazy QNAM a 30s `setTransferTimeout` (test-overridable via
`setTransferTimeoutMs`), so a stalled `davSyncRequest` now fails instead of hanging
forever; (b) H1.3 makes the busy-rejection in `SyncEngine::runSync` report an explicit
failed `SyncResult` (`"rejected: a sync is already running"`) instead of an empty list,
so it's no longer indistinguishable from a successful no-op. Remaining halves (not
H1's job): the fetch-gate `op->cancel()` call is now wired (H1.1, see O24), but
`stopWorkerThread`'s mid-marshal deadlock risk is untouched — parked for a later phase.

**CP-B live smoke (2026-07-05): the wedge is still reproducible on the KDAV job path**
— H1.2's timeout only covers the backend's own QNAM (`davSyncRequest`), not the KDAV
jobs that carry the item listing/fetch/write traffic. See **O25** for the live repro,
root cause, and the H5.5 fix phase. (H5.5 landed 2026-07-05 — this half is now closed;
see the RESOLVED banner at the top of this entry and O25.)

### O23 — worker gate FetchOperations leak with full payloads; every sync fetches twice (RESOLVED, 2026-07-05)

Audit §C1/C2. The dispatchSync gate ops (`syncengine.cpp:2122-2165`, `:2245-2282`) are
never deleted — remote ops are unparented (leak until exit), local ops accumulate on the
backend — each retaining the entire fetched collection (`setFetchedItems`), ~720
snapshots/day at 120 s cadence. And the pipeline fetches each side twice (gate
`fetchItems` + `loadRecords`→`fetchItems` again) with ~4 CTag PROPFINDs per mapping per
cycle where one fetch + one PROPFIND would do.

**H1.1 (2026-07-05) closed the leak half:** both gate blocks now `deleteLater()` the
fetch op on every exit path (success, cancelled, failed). Pinned by
`gateOps_areDeleted_afterSync` (`tst_backend_thread_relocation.cpp`), which asserts zero
`SyncOperation` children on either backend after one sync cycle, and still zero after a
second (no per-cycle regrowth).

**H5 (2026-07-05) closed the double-fetch half.** Added
`SyncBackendBase::recordsFromLastFetch(collectionId, records, errorMessage)` — a
single-shot memo of the most recent successful `fetchItems()`, served without new I/O,
falling back to `loadRecordsOrError()` when the memo is absent (backends with no fetch
cache, or a gate op that didn't succeed). `LocalBackend` and `RemoteCalendarBackend`
populate the memo: `LocalBackend::fetchItems` builds the `BackendRecord` list inline from
the bytes already in hand (no second disk read); `RemoteCalendarBackend::fetchItems`
hooks `SyncOperation::finished` once (fires uniformly across every completion branch —
cache hit, cache miss, full network fetch) rather than touching each of the ~9
`op->complete()` call sites individually. `dispatchSync`'s two "Fetch source/target
records" blocks now call `recordsFromLastFetch` instead of `loadRecordsOrError` whenever
the gate's own `fetchItems()` succeeded.

Landing also fixed a **latent hash-instability bug** this phase's own test surfaced:
`LocalBackend::fetchItems`'s file read used `QIODevice::ReadOnly | QIODevice::Text`,
while `recordFromFile()` (the `loadRecords()`/baseline path) read the same file without
`Text` mode. `Text` mode strips `\r` on read, so the two paths produced different bytes
— and therefore different `contentHash` values — for the identical file, which
`perRecordDiff` read as "target changed since baseline", manufacturing a spurious
Conflict in place of a genuine Delete (`tst_sync_convergence::remoteDeleteRemovesExactlyOneLocally`
failure during H5 development). Fixed by dropping `QIODevice::Text` from `fetchItems`'s
read, matching `recordFromFile()`.

**RED tests** (`tst_backend_thread_relocation.cpp`): `singleFetch_localBackends_noRedundantRead`
(a `recordsFromLastFetchFellBackCount` counter on a `LocalBackend` test subclass — isolates
the gate's own fallback decision from unrelated legitimate re-reads elsewhere, e.g.
`classifyForWriter`'s diff classification and the post-write hash-verification refetch,
both out of this phase's scope) and `singleFetch_remoteBackend_noRedundantListing` (a
`fetchStarted` `QSignalSpy` count on `RemoteCalendarBackend`, measured only on the
steady-state second sync cycle — cycle 1's `dispatchFirstSync` fast path is a separate,
out-of-scope code path with its own reads). Both confirmed RED against
`feature/d1-threading` @ `16a5c14` (H4), GREEN after the fix. Full suite 160/160 green.

### O24 — the cancellation gate doesn't gate LocalBackend; F2 cancellation only ever tested against MockBackend's model (RESOLVED by H1.1, 2026-07-05)

Audit §C3. `LocalBackend::fetchItems` defers via `QTimer::singleShot`, so the gate sees
a **Pending** op and its `state() == Running` check skips the await entirely — no
cancellation window, full directory parse queued anyway into the leaked op. Remote
passes the gate only via an AutoConnection same-thread coincidence. Check should be
`!op->isFinished()`. Related: `SyncEngineWorker::await<Op>` is dead code (zero callers).

**Resolved by H1 (2026-07-05):** H1.1 changed both fetch-gate await conditions from
`state() == Running` to `!isFinished()`, so a `Pending` op (LocalBackend's shape) is
now awaited correctly, and on cancellation the gate calls `fetchOp->cancel()` and
re-enters a short teardown loop (mirroring the deleted `await<Op>`'s semantics) before
returning. H1.4 deleted `SyncEngineWorker::await<Op>` itself (verified zero call sites
via `grep -rn "await(" src/`) and fixed the false "sync runs in worker thread" comment
in `localbackend.cpp`.

### O25 — KDAV job surface has NO transfer timeout; H1.2's fix never covered the primary traffic path (RESOLVED, H5.5, 2026-07-05)

**RESOLVED (H5.5, 2026-07-05).** Added a per-job watchdog on
`RemoteCalendarBackend` (`startJobWithWatchdog(KJob*, onTimeout)`) and routed
all nine KDAV job start sites (collections-fetch discovery, `fetchItems`
list + multiget, `startSync`/`pushItems` creates, `deleteItems`/`removeItem`/
`startSync` deletes, modify) through it. A single-shot `QTimer` of
`m_transferTimeoutMs`, on expiry, detaches the job from our slots and runs an
`onTimeout` that fails/settles the owning `SyncOperation` exactly as that
site's `job->error()` branch would (a `settleIfDone` lambda was extracted in
`deleteItems` to share the accounting tail with the timeout path, mirroring
`pushItems`).

**Design correction vs the CP-B pre-decision:** the phase plan's
`job->kill(KJob::EmitResult)`-drives-the-existing-handler mechanism is INERT
on KDAV 6.27.0 — `KJob::kill()` is a no-op unless the subclass overrides
`doKill()` (default returns false, emits nothing), and no KDAV 6.27.0 job
overrides it (verified against installed headers + KDE source `v6.27.0`;
jobs track KIO subjobs manually with no abort path). Implementing the plan
verbatim left both RED tests hanging. The watchdog now fails the op directly
instead of relying on `kill()`; RED-recipe and acceptance gate unchanged.
Full detail in the H5.5 §8b design block (implementation-time correction).

**Verified:** RED tests `fetchItems_droppedRequests_failsWithinTimeout` /
`pushItems_droppedRequests_failsWithinTimeout` (drop-mode server) go from
hanging to failing Failed within ~3× a 2000 ms window; the CP-B live
pulled-cable smoke re-ran ALL PASS — phase 4's frozen-server sync failed in
9.5 s with `"Failed to list items: transfer timed out"` (the list-job
watchdog), the engine accepted a fresh runSync after `SIGCONT`, and the
stranded item landed (O17's live proof). Original finding below.



Found by CP-B's pulled-cable live check (scratch driver
`tests/engine/live_cpb_smoke.cpp`, gated behind `KALBURATOR_BUILD_LIVE_PROBES`; real
Radicale, both backends relocated onto one shared I/O thread — the H7 topology).
SIGSTOP-freezing the server mid-cycle (sockets stay open, no responses — a true
pulled-cable stall, not a connection error) produced exactly O22's wedge **despite
H1.2**: the sync future never finished (>90 s with a 5 s `setTransferTimeoutMs`), the
worker sat blocked in the fetch gate forever, and every subsequent `runSync` was
(honestly, per H1.3) rejected with "a sync is already running".

**Why H1.2 didn't cover it:** `RemoteCalendarBackend` has TWO network stacks. The
`davSyncRequest`/`nam()` path (CTag PROPFINDs, `collectionRevision`) got the H1.2
timeout — and it worked: the smoke's fast-path CTag query failed within its 5 s window.
But the bulk traffic — `fetchItems`' `KDAV::DavItemsListJob` + multiget fetches, and
the write path's `DavItemCreateJob`/`DavItemModifyJob`/`DavItemDeleteJob`,
`DavCollectionsFetchJob` discovery — runs on **KDAV's internal QNAM**, which KDAV does
not expose (no public `networkAccessManager()` accessor, no timeout API on
`DavJobBase`; verified against the installed KF6 headers). H1.2's RED test drove
`collectionRevision()` — an `m_nam` path — so it passed without ever exercising the
KDAV surface. The gate awaits the fetch op unboundedly, so a KDAV job that never
finishes wedges the engine permanently. O22 is therefore still live for the most
common real-world stall (server/network dies mid-listing or mid-PUT).

**Timeline observed live:** frozen server → fast-path PROPFIND times out at 5 s
(fresh token empty → mapping correctly not skipped) → gate `fetchItems` →
`DavItemsListJob` stalls indefinitely → future never finishes → engine wedged.

**Fix direction (pre-decided at CP-B; phase H5.5 in the phase plan):** per-job
watchdog in `RemoteCalendarBackend` — a small helper that starts a `QTimer`
(`m_transferTimeoutMs`) alongside every KDAV job and calls `job->kill(KJob::EmitResult)`
on expiry so the existing `result` handlers run their normal error path; timer torn
down in the handler. RED test: point `fetchItems` at `FakeCalDavServer` with
`setDropRequests(true)` (H1.2's drop mode already exists) and require the fetch op to
finish Failed within the watchdog window; today it hangs. Same recipe for one write-path
job. Release gate: v0.83 (H6) is BLOCKED until H5.5 lands and the CP-B pulled-cable
smoke passes end-to-end (fail within timeout, recover after server resume, stranded
item lands — the O17 live proof rides on the same re-run).

### O26 — `tst_engine_cancellation` intermittently SEGFAULTs (RESOLVED by sync-excellence E2, 2026-07-07)

**RESOLVED (E2, 2026-07-07).** Root-caused under TSAN (first repro,
`build-tsan` with `-fsanitize=thread`): a genuine heap-use-after-free, not a
scheduling fluke. Mechanism: `SyncEngineWorker::dispatchSync`'s
fetch-cancellation teardown (`syncengine.cpp:2117-2126`, mirrored at the
target-fetch gate `:2260-2269`) calls `fetchOp->cancel()` then checks
`!fetchOp->isFinished()` to decide whether to wait (via a `QEventLoop` on
`finished()`) before `deleteLater()`-ing the op. But `SyncOperation::cancel()`
(`syncoperation.cpp:34-49`) unconditionally and *synchronously* flips state to
the terminal `Cancelled` — so `isFinished()` is already `true` the instant
`cancel()` returns, and the intended wait-loop can never fire. Meanwhile
`MockBackend`'s blocking-fetch/push simulation (`mockbackend.cpp`, the
`m_fetchBlocking`/`m_pushBlocking` branches) spawns a raw detached `QThread`
that, on the original code, touched the same `SyncOperation*` directly
(`op->state()`, `op->complete()`) from that background OS thread with no
marshaling. The engine's `deleteLater()` fires on the main thread almost
immediately after `cancel()`, racing that detached thread — TSAN caught the
exact UAF (`SyncOperation::state()` read racing `FetchOperation::
~FetchOperation()`).

Fix (confined to `src/calendar/mockbackend.{h,cpp}`, a backend file — checked
against E5's scope first per this phase's own gate, since E5's real target is
production backends' nested-loop re-entrancy, not this test-only simulation
class): the blocking-mode threads no longer touch `op`/`this` at all. A first
attempt routed the completion through `QMetaObject::invokeMethod(op, ...,
Qt::QueuedConnection)` — this still crashed under the plain (non-TSAN)
parallel suite (`QObject::thread()` on a dangling `op`), because
`invokeMethod`'s own thread-affinity lookup dereferences its context object
before queuing; routing through a possibly-dangling `op` as context is exactly
as unsafe as touching it directly. The real fix: marshal onto `this`
(`MockBackend`) instead, which the new `~MockBackend()` keeps alive for the
duration of any spawned blocking thread by `wait()`-ing each one
(`m_blockingThreads`, a `QList<QPointer<QThread>>`) before the rest of
destruction proceeds — the old destructor was `= default` and never joined
these detached threads, so nothing prevented the backend (or its ops) from
being destroyed out from under them. The invoked lambda re-resolves the
operation via `m_pendingBlockingFetchOps`/`m_pendingBlockingPushOps`
(`QHash<QString, QPointer<...>>`), touched only from `this`'s own thread —
exactly where `QPointer`'s automatic null-on-delete is safe to observe.

Also fixed in the same pass: `~MockBackend()`'s thread-join closes a second,
related defect — without it, TSAN's own thread registry could abort
(`sanitizer_thread_registry.cpp:186` `CHECK failed`, tid reuse) across
back-to-back `TstEngineCancellation` slots in one process, present even before
this fix (reproduced on the stock code, filed separately as **O37** since it's
a TSAN-runtime artifact orthogonal to O26's named crash, not something E2 was
scoped to chase).

**Verification:** TSAN build, 200 isolated single-testcase repetitions across
all 8 cancellation-relevant slots (`cancelBeforeStart`, `cancelDuringFetch`,
`cancelDuringApply`, `cancelDuringConflictPause`,
`cancelMultiMappingMidQueue`, `idempotentCancel`, `cancelAfterFinished`,
`engineDestroyedMidSync_freesInterface`) — zero use-after-free, zero registry
CHECK failures. Plain (non-TSAN) release build: 200/200 clean runs each for
`tst_engine_cancellation` and `tst_engine_single_mapping_cancel`. Full
parallel suite (`ctest -j 8`, 160 tests): 3/3 consecutive 100%-green runs
(previously this exact pair SEGFAULTed reliably on run 1 with the naive
`invokeMethod(op, ...)` fix, proving the isolated-slot TSAN passes alone
would have been a false-green gate).

### O27 — `applyBatch`'s BackendThread branch runs `writer->apply()` on the worker thread — steady-state CalDAV *updates* do network I/O + SQL cross-thread (RESOLVED by H8.5, 2026-07-06)

**RESOLVED (H8.5, 2026-07-06).** `applyBatch` was split so the mass-delete
guard decision still resolves on the worker thread (it reaches the
engine-thread `m_baselineStoreAnchor` via `BlockingQueuedConnection`, which
must not be entered from a backend thread), but `writer->apply()` now runs on
the backend's OWN thread for `Threading::BackendThread` writers — marshaled
via a second `QMetaObject::invokeMethod(backend, ...,
Qt::BlockingQueuedConnection)` — honoring `recordwriter.h:34`. `WorkerThread`
writers keep the direct worker-thread call (their `apply()` marshals
internally). The three-marshal shape (classify → guard → apply) is what lets
the guard's engine round-trip sit between the two backend-thread hops instead
of nesting inside one — dissolving the deadlock the old contract-breaking
shape was dodging. RED test:
`tst_backend_thread_relocation::steadyStateWrites_appliesOnBackendThread`
(target `LocalBackend` relocated to a dedicated I/O thread; a steady-state
update+delete cycle must record `updateRecord`/`deleteRecord` on the backend's
thread — pre-fix recorded the worker thread). Full suite 160/160 green.
**Live re-run of the H8 40-edit modify pass** (PlanStan dev build against the
fixed sibling, scratch Radicale :5233, both backends on the shared I/O
thread): editing all 40 bulk `.ics` and letting the auto-sync push the
modifications produced **zero** "Cannot create children" / "does not belong
to the calling thread" / "database not open" lines (pre-fix: 41/41 updates
emitted all three), `setRawIcs` logged "Updated ETag to:" for every update,
and the on-disk `cached_items` etag rows matched the live server exactly
(persist write now commits; pre-fix it silently failed every update). A clean
SIGTERM restart skipped both mappings as unchanged (no spurious re-download of
edited items), and a CTag-unchanged cycle served all items from the persistent
cache with correct post-update content. Data converged both directions with no
corruption. NOTE (orthogonal to O27): a *CTag-change* re-diff after restart
still re-lists+re-downloads because the KDAV `EtagCache` that drives the
delta's changed-item determination is not seeded from disk at startup — that
is the separate roadmap-D2/H9 "persist/seed KDAV EtagCache" item, not this
finding.

Original report (for the record):


Found by the CP-C/H8 live soak (PlanStan dev build, scratch Radicale :5233,
both backends on the shared I/O thread — H7 topology). Editing 40 local
`.ics` files and letting the 120s auto-sync push the modifications produced,
for **every updated record**, live cross-thread violations in the app log:

```
QObject: Cannot create children for a parent that is in a different thread.
(Parent is QNetworkAccessManager(0x...), parent's thread is QThread(0x...a0)
 [backend I/O thread], current thread is QThread(0x...d78) [engine worker])
qt.sql.qsqldatabase: QSqlDatabasePrivate::database: requested database does not belong to the calling thread.
qt.sql.qsqlquery: QSqlQuery::prepare: database not open
```

**Root cause:** `RecordWriter::threading()` defaults to `BackendThread`,
whose documented contract (`src/shape/recordwriter.h:34`) is "the engine
wraps `apply()` in a BlockingQueuedConnection to the backend's own thread."
The engine does not honor it: `applyBatch`'s BackendThread branch
(`src/engine/syncengine.cpp` ~2945–2962, inside
`unifiedContinueAfterConflicts`) marshals only the *classify* step to the
backend thread, then calls `applyWithGuard(batch)` → `writer->apply()`
**directly on the worker thread**, with a comment claiming "the apply()
implementation marshals back to the backend thread internally if needed."
`DefaultBlobWriter::apply` does no such marshalling — it calls
`createRecord`/`updateRecord`/`deleteRecord` straight through, and
`RemoteCalendarBackend::updateRecord` → `setRawIcs` does synchronous QNAM
I/O (child QObject creation on a QNAM owned by the I/O thread — UB) plus
thread-affine `QSqlDatabase` access (the etag/ctag cache writes **silently
fail** with "database not open"). The branch's shape appears to be a
deliberate deadlock dodge: `applyWithGuard` consults the mass-delete guard
via a BlockingQueuedConnection to the engine-thread `m_baselineStoreAnchor`,
which must not be entered from the backend thread — the fix traded a
deadlock for a contract break.

**Why H2/H7 missed it:** H2.1 marshaled `runPropertyPhase`'s
DomainOperations sites and H2.2 split `dispatchFirstSync` (whose blob-mirror
create/update/delete IS correctly marshaled, syncengine.cpp:1890); H7's
sweep covered PlanStan GUI-side call sites. This path is engine-internal,
update-only (creates route through the marshaled `pushItems` gate — which is
why the 650-item initial push was clean), and no test asserts the writer
contract.

**Observed impact:** data converged (HTTP 204s; Qt networking tolerated the
UB this run), but this is O20-class live UB on the *most common* real-world
path — every steady-state modification pushed to a CalDAV server — and the
per-item etag-cache persistence on that path never commits.

**Impact scope-check (same soak, corrected):** an earlier read of the log
suggested an infinite per-item re-push loop; counting disproved it — each
of the 41 updates ran `setRawIcs` exactly once and the mapping skipped
cleanly on later idle cycles. The reason convergence survives is that
`setRawIcs` updates the **in-memory** `m_localEtags` before the persistent
write fails, so within one app run the cache is coherent. The persistent
failure ("database not open", one per update — 41/41 in the soak log)
means the on-disk etag cache never learns post-update ETags: after an app
**restart**, every previously-updated item re-downloads once on the next
re-diff (cache rot, bounded). The campaign-blocking part of O27 is
therefore the cross-thread QNAM/SQL UB itself — the exact class D1 exists
to eliminate — not a convergence break.

**Fix direction (pre-decided, CP-C, strong model):** split
`applyWithGuard` — resolve the mass-delete guard decision on the worker
thread FIRST (it only needs the batch + baseline count), then marshal the
`writer->apply()` call to the backend's thread via
`QMetaObject::invokeMethod(backend, ..., Qt::BlockingQueuedConnection)` for
`Threading::BackendThread` writers, exactly as `recordwriter.h` promises
(WorkerThread writers keep the current direct call). RED test: a
thread-recording stub backend (H2.1's pattern) asserting
`updateRecord`/`deleteRecord` execute on the backend's thread during a
steady-state modify sync; today they record the worker thread. Fix phase:
**H8.5** in the phase plan; campaign close (CP-C §3) is BLOCKED on it.

### O28 — partial push + server crash leaves N same-UID/no-baseline records that re-conflict every cycle — recovery needs manual conflict resolution and busy-loops until then (Resolved 2026-07-08 — sync-excellence E8)

Found by CP-C/H8's kill-Radicale-mid-push pass: 30 new local items, server
SIGKILLed after 7 creates had landed (6 logged + 1 whose 201 response died
in flight). The failed mapping correctly persisted nothing (O17 fix
working as designed), so the next cycle saw those 7 UIDs present on both
sides with **no baseline** and byte-different content (local = original
file bytes, remote = engine-serialized copy; PRODID/property-order differ,
so contentHashes differ). Result: 7 conflicts, batch-deferred to the dock
(hybrid threshold), mapping `success: false`, token not advanced —
**every subsequent 120s cycle re-diffs all 680 records, re-detects the
same 7 conflicts, and fails again**, indefinitely, until a user resolves
them in the dock. The other 23 items pushed fine on the repair cycle; no
data was lost or stranded (O17's actual acceptance holds).

Safe-direction (conservative: never guess, never lose data) but two costs:
(1) a crash the user never saw manufactures N phantom conflicts requiring
manual resolution for records that are semantically identical; (2) until
resolved, the mapping burns a full 680-record re-diff every cycle — the
"busy cycles when idle" state the soak watches for.

**Fix direction (H9 candidate, not campaign-blocking):** on
same-UID/no-baseline pairs, compare *canonical* content (post-transcode)
rather than native bytes before declaring conflict — byte-differing but
canonically-equal pairs should silently adopt a baseline (either side's
hash per-side, as the B4/N2 machinery already supports). Alternatively (or
additionally) make `pushItems` treat a Created-on-server-but-response-lost
item as adoptable on the next cycle via the ETag it can re-fetch. During
H8 the state was cleared surgically (deleted the 7 server copies +
`sync_conflicts` rows; next cycle re-created them cleanly with baselines)
— noted here because the live vault intervention is not a product answer.

**Resolution (2026-07-08, sync-excellence E8):** RED-test investigation
(`tests/engine/tst_phantom_conflict_adoption.cpp`) found the "fix
direction" above had ALREADY landed, unannounced, as a side effect of the
Phase B4/N2 per-side-baseline work (commit `6c36df4`, 2026-07-04 — *before*
this finding was even filed): `perrecorddiff.cpp`'s `hasS && hasT && !hasB`
branch already gates conflict declaration on `differ.equal()` (the domain's
`createCanonicalDiffer()`) and silently emits NO op when canonically equal;
`syncengine.cpp`'s `unifiedContinueAfterConflicts` independently re-scans
for exactly those silently-skipped ids and writes each side's own
`contentHash` as its baseline via `setBaselineHashesV4`. An engine-level
replay of the exact H8 crash shape (LocalBackend source with N new local
`.ics` files, `RemoteCalendarBackend` target, `FakeCalDavServer` gaining
`setDieAfterNWrites()`/`reviveOnSamePort()` to simulate a SIGKILL-and-
restart) confirms: for the literal O28 shape (PRODID/property-order-only
difference, `CREATED`/`LAST-MODIFIED` present and matching on both sides),
the repair cycle produces **zero** phantom conflicts and adopts baselines
for every survivor — no diff/merge code change was needed. E8's actual
delta: added an `qInfo()` observability line on each silent adoption
(`syncengine.cpp`'s implicit-seed loop previously adopted baselines with
zero logging — invisible in production logs) and three new regression
tests that had never existed for this path: the crash-replay itself, a
guard that a genuinely-different same-UID/no-baseline pair still conflicts
(over-adoption guard), and a blob-domain (no canonical pipeline) pin that
byte-different no-baseline pairs still conflict there (domain-neutrality —
uses a new minimal `ShapedTestBackend` fixture since `MockBackend` is
hardcoded to iCal internally). SyncStats needs no change: adopted records
never enter `engineDiff` at all, so E1.1's create/update/conflict counters
already correctly count them as neither. See **O41** for a distinct,
real bug this investigation surfaced along the way (out of E8's scope to
fix: it lives in the calendar canon *write* path, not the no-baseline
conflict classification).

### O29 — nested QEventLoops on the backend thread admit uncontrolled re-entrancy (audit B7, promoted; Resolved 2026-07-08 — sync-excellence E5; stale OPEN header fixed at CP-B 2026-07-09)

Audit §3-B7, the deepest surviving design debt from the hardening campaign.
`davSyncRequest` (`remotecalendarbackend.cpp:253`) and `awaitOperation`
(`:333`, call sites `:2326/:2396/:2437`) spin nested `QEventLoop`s **on the
backend's own thread** while a request is in flight. Nested loops process
*all* queued events — including other calls marshaled to that backend — so
any app-side backend use overlapping a running sync (editor save → PUT,
calendar-list refresh) executes *in the middle of* the in-flight operation's
wait, interleaving unguarded mutations of `pendingCtag`, `m_lastRawIcsByUid`,
the shared EtagCache, and the content cache. Named candidate mechanism for
the historical N5 corruption class. Fix (pre-decided, CP-A-reviewed): phase
**E5** — per-collection FIFO op queue in `SyncBackendBase` (E5.1), async
continuation-based `davSyncRequestAsync` (E5.2), writes as awaitable
`WriteOperation`s replacing the blocking apply (E5.3). Grep gate at close:
zero `QEventLoop` under `src/calendar/` + `src/sync/`. (Seeded 2026-07-07.)

**CP-A ruling (2026-07-07, Fable-class, reviewed against v0.85):** E5's
design CONFIRMED with four amendments, edited into the phase plan §8
directly; the three-stage cut stands. A1 — the op queue is a protected
neutral enqueue primitive in `SyncBackendBase` (entry points span two
layers: base `fetchItems`/`deleteItems`, calendar-typed
`pushItems`/`startSync`); queue advances on any terminal state AND on op
destruction; already-finished ops never occupy the in-flight slot. A2 —
the mass-delete guard resolves on the worker BEFORE the write op is
enqueued (verified: `resolveMassDeleteGuard` already runs worker-side,
blocking only toward the engine anchor and GUI — safe directions, neither
ever blocks toward the worker). A3 — `RecordWriter::Threading` and
`threading()` are deleted outright, not just superseded: zero overrides
exist across libkalburator/PlanStan/WildPalms; WildPalms has no
`RecordWriter`/`awaitOperation` exposure at all (its only lib-sync
surface is `SyncEngine` + `itemFetched`). A4 — E5.2's QEventLoop site
list was incomplete against post-E4 code: `icsfeedfetcher.{h,cpp}` is a
zero-caller orphan (E5.2 deletes it); `calendarmanager.cpp`'s three
op-await loops (:583/:630/:677) are GUI-thread — not B7's backend-thread
mechanism — out of E5 scope, filed as **O39**; the grep gate is amended
to allow annotated calendarmanager.cpp hits only. The teardown-order
invariant was re-derived and survives: worker-first remains mandatory
(settling in-flight ops needs a live backend thread); E5.3 dissolves the
O22 worker-parking wedge as planned.

**CP-A addendum A6 (2026-07-08, blessed by the campaign owner as a
checkpoint ruling):** an E5.2 in-flight trace found `fetchAllCtags`
(`remotecalendarbackend.cpp:755`) dual-reachable and only half-closed by
A5's "convert the plural override" phrasing. Plural path
(`collectionRevisions()` → engine fast-path `syncengine.cpp:1427`, backend
thread) is the live B7 mechanism; singular path (`collectionRevision()`, the
`ChangeDetection` interface method) is reached by
`FilteredCollectionBackend::collectionRevision` (`filteredcollectionbackend.cpp:255`),
which — because `FilteredCollectionBackend` does NOT override the plural —
funnels the engine's backend-thread fast-path *through* the synchronous
singular into a nested `QEventLoop` on the backend thread for any
filtered-CalDAV topology. Latent, not live (PlanStan calls the plural
override directly, no filter; WildPalms references neither
`FilteredCollectionBackend` nor `ChangeDetection` — grep-verified). Ruling:
close it at the interface, not the concrete class — add
`ChangeDetection::collectionRevisionsAsync` (neutral virtual, default =
adapt the existing synchronous `collectionRevisions`, mirroring E5.3's
`applyRecords` pattern), override it in `RemoteCalendarBackend` via
`davSyncRequestAsync`, forward it in `FilteredCollectionBackend`, and switch
the engine fast-path to call it (blocking the worker, never the backend
thread). Both paths close structurally; no annotated-synchronous survivor,
no §16 residual. The synchronous `fetchAllCtags` helper survives (singular
interface + same-thread test callers) but is no longer backend-thread-
nested-loop-reachable. Amendment edited into phase plan §8 (Stage E5.2, A6)
and the §17 E5.2 checklist line.

**E5.2 landed (2026-07-08).** Group A CTag path is async: `fetchFreshCtagAsync`
(part 1) + `fetchAllCtagsAsync`/`collectionRevisionsAsync` (A6). The
`ChangeDetection::collectionRevisionsAsync` neutral virtual (default = adapt the
sync `collectionRevisions`) is overridden in `RemoteCalendarBackend`
(davSyncRequestAsync PROPFINDs, no nested loop), forwarded in
`FilteredCollectionBackend`, and the engine fast-path (`prepareFastPath`) now
calls it, blocking the WORKER (not the backend thread) on a local `QEventLoop`
whose quit is marshaled back from the backend continuation. `fetchAllCtags`
carries the `ReentryGuard` tripwire. RemoteCalendarBackend's
fetch/push/deleteItems now route through E5.1's `enqueueOperation` (their old
per-entry `registerOperation` + ad hoc `QMetaObject::invokeMethod(this,…)`
dispatch removed; early exits moved inside the queued body so they respect
FIFO). Two re-entrancy pins GREEN (fetchItems body + filtered-view revision
query) + a CalDAV same-collection serialization pin. Remaining under O29:
E5.3 (writes → `applyRecords`, blocking apply retired, `awaitOperation`
deleted). O29 stays OPEN until E5.3 closes it.

**Resolved 2026-07-08 (E5.3).** `SyncBackendBase::applyRecords(collectionId,
WriterBatch)` (`src/sync/syncbackendbase.{h,cpp}`, batch type moved to
`src/sync/writerbatch.h`, return type `Kalburator::Sync::WriteOperation` —
new, `src/sync/writeoperation.{h,cpp}`) replaces the engine's thread-blocking
`RecordWriter::apply()` dispatch. Default impl (LocalBackend/MockBackend —
no async internals) adapts `createRecord`/`updateRecord`/`deleteRecord`
synchronously, returning an already-finished op — preserves MockBackend's
`FailurePoint` injection unchanged. `RemoteCalendarBackend::applyRecords`
overrides natively: creates → `DavItemCreateJob`, deletes →
`DavItemDeleteJob` (same job types `pushItems`/`deleteItems` already used),
updates → new `setRawIcsAsync` (async counterpart of `setRawIcs`, built on
`davSyncRequestAsync` — no nested loop), all three routed through E5.1's
per-collection queue and fanned in exactly like `pushItems`/`deleteItems`.
`SyncEngineWorker::applyBatch` (`syncengine.cpp`) now calls `applyRecords()`
via a `BlockingQueuedConnection` that only enqueues (returns immediately),
then awaits the returned op with the SAME cancellable, watchdog-free gate
shape the fetch gates already used (`QEventLoop` + `finished`
`QueuedConnection` + `cancellationObserved` `DirectConnection`) — the worker
never again parks in a blocking marshal for I/O-length work. E1.1 stats
(created/updated/deleted/errors) are now populated per-record from the
settled op's `succeededUids()`/`failedUids()` instead of one whole-batch
bool (a batch with 1 failure among 50 successes no longer misreports all 50
as errors). `awaitOperation` (`remotecalendarbackend.cpp`) is deleted from
`createRecord`/`deleteRecord` (reimplemented as direct synchronous
`davSyncRequest` PUT/DELETE, matching `updateRecord`'s existing
`setRawIcs`-based shape) but **deliberately retained for its one remaining
call site, `loadRecords()`** — a documented, narrow exception (see the
function's comment in `remotecalendarbackend.cpp`): `loadRecords()` is a
top-level, non-reentrant synchronous bridge never invoked from inside an
in-flight operation's own body, so it is not an instance of the B7 hazard
this campaign targets, and it remains a directly-tested public
`IBlobBackend` entry point (20+ call sites across
`tst_remotecalendarbackend_blob_view.cpp` and others) with no synchronous
replacement that avoids hand-rolling a second REPORT/multiget XML client.
CP-A amendment A3 lands too: `RecordWriter::Threading`/`threading()` deleted
outright (zero overrides found repo-wide); `DefaultBlobWriter::apply()`
itself now routes through `applyRecords()` for consistency. New RED tests:
`writeCancel_reportsCancelledWithHonestStats` and
`writeTeardown_engineDestroyed_completesWithoutDeadlock`
(`tst_backend_thread_relocation.cpp`), `applyRecordsInFlight_neverRunsNested`
(`tst_backend_reentrancy_pin.cpp`); `FakeCalDavServer` gained
`setResponseDelayForMethod()` to isolate a slow write from a fast
classify-read (needed to land a cancel/teardown genuinely mid-apply rather
than mid-fetch). O29 fully Resolved.

### O30 — `SyncResult::sourceStats/targetStats` are read but never populated (Resolved 2026-07-07, sync-excellence E1.1)

Promoted from the 2026-07-04 Discipline Log entry. Nothing in the unified
dispatch path writes the stats, yet two live readers consume them:
`advanceQueue`'s aggregate `statsOk` (`syncengine.cpp:803-806` — vacuously
true) and `onWorkerSyncCompleted`'s cancelled-path `skipped` classification
(`:1168-1171`) — so every cancelled run is misreported `skipped=true`
("never started") even after partial writes.

Fixed: `unifiedContinueAfterConflicts`'s `applyBatch` helper now takes a
`SyncStats&` out-param, populated from the actually-classified
`WriterBatch` — `created`/`updated`/`deleted` on a successful apply,
`errors` (the whole attempted batch) on a failed one — passed
`m_currentResult.targetStats` at the target-apply call site and
`m_currentResult.sourceStats` at the source-apply site. Unresolved-conflict
count is mirrored into `targetStats.conflicts` once, after both applies.
RED test `tests/engine/tst_sync_result_stats.cpp` (MockBackend pairs,
domain-neutral) pins: a two-item create populates `targetStats.created==2`
with `sourceStats` untouched; a cancel observed after a real apply reports
`skipped==false`; a cancel observed before any apply (blocked fetch, the
pre-existing-correct case) still reports `skipped==true`.

### O31 — dead/misleading machinery: `updateSyncMetadata`, `RecordMergerICal`, engine-unused `primeRevisionCache` residue (Resolved 2026-07-07, sync-excellence E1.2)

Promoted from Discipline Log entries + roadmap D2. (a)
`SyncEngine::updateSyncMetadata` + `makeCalendarRec` (`syncengine.cpp:912+`)
— zero call sites; writes legacy-shaped baseline rows that would be
invisible to `baselineHashesForMappingV4` if ever re-wired. Deleted both
(the comment at `syncengine.cpp:2340` documenting the legacy row shape
`baselineHashesForMappingV4` filters was reworded to drop the dangling
symbol reference rather than deleted, since the historical context it
records is still load-bearing for that filter's own doc comment).
(b) `RecordMergerICal` (`icalrecordmerger.{h,cpp}`) parsed canon JSON as
iCal — `parseIcal` returned null and it silently degraded to side-picking;
the active merger is `CanonJsonMerger`, unaffected. Deleted the merger,
its CMakeLists registration, and its dedicated unit test
(`tests/calendar/differs/tst_ical_record_merger.cpp`) — no registration
site referenced it (already orphaned).
(c) `ChangeDetection::primeRevisionCache` — engine-unused since H3;
`~/dev/WildPalms` grep confirmed zero call sites (`primeRevisionCache`,
`cachedCollectionRevision`), so the pure-virtual and every implementation
were deleted outright: `AkonadiBackend`, `LocalBackend` (+ its now-dead
private `setCachedFingerprint` helper — the A2 direct-store-write hazard),
`RemoteCalendarBackend` (method body only — its `pendingCtag` staging
field stays live, used elsewhere in that file's CTag-commit path, so the
E1 "don't touch remotecalendarbackend.cpp diff/merge logic" guardrail was
read as not covering this single dead-interface-method deletion),
`AkonadiContactsBackend`, `RemoteContactsBackend` (inline stub),
`GenericSqliteBackend`, `FilteredCollectionBackend`. Three tests that
existed solely to exercise the deleted method were removed or trimmed:
`tst_generic_sqlite_backend.cpp` (whole test), `tst_filtered_collection_backend.cpp`
(whole test + the `FakeCDParent::primeRevisionCache` override + tracking
fields + trailing calls in two other tests), `tst_backend_thread_relocation.cpp`
(the primed-cache-round-trip half of one test; the fetch/collectionRevision
half stays). `cachedCollectionRevision` stays everywhere per plan.
`grep -rn "primeRevisionCache" src/ tests/` returns only a doc comment.

Full suite: 160/160 green (including `tst_engine_cancellation`, the O26
flake candidate — no reproduction this run).

### O32 — `updateRecord` try-all-calendars fallback can write into the wrong calendar (Resolved 2026-07-07, sync-excellence E4)

Promoted from roadmap D2. `RemoteCalendarBackend::updateRecord`
(`remotecalendarbackend.cpp:2401-2424`): when the `m_localEtags` ownership
lookup missed, a fallback loop PUT the item into every registered calendar
until one succeeded — wrong-calendar writes on multi-calendar backends and
multiplied failed-PUT latency. `deleteRecord` shared the pattern (tried
every registered calendar, first success wins).

**Fix:** both methods now route through a new
`RemoteCalendarBackend::findOwningCalendar(uid)` helper: pass 1 checks the
in-memory ETag map (an item this instance wrote or fetched); pass 2 checks
the persistent `CalDavContentCache` (a new `contains(url)` accessor — an
item fetched in a prior session). No match ⇒ FAIL with a distinct warning;
never guess by writing/deleting against an unowned calendar.

Also pinned (previously correct but untested — `FakeCalDavServer` always
succeeded regardless of headers, so nothing could exercise them):
`FakeCalDavServer` now enforces RFC 7232 `If-Match`/`If-None-Match`
preconditions on PUT (412 on ETag mismatch or on an existing resource with
`If-None-Match: *`). New tests in `tst_remotecalendarbackend_blob_view.cpp`:
ownership-miss fails without a guess-write (zero PUTs, RED against the old
fallback); a stale-ETag `updateRecord` surfaces the 412 rather than
silently overwriting a concurrent server-side edit; the next `fetchItems`
cycle after that 412 picks up the concurrent edit. The PROPPATCH-storm
regression pin (roadmap D2's last item) is `tst_sync_convergence.cpp`'s new
`colorChangeThenQuietCycle_secondCycleIssuesZeroProppatches` — passed
without further change (the property-phase suppression already worked for
the steady-state case; see O38 for a related but distinct gap this
uncovered).

### O33 — cancellation gaps: `processSync` erases in-flight cancels; DecSync active controllers run synchronously on the caller's thread (Resolved 2026-07-07, sync-excellence E3)

Promoted from audit §C4. (a) `processSync` clears `m_cancelled` at dispatch
(`syncengine.cpp:1542`): a cancel landing between queue advance and worker
start is erased; the cancelled queue runs one more full mapping. (b)
`driveQueue`'s active-controller loop (`:376-380`) calls
`runActiveSync()` inline on the caller's (GUI) thread — a §1 role violation
for whoever enables DecSync next. Related: `stopWorkerThread`'s mid-marshal
deadlock for non-relocated consumers (O22's parked note) gets a bounded-wait
diagnostic in E3 and its structural fix in E5.3. (Seeded 2026-07-07.)

**Resolved 2026-07-07 (E3).** (a) `SyncEngineWorker::processSync` no longer
clears `m_cancelled` at dispatch — it now only checks the flag and, if
already true, short-circuits to a cancelled/skipped `SyncResult` without
ever calling `dispatchSync`. The sole legitimate reset moved to a new
worker slot `resetCancellationFlag()`, invoked once per run (queued) from
`SyncEngine::driveQueue()` and `SyncEngine::processSingleMapping()` —
before that run's first mapping is ever requested, so the reset can never
race ahead of or erase an already-observed cancel. (b) The DecSync
active-controller loop was extracted from `driveQueue()` into a new
worker slot `runActiveControllers()`, dispatched via the same
command-channel pattern as `fastPathRequested`/`prepareFastPath`
(`activeControllersRequested` → `runActiveControllers` →
`activeControllersReady` → `SyncEngine::onActiveControllersReady` →
`continueDriveQueueSetup`). Pinned by `tst_decsync_active_controller_thread`
(a `Qt::DirectConnection` on `DecSyncActiveController::progressChanged`
records the thread `runActiveSync()` actually executed on — now the
worker thread, not the caller thread) and
`tst_engine_cancel_queue_race` (a cancel observed while mapping 1 is
fetch-blocked stops the queue before mapping 2 ever writes). Note:
`DecSyncActiveController`'s own internals (`DecSyncControllerStore`'s
SQLite connection) remain thread-affine to whichever thread constructed
them — now a real gap since `runActiveSync()` runs on the worker thread
instead of the constructing thread. Out of E3's scope (not
`syncengine.{h,cpp}`); flagged for whoever enables DecSync for real,
per the original §C4 note. (c) `stopWorkerThread`'s unbounded `wait()`
is now `waitForWorkerWithDiagnostic()` (new `src/engine/workerteardown.{h,cpp}`):
a bounded wait (30 s default), a loud `qCritical` naming the
"relocate backends" invariant on expiry, then an unbounded wait (never
`terminate()`). Pinned by `tst_worker_teardown`. This is the honest
interim per O22's parked note — the structural fix (the worker stops
parking in blocking marshals for I/O-length work) is E5.3's job.

**O22's parked note Resolved 2026-07-08 (E5.3).** The structural fix
landed: `SyncEngineWorker::applyBatch` no longer marshals a thread-blocking
`RecordWriter::apply()` call — it invokes `SyncBackendBase::applyRecords()`
(returns immediately, having only enqueued the write op) and awaits it in a
cancellable `QEventLoop`, same as a fetch gate. `stopWorkerThread()` /
`waitForWorkerWithDiagnostic()` (the E3 interim, still in place as a
belt-and-braces bound) is pinned against a genuinely in-flight write by the
new `writeTeardown_engineDestroyed_completesWithoutDeadlock`
(`tst_backend_thread_relocation.cpp`): destroying a `SyncEngine` while its
target backend is mid-apply behind a 60 s fake-server delay completes in
well under 5 s — the diagnostic path is no longer expected to fire for any
consumer whose backends are relocated (the D1 topology PlanStan/WildPalms
both use).

### O34 — `itemFetched` per-incidence signal storm (Resolved 2026-07-09 — sync-excellence E9)

Promoted from audit §C4/roadmap D2. `LocalBackend`'s fetch emits
`itemFetched(calendarId, inc)` once per incidence
(`localbackend.cpp:772-775`) — post-relocation, one cross-thread queued
event per item, thousands per fetch on big mirrors. Fix: batch
`itemsFetched(calendarId, items)` at fetch-pass/multiget-chunk granularity;
deprecate then remove the singular signal (PlanStan port at E10).
(Seeded 2026-07-07.)

**Resolution (E9, 2026-07-09) — two independent sub-items, both landed:**

**E9.1 (signal batching):** `SyncBackend` (`syncbackend.h`) gained
`itemsFetched(calendarId, items)` alongside `itemFetched`, which is now
doc-commented `@deprecated` (removal at E10 once PlanStan's
`ItemLoadingCoordinator` ports to the batch form — do not remove yet).
Emitted once per natural fetch-pass/chunk boundary, never debounced: (1)
`LocalBackend::fetchItems` — once after its single per-directory loop
completes (`localbackend.cpp`); (2)-(4) `RemoteCalendarBackend` — once each
in `serveCachedItems` (also covers the sync-collection full-snapshot
reconstruction path, which calls it), the partial-cache-hit branch of
`fetchItems`, and `processFetchedItems` (the full/mixed network+cache
multiget path) (`remotecalendarbackend.cpp`). RED test
`testLocalBackend_fetchItems_emitsItemsFetchedBatched`
(`tst_backend_signals.cpp`) pins a 50-item fetch: `itemFetched` still fires
50 times (unchanged), `itemsFetched` fires exactly once with the full
50-item list.

**E9.2 (incremental expected-fingerprint, removes the H3 one-cycle re-diff
lag for LocalBackend the sound way — audit A2):** `WriteOperation`
(`writeoperation.h`) gained `resultRevision()`/`setResultRevision()` —
empty by default ("no revision computed"; `RemoteCalendarBackend` never
sets it, per design: no server-side CTag guessing). `LocalBackend` now
tracks `m_lastFetchFingerprintSnapshot` (per-collection `filename ->
(mtimeMs, size)`), captured at the end of every `fetchItems()` pass
(`localbackend.cpp`/`.h`). `LocalBackend::applyRecords()` (new override —
previously used `SyncBackendBase`'s default synchronous adapter unchanged)
delegates to that same base dispatch for the actual create/update/delete,
then patches the snapshot in place using ONLY the files the settled
`WriteOperation::succeededUids()` actually wrote/deleted (stat exactly
those, via the existing `icsPathFor`; deleted files' entries removed
outright — no directory re-list), and sets the re-hashed result as
`resultRevision()`. The hashing itself was extracted into a shared
`LocalBackend::hashFingerprintEntries()` static helper, fed by a
`QMap<QString, QPair<qint64,qint64>>` (sorted by key regardless of
insertion order) so the incremental value is guaranteed bit-identical to
what `calendarFingerprint()`'s full rescan would produce for the same
on-disk state — `calendarFingerprint()` itself now builds the same QMap
shape and calls the shared helper. A collection with no prior fetch-time
snapshot (no preceding `fetchItems()` in this backend instance's lifetime)
leaves `resultRevision()` empty — never guessed.

Engine side: `SyncEngineWorker` gained
`m_lastAppliedTargetRevision`/`m_lastAppliedSourceRevision` (reset at the
top of every `unifiedContinueAfterConflicts` run), populated by
`applyBatch`'s new optional `QString *outRevision` out-parameter at its two
call sites (target, then source). `SyncEngine::onWorkerSyncCompleted`'s
existing H3 token-write block (`syncengine.cpp`, unchanged call site)
now takes a local copy of the pre-fetch `FreshSyncState` and overrides
`.targetRevision`/`.sourceRevision` with the worker's captured value ONLY
when non-empty, before the existing `setSyncToken` calls — the token
STORE, its persistence gating (`result.success`), and engine ownership of
sync-progress tokens are completely unchanged; only the VALUE fed in for a
side whose backend computed a fresher one. Cross-thread read safety: the
worker-thread writes happen strictly before `syncCompleted()` is emitted
each cycle and are never touched again until the NEXT
`unifiedContinueAfterConflicts` reset, so reading them from
`onWorkerSyncCompleted` (which only runs after that queued signal is
delivered) is safe by the same happens-before argument the rest of the
worker/engine split already relies on.

Two new RED-turned-green tests in `tst_sync_token_soundness.cpp`:
`writingCycleImmediatelyFollowedByQuietCycle_skips` (a local<->local
mapping's quiet cycle immediately after a writing cycle is now
skip-eligible — pre-E9.2 only the cycle AFTER that skipped, the accepted
lag) and `foreignEditDuringWritingCycle_defeatsIncrementalSkip` (the safety
pin: a foreign edit landing directly in the target's directory right after
the writing cycle still defeats the very next cycle's skip — the
incremental patch only ever touches files LocalBackend itself wrote, so it
cannot absorb a change to a file it never touched). Both confirmed RED
beforehand via `git stash` of the implementation files (a) failed for the
stated reason (`sawSkipLog` false); (b) passed vacuously (no mechanism yet
to over-absorb).

Full suite 167/167 green (`WAYLAND_DISPLAY=wayland-0 ctest --test-dir
build -j 8`; `tst_engine_cancellation` — O26 — not observed flaking).
`docs/campaign/archive/2026-07-05-sync-hardening-phases.md`'s H3
"accepted costs" paragraph annotated: local-side lag removed by E9.

### O35 — KDAV EtagCache not seeded from the persistent content cache: post-restart CTag-change re-downloads the whole collection (Resolved 2026-07-08 — sync-excellence E6)

Promoted from the H8.5 verification note (see O27's NOTE) / roadmap D2.
`m_etagCache` (`remotecalendarbackend.cpp:377`) is in-memory per session;
`DavItemsListJob` computes its changed-set against it, so the first
CTag-*changed* cycle after an app restart classifies every item as changed
and re-downloads all of them even though `CalDavContentCache` holds current
bytes keyed url+etag. Fix: lazily seed the EtagCache from the content
cache's `(url, etag)` rows before the first listing per collection.
(Seeded 2026-07-07.)

**Resolution (E6, 2026-07-08):** `CalDavContentCache` gained
`urlEtagPairs(pathFragment)` (`caldavcontentcache.{h,cpp}`) — like
`rowsByPathFragment()` but without loading `ical_content`, since seeding
only needs the etags. `RemoteCalendarBackend::continueFetchWithListing`
now seeds `m_etagCache` from these pairs the first time it runs for a
given `calendarId` in the backend instance's lifetime (tracked by new
`m_etagCacheSeededCalendars`), before constructing the `DavItemsListJob` —
both the url keys (`normalizeUrlKey`'d) and the seeding site (only reached
on a real CTag mismatch, never on the CTag-match short-circuit) line up
exactly with the existing `noteItemWritten`/`noteItemErased` write paths.
New test `tests/calendar/tst_etagcache_seed.cpp`: RED confirmed a 3-item
restart-with-1-changed-item scenario re-downloaded all 3
(`multigetReportCount()` 6 across both syncs instead of 4); GREEN after
the fix. Companion pins the CTag-unchanged short-circuit is unaffected.
Full suite 165/165 green.

### O36 — no RFC 6578 `sync-collection` support: every changed-CTag poll pays an O(collection) ETag listing (Resolved 2026-07-08 — sync-excellence E7)

Promoted from roadmap D2. The delta enumeration is a Depth:1 PROPFIND of
every item's getetag — O(collection size) response XML for a one-item
change — and delete detection depends on the full listing
(`remotecalendarbackend.cpp:2473`). RFC 6578's REPORT has the server
compute changed + deleted (tombstones) hrefs since a sync-token. KDAV
6.27.0 ships no sync-collection job (verified against installed headers),
so E7 implements the REPORT on E5.2's async request primitive, with
capability detection via supported-report-set, token persistence beside
the CTag, RFC §3.3 token-invalidation fallback, and the CTag+PROPFIND path
kept permanently as the fallback. The backend-owned sync-token is a
*cache-validity* token — it must not be conflated with the engine's H3
per-mapping sync-progress tokens. (Seeded 2026-07-07.)

**Resolution (2026-07-08):** implemented exactly as designed, confined to
`src/calendar/remotecalendarbackend.{h,cpp}` and
`tests/sync/fakecaldavserver.{h,cpp}`. Capability detection: a Depth:0
`supported-report-set` PROPFIND fired once per calendar right after
discovery (fanned in before `loadCalendarsFinished`), recorded as
`CalendarFacts::supportsSyncCollection` (default false — primed calendars,
which deliberately issue zero PROPFINDs, never probe and so stay on the
permanent fallback). Token store: `CTagStore` gained an additive,
self-migrating `sync_token` column (`PRAGMA table_info` probe + `ALTER
TABLE`, same pattern as `BaselineStore::ensureSchemaV6`); its `set()` and
new `setToken()` both moved from `INSERT OR REPLACE` to update-else-insert
so a plain CTag commit can no longer null out a previously-stored token.
Fetch path: `continueFetchWithSyncCollection` issues the Depth:0 REPORT via
`davSyncRequestAsync`, applies 404-tombstone deletions directly
(`noteItemErased`, no listing), multigets the changed hrefs through the
existing chunked-batch machinery, and — the one real design subtlety
beyond §10's text — reconstructs the FULL current-collection snapshot via
`serveCachedItems()` before calling `op->complete()`, because
`recordsFromLastFetch()`'s H5/O23 contract is a full snapshot every cycle
(same as the CTag+listing path); returning only the delta would have
looked to the engine's diff like everything else in the collection was
deleted. Token invalidation (409/410/507) clears the stored token and
falls back to `continueFetchWithListing` for that cycle;
`bootstrapSyncTokenIfNeeded` (an empty-token REPORT run once after any
full-listing cycle) re-acquires a token afterward. `FakeCalDavServer`
gained a per-collection change journal (`logChange`, fed by
`setSeedEvents`/`removeEvent`/PUT/DELETE) whose length IS the collection's
sync-token, `setSupportsSyncCollection`/`setInvalidateSyncTokens` knobs,
and `syncCollectionReportCount()`. Five new RED-turned-green tests in
`tests/calendar/tst_sync_collection_report.cpp` cover §10(a)-(e); RED
confirmed against the pre-E7 backend (steady-state/deletion/invalidation/
restart scenarios all failed for "today's full listing runs" reasons; the
unsupported-server regression pin passed trivially, as expected). Full
suite 166/166 green. Live-verified against a scratch Radicale 3.7.5
instance — see `docs/campaign/2026-07-07-sync-excellence-phases.md` §17's
E7 entry for the server-log evidence.

### O37 — TSAN thread-registry `CHECK failed` under rapid QThread churn in `tst_engine_cancellation` (OPEN, tool artifact, seeded 2026-07-07 during E2)

Surfaced while root-causing O26 under a TSAN build: running the full
`tst_engine_cancellation` binary (all `TstEngineCancellation` slots in one
process) intermittently aborts with `ThreadSanitizer: CHECK failed:
sanitizer_thread_registry.cpp:186 "((live_.try_emplace(user_id,
tid).second)) != (0)"` when `SyncEngine::startWorkerThread()`
(`syncengine.cpp:211`) spins up a fresh worker `QThread` for the next test
method shortly after the previous test's engine (and its worker thread) was
destroyed. **Not an app bug:** `SyncEngine::stopWorkerThread()` already does
the textbook-correct `m_workerThread.quit(); m_workerThread.wait();` join
before returning (verified by reading the source), and this exact failure
signature is reproducible on the *pre-O26-fix* code too (same line, same
call chain) — it predates and is independent of the O26 UAF and its fix.
Read as a TSAN-runtime limitation with pthread tid reuse outpacing TSAN's
own internal registry bookkeeping under fast thread churn (likely aggravated
by the very recent GCC 16.1.1 / compiler-rt pairing in this environment).
**Impact:** blocks running `tst_engine_cancellation`'s full slot list as a
single TSAN-instrumented process; does not reproduce for any slot run in
isolation (verified 200x per slot, 8 slots, zero hits — see O26's
Verification note) and does not reproduce at all in non-TSAN builds (3x
full 160-test parallel suite green). No action taken — not chased under E2
(out of its O26 scope) or any other numbered phase; revisit only if it
starts blocking a CI/gate that runs the whole TSAN binary in one process,
or if a compiler-rt/glibc upgrade is available to test against.

### O38 — `runPropertyPhase`'s baseline argument is always empty; the persisted collection-property baseline (T9) is never read back (OPEN, found 2026-07-07 during E4's PROPPATCH-suppression pin)

`SyncEngineWorker::dispatchSync` (`syncengine.cpp:2163`) calls
`runPropertyPhase(ops, srcBackend, tgtBackend, srcColId, tgtColId,
/*baseline=*/QVariantMap{}, request.mapping)` — the baseline argument is a
literal empty map, unconditionally, every cycle. T9
(`unifiedContinueAfterConflicts`, `syncengine.cpp:3153-3174`) persists a
real snapshot via `BaselineStore::setCollectionBaseline` after every
successful write, but nothing in `syncengine.cpp` ever calls
`BaselineStore::collectionBaseline()` to read it back before the next
cycle's property phase. The write half of the T9 contract exists; the read
half doesn't.

**Why E4's suppression test still passes:** `computeMapDiff`'s "both
changed" branch (`propertydiff.cpp`) has a same-value shortcut — when
`srcVal == tgtVal` it treats the pair as "already converged, no apply
needed" regardless of what `baseVal` was. With baseline always `{}`, a
key that both sides already agree on after a prior apply cycle hits
`srcChanged=true, tgtChanged=true` (both differ from the empty baseline)
but then `srcVal == tgtVal` short-circuits to no-op — the correct outcome,
by coincidence rather than by design.

**Where it actually bites:** an asymmetric one-sided edit. If only the
target's color changes between cycles (source untouched since its last
applied value), the empty baseline makes BOTH sides look "changed from
baseline" even though only one genuinely diverged — `computeMapDiff` then
routes it to the `conflicts` branch instead of `toApplyToSource`, i.e. a
property phase that never reads its own persisted baseline back turns
ordinary one-sided property edits into spurious every-cycle conflicts
exactly like O28's record-level phantom conflicts, just for collection
properties instead of records.

**Not fixed here:** out of E4's stated scope (the phase's Files list is
CalDAV write-path only) and the one test E4's design actually calls for
passes without it. Fix shape for whoever picks this up: before calling
`runPropertyPhase`, read `m_baselineStore->collectionBaseline(mappingId,
srcColId)` (mirroring T9's write side) and pass it through instead of
`QVariantMap{}`; add a RED test with one side's color changed and the
other held constant across two cycles, asserting no conflict and exactly
one PROPPATCH (today: a conflict every cycle, `toApplyToTarget` re-sent
from the "conflict resolved SourceWins" default — never fully quiescing).

### O39 — CalendarManager blocks the GUI thread in nested op-await loops and calls backend op methods cross-thread (SCHEDULED as phase E11, 2026-07-08; filed 2026-07-07 at CP-A)

Found while verifying E5.2's QEventLoop site list at CP-A.
`CalendarManager` (`src/calendar/calendarmanager.cpp`) spins a nested
`QEventLoop` awaiting `SyncOperation::finished` in three places
(`createIncidence` :583, update :630, delete :677). Two distinct
problems: (a) these loops run on the CALLER's thread — in PlanStan
(`CollectionSession`, `MainWindow`) that is the GUI thread, so every
editor save/delete pumps a nested event loop on the GUI thread until
the backend round-trip settles (re-entrancy for GUI events: a user can
trigger a second command mid-await); (b) the preceding
`backend->pushItems(...)` calls are DIRECT cross-thread calls into a
backend that post-D1/H7 lives on the I/O thread — the method body runs
on the caller's thread up to `onOwnerThread`'s op re-parenting, relying
on that helper's affinity push rather than the sanctioned marshal-first
discipline (`PlanStan::invokeOnBackend` app-side / queued invoke
lib-side). NOT B7 (the loops are not on the backend thread), so NOT E5
scope — E5.2 only annotates the three loops with `// O39:` comments.
Fix shape for whoever picks it up: make CalendarManager's mutation API
async (return the op / a completion callback; PlanStan consumers already
tolerate signal-driven completion), and route the backend calls through
a queued invoke onto the backend thread.

**Update 2026-07-08 (E5.2 amendment A5):** promoted from "decide at CP-C"
to a scheduled campaign phase — **E11** (§14b of the phase plan). E11's
scope expands beyond these three GUI loops to also absorb the Group C
calendar-collection CRUD backend loops (`createCalendar`/`updateCalendar`/
`deleteCalendar`) discovered to share the app-facing-API-conversion
character, and to delete the synchronous `davSyncRequest` helper (Group C
is its last caller). Rationale: keeping O39 parked would leave the
`davSyncRequest` helper's `QEventLoop` alive past campaign close, so the
"grep QEventLoop empty" end-state B7 promises would never be reached —
E11 makes that end-state achievable in-campaign. Resolved when E11 lands.

**RESOLVED (2026-07-09, E11 landed).** Both problems (a)/(b) closed:
`CalendarManager::createIncidence`/`updateIncidence`/`deleteIncidence`
are now `void` and genuinely async (fan-in over `SyncOperation::finished`,
no `QEventLoop`), and their `pushItems`/`deleteItems` dispatch goes through
a new `Kalburator::Sync::callOnOwnerThreadBlocking` marshal
(`src/sync/blockonasync.h`) instead of a raw cross-thread call. Group C
(`RemoteCalendarBackend::createCalendar`/`updateCalendar`/`deleteCalendar`)
converted to an `*Async` trio on `SyncBackend`
(mirrors `ChangeDetection::collectionRevisionsAsync`, E5.2/A6); the sync
overrides are deleted from `RemoteCalendarBackend` (falls back to
`SyncBackend`'s `return false` default — a poison pill, nothing should call
the sync form on this backend). All callers (this repo's
`CalendarDomainOperations::applyCollectionProperties`,
`CalendarManager`'s LogicalCalendar-level `createCalendar`/`updateCalendar`/
`deleteCalendar`, and the write-path test suite) migrated to the Async form,
using a new generalized `Kalburator::Sync::blockOnAsync<T>` rendezvous
helper (same heap-owned, mutex-guarded, O43-teardown-hardened shape as the
engine fast-path's inline `Rendezvous`) where a synchronous answer is
needed.

**Correction to the phase plan's §14b acceptance gate, discovered during
implementation:** §14b claimed Group C was `davSyncRequest`'s LAST caller
and scheduled the helper's deletion. That was stale by the time E11
landed — `fetchAllCtags()` (A6, deliberately kept synchronous, tripwire-
guarded), `getRawIcs()`/`setRawIcs()` (debug-only accessors), and
`createRecord()`/`deleteRecord()` (the E5.3 documented top-level-bridge
deviation) all call `davSyncRequest` directly too, and none are B7 hazards
(none run from inside an in-flight operation's own body). **`davSyncRequest`
and its `QEventLoop` survive E11** — only Group C's calls into it are gone.
The acceptance gate is amended the same way A5/A6 amended earlier ones: the
real end-state is `grep -rn "davSyncRequest\b" src/` returning exactly
those five legitimate non-reentrant call sites (down from Group C's three
plus those five), and `grep -rn "QEventLoop" src/calendar/ src/sync/`
returning only `davSyncRequest`'s own loop, `awaitOperation`'s (the
documented `loadRecords()` exception), and the new `blockOnAsync`/
`blockonasync.h` rendezvous loops (worker/GUI-thread blocking helpers, not
backend-thread nested loops — not a B7 instance). Audit B7 family is fully
closed in the calendar backend: every remaining `QEventLoop` is either a
non-reentrant top-level bridge (pre-existing, out of scope) or a caller-
thread rendezvous that never re-enters a backend mid-operation.

## Resolved

### O7 — Ambient-Context default bundle removed (resolved 2026-05-27)
Once PlanStan and WildPalms adopted the injecting `ShapeRegistries` ctors (per
`docs/2026-05-27-downstream-port-checklist.md`), the transitional scaffolding was
deleted: the process-global `defaultShapeRegistries()`, the three
`TransformationRegistry`/`DomainRegistry`/`DomainOperationsRegistry` `::instance()`
accessors, `src/shape/shaperegistries.cpp`, and the no-`ShapeRegistries` ctor
overloads on `SyncEngine` and `PluginManager`. A `ShapeRegistries` is now
constructed only at the composition root and injected by reference — the OSGi
`BundleContext` topology is complete and the Ambient-Context anti-pattern is gone.
Internal callers updated: `tst_calendar_sync_oneway`, `tst_syncruncoordinator`,
`tst_pluginmanager_resolve`/`_smoke`, `tst_provider_plugin_registration`,
`tst_akonadiprovider_plugin_registration`, `tst_multiprotocoldavprovider`, and
`examples/reference_consumer` (now models the injecting pattern with no global
cleanup). Build clean, suite 124/124. (Resolved 2026-05-27.)

### O12 — Downstream backend port (RESOLVED 2026-06-10)
Plan 4 dropped the `TranscodingPlan` parameter from `SyncBackend::pushItems`/`startSync`.
All downstream `SyncBackend` subclasses (PlanStan + WildPalms) ported after merge; the
canon-upgrade branch merged to `main`. Org-on wiring (`KALBURATOR_HAVE_ORG_IO=ON`) also
resolved under that build profile. (Seeded 2026-05-24; closed 2026-06-10.)

### O13 — baseline-load filters to blob domain (RESOLVED — was a misdiagnosis, 2026-05-26)

Original claim: `src/engine/syncengine.cpp` (~line 2121) loads only `blob`-domain
baselines, so baseline-driven *deletion* detection for calendar/contacts is "not active
for ANY backend." **Investigation showed this framing is wrong for the converged engine.**
The unified engine persists *every* baseline as `blob`/`raw` with `data = contentHash`
bytes (both `setBaselineV3` sites, regardless of the record's real domain), and
`perRecordDiff`'s `equalRecords` does hash-equality when both sides carry a `contentHash`
(every calendar/contacts backend populates one — SHA-256 of the bytes). So the load
filter drops *nothing* the unified engine wrote; it correctly skips only legacy
`calendar`/`ical` baselines (iCal text, not hashes) whose inclusion would corrupt hash
comparison. Proven by `tst_calendar_subsequent_sync_uses_blob_view`
`::subsequentSync_deletedSourceRecordPropagatesDeletion`: a source-deleted calendar
record known via a blob baseline is correctly deleted on the target (delete op, not a
spurious conflict). Fixed the stale `blobBatchDiff` comment at the filter site in the
same change. (Resolved 2026-05-26.)

### O15 — CalendarPluginWriter dual write-path (resolved 2026-05-27)
Converged the calendar domain onto the uniform `DefaultBlobWriter` record path and
deleted `CalendarPluginWriter` + the `SyncTransaction`/`*IncidenceItem` machinery
(`synctransaction`, `synctransactionitem`, `createincidenceitem`,
`updateincidenceitem`, `deleteincidenceitem`, `synctesthooks`). Investigation found
path (1) provided no live benefit it appeared to: the host `MemoryCalendar` was
never written, the `simulate()`-based collision/version checks were never invoked
(the writer only called `commitAll`), and its one live differentiator —
transactional rollback — was a MockBackend artifact (`shouldFail` is sticky
per-op-type, so rollback ops of a *different* type succeeded; under systemic
failure all writes fail and `rollbackCommitted` misreports success via an
unconditional `rollbackCompleted(true)`). The genuinely robust property —
baselines not saved on failure, so a retry re-attempts — is preserved and
domain-uniform; conflict detection lives in the canon diff/merge + `ConflictManager`
layer. The ~9 rollback-asserting slots in `tst_calendar_sync_error_recovery` were
rewritten to the retry-safe contract (3 redundant duplicates deleted). MockBackend's
`IBlobBackend` create/update/deleteRecord gained the failure-injection checks the
old `pushItems`/`startSync` paths had (the converged path exercises those methods).
Downstream caveat: PlanStan/WildPalms code constructing `CalendarPluginWriter` or
the `*IncidenceItem` classes directly will fail to compile until ported (consistent
with O7/O12). See `docs/2026-05-26-o15-calendar-write-path-convergence-design.md`.

### O10 — incidencediff/syncdiff relocated; transcoding deleted (resolved Plan 4 T1/T8, 2026-05-24)
Decision (human): the two diff engines moved to a new `src/diff/` (Task 1, commit involving `git mv`),
and the transcoding machinery was deleted with `src/transcoding/` removed entirely (Task 8, `88122b8`).
The design §10 "delete in full" is thus honored in spirit (dir gone) without losing the load-bearing
diff engines. Tree-wide grep confirms zero live transcoding references outside the (now-absent) dir.

### O11 — legacy RRuleReverseTranscoder no-op (superseded/deleted Plan 4 T2/T8, 2026-05-24)
The broken `RRuleReverseTranscoder` is gone with the rest of `rruletranscoder.cpp` (Task 8). RRULE
simplification + faithful restoration now live in `CanonToOrgICalStage`/`OrgICalToCanonStage`
(`src/calendar/orgicalcanonstages.cpp`), covered by `tst_orgical_canon_roundtrip`.

### O1 — `LossProfile` engine-layer migration (resolved Plan 1 Task 2, 2026-05-23)
`tst_engine_unified_routing.cpp` and `tst_carddav_engine_integration.cpp` were migrated
to `isLossless()` / `droppedProperties()` in Task 2. Full build confirmed no remaining
`LossLevel`/`.level`/`.dropped` references in production or test code.

### O2 — Carry-verbatim containers (resolved Plan 3, 2026-05-24)
All non-isomorphic structures (`relatedTo` tree / `parentUid` / `checklistItems` for todo;
`recurrence` verbatim string list for both ical and vtodo) landed in Plan 3:
`todocanonproperties.cpp` defines the catalogue fields; `vtodocanonstages.cpp` +
`icalcanonstages.cpp` capture RRULE/RDATE/EXDATE as raw RFC5545 lines (invariant 3). The
differ (CanonJsonDiffer) treats each as one opaque-field change per coarse-granularity rule.

### O3 — Live API validation deferred (partially resolved Plan 3, 2026-05-24)
The concrete JSON (de)serialization stages for all three domains are written and covered by
round-trip tests. The remaining unknowns (live Google/Graph payloads, IANA↔Windows timezone
mapping, Graph immutable-id edge cases) still require live integration testing, deferred to
when real provider connectors are wired. Not a blocker for convergence (Plan 4).

### O5 — `pipeline.cpp` loss folding (resolved Plan 1 Task 2, 2026-05-23)
`composedLoss()` in `src/shape/pipeline.cpp` uses only `compose()` — no direct field
access — so it compiled cleanly without changes. Confirmed by Task 2's full build.

---

## Discipline Log

Format: `YYYY-MM-DD — file:line — inv N — phrase`

2026-08-22 — src/engine/syncengine.cpp (harvestBaselinesAfterFirstSync, pre-O55 line ~2999) — inv 9 (fixed in passing) — the "shouldn't happen" comment claiming a target hash miss was impossible was factually wrong for any id-re-namespacing target (the exact O55 case); fail-loud comments must be checked against namespace assumptions, not just emptiness.
2026-08-22 — tests/calendar/tst_backend_signals.cpp — process/doc drift — fails on the pristine tree at this commit (Radicale live-state HTTP 412) but was absent from CLAUDE.md's known-failures list (which catalogued only two); stash-verified pre-existing during O55. Catalogued here and in CLAUDE.md's O55 note so the next suite tally isn't misread as a regression.

2026-05-24 — src/contacts/vcardcanonstages.cpp (VCard4ToCanonStage, birthday mapping) — inv 4 — `birthday.hasYear` is hardcoded `true`: KContacts exposes `birthdayHasTime()` but no `birthdayHasYear()`, so a `--MMDD` (year-less) vCard4 BDAY round-trips with a spurious year. Edge case, not exercised by current tests; revisit if year-less birthdays become a contract.
2026-05-24 — {contacts,todo,calendar}domaindefinition.cpp `richnessRank()` — deviation note — Plan 3 A4/B4/C4 specified `s==canonicalShape()?100:10`; implementations use 100 (canon) / 50 (primary legacy peer: vcard4, ical-vtodo, ical) / low (vcard3=10, todotxt=3, calendar-other=0). Documented deviation per the INVARIANTS deviation rule: only relative ordering matters (canon strictly highest) and the 3-tier scheme is consistent across all three domains and models the extra peers (vcard3, todotxt) more accurately than a flat 10. Harmless; recorded so the spec/code divergence is not mistaken for a bug.
2026-05-24 — build system (AUTOMOC + parallel QtTest link) — process note — clean parallel builds
intermittently emit `undefined reference to 'main'` on a QtTest target (the `QTEST_MAIN`/`.moc` racing
under `-j`); it is NOT a real error and resolves on a build re-run. Seen on `tst_canonjson_diff_merge`
and `tst_contacts_canon_roundtrip` during Plan 4. If it ever becomes persistent (not race), investigate
AUTOMOC dependency wiring for the affected target.
2026-05-24 — src/engine/syncengine.cpp (Task 4 materializedLoss warning) — inv 4 (minor) — the
re-sourced lossy-sync warning fires per present + non-Reversible affected property, but the static edge
LossProfile is value-independent: e.g. `canon→ical` charges `classification=Degraded` unconditionally,
so a record with `classification:"public"` (which iCal represents losslessly) triggers a spurious
"classification" warning. Harmless (no test asserts on it; warning, not failure) and matches the
design's "warn on composed path loss" framing, but a future refinement could make Degraded
value-dependent (only `personal`→private actually degrades). Documented, not fixed.
2026-05-24 — src/shape/lossprofile.{h,cpp}, src/calendar/icalcanonstages.cpp, src/engine/syncengine.cpp
— inv 4 (resolves the 2026-05-24 over-charge above) — FIXED. Added an optional
`LossProfile::losslessValues` (`PropertyId → QSet<QString>`): the static `affected` map stays
value-independent (routing relies on the conservative upper bound), but the warning path
(`materializedLoss`) now skips a present string value listed there. The `canon→ical` edge declares
`classification`'s `{public,private,confidential}` lossless, so only `personal` warns. `compose()`
carries the field forward, intersecting safe sets on a shared property (lossless only if every hop
agrees). Additive — every existing `affected` site untouched. Covered by tst_loss_profile
`composeIntersectsLosslessValues`/`composeCarriesUnsharedLosslessValues`; full suite 112/112.
2026-05-24 — src/calendar/calendarmanager.cpp (Task 6b) — decision — `CalendarManager`'s direct-write
path (createIncidence/updateIncidence/transcodeForBackend) was converged OFF `TranscodingRegistry`
(human decision): it now pushes incidences without per-target transcoding (conversion is the
backend/shape graph's job) and no longer emits `dataLossWarning` on those sites (the signal declaration
is kept). The methods have no in-repo callers; downstream apps using this facade with an org backend
should route through the sync engine to get RRULE simplification + the loss warning.
2026-07-04 — tests/engine/tst_engine_cancellation.cpp — process note — segfaults intermittently
(observed 3+ times across the sync-convergence campaign's A1/B1/B2/B3 checkpoints) when run inside a
full `ctest -j8` parallel batch, but passes every time when run standalone (`ctest -R
tst_engine_cancellation`). Looks like a pre-existing resource-contention/timing race in the test's own
cancellation-timing assertions, not a regression from this campaign's changes — none of A1/A2/B1/B2/B3
touch cancellation. Not investigated further (out of this campaign's scope); flagging so a future
session doesn't mistake it for a regression from this branch's work.
2026-05-24 — Plan 3 Parts A, B & C — inv 4/5 post-review fixups (commits 89edbbb, 7f68e36, 5b00a47) — a single subagent ran A5→Task13 unsupervised (per-task review checkpoints skipped) and introduced the SAME false-loss-contract bug in all three domains: a loss classified Reversible/Degraded whose verbatim stash was never emitted in code. Specifically: contacts sipAddresses/calendarUrls/externalIds (Reversible, A5); VTODO Degraded-status + checklistItems/sortOrder Reversible (B5); iCal classification="personal" Degraded (C5). Recurrence round-trip tests also asserted substring, not byte-identity. Fixed: originals now stashed as `CANON-*`/`X-CANON-*` custom props that round-trip into providerExtras; recurrence tests assert byte-identical RRULE/EXDATE; each fix has a falsifiable round-trip test. Caught by retroactive spec-compliance review of B and C, then — prompted by the human asking "is the mess cleaned up" — the identical bug was found and fixed in contacts (A) too. Lesson: when one agent silently expands scope past its task, review EVERY task it touched, not just the ones you remember dispatching.
2026-07-04 — src/engine/syncengine.cpp `SyncEngine::updateSyncMetadata` (Phase B4 work) —
inv 9 (off-topic notice) — this method (and its `makeCalendarRec` helper) is dead code: declared,
defined, never called from anywhere in src/ or tests/ (calendar sync has routed through the unified
`dispatchSync`/`unifiedContinueAfterConflicts` path since Phase Ib.5 Task 7 removed the if-calendar
guard). It stores baselines under domain="calendar"/encoding="ical" with real iCal text as the
payload — a different shape than the unified path's domain="blob"/encoding="raw" hash-only rows — so
if it were ever accidentally wired back in, its rows would be silently invisible to
`baselineHashesForMappingV4()`'s legacy-fallback filter (which only treats domain="blob" rows as
hash rows, correctly ignoring "calendar"/"ical" ones as non-hash data). Not fixed this session
(true dead code, zero runtime risk); flagging so a future cleanup pass deletes it deliberately
rather than rediscovering the discrepancy under time pressure.
2026-07-04 — src/types/synctypes.h `SyncStats` / `SyncResult::sourceStats,targetStats` (Phase B4
work) — inv 9 (off-topic notice) — grep-confirmed: nothing under src/engine/ ever populates
`sourceStats`/`targetStats` for the unified dispatch path (`unifiedContinueAfterConflicts` never
touches `.created`/`.updated`/`.deleted`/`.conflicts`). The fields default-construct to all-zero and
stay that way regardless of what actually happened during a sync. `tests/engine/tst_sync_convergence.cpp`
could not use these fields to prove "zero writes on the second sync" for this reason and instead
witnesses convergence via FakeCalDavServer's PUT/DELETE request counters plus on-disk mtime/byte
comparison of LocalBackend's written files. Not fixed this session (out of B4's scope — this is a
pre-existing observability gap, not a correctness bug); a future phase should either wire these
fields up from `EngineMerge`/`WriterBatch` counts or delete them if genuinely unused by any consumer.

2026-07-04 — src/calendar/eventcanonfields.cpp:168-190 / src/todo/vtodocanonfields.cpp:115-125,
src/calendar/remotecalendarbackend.cpp (loadRecords + the three incidence-parse sites) — Phase B5,
**MAJOR fix, not just a notice** — a real, previously-latent non-convergence bug found by the B5
acceptance-matrix tests (`tests/engine/tst_sync_convergence.cpp`), intermittently flaky (~1-in-3 to
1-in-4 runs) in a way that made it easy to miss in a single manual run. Root cause:
`RemoteCalendarBackend::loadRecords()` re-derived each record's raw iCal bytes via
`icalFromIncidence(incidence)` — parse-then-reserialize through a throwaway KCalendarCore
MemoryCalendar — on every call, including calls that serve content the backend already has
cached verbatim. Two independent KCalendarCore behaviors make that re-serialization
non-deterministic byte-for-byte across separate calls to the SAME logical content: (1)
`Incidence::created()`/`lastModified()` default to the wall-clock time AT PARSE, not "unset", when
the source lacks explicit CREATED/LAST-MODIFIED properties — so a freshly re-parsed object's
`eventFieldsToCanon`/`todoFieldsToCanon` call re-derives a DIFFERENT literal CREATED/LAST-MODIFIED
canon value every time, since those functions trusted the accessors directly; (2) KCalendarCore's
`ICalFormat` writer regenerates `DTSTAMP` unconditionally to "now" on every `toString()`/
`toICalString()` call regardless of the source's own DTSTAMP (RFC 5545 semantics: DTSTAMP is "when
this representation was produced", so this is correct KCalendarCore behavior, not a library bug).
Net effect: `BackendRecord.contentHash` for ANY event/todo was unstable across independent
`loadRecords()` calls for byte-identical server content — silently defeating B4's per-side baseline
convergence whenever the two independent `fetchItems()` calls per sync (the "runs at least twice"
structural residual, §Phase B5 item 3) landed in different wall-clock seconds, which is common
enough to be genuinely disruptive on a real 120s-cycle collection, not just a test artifact.
**Fix (two parts, both needed for full closure):** (a) `eventFieldsToCanon`/`todoFieldsToCanon` now
read CREATED/LAST-MODIFIED via a new `Kalburator::Calendar::extractICalPropertyLiteral()` (icaltimestamp.{h,cpp})
— literal per-property presence in the ORIGINAL bytes, no accessor-trusting — so a canon encoder
never invents a field the source didn't have. This alone is insufficient when the "original bytes"
passed in have themselves already been through one icalFromIncidence round-trip (see (b)). (b) the
actual closure: `RemoteCalendarBackend` now remembers the LAST VERBATIM raw bytes served for each
uid (`m_lastRawIcsByUid`, populated at all three sites that parse raw ics text: `serveCachedItems`,
the all-from-cache branch, `processFetchedItems`) and `loadRecords()` prefers that verbatim blob
over `icalFromIncidence()`, falling back only if the map has no entry. Verified: the acceptance-
matrix tests (`localEditPropagatesExactlyOncePut`, `remoteEditFetchesExactlyOneChangedItem`,
`remoteDeleteRemovesExactlyOneLocally`, `fastPathSkipsGenuinelyUnchangedMapping`) went from ~30-40%
flaky to 0/40 failures across a stress-test loop after fix (b) landed; fix (a) alone did not resolve
the flakiness (confirmed by testing it in isolation first) because DTSTAMP corruption survived it.
Also fixes a companion (b1) one-cycle staleness in `SyncEngine::onWorkerSyncCompleted`'s
`persistRevision` helper: it used to persist the PRE-dispatch `fresh.targetRevision`/`sourceRevision`
snapshot captured by `prepareSyncFastPath` before the mapping ran, which for a target a mapping just
WROTE TO (e.g. LocalBackend on the mirror-populating first sync) is already stale the instant the
callback runs — it now re-queries each side's LIVE `ChangeDetection::collectionRevision()` after the
mapping completes, falling back to the pre-dispatch snapshot only if the live query comes back empty.
This — not the DTSTAMP bug — was the direct cause of the roadmap's "of 7 mappings, 0 are unchanged"
real-world symptom for the LOCAL/target side (see the fast-path test's doc comment for the parallel,
separate, NOT-a-bug one-cycle warm-up the REMOTE/source side's CTagStore still needs).

### O40 — `stopWorkerThread()`'s cancel() never wakes an in-flight cancellable gate — only `future.cancel()`'s path did (found + Resolved 2026-07-08, sync-excellence E5.3)

Found while writing E5.3's teardown RED test
(`writeTeardown_engineDestroyed_completesWithoutDeadlock`). `SyncEngine::
stopWorkerThread()` called `m_worker->cancel()` — a plain synchronous method
that only sets the `m_cancelled` flag (mutex-guarded) — believing this was
enough to unwind any in-flight gate the same way `future.cancel()` does.
It is not: only `SyncEngineWorker::observeCancel()` (the queued slot
`future.cancel()`'s `QFutureWatcher::canceled` → `SyncEngine::
onCancelObserved` → queued-to-worker path invokes) actually `emit`s
`cancellationObserved()`, the signal every cancellable gate (fetch gates,
and now E5.3's write gate) connects to wake its nested `QEventLoop`. Calling
`cancel()` alone during teardown left a genuinely in-flight write-await gate
with nothing to wake it except the op's own `finished` signal — which, for
a write stuck behind a slow/frozen network call, only fires when that
backend-level watchdog (`m_transferTimeoutMs`, independent of engine/worker
synchronization) eventually times out. Observed live in the RED test before
the fix: `~SyncEngine()` blocked for the full 30s E3 bounded-wait window,
logged the "worker thread did not stop" diagnostic, and only unblocked when
the PUT watchdog separately fired at ~30s — at which point a dangling
`WriteOperation` access (a separate bug, see the E5.3 landing note) crashed
the process. This directly contradicted the sync-excellence plan's claim
that "E5.3 structurally dissolves E3's `stopWorkerThread` interim" — without
this fix, it did not, for teardown specifically (the `future.cancel()` path
was never affected, since it already routed through `observeCancel()`).

**Fix:** `stopWorkerThread()` now additionally queues `observeCancel()` onto
the worker thread (`QMetaObject::invokeMethod(m_worker, &SyncEngineWorker::
observeCancel, Qt::QueuedConnection)`) alongside the existing synchronous
`cancel()` call. Queued, not direct: `observeCancel()`'s
`cancellationObserved` → `loop.quit()` wiring is a `Qt::DirectConnection`,
safe only when both ends run on the same thread — a nested
`QEventLoop::exec()` (e.g. the write-await gate) still pumps its own
thread's full event queue, so the queued `observeCancel()` reaches and runs
inside it, waking the loop promptly instead of after a network timeout.
Verified: `writeTeardown_engineDestroyed_completesWithoutDeadlock` now
completes in ~3s (a 60s server-side PUT delay, engine destroyed ~200ms into
the write) instead of hitting the 30s+ bounded-wait/watchdog path.
`waitForWorkerWithDiagnostic`'s bounded wait (E3) stays in place as a
belt-and-braces backstop — it should no longer be the mechanism that
actually ends an I/O-length wait for any consumer using the E5.3 write path
or the pre-existing fetch gates.

### O41 — calendar canon write path stamps `CREATED`/`LAST-MODIFIED` with wall-clock "now" on records whose source bytes never had them, defeating canonical-equality checks for that shape (RESOLVED 2026-07-09, phase E12, branch `feature/e12-canon-timestamp-write-fix`)

Found while writing E8's crash-replay RED test
(`tests/engine/tst_phantom_conflict_adoption.cpp`): an EARLY version of the
fixture wrote synthetic local `.ics` files with `DTSTAMP` but no explicit
`CREATED`/`LAST-MODIFIED` properties (plausible for content dropped in by
some external tool — RFC 5545 makes both optional). That version DID
reproduce phantom conflicts on the repair cycle, but for a different reason
than O28 described (PRODID/property-order): `perrecorddiff.cpp`'s
`differ.diff()` on the no-baseline pair showed `created`/`lastModified` as
the ONLY changed properties, not any content field.

Root cause: `eventcanonfields.cpp`'s ical→canon encoder (the read/diff
side) deliberately only trusts a LITERAL `CREATED:`/`LAST-MODIFIED:` line
in the source bytes (`extractICalPropertyLiteral`), never
`KCalendarCore::Incidence::created()/lastModified()`'s construction-time
"now" default — a fix already landed for the read side (see that file's
"Phase B5 finding" comment, guarding against exactly this class of bug).
But nothing guards the WRITE side symmetrically: when a record whose canon
JSON has no `"created"`/`"lastModified"` key gets pushed as a brand-new
create, the canon→ical materialization builds a `KCalendarCore::Incidence`
with those fields unset, and `KCalendarCore::ICalFormat::toICalString`
appears to serialize its own defaulted "now" into the outbound bytes
regardless. The server then stores real `CREATED`/`LAST-MODIFIED` lines
that were never in the canon record. On the NEXT fetch, the target's
ical→canon read (correctly, per the read-side fix) finds these literal
lines and includes them in canon — while the source side (whose original
bytes still lack them) does not. The two canon records permanently
disagree on `created`/`lastModified`, `differ.equal()` returns false
forever, and any same-UID/no-baseline pair in this shape can never
canonically-adopt (E8's fix) OR reach steady-state no-op convergence
(secondSyncIsNoOp's class of test) for as long as the source keeps
omitting those fields — which, since nothing ever backfills them into the
source, is forever.

**Not campaign-blocking:** E8's own RED test uses a fixture with explicit
matching `CREATED`/`LAST-MODIFIED` (the realistic shape — most real
calendar clients emit both), which reproduces O28's literal
PRODID/property-order-only symptom faithfully and passes clean; this
finding is a distinct, narrower gap (records that structurally never had
these fields) than what O28 described or E8 owns. Out of E8's file scope
(`src/engine/`, `tests/engine/`) — the bug lives in
`src/calendar/eventcanonfields.cpp`'s write-side canon materialization
(and possibly other `*canonfields.cpp` files sharing the pattern —
`journalcanonfields.cpp`'s read side already guards with `.isValid()`
checks but wasn't audited here for the same write-side asymmetry).

**Fix direction (untriaged, no phase assigned yet):** either (a) make the
canon→ical write stage leave `CREATED`/`LAST-MODIFIED` unset when canon
has no such key (requires confirming `ICalFormat::toICalString` can be
told not to stamp defaults — may need constructing the `Incidence` with
`setCreated()`/`setLastModified()` explicitly skipped, or post-processing
the serialized bytes to strip a KCalendarCore-injected default), or (b)
backfill: on first successful create, re-read the server's own
authoritative `CREATED`/`LAST-MODIFIED` back into the LOCAL side's stored
baseline hash context too (asymmetric-but-consistent, cheaper, but leaves
the local .ics file itself still lacking the fields — only the diff
machinery would agree). Needs live verification against a real CalDAV
server (Radicale may itself normalize `CREATED` differently than the fake)
before committing to either direction.

**CP-B live confirmation (2026-07-09):** the CP-B smoke's kill-mid-push run
(PlanStan dev app against scratch Radicale :5233, 40 minimal VEVENTs with no
`CREATED`/`DTSTAMP`/`LAST-MODIFIED` in the source `.ics`, SIGKILL with 12
pushed) reproduced this LIVE: the repair cycle flagged all 12
already-pushed records as conflicts (`!12`, mapping `success: false`), and
every subsequent cycle re-presented them — the vault never converges and
the conflict store accumulates duplicate unresolved rows (48 rows for the
12 UIDs after 4 cycles; that store-side dedup gap is filed separately in
PlanStan `docs/bugs/sync-conflict-store-duplicate-rows.md`). The isolation
re-run with the identical protocol but `CREATED`/`DTSTAMP`/`LAST-MODIFIED`
present in the source bytes recovered perfectly (10 silent adoptions with
E8's qInfo line, +30 pushed, zero conflicts, converged) — E8's machinery is
sound; this canon write-side stamp is the sole live phantom producer.
Radicale does NOT normalize `CREATED` itself (the stored server copy's
timestamps are exactly the push-time stamps KCalendarCore injected), which
answers this entry's open live-verification question. Elevated: scheduled
as phase E12 (see the campaign plan §14c), gating CP-C.

**Resolution (E12, 2026-07-09):** fix direction (a) — leave-unset —
confirmed empirically NOT directly achievable via KCalendarCore's public
API: `KCalendarCore::Incidence::created()`/`lastModified()` hold a valid
construction-time "now" from the moment an `Event`/`Todo`/`Journal` object
is constructed, with no setter to mark them absent, and
`ICalFormat::toICalString()` unconditionally serializes both. Landed as
"(a) via post-processing": a new `Kalburator::Calendar::stripICalPropertyLine`
helper (`src/calendar/icaltimestamp.{h,cpp}`) removes a named property's
line from serialized iCal bytes; `eventcanonfields.cpp`,
`vtodocanonfields.cpp`, and `journalcanonfields.cpp`'s canon→ical write
sides now track whether canon had a `created`/`lastModified` key and strip
the corresponding line post-serialization when it didn't. The sibling
audit found `journalcanonfields.cpp` had BOTH the write-side bug AND had
never received the Phase B5 READ-side fix (`journalFieldsToCanon` was
still trusting `journal->created()/lastModified()`'s construction-time
default directly, discarding the `originalBytes` parameter it was passed)
— fixed to use `extractICalPropertyLiteral` like `eventcanonfields.cpp`/
`vtodocanonfields.cpp` already did. RED tests: (a) engine-level
`tst_phantom_conflict_adoption::crashMidPush_timestampLessSource_
nextCycleAdoptsSilently_noPhantomConflicts` (timestamp-less twin of the
E8 crash-replay fixture) and (b)
`tst_calendar_canon_roundtrip::timestampLessSourceRoundTripsWithoutManufacturedStamps`
(round-trip pin), both green; full suite 168/168, zero regressions. Live
re-run of the CP-B kill-mid-push protocol against a real scratch Radicale
(new probe `tests/engine/live_e12_smoke.cpp`, opt-in
`KALBURATOR_BUILD_LIVE_PROBES`): 12 timestamp-less local creates, killed
Radicale mid-push (1 survivor landed with no CREATED/LAST-MODIFIED line in
the stored bytes — confirms Radicale does not normalize and the fix holds
against a real server, not just the fake), revived on the same port,
repair cycle recovered with ZERO phantom conflicts, all 12 present both
sides, a third cycle stayed a hard no-op. FINDINGS O41 → Resolved.

### O42 — first sync of each app process never uses `sync-collection`: the fetch races the supported-report-set probe, and the capability is in-memory only (RESOLVED 2026-07-09 at CP-C; fix candidate (b) — lazy first-fetch probe)

Found during the CP-B live smoke's restart-plus-one-remote-edit proof.
`RemoteCalendarBackend` only takes the RFC 6578 `sync-collection` path
when `m_calendars[calId].supportsSyncCollection` is true AND a stored
sync-token exists (`remotecalendarbackend.cpp` fetch decision, E7 design
step 3). The token persists (CTagStore `sync_tokens`), but the capability
flag is populated ONLY by `probeSyncCollectionSupport()` during
`loadCalendars()` discovery. PlanStan's auto-sync-on-load drives the first
fetch through the primed/registered-URL path WITHOUT awaiting discovery:
in the CP-B logs the first sync run's fetch (listing path, "Delta sync -
11 total, 1 changed") completed BEFORE the "discovered calendar" lines
appeared. Result: the first sync of every app process pays the Depth:1
ETag listing (correctness unaffected — E6's seeded cache still limited the
run to exactly 1 item download); every later cycle in the same process
uses sync-collection correctly (confirmed live, both for a tick-driven
pull and for the post-SIGSTOP recovery cycle).

**Fix candidates (pick at CP-C / fold into E10 or E11):** (a) persist the
capability next to the token in CTagStore (survives restart, zero races);
(b) have the fetch path lazily probe supported-report-set on first fetch
when the flag is unset (one extra small PROPFIND, self-healing); (c)
PlanStan-side: await `loadCalendarsFinished` before the first auto-sync
(fixes the race but leaves the capability amnesia to (a)/(b)).

**Resolution (2026-07-09, CP-C ruling — candidate (b)):** `CalendarFacts`
gained `syncCollectionProbed` (per-instance, set by
`probeSyncCollectionSupport()` on completion, success or failure);
`fetchItems`' path decision lazily runs the probe when the calendar is
unprobed AND a persisted sync-token exists, then decides REPORT vs
listing with an accurate capability. Candidate (a) was REJECTED for a
downgrade hazard: only 409/410/507 REPORT failures fall back to listing,
so a persisted-true capability against a server that stopped advertising
sync-collection would fail every fetch cycle with no self-heal; the
per-instance probe is self-healing by construction. Candidate (c)
rejected: leaves the amnesia, adds app-side coupling. Cost: one Depth:0
PROPFIND on the first fetch of a process, only for token-holding
calendars. RED test
`tst_sync_collection_report::firstFetchBeforeDiscovery_storedToken_usesReport`
(0 sync-collection REPORTs pre-fix, 1 REPORT + 1 multiget post-fix).

### O43 — `prepareFastPath`'s A6 revision-query marshal is teardown-unsafe: a pending backend-thread lambda outlives the worker's stack frame and invokes a dangling `QEventLoop*` (RESOLVED 2026-07-09, v0.90.1; found same day at E10 — deterministic SEGV in PlanStan's suite, blocked E10's acceptance gate)

Found at E10 step 1 (PlanStan pin bump v0.84 → v0.90). PlanStan's
`tst_collectioncontroller::testAutoSyncOnLoadDeferredUntilSyncInfraReady`
segfaults 5/5 (was 28/0 green at v0.84 per the H7 verification record), so
the E10 gate ("PlanStan suite green") cannot pass until this is fixed.

**Mechanism** (`src/engine/syncengine.cpp`,
`SyncEngineWorker::prepareFastPath`, the E5.2/amendment-A6 async revision
query): the worker posts a QueuedConnection lambda onto the backend's
thread capturing `&revs` and `&loop` — both on the worker's STACK — then
blocks in `loop.exec()`. If the engine is torn down while that lambda is
still pending on the backend thread (PlanStan's `~CollectionController`
stops the engine worker FIRST — `QThread::quit()` sets `quitNow`, which
exits nested event loops too, so `loop.exec()` returns and
`prepareFastPath`'s frame unwinds), the pending lambda now holds dangling
stack pointers. `stopBackendIoThread()`'s subsequent blocking invoke
flushes the backend I/O thread's queue, the stale lambda runs, its
continuation calls `QMetaObject::invokeMethod(&loop, …)` on the dead
`QEventLoop` → SIGSEGV (SEGV_MAPERR inside `invokeMethodImpl`). Verified
against the crash stacks: the crashing backend-I/O thread is inside
`ChangeDetection::collectionRevisionsAsync`'s continuation while the main
thread waits in `stopBackendIoThread()`'s `QLatch` and the test frame is
`~SyncTestHarness → ~CollectionController`.

**Why the lib suite missed it:** the trigger needs (a) backends on a
thread distinct from the worker, (b) teardown racing an in-flight
fast-path preparation — i.e. destroy-immediately-after-triggering-sync,
which PlanStan's auto-sync-on-load test does and the lib's neutral
engine tests don't. The same window exists live: app close mid-sync
(exactly the E10/CP-C close-mid-sync proof) can land in it.

**Fix direction (decide before re-attempting E10; candidates):**
(a) heap-own the rendezvous — put `revs`/the loop-quit handoff behind a
`std::shared_ptr` state block co-owned by the pending lambda, with an
`aborted`/generation flag checked before touching the loop, so an
unwound `prepareFastPath` leaves the pending lambda harmless; (b) fence
teardown — `stopWorkerThread()` (or the engine) must drain/invalidate
continuations it posted to backend threads before the worker frame
unwinds (matches E5.3's cancellation-honesty work, O40's sibling);
(c) both — (a) is the minimal safe shape, (b) alone still leaves the
loop-exit ordering fragile. Any fix lands lib-side as v0.90.1 (the patch
tag E10 already anticipated for the `itemFetched` deletion) with a RED
test that quits the worker mid-`prepareFastPath` with a pending
backend-thread revision query.

**Resolution (2026-07-09, tagged v0.90.1):** fix candidate (a) — the
rendezvous (`loop` + `revs` pointers) now lives in a heap-owned,
mutex-guarded state block co-owned by the lambda posted to the backend
thread; `prepareFastPath` nulls both pointers under the mutex before its
frame (and the `QEventLoop`) dies, so a late continuation drops the
result instead of invoking a dangling pointer. The second hop stays safe
unguarded: it is posted under the same mutex (loop provably alive at
post time) and `~QObject` removes any still-undelivered metacall. RED
test `tests/engine/tst_fastpath_teardown_race.cpp` (SEGV 3/3 pre-fix,
green 5/5 post-fix); full suite 168/168. Shipped together with the
planned E10 deletion of the deprecated per-item `itemFetched` signal
(all seven lib backends + WildPalms' PalmCalendarBackend now emit only
the batched `itemsFetched`; `tst_backend_signals` ported to batch
assertions).

### O44 — PlanStan presentation-side sync churn: GUI-thread busy-storm freezes on large fetch/sync (quadratic per-item signal fanout) + `recordChanged`'s GUI tail runs on the engine worker thread (RESOLVED 2026-07-09, phase E13)

Found diagnosing the hard GUI freeze that blocked E10's interactive
in-editor-save gate item live (500-item push, 730-item vault: window
stops repainting, clicks dead, KWin "Not Responding"). NOT an engine
fault — E5/H7's off-thread work is verified live and holds. Two distinct
sub-findings, both in PlanStan/libkalcal presentation code, both owned
by new phase **E13** (campaign plan §14d; full diagnosis + decided
design in PlanStan
`docs/plans/2026-07-09-e13-sync-gui-freeze-presentation.md`):

**(a) GUI-thread busy storm, O(n²).** The E10 batch signal delivers one
queued event per fetch pass (correct), but PlanStan's
`ItemLoadingCoordinator::onItemsFetched` then mutates
`GlobalIncidenceModel` per item (per-row begin/endInsertRows or
per-item `dataChanged`, plus a MemoryCalendar delete+add observer pair
per updated item), and widgets like `TagDockWidget` run a full model
rescan on EVERY `rowsInserted`/`dataChanged`/`rowsRemoved` with no
debounce. n items × O(n) handlers × several widgets = minutes of
unprocessed GUI events. Worse, every sync cycle re-delivers the primary
side's full fetch and the coordinator updates every item even when
unchanged — the GUI-side inefficiency twin of what E6/E7 fixed on the
network side, re-running the storm on each 120 s auto-tick.

**(b) Cross-thread model mutation.** `SyncEngine` invokes
`ISyncHost::recordChanged` as a direct virtual call on the engine
worker thread (`syncengine.cpp:3175-3188`, source side,
`notifyHost=true`). PlanStan's override marshals its backend re-read
correctly but then calls `onItemFetched`/`onItemDeleted` directly —
mutating a GUI-affine `QAbstractItemModel` with live views attached
from the worker thread. Data race / UB on every source-writing cycle.
Latent since Phase P T4; exposed to real concurrency when H7 moved the
engine worker off the GUI thread.

**Fix:** phase E13 (E13.1 batch model insert, E13.2 unchanged-skip,
E13.3 widget debounce, E13.4 queued marshal of the recordChanged GUI
tail) — all PlanStan/libkalcal-side, zero libkalburator changes. Gates
CP-C: the CP-C soak's "GUI responsive" assertion at 650+ items fails
without it, and the E10 leftover (in-editor Save during sync) is
un-runnable while the window freezes.

**Resolved 2026-07-09.** All four RED tests green (batch-insert signal
count, unchanged-skip, widget-debounce run-count, thread-pin via a
genuine foreign QThread). Full PlanStan suite: no new failures vs the
documented dev/offscreen baseline (18/123 fail, identical composition —
13 evicted-subsystem Not-Runs, `tst_collectionassembler`'s pre-existing
release-build `Q_ASSERT` gap, and the four pre-existing integration/
sync-workflow harness failures). Widget-debounce audit of the plan's
named check list (datepickerdock, temporalribbon(dock), collectionexplorer,
CategoryManager::refresh triggers) found no other offenders in PlanStan;
separately found the same full-rescan-per-signal pattern in libkalcal's
calendar-views scenes (AgendaScene/MonthScene/YearScene/ScheduleView/
RangeAgendaView) — NOT fixed here (not named in the E13 plan, larger
risk surface); flag for a future finding if it proves load-bearing.
Live spot-check on the H8 scratch-Radicale rig (700-item vault, local
`bulk` fetch of 500 items each cycle): two full auto-sync cycles
completed with no crash, no assertion, no per-item log flood, and E6
skip-unchanged correctly engaging on the second cycle for the settled
`soak` mapping — consistent with the fix. **Caveat:** this session's
tool environment has no GUI-automation (no `xdotool`/`grim`/`wmctrl`),
so the interactive "window accepts clicks during an active push"
half of the acceptance gate could not be driven headlessly; it is
deferred to a session with a human at the GUI, per the phase plan's own
allowance. The live run also surfaced an unrelated CalDAV-transport
issue — filed as **O45** — that blocked the `bulk` mapping's remote
push both times; it does not implicate E13's diff (presentation-layer
only, zero contact with the CalDAV write path) and does not gate E13.

### O45 — CalDAV create jobs against the H8 scratch-Radicale rig time out 100%, every retry, with zero successes (RESOLVED 2026-07-09 at CP-C; genuine client-side bug, NOT a rig artifact — bounded in-flight write-dispatch window)

Live spot-check for E13's acceptance gate (H8 scratch-Radicale rig,
`AcidTestH8.kalb`, `bulk` mapping — 500 local items, 355 already
adopted on the remote via E8's canonical-equality baseline adoption,
145 genuinely new). Every one of the 145 `RemoteCalendarBackend::
applyRecords` create jobs hit the 30000 ms KDAV transfer timeout —
`SyncRunCoordinator` reported `target: "... !0 E145"` (145/145 failed,
0 succeeded) — on TWO independent app launches in the same session,
the second with no other process touching the window. Radicale itself
was not the bottleneck: a manual `curl -X PUT` against the same
collection while the app's sync was still retrying returned `201` in
30 ms. The first launch also had a confound (a proactive incidence
editor the user had open, unconfirmed whether it contributed), but the
second launch reproduced the identical 100%-failure shape with no
editor open and no other window interaction, which rules out an
editor-modal explanation.

**Hypothesis (untested):** the KDAV job dispatch path fires many create
jobs concurrently against a single-threaded dev Radicale process; if
Radicale serializes them and the per-PUT rate is the ~1–2 items/s this
rig has shown before (`h8-live-verification-setup` memory), jobs queued
behind ~30+ others would individually exceed the 30 s per-job timeout
even though the server is healthy and each individual request is fast
in isolation — i.e. a timeout-budget-vs-concurrency mismatch, not a
transport failure. Alternative: something specific to concurrent
*create* dispatch (as opposed to the GET/REPORT traffic that worked
fine for `soak`) hangs client-side before the request is even sent.
Not root-caused; needs a session with request-level tracing (Radicale's
own debug log already captures method/path/timing) to distinguish "all
145 requests queued server-side and individually starved" from "some
requests never left the client."

**Scope note:** out of E13 (zero contact with CalDAV code) and not
observed at CP-B's live smoke (which pushed only 40 items — likely
under whatever concurrency threshold triggers this). Decide at CP-C
whether this is a rig artifact (single-threaded dev Radicale can't
sustain the app's concurrency at 100+ item pushes) or a genuine client-
side create-dispatch bug worth its own phase.

**Second reproduction (2026-07-09, E10's interactive-editor-Save gate,
new rig `AcidTestE10Gate.kalb`, scratch Radicale on :5234, 400-item
calendar):** identical shape — `SyncRunCoordinator` reported `target:
"+45 ~0 -0 =0 !0 E55"` (55/100 new creates timed out client-side) — but
this time the SERVER-side evidence was checked directly: after the
client-reported failure, `find .../collections/.../e10gate -iname
'*.ics' | wc -l` showed **all 300 of the FIRST batch's items present**
on disk, byte-content matching what the client sent (confirmed
CREATED/LAST-MODIFIED absence round-tripped correctly per E12). The
next sync cycle's delta fetch found the "failed" creates already on the
server and silently adopted them via E8's canonical-equality baseline
path (`adopted baseline for "e10gate-NNN" ... canonically equal to
source — not a conflict`), not as phantom conflicts. This strengthens
the timeout-budget-vs-concurrency hypothesis over a transport/data-loss
bug: **the writes are landing, the client just gives up waiting before
seeing the response** under this rig's request volume. Does not
implicate E10's actual target (the interactive Save round-tripped
correctly regardless — see E10's §17 entry). Still not root-caused;
still decide at CP-C.

**Root cause + resolution (2026-07-09, CP-C ruling):** the
timeout-budget-vs-concurrency hypothesis was CONFIRMED structurally and
by deterministic reproduction — this is a client-side design flaw, not a
rig artifact. `RemoteCalendarBackend::applyRecords` dispatched every
create/delete job up-front, and each job's `startJobWithWatchdog` timer
(30 s, H5.5) starts at DISPATCH time, so the watchdog measured queue
position, not server health: any batch larger than
(server drain rate × timeout) self-reported spurious timeouts even
though every request eventually landed (both live reproductions' exact
shape — items present on disk, client reported E145/E55). A
progress-based batch watchdog was considered and REJECTED by the RED
test itself: a KDAV create job issues a trailing ETag-fetch request
after its PUT, which queues behind the remaining PUTs on a serialized
server, so NO job completes until nearly the whole batch drains
(observed: 40/40 RED failures, matching live 145/145 — not just the
tail) and a no-progress window would fire spuriously too. Fix: a
**bounded in-flight dispatch window** (`kMaxInFlightWriteJobs = 4`) —
the create/update/delete loops enqueue starters; at most 4 jobs run at
once; each job's watchdog starts at true dispatch and competes with at
most 3 siblings, so it fires only on a genuinely unresponsive server.
Cancel/teardown semantics preserved (starters are dropped once the op
is finished; the E5.3 QPointer guards and O43-era teardown tests
unchanged). RED tests in new `tst_bulk_write_dispatch.cpp`:
`slowButHealthyServer_bulkCreatesAllSucceed` (40 creates, serialized
150 ms/request fake via new `FakeCalDavServer::setSerializeResponses`,
1.5 s timeout — 40/40 failed pre-fix, 40/40 succeed post-fix) and
`genuinelyStalledServer_failsWithinTimeout` (stall honesty preserved).
Residual note: `pushItems`/`deleteItems` (CalendarManager's single-item
incidence CRUD paths) and the chunked multiget fetch keep the
all-at-once shape — their batch sizes are structurally small (single
incidences; few large chunks), so they cannot hit the drain-rate
threshold; noted here rather than changed.

---

## Post-sync-excellence inbound (consumer RFCs/handoffs against v0.94)

These arrived after CP-C (v0.91) during WildPalms' v0.77→v0.94 re-pin. Neither
is a release blocker. Cross-repo status index: `docs/2026-07-19-consumer-coordination-status.md`.

### O46 — read-only write-skip is invisible in `SyncResult` (RESOLVED 2026-07-19)

**Resolution:** both write gates now record a stable-prefix warning when a
target reports `discoveredWritable()==false` — `target-readonly:<col>` on the
first-sync mirror success path (`dispatchFirstSync`) and `%1-readonly:<col>`
(source/target by side) on the steady-state `applyBatch` gate. No behavior
change — the skip stays a no-op success. Pinned by
`tests/calendar/tst_calendar_readonly_skip.cpp` (warning present + success +
zero target stats for a read-only target; no false-positive on a writable one).
_Original finding below._


The engine correctly refuses to write to a target whose backend reports
`discoveredWritable() == false`, on both write paths, as a deliberate no-op
success — but the refusal is only a worker-thread `qWarning`. The `SyncResult`
comes back `success = true`, zero target stats, empty `warnings`, so a consumer
cannot tell "writes withheld because the target is read-only" from "nothing to
do". Gates: `syncengine.cpp:2016` (`dispatchFirstSync` first-sync mirror) and
`syncengine.cpp:3140` (steady-state apply). Ask (no behavior change, skip stays
a no-op success): record it on `SyncResult` — a stable `target-readonly:%1`
(and symmetric `source-readonly:` for TwoWay back-prop) entry in the existing
`warnings` list (`types/synctypes.h:158`), or a `skipReason` field. Both consumer
UIs already consume `SyncResult::warnings` (WP Patchbay edge badges; PlanStan
graph channel-edge badges, spec §5.7). Same honesty principle as E1 (stats read
but never populated) and the 2026-06-12 Akonadi Fix-B ruling ("failures/no-ops
must be discriminable"). Source: `WildPalms/docs/2026-07-18-libkalburator-readonly-skip-reporting-rfc.md`.

### O47 — `MockBlobBackend` never computes `BackendRecord::contentHash` (RESOLVED 2026-07-19)

**Resolution:** `MockBlobBackend::createRecord`/`updateRecord` now hash the
record's data (SHA-256 hex, matching `LocalBlobBackend::sha256Hex`) when the
incoming `contentHash` is empty; a caller-supplied hash is preserved. Removes
the spurious `BothModified` conflict on two-pass TwoWay/AskUser mock syncs.
Pinned by `tst_mockblobbackend::computesContentHashWhenIncomingEmpty`.
_Original finding below._


`src/blob/mockblobbackend.cpp` stores the caller's `BackendRecord` verbatim in
`createRecord`/`updateRecord` and never populates `contentHash` — unlike the
production blob backends (`localblobbackend.cpp:144` `r.contentHash =
sha256Hex(r.data)`, `GenericSqliteBackend` likewise). Before v0.93 a
`useQuickPath→SourceWins` downgrade in `unifiedHandleConflicts`
(`syncengine.cpp:2826-2831` comment, "always true for non-calendar domains")
absorbed the empty-hash ambiguity; the v0.93 fanout-collapse deleted it
(alongside `CalendarBaselineStore`), so `perrecorddiff.cpp:130-143`'s deliberate
"fail loud on missing hash" rule is now reached for the first time. Result: any
two-pass `TwoWay` + default-`AskUser` sync through `MockBlobBackend` on both
sides manufactures a spurious `BothModified` conflict on pass 2 (and every
subsequent pass — the mock never starts hashing). Production backends are
unaffected (they always hash on write); this is confined to the **lib-owned**
test double. Suggested fix (a, WP's lean): `MockBlobBackend` computes
`QCryptographicHash::Sha256` when the incoming record's `contentHash` is empty,
matching `LocalBlobBackend`. WP is not blocked — it added a local
`BlobSyncBackendWrapper` test-helper workaround (v0.94 port Phase 1 Task 1.6).
Source: `WildPalms/docs/2026-07-19-libkalburator-mockblobbackend-contenthash-gap-handoff.md`.

### §16 residual — "parallel mapping execution" was PARKED at sync-excellence CP-C (RESOLVED 2026-08-20, parallel-sync campaign)

**Resolution:** implemented. `SyncEngine::setMaxConcurrentMappings(int)`
(Task 7, default 1 — bit-identical to every existing consumer) plus an
endpoint-collision scheduler (Task 8): `pumpQueue()` dispatches every
mapping whose source and target `endpointKey()`s are both unclaimed, up to
the effective cap, reusing the same `endpointKey()` L1/L2's convergence
fixpoint already uses so two mappings never diff/apply against the same
(backend, calendar) concurrently — the per-collection FIFO does NOT
provide that guarantee, it serialises operations, not diff/apply cycles.
`resolveEffectiveCap()` pins Monitored runs to 1 (the conflict-pause
interaction stays one-at-a-time) and `capForMapping()` adds a per-resource
ceiling from `SyncBackendBase::maxConcurrentOperations()`, forcing 1 for
any backend still living on the engine's own thread (the GUI-thread
BlockingQueuedConnection backstop — PlanStan's Task 11 relocates every
sync backend onto its own I/O thread to actually benefit from N>1).
`phaseChanged(SyncPhase::Complete)` and `progressUpdated`'s semantics were
redefined for concurrency (Task 9): Complete now describes the RUN, not
one mapping — WildPalms' `shouldPauseTickle()` depends on this.

**Reversal rationale** (why this campaign reopened a parked decision): CP-C
judged the busy cycle throughput-bound on one server. A 2026-04-10
measurement instead found it latency-bound — 11 mappings took ~76s
sequentially against one CalDAV account, round-trip latency dominating —
and latency parallelises fine against a single host.

**Task 10's N=4 full-suite sweep** (`KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS`,
test-only env override, never consulted unless set, three consecutive
identical runs) found zero engine bugs and three test-side single-in-flight
assumptions, all fixed: `tst_engine_skip_invalidation`'s
`testUntouchedMappingStaysSkipped` indexed `results.at(2)` assuming
submission-order completion (SyncResult carries no mapping id;
`MappingQueue::recordResult()` appends in completion order) — fixed to
rely on the already-present order-independent backend-operation-log
assertions instead. `tst_syncengine_unification`'s
`multiMappingSequentialCompletesInOrder` and `tst_engine_cancellation`'s
`cancelMultiMappingMidQueue` both assert genuine concurrency-1 contracts
(dispatch order; "mappings past the cancel point never ran") that
`setMaxConcurrentMappings(1)` cannot pin against the sweep — the env
override's `static` in `resolveEffectiveCap()` is memoized for the whole
process once any test reads it — so both guard the sweep-invalidated
assertions behind `!qEnvironmentVariableIsSet("KALBURATOR_TEST_MAX_CONCURRENT_MAPPINGS")`
rather than weaken what they prove at the production default.

Five real defects surfaced along the way, none previously covered by any
test, three pre-existing in shipped code: `TransformationRegistry::compile()`
data race on a `mutable QSet` (Task 16); cross-mapping sync-token
corruption from a worker-persistent `m_lastAppliedTargetRevision` (fixed
structurally by Task 1's move onto `SyncResult`); live-probe files broken
by the `stopWorkerThread`→`stopWorkerPool` rename; `tgtFetchOp` leaked on
three early-return paths introduced by Task 3's fetch overlap (fixed with
an RAII `FetchOpGuard`); `fetchFinished` dropped on Task 4's chunked abort
path (would have left PlanStan's `ItemLoadingCoordinator` stuck "loading"
forever).

Suite at v0.96: 179 total, 177 passing at both N=1 and N=4 (three
consecutive sweeps) — the same two pre-existing failures throughout
(`tst_remotecalendarbackend`: broken local Radicale test-server auth;
`tst_calendar_canon_roundtrip`: pre-existing on `main`, not campaign-caused).

### O48 — `ConflictInfo::baselineIcalData` can never be populated: the engine carries baseline HASHES, not bytes (OPEN, found 2026-08-21, conflict-resolution-repair Task 1)

Task 1 of the conflict-resolution repair campaign was specified to wire
`ConflictInfo::baselineIcalData` from `EngineDiffOp::baselineRecord.data`
(the handoff assumed those bytes existed and were "presumably also
canonical"). They do not exist. Two independent reasons, both deliberate
prior decisions:

- `perRecordDiff` builds `baselineRecord` via `baselineShell()`
  (`src/engine/perrecorddiff.cpp:41-48`), which sets only `.id` and
  `.contentHash` — its own comment says "`.data` … baseline entries never
  carried even before B4". `Engine::BaselineEntry` (`baselineentry.h`) is
  `{id, sourceHash, targetHash}`; there is nowhere for bytes to come from.
- `dispatchSync` loads baselines through
  `BaselineStore::baselineHashesForMappingV4()` (`syncengine.cpp:3057-3068`),
  never the v3 canonical-bytes API. And `setBaselineHashesV4()`
  (`baselinestore.cpp:316-354`) INSERT-OR-REPLACEs `canonical_bytes` with an
  empty blob, so even the stored bytes are erased on the first steady-state
  save after an upgrade.

The demotion is wired anyway (it costs one line and lights up for free if
baseline bytes ever return), but it yields empty today, always. **Consumer
impact:** PlanStan's `ConflictResolutionDialog` picks 3-way vs 2-way diff on
`!baselineIcalData.isEmpty()` (`src/sync/conflictresolutiondialog.cpp:166-172`)
and will therefore always take the 2-way path. Restoring 3-way diff is a
baseline-storage change (retain canonical bytes alongside the per-side
hashes), well outside Task 1's scope, and needs a decision about the storage
cost before anyone attempts it. Pinned as the current truth by
`tst_syncengine_unification::unmonitoredConflictRecordsIcalData`'s
`baselineIcalData.isEmpty()` assertion — flip that assertion to a
parse+summary check when this is fixed.

### O49 — the unmonitored-defer `ConflictInfo` never set `sourceModified`/`targetModified` (RESOLVED 2026-08-21, conflict-resolution-repair Task 1)

The second instance of the same drift as
`docs/bugs/sync-conflict-store-duplicate-rows.md`, in the same two branches:
`unifiedHandleConflicts`' monitored-yield branch set `info.sourceModified`/
`targetModified` from the two records and the unmonitored-defer branch did
not, so every deferred conflict persisted null `local_modified`/
`remote_modified` in `SyncConflictStore` and reached the UI with no
"last modified" to show. Fixed structurally rather than by adding the two
missing lines: both branches now call one `SyncEngineWorker::buildConflictInfo(op)`
that constructs the whole struct, so a field added for one branch cannot be
missing from the other. (INVARIANTS §9 — noted while fixing Bug A in the
same two constructions.)

### O50 — two MORE hand-built `ConflictInfo`s in the resolution path, both missing every payload field (RESOLVED 2026-08-21, conflict-resolution-repair Task 3)

Task 1 collapsed `unifiedHandleConflicts()`'s two `ConflictInfo`
constructions onto one `buildConflictInfo(op)` (O49). There are **two more**,
now in `SyncEngineWorker::applyConflictResolution`
(`src/engine/syncengine.cpp`, the `CustomMerge`-without-a-merger branch and
the `default:` Skip/AskUser deferral): each builds the struct by hand and sets
only `mappingId`/`sourceId`/`targetId`/`calendarId`/`sourceBackendId`/
`targetBackendId`/`type`. So a conflict deferred **out of a resolution** —
the user hit Skip, or the domain has no merger — reaches
`SyncResult::unresolvedConflicts` and `SyncConflictStore` with no
`detectedAt`, no `source/targetModified`, no payload data at all, and a
hardcoded `ConflictType::BothModified` even for a ModifyDelete. A conflict
deferred out of the **detection** walk carries all of it. Same struct, same
consumer, two different fidelities depending on which branch produced it.

Not fixed in Task 2: the task's contract was "behaviour identical for the
Monitored path apart from Bugs C and D", and populating these would change
what PlanStan's dock/store sees for skipped conflicts. The fix is mechanical
once someone owns it — call `buildConflictInfo(op)` in both branches, exactly
as O49 did — but it should land with a consumer-visible note, because
`ConflictInfo::hasFullData()` starts returning true for skipped conflicts
that previously returned false. (INVARIANTS §9 / §1.)

**Fixed in Task 3, as a prerequisite rather than a follow-up.** Bug B's
staleness guard compares a stored resolution's `sourceModified`/`targetModified`
against the live records, and one of the two branches this finding names — the
`default:` Skip/AskUser deferral — is on the path a Skipped conflict takes back
to the store. A conflict deferred with null timestamps could never be matched
against later. Both branches now call `buildConflictInfo(op)`, the same builder
the detection walk uses.

**Consumer-visible (Task 4 must document):** a conflict deferred out of a
RESOLUTION (user hit Skip, or the domain has no merger) now reaches
`SyncResult::unresolvedConflicts` and `SyncConflictStore` with `detectedAt`,
`source/targetModified`, both native-encoding payloads, and a correct
`ConflictType` — so `ConflictInfo::hasFullData()` returns **true** for those
where it previously returned false, and PlanStan's dock/dialog will render a
real diff for them instead of an empty one. Correct, but visibly different.


### O51 — the conflict-resolution staleness guard is blind on a backend that reports no `lastModified` (OPEN, found 2026-08-21, conflict-resolution-repair Task 3)

Bug B's staleness guard (locked decision 3) is the only thing standing between
a stored "Keep Local" and an edit made *after* the user answered the dialog. It
compares `PendingConflictResolution::source/targetModified` (captured at
detection) against the live `EngineDiffOp` records, via
`sameModifiedInstant()` in `src/engine/syncengine.cpp`. Two deliberate
weakenings, both load-bearing for something else:

- **Second granularity.** `SyncConflictStore` round-trips the timestamps
  through `QDateTime::toString(Qt::ISODate)`, which drops milliseconds. A
  millisecond-exact comparison would call every *rehydrated* resolution stale
  and silently kill the restart-durability path — the case PlanStan is actually
  sitting in. So an edit landing inside the same wall-clock second as the
  detection reads as unchanged.
- **`invalid == invalid` counts as a match.** A ModifyDelete conflict's deleted
  side legitimately has no `lastModified`, and treating that as "changed" would
  make such a conflict permanently unresolvable. The consequence is that a
  backend which reports **no** `lastModified` at all gets **no** staleness
  protection whatsoever: every stored resolution for it applies unconditionally.

Every calendar backend in the tree today does report one (`MockBackend` takes
it off the incidence; the iCal round-trip supplies `LAST-MODIFIED`), so this is
latent, not live. The durable fix is to compare content rather than clocks —
the `ConflictInfo` payloads are already persisted, and `BackendRecord` carries
a `contentHash` that the store has no column for. That is a schema change and a
decision about what "the same version" means, so it is not Task 3's to make.
Pinned as-is by `tst_syncengine_unification::staleResolutionIsDiscarded`, which
moves the target's `lastModified` by 600s precisely so it does not depend on
either weakening.

### O52 — a rehydrated `CustomMerge` resolution loses the user's merged payload (OPEN, found 2026-08-21, conflict-resolution-repair Task 3)

`SyncConflictStore` has columns for the two sides and the baseline, but none
for a *merge result*. So when Task 3's rehydration reads a resolved-but-
unapplied row back after a restart, a `CustomMerge` row comes back with
`PendingConflictResolution::mergedNative` empty, and
`applyConflictResolution`'s Bug C branch correctly falls back to the automatic
`m_unifiedMerger`. The user's hand merge is silently replaced by the
auto-merge.

In-process this does not happen: `ConflictManager::mergedDataFor(conflictId)`
carries the payload from the resolver to `SyncEngine::onConflictResolved`, and
`rehydratePendingResolutions()` deliberately refuses to overwrite an in-memory
entry that has one. The gap is exactly "app closed between choosing the merge
and the next sync". Fixing it means a `merged_ical` column and a schema
migration — same storage decision as O48's baseline bytes, and worth doing at
the same time. Until then a Custom Merge should be treated as needing the app
to stay open for one more sync cycle.

### O53 — the batch conflict dialog is modal and runs inside `onWorkerSyncCompleted` while other mappings may still be in flight (RESOLVED 2026-08-21, O53 follow-up — live-confirmed by PlanStan the same day it was filed)

**Confirmed live, not just theoretically.** The day this was logged (as
"out of scope for Task 3... do not try to fix it, but log it"), a PlanStan
session hit it for real on the very first live conflict after Bug B's fix
landed: two identical "Resolve Sync Conflict" dialogs for one conflict,
answered independently ("Keep Local" on one, "Keep CalDAV" on the other) —
exactly the re-entrancy this entry predicted. Fixed the same day, on
`feature/conflict-resolution-repair` (same branch, on top of Task 3).

**Fix:** `onWorkerSyncCompleted()` no longer calls
`ConflictManager::handleConflicts()` (and its modal dialog) directly. It only
appends to `m_pendingUnmonitoredConflicts` and, if no presentation is already
scheduled (`m_conflictPresentationScheduled`), defers the actual call via
`QMetaObject::invokeMethod(this, &SyncEngine::presentPendingConflicts,
Qt::QueuedConnection)` — landing on a fresh, non-nested event-loop turn. A
conflict from another mapping arriving while a presentation is already
running (including one delivered during that presentation's OWN modal call)
just accumulates and rides the next round, which `presentPendingConflicts()`
reschedules itself if needed. This is the "hand the batch to the host
asynchronously" fix this entry originally called for, not the smaller
re-entrancy-guard alternative.

**The flat, mapping-unaware `m_pendingUnmonitoredConflicts` list this entry
flagged is now fine as-is** — it doesn't need to be keyed by mapping, because
nothing can present a batch concurrently with another presentation anymore
(the whole point of the fix), so which mapping's completion happened to
trigger the scheduling no longer matters. `presentPendingConflicts()` logs
every mapping id actually IN the batch instead of the single (sometimes
wrong) `mappingId` the old inline call had in scope.

**Real cost paid for the fix, found via a genuine SIGSEGV during
development, not theorized:** locked decision 2's "same-run" instant
reapply (a resolution answered mid-run gets applied before that run's
future resolves) no longer works in general. A first attempt reused the
existing rehydration path with a fresh `runSync()` call from inside
`presentPendingConflicts()` — looked safe, crashed instead: the deferred
call can run at any point after being scheduled, with no guarantee the
engine or its backends still exist by then (a test's teardown between the
run finishing and the deferred presentation firing destroyed the backends
first; `dispatchSync()` reached a worker thread mid-teardown and crashed in
`kickFetch()` on a dangling `SyncBackendBase*`). A host closing a
collection right after answering a conflict dialog would hit the identical
hazard in production. Backed out. The resolution is still durably recorded
(unaffected — same `m_pendingResolutions`/`SyncConflictStore` rehydration
Task 3 built) and applies on whichever sync the host runs next, exactly
like a resolution answered after a restart already does — just not
necessarily within the same run anymore. Restoring "instant" safely needs
its own design pass: a follow-up mechanism tied to the engine's actual
lifetime (cancellable/awaitable), not a bare `runSync()` call from a
queued slot. New finding, not filed separately — this paragraph is that
finding's home; revisit if the added latency (worst case: PlanStan's next
auto-sync tick, ~30s) turns out to matter in practice.

`m_resolvingMonitoredConflict` (the flag distinguishing an inline Monitored
resolution from a deferred one) is untouched by this fix — it belongs to the
Monitored yield/resume path, which this entry was never about and which this
fix does not change.

Regression coverage:
`tests/engine/tst_syncengine_unification.cpp::concurrentMappingCompletionDoesNotDoublePresentConflict`
— two mappings dispatched together (one genuine conflict, one artificially
slowed via `MockBackend::setOperationDelay` so it is still in flight when the
first mapping's resolver "shows its dialog"), a `PumpingConflictResolver` that
spins a real nested `QEventLoop` to faithfully simulate `QDialog::exec()`'s
defining property. Shown RED against the pre-fix code during development
(`resolver->calls == 2`, confirmed by hand), GREEN after. Five existing tests
needed updating for the (expected, documented above) loss of same-run instant
reapply: `unmonitoredResolutionReachesTheBackend`,
`appliedResolutionIsNotReapplied`, `autoResolveWorkflowResolutionIsApplied`,
`storeLessHostStillAppliesResolution` (all in the same file), and
`tst_calendar_conflict.cpp::unmonitored_sameUidDivergent_emitsConflictDetected`
— each now explicitly waits for the deferred presentation (via a resolver
call count or a `ConflictManager::conflictResolved` signal spy constructed
*before* `runSync()`, not after — a spy constructed after the first
`QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), …)` can already have missed
the signal) and then plays a second `runSync()` itself, exactly as a real
host's next sync tick would, before asserting on applied state. Full suite:
177/179 (unchanged pre-existing baseline — `tst_remotecalendarbackend`
Radicale auth, `tst_calendar_canon_roundtrip`).

### O54 — RESOLVED 2026-08-22 — `RemoteCalendarBackend` assumed every item's URL is `<calendar>/<uid>.ics`; false for any item another CalDAV client created (found 2026-08-21, unrelated to the conflict-resolution campaign)

**RESOLVED 2026-08-22** on branch `fix/o54-uid-url-assumption`, exactly per
the recommended fix shape in the original writeup
(`docs/2026-08-21-remotecalendarbackend-uid-url-assumption-critical-bug.md`
— its status header is updated too). The fix, in `RemoteCalendarBackend`:

- **`QHash<QString, QString> m_uidToUrl`** (uid → normalized real URL, the
  `normalizeUrlKey()` form) — populated wherever an item's real URL is
  known simultaneously with its parsed UID: `processFetchedItems()`, the
  all-from-cache branch of `fetchItems()`, `serveCachedItems()` (covers the
  sync-collection delta path's full-snapshot rebuild), and every write
  success handler via `noteItemWritten(..., uid)` / `setRawIcs{,Async}`
  success. Evicted via `noteItemErased(urlKey, uid)` — by uid where the
  delete path knows it, by URL-scan where only the href is known
  (tombstones, deleted-item listings).
- **`resolveItemUrl(davUrl, uid)`** — `m_uidToUrl` first,
  `generateItemUrl()`'s guess ONLY on a miss (a genuine client-side create,
  where the guess is correct by definition). Every update/delete/read call
  site converted: `removeItem`, `startSync` updates/deletes +
  `launchStartSyncModify`, `deleteItems`, `applyRecords` deletes,
  `setRawIcsAsync` (the live failure site), `getRawIcs`, `setRawIcs`,
  `findOwningCalendar` (new pass 0 through the map — the old
  URL-guessing passes could never find an adopted item at all, which made
  `updateRecord`/`deleteRecord` fail before any network I/O). The five
  remaining `generateItemUrl()` call sites are create-only paths and are
  deliberately untouched.
- In-memory only, same lifecycle as `m_localEtags`: a fresh instance
  repopulates on its first fetch, which every engine run performs before it
  writes. No SyncEngine changes (INVARIANTS §1 respected).

**Regression test** (RED first — reproduced the live failure byte-for-byte:
PUT to `<uid>.ics` → SabreDAV 400 "uid already exists" — then GREEN):
`tst_remotecalendarbackend_convergence::
o54_edit_of_adopted_item_writes_to_its_discovered_url`, driven by
`FakeCalDavServer` extensions for exactly this bug class:
`setSeedEventAt(collectionHref, fileName, ics)` (server-assigned filename ≠
uid, kept for the item's life), SabreDAV-shaped 400 on a UID-colliding PUT
to the wrong URL, and `requestPaths(method)` so tests assert WHERE a write
landed, not just that one happened. The fake's If-Match comparison is now
quote-insensitive (davSyncRequest strips ETag quotes before echoing one
back — a latent fixture mismatch no prior test chained PUT→PUT through).

**CardDAV audited, not affected**: `RemoteContactsBackend` keeps per-record
`RecordHandle{href, etag}` from `loadRecords()` and PUTs/DELETEs the stored
href; `<uid>.vcf` is derived only on create. `LocalBackend`'s `<uid>.ics`
filename convention remains correct as documented (PlanStan creates every
local file itself); the "imported .ics dropped in a watched directory" gap
the original writeup flagged is still un-audited — folded into the watch
note below.

**Suite: 179 total, 177 passing** — the identical pre-existing baseline
(`tst_remotecalendarbackend` Radicale auth — its three failing slots
verified identical on unmodified code this session;
`tst_calendar_canon_roundtrip`).

**LIVE-VERIFIED 2026-08-22, real Nextcloud account, exactly the originally-failing
scenario.** Same collection, same item (`8fecdc8c-cf00-4b74-b2dc-f6d84790b74d`,
real server filename `1755247320.R237.ics`, discovered via `inbox-1`). A
Keep-Local resolution that previously failed permanently with the SabreDAV
400 now applies cleanly on the next sync — log shows
`SyncEngineWorker::unifiedHandleConflicts - applied stored resolution 0`
followed by `SyncRunCoordinator: mapping completed: success: true ...
target: "+0 ~1 -0 =0 !0 E0"` (a real write, not the all-zero "success"
this same scenario silently produced before Bug B was fixed) — no HTTP
error, no repeat on the following tick. Both O54 and the conflict-resolution
resolution-injection path (O53's era of fixes) are now confirmed working
together, end to end, against a real server.

**Original finding (2026-08-21), kept for context:**

`generateItemUrl(davUrl, uid)` (`remotecalendarbackend.cpp:824`) guesses
every item's URL as `<calendar>/<uid>.ics`. True only for items PlanStan
itself created. An item created by any other CalDAV client keeps its
original server-assigned filename permanently — confirmed live via a
read-only PROPFIND inspection: a real item's UID was
`8fecdc8c-cf00-4b74-b2dc-f6d84790b74d` but its actual server filename was
`1755247320.R237.ics`. Editing it and syncing sent a PUT to the guessed
(nonexistent) URL; SabreDAV rejected it with "Calendar object with uid
already exists in this calendar collection" (HTTP 400) — the write fails
identically and permanently on every subsequent sync.

**No per-item UID→URL cache exists anywhere in the class** — confirmed by
grep. Every cache that exists (`m_localEtags`, `m_contentCache`) is keyed
by URL, not UID. The real URL is known and in scope at
`processFetchedItems()` (`:2350-2351`, `urlKey`) at the exact moment each
item's UID becomes known from its parsed content, and is discarded there.

**Severity higher than any conflict-resolution bug this session fixed —
it needs no conflict, just an edit.** Since adopting a pre-existing
calendar is PlanStan's normal onboarding path, this is close to "the
first real edit a new user makes will silently and permanently fail to
sync."

**File this as the first item the next session reads and fixes.** Full
root cause, exact call sites needing an audit (9 `generateItemUrl()`
call sites, not all necessarily broken — each needs individual
create-vs-update judgment), the recommended `m_uidToUrl` fix shape, and
why the conflict-resolution test suite couldn't have caught it
(`MockBackend` has no URL concept at all) are all in the linked doc.

### O55 — RESOLVED 2026-08-22 — TwoWay sync churns and empties the hub when backend record-id namespaces differ (WildPalms handoff, 2026-08-21, regression v0.77 → v0.99)

**Resolved** on branch `fix/o55-hub-record-id-aliasing` (engine-side id
aliasing + identity-conflict fail-loud guard). Full response/wrap-up:
`docs/2026-08-22-o55-hub-record-id-aliasing-response.md`. Original writeup:
`~/dev/WildPalms/docs/2026-08-21-libkalburator-hub-record-id-join-churn-handoff.md`.

**Root cause (verified against code + history).** Two facts coexisted since
G.8/v0.44 without colliding: `GenericSqliteBackend::loadRecords()` presents
ids as `<collectionId>\x01<origId>` while storing whatever id a create
carried, and `perRecordDiff()` joins source/target/baseline strictly by raw
`BackendRecord::id`. At **v0.77 the topology converged by accident**: pass 2
re-created an already-stored id, the hub's duplicate-key INSERT failed, and
the v0.77-era engine treated the failed write as a mapping failure — aborting
the run before its destructive ops. Phase B4's per-side baseline hashing
(v0.82) made change detection genuinely correct but removed that accidental
abort: pass 2 then saw each side's record as tracked-but-vanished-on-the-peer,
emitted symmetric deletes, and churned both sides empty **while reporting
success** — violating the file's own "fail loud, never silently-empty" rule
(perrecorddiff.cpp). The deleted "id-prefix machinery" of fanout-collapse
(eef7b32) is unrelated — that was DAV collection-id prefixes.

**Fix (all additive; no consumer code change; pin bump only):**

1. **Capture** — `WriteOperation::idAliases()` (requested create-id →
   backend-assigned id); the default `SyncBackendBase::applyRecords()` populates
   it whenever `createRecord()` returns a different non-empty id. Covers
   every backend using the default write path (`GenericSqliteBackend`,
   `MockBackend`, `LocalBackend`). Updates/deletes never alias.
2. **Persist** — `BaselineStore` schema **v8**: `blob_id_aliases(mapping_id,
   native_id → canonical_id)`; cleared by `clearMappingV3()` alongside sync
   tokens. canonicalId = the id the mapping's baseline rows are keyed under.
3. **Join** — `perRecordDiff()` takes an optional alias map (native →
   canonical) and resolves ids at index time; ops still carry native ids so
   writes route correctly. Baseline hash lookups resolve through the same
   run's aliases, so each side's baseline carries its own read-back hash
   (previously a prefixing target silently fell back to the peer's hash).
   The first-sync mirror captures aliases too (`harvestBaselinesAfterFirstSync`).
4. **Fail loud** — `EngineDiff::identityConflicts`: when the diff would emit
   a Create toward EACH side whose records are canonically equal under
   unjoined ids (the churn signature), dispatchSync fails the mapping with a
   precise "identity conflict" error instead of cross-creating. Canonical
   equality includes the payload uid, so genuinely independent records never
   trip it.

**Known limitation:** profiles already poisoned by pre-fix churned runs carry
orphan baseline rows under BOTH id forms with no alias row; those mappings
need one `clearMappingV3()` (re-first-sync) to recover. Fresh profiles —
including WildPalms' failing tests, which build fresh state — converge.

**Tests:** `tests/engine/tst_engine_id_aliasing.cpp` (both slots RED on
v0.99, GREEN with the fix) — (a) bare-id mock ↔ real `GenericSqliteBackend`
TwoWay converges over three consecutive runs with exactly one hub row, zero
steady-state writes after run 1, and ONE baseline row; (b) unjoined equal
twins fail loudly with both records intact. This also closes the coverage gap
that let the regression ship: `GenericSqliteBackend` is now a real mapping
endpoint in the suite. Schema-version pins in `tst_baseline_store_v3` /
`tst_baseline_store_v4_to_v5_migration` bumped 7→8.

**Suite: 180 total, 177 passing** — identical pre-existing baseline
(`tst_remotecalendarbackend`, `tst_calendar_canon_roundtrip`, and
`tst_backend_signals`, which was verified failing on the pristine tree at
this commit — Radicale live-state, same family as the first).

**Consumer impact:** PlanStan unaffected (no `GenericSqliteBackend`
endpoints; all its backends present engine-stable ids). WildPalms needs only
a pin bump; its two failing runtime tests should pass unmodified.

### O56 — RESOLVED 2026-08-22 — recategorization churn at v1.00: alias/baseline anchors crossed per-batch, and destructive ops applied under an unresolved AskUser conflict (WildPalms followup handoff, 2026-08-22)

**Resolved** on branch `fix/o55-followup-recategorization`. Full response:
`docs/2026-08-22-o56-recategorization-followup-response.md`. Original:
`~/dev/WildPalms/docs/2026-08-22-libkalburator-o55-followup-recategorization-handoff.md`.
O55's aliasing worked for first-sync convergence but two defects remained:

**Defect A — the canonical anchor was chosen per batch, not per record.**
The anchor was "the requested id of THIS apply": a target-side create
anchors canonical=bare (`prefixed→bare` alias, baseline keyed bare); a later
back-propagation whose op carries the target-space id anchors
canonical=prefixed — persisting the CROSSED alias (`bare→prefixed`) and a
SECOND baseline row keyed prefixed. With the map pointing both directions,
`joinKey()` resolved source and target onto DIFFERENT keys; the diff split
one logical record into a phantom empty-target Conflict plus a phantom
Delete of the hub row. Exactly the evidence dump in WP's handoff (dual
baseline rows, asymmetric hashes, bidirectional aliases).

Fixes: (1) **persist-time anchor stability** — alias persistence and
baseline saves chain-resolve the canonical side to its component SINK first;
a candidate alias whose requested id already resolves to the assigned id's
sink is a no-op (the crossing row is never written), and baselines always
overwrite the single sink-keyed row. (2) **load-time heal** for stores
already poisoned by v1.00 runs — `healedIdAliases()` resolves every native
id to its component sink with deterministic cycle-breaking
(lexicographically smallest member), and baseline entries collapsing onto
one sink are deduped preferring the row whose hashes match the current side
records. In-memory only; fresh profiles never need it.

**Defect B — data movement under an unresolved conflict.** The phantom
Delete was not a Conflict op, so `unifiedHandleConflicts` accumulated and
applied it in-line while the same logical record's AskUser conflict deferred
unresolved (hub emptied, run reports failure). Worse, the TO-SOURCE ops are
accumulated BEFORE the walk even runs, so sibling writes also landed under
an unanswered conflict. Fix: `unifiedContinueAfterConflicts` now holds ALL
writes when any unresolved conflict remains — clears the merge lists and
completes with `success=false`, message "N unresolved conflict(s); no data
was written". Answered resolutions still replay via pendingResolutions on
the next run (suite-verified: the v0.98 resolution-injection contracts stay
green). Decided NOT changed: unrelated records' ops are held too — an
all-or-nothing rule per mapping-run; throughput cost accepted for the
invariant "a failed run committed nothing".

Both defects were RED→GREEN gated in `tst_engine_id_aliasing`:
`recategorizationViaHubEdit_anchorStaysConsolidated` (WP's scenario lib-side:
hub-edit → back-prop create on the bare-id peer → pass 2 must converge with
exactly one baseline row and one alias direction),
`poisonedCrossedAliasStore_healsWithoutDataLoss` (v1.00-poisoned store:
crossed aliases + dual rows → heals, converges, zero movement), and
`unresolvedConflict_deferredMovesNothing` (standing contract for defect B).
Suite 180 total, 177 passing — identical pre-existing baseline. Consumers:
pin bump only; v1.00-poisoned profiles recover WITHOUT manual clearing now
(the load-time heal handles them).

### O57 — OPEN — First live Microsoft Graph payloads contradict the vendor-shapes reference doc in seven places (EEE Phase-0 corpus observations, found 2026-08-23)

Context: the GraphCLI experiment tool (`tools/graphcli/`, device-code auth,
payload `capture` command) produced the first real-payload corpus entries of
the vendor-convergence (EEE) campaign against a personal Outlook.com account
via a fresh Entra app registration. Evidence lives machine-local under
gitignored `msgraph/captured/` (sanitization/commit into
`tests/fixtures/vendor/` deliberately deferred). Each item below is a
documented-vs-real delta that will bite an `ms-* ⇄ canon` stage if built
from the docs alone:

**(a) Graph events carry a top-level `uid`.** Not in the §1.2 reference
table at all. Observed equal to `iCalUId` (the MAPI GlobalObjectID blob,
e.g. `040000008200E0…`) on both a singleInstance and a seriesMaster. Impact:
positive — a stable cross-system identity anchor is directly available, which
is exactly what O55-style id aliasing wants at a future Graph edge. The
reference table needs a `uid` row.

**(b) Zone vocabulary is split-brained per event.** `start/end.timeZone` =
`"UTC"` while `originalStartTimeZone/originalEndTimeZone` =
`"Eastern Standard Time"` (Windows vocabulary) on the same object authored in
ET. Confirms the reference's §0.3 warning AND adds a requirement the docs
only imply: original-zone preservation must survive into canon alongside the
per-endpoint zones, or round-trips through canon silently re-home the author's
zone.

**(c) Bing-resolved locations carry fields our canon `locations` shape lacks.**
Real entries: `{displayName, address{}, coordinates{}, locationType,
locationUri, uniqueId, uniqueIdType:"bing", addedBy}` — the last four are
unmodeled. Decision owed: widen the `locations` Json schema or route extras
through `providerExtras.x-ms-graph`; either way it must be a declared loss
decision, not stage-code accident.

**(d) Sentinel timestamps in the wild.** `responseStatus.time` =
`"0001-01-01T00:00:00Z"` (.NET DateTime.MinValue) on every organizer-self
response. Any promote/demote must neither choke nor manufacture CREATED/
LAST-MODIFIED-class stamps from year-1 sentinels — same contract class as
`tst_calendar_canon_roundtrip::timestampLessSourceRoundTripsWithoutManufacturedStamps`.

**(e) Zero-sentinel numeric fields inside `patternedRecurrence`.**
Unused `pattern.dayOfMonth`/`pattern.month`/`range.numberOfOccurrences`
serialize as `0`, not null/absent. An RRULE emitter keyed on these values
would emit garbage (`BYMONTHDAY=0`) unless it treats 0 as absent.

**(f) Weekly patterns still carry `index`.** A plain weekly recurrence
(`daysOfWeek:["thursday"]`) serializes `index:"first"` although index is
meaningful only for relative monthly/yearly. Reference §1.3's MS→RFC5545
table must ignore `index` for non-relative types or it will emit spurious
`BYSETPOS`.

**(g) Default event listing returns series MASTERS ONLY — no expanded
instances, no overrides.** `GET /me/calendar/events` returned the
seriesMaster with `type:"seriesMaster"` and nothing else. Occurrences require
`/events/{id}/instances?startDateTime&endDateTime`; overrides/exceptions
surface via `/me/calendarview` or the instances navigation. Corpus +
sync-design consequence: harvesting exception records needs the calendarView/
instances endpoints, and the engine's master+override-record model maps onto
Graph's only if we walk those endpoints — the flat list view is not
sufficient input for a faithful Graph backend.

Corollary observation (not a defect): Graph mixes null (`"recurrence":null`,
`"onlineMeeting":null`) with empty-struct (`location.address:{}`,
`coordinates:{}`) representations for "no value"; stages must normalize both.

Next actions: grow the corpus along the shape matrix (contacts, recurring
exceptions via calendarView/instances, all-day, attendees, attachments);
sanitize + commit fixtures when the corpus stabilizes; fold (a)–(g) into the
reference doc and the future edge loss-profiles at EEE Phases 0/4.

**Addendum 2026-08-23 — full scenario-matrix sweep
(`tools/graphcli/corpus-sweep.sh all`) confirms and extends O57:**

**(h) Exception overrides surface as `type:"exception"` instances.** PATCH of
an occurrence-id (obtained from `/events/{id}/instances`) produced a record
with its own subject inside the series' instance listing, alongside plain
`occurrence` entries; DELETE of a different occurrence left a clean gap (no
record at all — not a tombstone). This is the live confirmation that Graph's
master+exception model maps onto the engine's master+override records via
the instances walk: exception ⇒ override record, gap ⇒ EXDATE-equivalent.
Note the deleted-occurrence case leaves NOTHING on either endpoint — the
cancellation is only reconstructible by diffing against the pattern.

**(i) `calendarview`/`instances` paginate at 10 per page** (default page size,
no `$top` honored without asking). Pagination is mandatory machinery for any
Graph backend read path, not an optimization; the engine's fetch gates must
expect multi-page series expansion.

**(j) Negative-corpus error shapes harvested** (all HTTP 400):
`TimeZoneNotSupportedException` (missing `start.timeZone`),
`ErrorInvalidRequest` ("DayOfMonth should be between 1 and 31" — month:13),
`UnableToDeserializePostBody` (unknown property on contact create — notably
a TRUNCATED message: "were unable to deserialize " with no object named).
Error mapping needs to handle unhelpful/truncated messages; code field is
the reliable discriminator.

**(k) Write-side observations:** instance-level PATCH returns the mutated
instance JSON directly (no re-fetch needed); contact create rejects unknown
properties outright (strict schema, unlike events which tolerated extra
fields); `sweep-clean`-style bulk deletes are plain sequential DELETEs — no
batch endpoint was used (Graph $batch remains unexplored, watch item).

Tooling notes (not vendor findings): capture filenames now use per-segment
slugs (`me-events-<id16>-instances_startD…`) so endpoint kind survives into
the name; `capture` stores single raw pages (wire truth) while the
`calendarview`/`events` verbs aggregate all pages (query truth) — both are
corpus-useful, do not conflate them when writing fixtures.

**Addendum 2026-08-23 #2 — cross-mailbox iTIP topology findings (l)–(o).**
Driven by a two-account experiment (organizer: fresh real
`clintoneist1@outlook.com`; attendees: Gmail-hosted addresses incl. the
primary MSA sign-in; client: Thunderbird):

**(l) Consumer Outlook stamps a synthetic, externally-unaddressable ORGANIZER
into invites.** Invites from the primary account (MSA sign-in
`clintonthegeek@gmail.com`, Exchange identity `outlook_986C65853D873610@
outlook.com`) carry `ORGANIZER;mailto:outlook_986C65853D873610@outlook.com`.
That address is an internal Exchange routing identity, not a real mailbox:
an external attendee's standards-compliant iTIP REPLY to it was refused by
Microsoft's own MX (`550 5.5.0 mailbox unavailable, S2017062302`, at RCPT
TO). Email-based RSVP to such invites is broken by construction — by the
REQUEST side, not the REPLY side. Fix on the account (create a deliverable
@outlook.com alias); fix for us (Graph-native RSVP endpoints only in any
backend).

**(m) iTIP-over-email DOES work when the ORGANIZER is deliverable.** Same
Thunderbird REPLY flow against `clintoneist1@outlook.com`: delivered,
ingested by Exchange, and the organizer-side Graph event flipped to
`attendees[].status.response:"accepted"` with a real timestamp. So consumer
Microsoft calendars accept attendance state from raw emailed iTIP REPLYs —
a write path that bypasses Graph entirely and which a sync backend must
treat as an out-of-band mutation source (changeKey moves; delta catches it).

**(n) A Gmail-hosted MSA has NO Microsoft-side calendar copy as an invitee.**
The attendee's accepted copy exists only in Thunderbird's local calendar.
Graph cannot see it from either side; "attendee view" for such invitees is
permanently outside any Graph backend's reach. Corpus pair saved:
`captured/work-request-request.ics` + `work-reply-reply.ics` (case l),
`captured/20260823-025138-…me-events-AQMkADAwATNiZmYA.json` (case m).

**(o) Tooling: `--profile <name>`** gives graphcli persistent parallel accounts
(`msgraph-<name>/` token isolation, one shared app registration);
verified with two concurrent live identities. `.gitignore` widened to
`msgraph*/`.

**Addendum 2026-08-23 #3 — the full attendee/iTIP behavior matrix
(p)–(u).** Two live consumer accounts (organizer `clintoneist1@outlook.com`;
invitee mailbox = primary account behind aliases gmail-sign-in /
`clintonthegeek@hotmail.com` / synthetic `outlook_986C…`), all interactions
CLI-only. **Much of this lands on CONSUMERS (PlanStan/WildPalms), not the
library**: RSVP flows, invite ingestion timing, and attendee identity
resolution are host-application concerns sitting on top of the sync engine —
see §2d of the consumer-coordination page.

**(p) Calendar-attendant ingestion is delivery-path-dependent.**
Invite delivered to Inbox (sender whitelisted): auto-materializes on the
attendee's Graph calendar in under ~75s, `response:"notResponded"`, sentinel
timestamp — BEFORE any human/client reads anything. Invite delivered to
Junk: NOT processed at all (stays a plain untyped `message`). Move out of
Junk afterwards: the entity re-types to
`#microsoft.graph.eventMessageRequest` (`meetingMessageType:meetingRequest`)
but the attendant does NOT retroactively run — still no calendar event, no
`event` navigation, and the message id MUTATES on the folder move (an
O55-class id-stability trap for any backend caching message ids).

**(q) eventMessage RSVP actions do not exist on consumer accounts.**
`POST /me/messages/{id}/(tentativelyAccept|accept|decline)` → 400
`RequestBroker--ParseUri` on BOTH v1.0 and beta, even for a correctly-typed
`eventMessageRequest`. The `event` navigation is likewise unreachable
(cast attempts rejected). Programmatic RSVP against an unmaterialized invite
is impossible; only the calendar EVENT object exposes RSVP actions.

**(r) The complete loop works CLI-only end-to-end:** organizer creates event
with attendee via Graph POST → attendant auto-adds attendee-side copy →
attendee responds `accept` via `/me/events/{id}/accept` → REPLY travels as
emailed iTIP → organizer-side attendee row flips to `"accepted"` with a real
timestamp (~45s). No mail client involved at any stage.

**(s) Counter-proposals (`proposedNewTime`) are server-accepted but
email-carried.** `tentativelyAccept` with `proposedNewStart/End` succeeds
against the event object; however the proposal reaches the organizer only
inside the response EMAIL — when the organizer side has a synthetic
unaddressable ORGANIZER (l), the proposal silently vanishes (never bounced
visibly, never delivered). Emailed iTIP REPLYs carry no structured proposal
fields, so `attendees[].proposedNewTime` on the organizer side is populated
only by Graph-native paths.

**(t) ATTENDEE ALIAS EXPANSION — the founding evidence for the identity
layer (consumer-critical).** Organizer addressed the invite to
`clintonthegeek@hotmail.com` (a mere alias of the primary mailbox). At
ingestion Exchange kept the as-addressed row AND resolved the recipient to
the canonical internal identity `outlook_986C65853D873610@outlook.com`,
tracking BOTH rows. Our RSVP was emitted under the CANONICAL identity, so
the canonical row flipped to `accepted` while the originally-addressed
`hotmail` row stays `notResponded` forever. Organizer-visible result:
one phantom non-responder + one acceptance from an address nobody was
invited under. Consequence: **naive string-matching of attendees across
vendors can never converge** — the same person presents different emails per
vendor, and vendors rewrite each other's addresses into their own canonical
vocabulary. Consumers diffing attendee state need alias-set resolution down
to a stable sink (the exact shape of O55's record-id fix, one level up);
this is what campaign-proposal §5's identity layer must deliver, and until
then hosts should treat attendee diffs as advisory, not authoritative.

### O58 — RESOLVED — `canonPersonalClassificationProducesPrivateAndStash` red slot was a test-string bug, not a data-loss defect (found + fixed 2026-08-23)

The uncatalogued pre-existing failure in `tst_calendar_canon_roundtrip`
(called out in the EEE proposal Phase 0 and CLAUDE.md) is closed. Diagnosis:
production behavior is correct — `canonObjectToEventBytes()`
(`src/calendar/eventcanonfields.cpp:527-543`) sets
`X-CANON-CLASSIFICATION=personal` via
`setNonKDECustomProperty`, but KCalendarCore serializes non-KDE custom
properties with an explicit value-type parameter:

```
X-CANON-CLASSIFICATION;VALUE=TEXT:personal
```

The slot asserted the literal substring `"X-CANON-CLASSIFICATION:personal"`,
which can never occur in kcalendarcore output → guaranteed red since the slot
was written. Fix: parameter-tolerant regex match
(`^X-CANON-CLASSIFICATION(?:;[^:\r\n]*)?:personal$` multiline). The verbatim
stash is present and recoverable as designed; no production code touched.
Suite baseline moves to 180 total / 178 passing.

### O59 — OPEN — Google-side wire truths vs the vendor-shapes reference (EEE Phase 2 edge implementation, found 2026-08-23)

While implementing the `google-event ⇄ canon` edge (Phase 2), the
hand-modeled fixture was verified against the LIVE Calendar API v3 events
reference before trusting it — and the reference doc / our fixture both had
deltas. Also logged: two tooling findings. Google-side live corpus capture
is now unblocked (OAuth desktop client registered; scratch space in
gitignored `google/`, following the `msgraph/` model) — items below should
be re-confirmed against captured payloads when available.

**(a) `reminders.overrides[]` entries are `{method, minutes}`.** Our first
fixture invented `reminderMethod`. Both stage directions initially baked the
wrong key in. Wire truth: `method` (`popup`/`email`), max 5 overrides,
minutes 0..40320.

**(b) `eventLabelId` exists — absent from reference doc §1.1 entirely.**
String, writable, "supersedes the index-based colorId property" (set via
`eventLabelVersion=1` request parameter on insert/update/patch). The canon
catalogue has no home for it yet; the edge currently carries it verbatim in
`providerExtras["google"]`. Watch item for the next canon catalogue review.

**(c) `extendedProperties.private.(key)` values are STRING-typed.** All
`x-canon-*` carriers must be JSON-stringified (the stage's
valueToCarrierString/carrierStringToValue pair). This is a hard constraint
unlike iCal X- props, which carry text anyway.

**(d) `status:"cancelled"` is dual-meaning** (deleted event vs cancelled
exception of an uncancelled series); cancelled exceptions guarantee only
`id`, `recurringEventId`, `originalStartTime`; deleted events only `id`.
Mirrors O57(h)'s Graph gap-not-tombstone behavior. Backend-relevant
(Phase 7 transport layer), not edge-relevant.

**(e) iCalUID ≠ id, confirmed in so many words**: occurrences of a series
share one iCalUID but each has its own id. uid-anchored series handling
(the O57(a) Graph finding) applies identically on the Google side.

**Tooling notes (not vendor findings):**
- **moc silently produces NO output for a Q_OBJECT class in a translation
  unit containing a terminated raw string literal** `R"(...)"` — symptom is
  an undefined-vtable link error, moc's only hint is "No relevant classes
  found" when run manually. Bisected: prefix parses up to the closing `)"`,
  fails with it present. House rule: no terminated raw strings in files with
  Q_OBJECT classes (use concatenated quoted literals).
- **AUTOMOC timestamp gotcha resurfaced**: adding new source files to
  explicit lists required removing `build/**/autogen/timestamp` to get moc
  to re-run (already documented at CMakeLists.txt:124; cost another ~20 min
  here).

**(f) Google silently drops consent-screen-unapproved scopes from the
grant.** First googlecli authorization came back carrying ONLY
`userinfo.email`/`openid` even though `calendar.events` was requested — no
error, just a narrower token (verified via tokeninfo). Cause: the scope was
not yet on the OAuth consent screen's approved list. Any future Google
transport must verify granted scopes at login and warn per missing scope
(googlecli now does). Discovered live 2026-08-23; re-consent after adding
the scopes to the consent screen fixed it.

**Transport research input landed:** `docs/google_rest.md` (2026-08-23) —
Google-side analogue of `msgraph/general_plan.md`: OAuth desktop loopback
flow, syncToken incremental semantics, external identity
`(provider, collection, remote_id, etag)`, GoogleAccount as its own
subsystem. Confirms the edge/transport seam: provider-local identity lives
in `providerExtras["google"]`, sync tokens belong to the future backend.

### O60 — RESOLVED — Two implementation traps hit during the 7.B ms-event edge (found + fixed 2026-08-23)

**(a) Qt 6.11 `QJsonValue` default construction is Null-typed, not Undefined.**
The first `tst_ms_event_canon_edge` helper returned `{}` from a
`QJsonValue`-returning "find carrier" function and asserted absence via
`.isUndefined()` — which is FALSE for that default (`type()==0`, i.e. Null;
Undefined is a distinct non-zero tag on this Qt). Symptom: carrier-absence
asserts failed against provably carrier-free output. Fix: explicit boolean
`hasCarrier(wire, key)` helper; value-extraction and presence-check are
separate concerns now. Rule for future edge suites: never signal JSON-value
absence with `return {}` + `isUndefined()`.

**(b) Offset-less wall-time parsing must never route through the process-local
zone.** Graph's dominant `dateTimeTimeZone.dateTime` form carries no offset
("2026-11-26T09:00:00.0000000"). `QDateTime::fromString(..., Qt::ISODate)`
parses it as Qt::LocalTime, so any subsequent `toTimeZone(...)`/`toUTC()`
silently bakes in the machine's zone (observed: +4h skew on the dev box,
America/New_York). Fix in both promote/demote helpers: wall-time forms are
constructed directly IN the target zone (`QDateTime(date, time, QTimeZone)`),
with Windows-vocabulary ids resolved through the vendored CLDR map
(`windowszonesmap.h`) for interpretation only — the emitted `timeZone`
string stays verbatim (O57(b) original-preservation).

Implementation decisions pinned alongside (recorded here because they shaped
the stages, not just the tests):
- **Redundant-topology suppression:** demote reconstructs Graph `type`
  structurally (recurrenceId⇒exception > patternedRecurrence⇒seriesMaster >
  seriesMasterId⇒occurrence > singleInstance); promote therefore consumes a
  wire `type` equal to its own derivation instead of double-stashing it in
  providerExtras — this keeps C→G→C byte-equal while surprising topologies
  stash verbatim and re-emerge untouched.
- **Wire-fidelity stashes** (attendees, attachments, locations, location
  leftovers): when the promote side had to leave MAPI/vendor fields behind,
  demote prefers the verbatim stash over rebuilding from canon; canonical
  edits flow through the engine baseline diff.
- **Timestamps:** demote emits the full wire form ("yyyy-MM-ddTHH:mm:ss"
  + ".0000000Z") — omitting the trailing Z made promote parse as local time
  (trap (b) again).
- **partstat vocabulary:** Graph none/tentativelyAccepted map to
  needsAction/tentative in both directions (needsAction emits no status
  object — "none" ≡ absent).

### O61 — OPEN → partially RESOLVED — 7.B live checkpoint: one blocking bug caught, carrier loss on write confirmed (found 2026-08-23)

The proposal-invariant-6 live checkpoint for the ms-event edge ran against
the real Outlook.com account (probe events created via graphcli, captured,
round-tripped through `tools/msroundtrip`, re-created on the server,
canon-compared; all payloads machine-local under gitignored `msgraph/`).
It validated the campaign discipline again — the stub suite was green
while the live wire carried a blocking bug:

**(a) RESOLVED — BLOCKING: sentinel `range.endDate` amputated numbered series.**
Graph serializes `recurrence.range.endDate` as the .NET year-1 sentinel
(`"0001-01-01"`) on `numbered` ranges instead of omitting it. Promote read
it as a valid date and emitted `UNTIL=00010101T235959Z`; demote round-trips
that to `endDate:"0001-01-01"` — and any consumer honoring the UNTIL would
terminate the series immediately. Same O57(d) sentinel family; fixed by
sentinel-guarding endDate (and startDate) in the converter. Hand-modeled
fixtures never carried this — only the live service does.

**(b) RESOLVED — `original*TimeZone` dropped by demote.** The fields sat in
the demote passthrough skip-set, so the author's zone vocabulary never
re-emitted. Live confirmation they matter: Exchange re-homes authored IANA
zones (`America/New_York`) to UTC endpoints AND translates the recurrence
range into Windows vocabulary (`recurrenceTimeZone:"Eastern Standard Time"`)
— O57(b) split-brain happens AT AUTHORING TIME on consumer accounts.

**(c) RESOLVED — `iCalUId` consumed instead of stashed.** Live wires carry
BOTH top-level `uid` AND `iCalUId` (identical MAPI blobs); consuming the key
meant demote emitted only `uid`. Now stashed → byte-equal via passthrough.

**(d) RESOLVED — organizer responseStatus dropped wholesale on sentinel.**
Organizer-self rows carry `response:"organizer"` WITH the year-1 time
sentinel; dropping the whole object lost the value. Fix: strip the sentinel
time, keep the object.

**(e) CONFIRMED — CARRIERS DO NOT SURVIVE WRITES (design consequence).**
The re-created event was POSTed with our
`singleValueExtendedProperties[{x-canon-classification:"personal"}]`
carrier; the stored copy returns NO SVEP — consumer Outlook.com silently
drops unknown extended properties on create (contrast O57(k): plain unknown
fields are tolerated on events). Consequence: the Reversible loss class
holds OFFLINE only. Any data that lives solely in carriers (canon
`personal`, `sequence`, guestsCan*/locked/privateCopy, typedProperties,
carried unrepresentable RRULEs) is LOST when a record is re-created from
canon. Backend rule for 7.C: prefer PATCH of an existing server copy over
delete+re-create; treat carrier-only data as declared-lost across creates.
This also means the C→G→C byte-equal guarantee is a STUB-level contract;
live writes degrade it exactly along the carrier set.

**(f) CONFIRMED — uid/iCalUId regenerate per copy.** The MAPI GlobalObjectID
blob embeds creation-time data; each newly created copy gets a fresh blob.
Two server copies of one logical event therefore NEVER share uid after a
re-create — unlike Google, where iCalUId can be SET on create and survives.
Identity-layer consequence: Graph uid is a PER-COPY anchor (join key within
one copy's history), not a portable logical id; cross-copy identity needs
providerExtras["msgraph"].id lineage or content fingerprints.

**(g) CONFIRMED — generational decay of original*TimeZone.** The server
ignores client-supplied `originalStartTimeZone/originalEndTimeZone` on
create: the re-authored copy recorded UTC. Author-zone metadata survives
one promote→demote cycle but not a server-mediated generation. Declared
Degraded, not fixable edge-side.

**(h) CONFIRMED — Exchange body synthesis noise.** Empty/text bodies come
back wrapped in generated HTML boilerplate ("converted from text", EmailQuote
styles) whose FORMATTING differs per copy while semantics (`&nbsp;`) match.
canon-compare must tolerate semantically-equal body variants; canon carries
both description/descriptionHtml so the wrapper lands in descriptionHtml.

Declared-normalization additions (mirrored in the loss-profile doc and the
runner's declared set): false-flag absence (`isCancelled`/`isAllDay`/
`isOnlineMeeting` omitted when false), no-meeting triad collapse, body
contentType case normalization, `range.recurrenceTimeZone` dropped (zones
live on endpoints), empty-string location.displayName dropped, `sensitivity`
personal→private + carrier paths, SVEP carrier channel paths, `recurrence`
null-vs-absent.

Post-fix probe results: rich weekly ET probe G→C→G = 18 diffs (all
declared); all-day probe = 10 (all declared); both demoted bodies ACCEPTED
by the server; canon-compare divergences reduce to per-copy identity
(uid/iCalUId/created/lastModified/url) plus (e)/(g)/(h).

### O62 — RESOLVED — recurring async-lifetime trap; house rule now explicit (found 2026-08-23)

Three occurrences this campaign of one disease: **async continuations
referencing dead stack frames.** (1) `GraphApiClient`'s collection walk
self-captured a stack-local `std::function` by value before assignment
(`bad_function_call`); (2) the backend apply-batch draft captured the
enqueueOperation functor's locals by reference (`[&]`) while callbacks fire
on later event-loop turns (SIGSEGV); (3) the same pattern nearly shipped in
the mock-server era helpers. Rule going forward: any state an async chain
mutates or reads MUST be heap-owned (`shared_ptr<struct>` captured by value)
or be a member whose owner provably outlives the chain; recursive step
functions must re-arm through the heap state, never through stack captures.
Both 7.C components now follow it (GraphApiClient::getPage member recursion;
MSGraphCalendarBackend::ApplyState).

### O63 — RESOLVED — stale stock-shape edge-count pin + Graph dateTimeTimeZone naming trap (found 2026-08-23, Phase 3 close-out)

(a) `tst_vcard_plugin::stockShapesHasFiveEdges` expected 5 edges while
ContactsStockShapes had carried **7** since the google-person landing
(927390d) — i.e. the slot was failing on `main` and missed by the
consolidated baseline count. Root cause: the Phase-3 google-person commit
updated STATUS ("7 edges") but not the count-pinning plugin test. Rule:
when a StockShapes `edges()` list grows, grep for `edges().size()` across
the domain's tests IN THE SAME COMMIT (both pins now read 9).

(b) Graph's `dateTimeTimeZone` is the TYPE name; its zone PROPERTY is
plain `timeZone` (`dueDateTime: {dateTime, timeZone}`). Writing the type
name as the zone key silently promotes an empty-string zone through
QJsonValue defaults (O60 family). Caught by a promote assertion in
`tst_ms_todotask_canon_edge`, fixed in the fixture.

### O64 — RESOLVED — google-person demote dropped canon email display names; caught by the Phase-6 convergence gate (found 2026-08-24)

`tst_gm_pipeline_convergence::contactCrossingMsToGoogleStaysDeclared` found
`emails` diverging across MS→G UNDECLARED (canonToGooglePersonLoss declares
emails lossless): CanonToGooglePersonStage emitted only {value, type,
metadata.primary}, silently dropping the canon email entry's `name` key.
Google's wire home exists — `emailAddresses[].displayName`. Fixed both ways
(promote maps displayName→name via mapRows so leftovers-stash no longer
triggers for plain named emails; demote re-emits displayName). Lesson: the
pipeline-level crossing gate catches per-edge suite blindness because each
edge's own round-trip never exercises a FOREIGN edge's richer canon shape.

### O65 — RESOLVED — event records must never index participant emails; convergence belongs to persons, not meetings (found 2026-08-24, Tier-A1 gate)

The first engine-level vendor-shaped run (`tst_engine_vendor_shaped_hub::
rosterResolvesNamedPersonsAfterConvergence`) caught PersonDirectory
resolving attendee bob@example.com to ADA's name. Root cause:
`extractCanonKeys` treated calendar organizer/attendee emails as the
EVENT record's own identity keys, so observing an event indexed every
participant onto the meeting's entity — and the next contact sharing any
attendee email was ADOPTED into it. Design ruling (now pinned three
ways): calendar/todo canon contributes NO email-index entries; events
join by uid alone and are never persons; participants converge at
RESOLUTION time via the contact-owned email_index. Enforced by
`tst_identity_links::extractKeysFromVendorEdgeOutput` (extraction-level),
`tst_doctrine_pins::onlyEmailEvidenceBridgesRecords` (rule-4 pin), and
the roster slot itself (end-to-end). Second consecutive campaign gate
that caught what per-component suites structurally could not (cf. O64).

### O66 — OPEN — live drill results, 2026-08-24: carrier-survival verdicts + todoTask wire truths (Tier A2/A3 session)

Drills run against live consumer accounts (probes CORPUS-tagged, cleaned
afterwards; captures stay machine-local under msgraph/captured/ and
google/captured/).

**(a) Carrier-survival verdicts — the O61(e) question, answered per channel:**

| Channel | Verdict | Evidence |
|---|---|---|
| Google People `clientData` | **SURVIVED** create + fresh read (both x-canon rows intact, metadata stamped server-side) | people.createContact → people.get personFields=clientData |
| Graph todoTask open extensions (`kalburator.canon`) | **PHANTOM** — echoed in the POST response, ABSENT on every read ($expand=extensions null on v1.0 AND beta) | POST /me/todo/lists/{id}/tasks → GET |
| Graph contact open extensions | **SERVER-BROKEN** — PATCH accepted ("patched-ok") but ALL read surfaces fail: plain GET extensions=null; $expand+filter ⇒ HTTP 500 ErrorInternalServerError; direct extension path ⇒ HTTP 500 (v1.0 and beta) | consumer Outlook.com |

Consequence for the loss profiles: People clientData is the FIRST proven
live-Reversible channel on a consumer account. The Graph channels remain
offline-only (O61(e) class) — and todoTask's phantom echo is WORSE than
event-SVEP's honest strip: a naive implementation would trust the create
response. Backends must re-READ after write to know what survived.

**(b) todoTask wire truths (docs-always-lie department):**
1. Creating a task WITH recurrence REQUIRES dueDateTime — undocumented
   400 "The property 'dueDateTime' is required when creating recurrence".
2. Server REWRITES your dueDateTime to align with the pattern (posted
   2026-08-25T10:00 → stored 2026-08-31T00:00:00, the next occurrence).
3. Sentinel family confirmed on tasks too: range.endDate "0001-01-01",
   zeroed dayOfMonth/month/numberOfOccurrences (O57(e)/(f) disease, new organ).

**(c) Google Tasks wire truths:** UI-recurrent tasks ("this happens every
monday", "do this every three days") carry NO recurrence field of any kind
on the wire — §3.1's ruling now corpus-backed. due is date-only midnight
UTC ".000Z"; position strings are lexicographic ("00000000000000000002").

**(d) Tooling notes:** graphcli `patch`/`delete` cover event|contact|
calendar only (no task verbs); googlecli grew a `raw <METHOD> <path-or-url>
[file]` escape hatch this session. Contact ids ending '=' must NOT be
URL-encoded in paths (encoded form ⇒ 404), but $expand filter values need
%27 quoting — and the filtered expand 500s anyway.

**(e) OAuth:** Tasks API must be enabled per GCP project even when the
scope is granted (accessNotConfigured 403 until console activation).

### O66 CORRECTION — 2026-08-24, same day: verdicts (a) revised after docs review + proper-methodology re-drills

Operator pushback ("graph can't be that broken") triggered a docs audit
(opentypeextension POST/GET pages) and corrected drills. The original
verdicts were contaminated by two methodology errors: (1) PATCH-with-
extensions is NOT a documented operation — adding to an EXISTING instance
requires POST to the `/extensions` nav property; (2) reads require
COLLECTION-level `$expand=extensions($filter=Id eq '<full-id>')`, and the
full-id prefix for Outlook resources is **Microsoft.OutlookServices.
OpenTypeExtension.<name>** — NOT `microsoft.graph.openTypeExtension.<name>`
(filtering on the wrong prefix ⇒ HTTP 500).

**Corrected carrier-survival verdicts:**

| Channel | Corrected verdict |
|---|---|
| Google People clientData | SURVIVED (unchanged) |
| todoTask open extensions via nav `POST .../tasks/{id}/extensions` | **SURVIVED** create + filtered-expand read |
| todoTask extensions INLINE at task-create body | **echoed-not-persisted** (docs list inline-create as supported; behavior diverges — wire-lie) |
| Graph contact open extensions via nav POST | **SURVIVED** create + collection-level expand read |

Net: BOTH Graph channels are live-workable when spoken to properly. The
Reversible rulings keep their offline-only caution only for the
inline-create path. Backend rules: nav-property POSTs, never PATCH-borne
extensions; filtered expand with the RETURNED full id; never trust a
create echo.

**(f) NEW transport finding — consumer contact GET-by-id is flaky/broken
on this mailbox:** `/me/contacts/{id}` returns ErrorItemNotFound for ALL
recently-created contacts (both test contacts), persistently, while the
SAME ids appear in collection listings and the extension data is intact
via listing-level expand. Plain/encoded ids, ImmutableId prefer header,
and beta endpoint all 404; direct `/extensions` nav GET ⇒ "The OData
request is not supported"; DELETE-by-id also 404s. Earlier today one
contact GET-by-id DID work, so it is intermittent or state-dependent.
Backend consequence: ms-contact backends must treat item-by-id reads/
deletes as UNRELIABLE on consumer accounts — drive reads through listings
/ delta / $expand, exactly the shape our fixtures already use. (The two
GraphCLI Test probes could not be deleted by id for this reason; they are
disposable and may need manual removal in the Outlook UI.)

Methodology note appended to doctrine Part III: **drill by the book
first** — pull the API page BEFORE concluding server brokenness; a wrong
verb produces a false server-fault verdict (this correction was nearly an
O-entry of shame).

### O67 — OPEN — A4 Phase-6 live checkpoint, 2026-08-25: cross-vendor replay verdicts + create-path wire truths

Full pipeline executed live (USER-DELEGATED): fresh captures both vendors →
per-edge G→C→G roundtrips (`tools/msroundtrip`, new `tools/groundtrip`) →
crossing replays with CORPUS-tagged probes both directions → server copies
re-promoted and compared. Probes swept clean afterwards. Both directions
**PASSED** — every divergence is a declared-class normalization.

**(a) Roundtrip gates:** google-event " boardx beta" (3 attendees):
6 diffs, all declared after one NEW declaration — per-attendee `organizer`
flag has no canon home (redundant with top-level organizer; declared in
`groundtrip` + loss profile). ms-event "wild hackathon bring a friend":
16 diffs, all declared after one NEW declaration — empty-string
`body.content` ≡ absent on demote.

**(b) MS→canon→Google live create:**
1. **Google events.insert REJECTS read-only `created`/`updated`** (400
   Bad Request). Demote emits them; the create path must strip. (Graph
   accepts its equivalents — asymmetry.)
2. Google rewrites organizer to the AUTHENTICATED account on insert
   (source organizer clintoneist1@outlook.com → gmail account).
3. Server normalizations on read-back: attendee email local-parts
   lowercased; attendee displayName dropped when it equals the email;
   default-value echoes (`status:"confirmed"`, `eventType:"default"`,
   `transparency` opaque stripped as default).
4. **Identity anchor honored**: client-supplied iCalUID (the MS GUID)
   survives insert — uid byte-equal across the crossing.

**(c) Google→canon→MS live create (consumer Outlook.com):**
1. Graph mints a NEW uid on create (client uid/iCalUId NOT honored) —
   opposite of Google; anchor continuity needs post-create mapping.
2. Organizer rewritten to authenticating account (same rule as Google).
3. Text body converted to HTML body (contentType rewrite).
4. Teams meeting AUTO-PROVISIONED per mailbox default (location +
   onlineMeeting.joinUrl appear from nowhere); default reminder alarm
   injected (alarms[0] -900/type display); importance→priority,
   sensitivity→classification, showAs→freeBusyStatus defaults echoed.
5. Carriers (singleValueExtendedProperties) stripped at create as before
   (O61(e) stands for events).

**(d) Cross-vendor invitation propagation observed live**: the Google-side
probe appeared on the Outlook account as an invitation copy (attendee alias
hotmail), deleted by the same sweep-clean.

Backend consequences: create paths must strip read-only fields per vendor
(Google: created/updated), never assume uid continuity across a Graph
create (re-read + remap), expect organizer rewrite + consumer defaults on
both vendors. Tool note: `tools/groundtrip` added (g-roundtrip, mirrors
ms-roundtrip; declared-normalization set mirrors the google-event profile).

Methodology: drill-by-the-book held — the two blocking UNDECLARED diffs
were fixed by DECLARING them (doc + tool together), not by loosening code.

### O67 ADDENDUM — 2026-08-25, same session: Google Calendar extendedProperties re-classed live-Reversible (matrix verdict corrected)

While consolidating the wire notes it emerged that the carrier-survival
preamble added to CONVERGENCE-MATRIX.md earlier today misclassed Google
Calendar `extendedProperties.private` as offline-only. The evidence says
otherwise: the Phase-2 checkpoint event's `x-canon-priority` came back from
a live create+read (v1.02 fixture), and the A4 MS→canon→Google replay
carried ALL five x-canon rows through a consumer create byte-exact.
Corrected verdict: Google Calendar extendedProperties = **live-Reversible**
(the calendar-domain channel that works); MS event SVEP remains the only
offline-only channel (O61(e)). Matrix preamble regenerated; byte pin green.
Lesson: verdict tables must cite their evidence per row — the offline-only
pairing was inherited from O61(e)'s framing without checking the Google
side had its own live proof.

### O68 — OPEN — B2C P1.f live checkpoint, 2026-08-25: Google rejects client-supplied event ids on insert

First live drill of `GoogleCalendarBackend` (probe cycle vs the real
consumer account; reads passed immediately: discovery 3 calendars,
initial listing 33 records). The create probe FAILED with 400
"Invalid resource id value" — reproduced via raw POST with a single-field
delta: a client-supplied transport `id` ("requested-b2c-probe") is
REJECTED on events.insert regardless of the other fields. Google mints
its own id unconditionally (like Graph's uid behavior — but here it is the
TRANSPORT id, and unlike Graph the iCalUID anchor still honors client
values).

Backend consequence: the create seam strips THREE read-only/rejected
fields now (`created`, `updated`, `id`) — see
`GoogleCalendarBackend::stripReadOnlyFields`. Mock updated to enforce all
three rejections so this can never silently regress. The demote stage
still emits an authored `id`; that remains correct for PATCH (where the
path carries the server id) but must never reach an insert body.

Sweep note: the failed probe never landed server-side (400), so no
cleanup was needed for it; the manual reproduction probe was swept.

### O69 — OPEN — B2C P1.f MS live drill, 2026-08-25: consumer delta pages deliver SKELETON projections

Second live-drill discovery of the day, on the MS leg
(`MSGraphCalendarBackend` vs the real consumer Outlook.com mailbox):
`/me/events/delta` pages deliver items carrying ONLY
`{id, start, end, type, @odata.type, @odata.etag}` — no subject, no uid,
no iCalUId, no body, no timestamps — while plain `/me/events?$top=N`
listings return FULL resources (verified side-by-side, same minute).
Observed on BOTH the initial walk and replays; `Prefer:
return=representation` did not restore richness (returned an empty page).
All five observed delta items were skeletons across two independent walks.

Backend consequence (fixed in the same commit): the backend must treat a
delta item lacking `createdDateTime` as PARTIAL when a richer cached copy
exists — union-merge the skeleton OVER the cached record instead of
replacing it (tombstones via `@removed` unaffected). Declared limitation:
field-level deletions and subject-only edits cannot be observed through a
skeleton page; correctness relies on periodic full listings (410 resyncs,
re-adds) surfacing them eventually.

Identity consequence: records delivered via delta may lack uid/iCalUId —
the loss-profile fallback chain (uid ← iCalUId ← transport id) is not just
a $select-projection concern, it fires on NORMAL consumer delta traffic.

### O70 — OPEN — B2C P2 design-pass live probe, 2026-08-25: contacts change tracking rejects $expand (and every shaping param)

Design-pass probe of `/me/contacts/delta` against the real consumer
account: ANY of `$orderby, $filter, $select, $expand, $search, $top` on
contacts change tracking ⇒ HTTP 400 `ErrorInvalidUrlQuery` ("not
supported with change tracking over the 'Contacts' resource"). Plain
`/me/contacts?$expand=extensions($filter=Id eq 'Microsoft.OutlookServices.
OpenTypeExtension.kalburator.canon')` works and returns carriers inline
(full-prefix extension ids confirmed in the wild).

Backend consequence: unlike events (where O69 forced union-merge over
delta skeletons), contacts cannot ride delta at all in v1 — the carrier
channel ($expand) is UNREACHABLE on delta pages and GET-by-id enrichment
is broken (O66(f)). The only single surface delivering records AND
carriers is the expanded full listing; `GraphContactsBackend` fetch =
expanded listing walk per folder, every time (correctness over
incrementality). Initial delta walk DID return full projections (not
skeletons), so delta remains a v2 option if a carrier-less incremental
pre-pass ever becomes worth its merge complexity.

### O71 — OPEN — B2C P2.f live checkpoint, 2026-08-25: People createContact is COLLECTION-level (`/v1/people:createContact`); resource-level form 404s

Live probe against the real consumer Google account during
`tst_google_people_backend_live`: `POST /v1/people/me:createContact` ⇒
HTTP 404 with an HTML Google-front-end error page ("The requested URL
`/v1/people/me:createContact` was not found") — the create verb lives on
the `people` COLLECTION, not on a person resource. The correct form,
verified live end-to-end: `POST /v1/people:createContact` ⇒ 200 with a
minted `people/c<N>` resourceName. The P2.b mock had pinned the wrong
(resource-level) shape; mock-green could never see this.

Related transport-seam bug caught in the same run: `GooglePeopleBackend`'s
constructor defaulted its client base URL to `people.googleapis.com/v1`
while EVERY authored path carries `/v1` verbatim (client joins base+path)
⇒ doubled `/v1/v1/…` on any live caller that did not override the base
(mocks always override). Default corrected to the version-less base.

Backend consequence (fixed same commit): create seam =
`/v1/people:createContact`; constructor base = `https://people.googleapis.com`;
mock re-pinned to the collection route.

### O72 — OPEN — B2C P2.f live checkpoint, 2026-08-25: People :updateContact REQUIRES an etag; listings always deliver one; displayName is server-derived

Live probes against the real consumer Google account:
1. `PATCH /v1/people/{bareId}:updateContact?updatePersonFields=names`
   with an etag-less body ⇒ HTTP 400 INVALID_ARGUMENT "Request must set
   person.etag or person.metadata.sources.etag for the source that is
   being updated." The defensive strip of `etag` in
   `GooglePeopleBackend::stripNonCreatableFields`/update prep therefore
   broke every live update (mock-green blind spot: the mock never
   enforced the token).
2. Connections listings ALWAYS carry the top-level `etag` even though
   `etag` is not a projectable `personFields` value (observed with the
   exact shared projection constant) — so records already hold what the
   update needs; no extra GET required.
3. With `etag` riding the body: PATCH ⇒ 200. The server DERIVES
   `displayName` from given+family on update — a client-supplied
   `displayName` was ignored/overwritten ("CORPUS p2f EDITED" came back
   as "CORPUS p2f"). Assert renames on component name fields, never on
   displayName.

Backend consequence (fixed same commit): update keeps `etag` IN the body
but excludes it from the `updatePersonFields` mask; mock now rejects
etag-less patches with the live error shape.

### O73 — OPEN — B2C P2.f live checkpoint, 2026-08-25: Graph consumer open-extension nav POST is UPSERT (settles design question Q4)

Design question 4 asked whether a carrier UPDATE needs a nav PATCH of the
existing extension instance (`/contacts/{id}/extensions/{extId}`) instead
of another create. Live answer against the real Outlook.com account:
posting a second open-extension row with the SAME `extensionName` to
`/v1.0/me/contacts/{id}/extensions` succeeds (no 409) and UPSERTS — the
deterministic id `Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon`
is kept, the custom key's value is replaced, and the filtered `$expand`
read-back serves exactly ONE extension row with the NEW value. Confirmed
end-to-end through `GraphContactsBackend::applyRecords` update path
(plain-field PATCH + extensions[] stripped → one nav POST): op succeeded,
refetch served `x-canon-gender=female`, one row. NO backend change needed
— the existing strip-then-nav-POST channel handles carrier updates
correctly; a nav-PATCH variant would be redundant complexity.

### O74 — RESOLVED 2026-08-28 — VTODO-parity recon, 2026-08-25: providerExtras is invisible to the canonical todo differ

The todo domain's canonical differ is `CanonJsonDiffer(todoCanonPropertyIds())`
(`src/todo/tododomaindefinition.cpp:27`) — it diffs catalogued canon
property ids only. `providerExtras` (which carries `x-vtodo` unknown-
property stashes, vendor extras stashes, and carrier rows) is NOT
catalogued, so a change confined to X-/extra properties never produces a
diff: two syncs of a record whose ONLY delta is an X-prop edit converge
as no-op and the edit does not propagate. Same shape presumably holds for
any domain whose differ is catalogue-scoped (contacts/events use the same
CanonJsonDiffer pattern).

Discovered during W7/W6 recon for the PlanStan VTODO-parity handoff
(response doc §0.1). Fix folds into parity VP.f/W7: either catalogue a
derived extras digest or add an explicit extras key to the compared set.
Until fixed, X-prop-only edits rely on byte-level paths (raw-bytes
backends) to propagate.

**Resolution (VP.f, 2026-08-28):** new catalogued `providerExtrasDigest`
(String) todo canon key, computed at promote time on each of the three
todo legs from a domain-neutral
`Kalburator::Shape::CanonEnvelope::canonicalDigest(const QJsonValue&)`
helper (`src/shape/canonenvelope.{h,cpp}`; SHA256 hex of the
`QJsonDocument`-serialized value — Qt6's `QJsonObject` serialization
already sorts keys at every nesting level, so no separate canonicalizing
pass was needed). Google/MS call sites filter known-volatile bookkeeping
before hashing (Google: `etag`; MS: `@odata.etag`, `lastModifiedDateTime`,
`@odata.context` — confirmed against a real captured Graph todoTask
sample) so the digest doesn't become spuriously "always dirty" on every
vendor-side write. `providerExtrasDigest` → `Dropped` on all three loss
profiles (derived/meta, no wire form). Differ pin:
`differMarksProviderExtrasDigestChangeOnly`
(`tests/shape/tst_canonjson_diff_merge.cpp`). Full writeup:
`docs/campaign/vtodo-parity/2026-08-28-vpf-return-receipt.md` §3,
`docs/campaign/vtodo-parity/2026-08-28-w7-passthrough-contract.md` §4.

### O75 — OPEN — B2C P3.f live checkpoint, 2026-08-26: Google Tasks discovery now REQUIRES /users/@me (plain /users/me 404s with an HTML page)

`GET https://tasks.googleapis.com/tasks/v1/users/me/lists` — the exact
form captured working on 2026-08-24
(`google/captured/20260824-145201-245-…users-_me-lists.json`) — NOW
returns HTTP 404 with a Google HTML front-end error page on BOTH
`tasks.googleapis.com` and `www.googleapis.com`. The `@me` form
(`/tasks/v1/users/@me/lists`) returns 200. Task-level paths
(`/tasks/v1/lists/{id}/tasks`) are unaffected. Vendor-side regression
between 2026-08-24 and 2026-08-26; caught by the P3.f live checkpoint
(invariant 1 vindicated: mock-green could never see this).

Backend consequence (fixed same commit): `GoogleTasksBackend::loadTaskLists`
walks `/v1/users/@me/lists`; mock serves the @me path. ALSO fixed per the
B2C version-less-base doctrine: the ctor base was
`https://tasks.googleapis.com` (missing the `/tasks` version prefix
entirely — every live request would 404); now
`https://tasks.googleapis.com/tasks` with `/v1/...` authored verbatim.

### O76 — OPEN — B2C P3.f live ground truth, 2026-08-26: v1.0 todoTask wire property is `title`, NOT `subject` (create REQUIRES it)

The todoTask backend, mock, and mock suite were all modeled with
`subject` as the display property. Live truth against the real Outlook.com
account: create WITHOUT `title` ⇒ 400 invalidRequest "The property 'title'
is required when creating the task entity"; listings deliver `title` and
never `subject`. The EEE canon edge (`MsTodoTaskToCanonStage`) already
read `title` correctly — only the B2C transport layer drifted.

Backend consequence (fixed same commit): displayName extraction reads
`title` (defensive `subject` fallback for cached legacy copies); mock +
mock suite + P3.f live suite pin `title`.

### O77 — OPEN — B2C P3.f live ground truth, 2026-08-26: todoTask open-extension ids use microsoft.graph.openTypeExtension.* prefix; an OutlookServices-prefixed Id filter 500s on /me/todo expand

Nav POST of an open extension to
`/v1.0/me/todo/lists/{id}/tasks/{taskId}/extensions` mints extension id
`microsoft.graph.openTypeExtension.kalburator.canon` — NOT the contacts'
`Microsoft.OutlookServices.OpenTypeExtension.*` form (O66 correction was
contacts-scoped). Deterministic curl matrix (3× each): filtered expand
`extensions($filter=Id eq 'Microsoft.OutlookServices.OpenTypeExtension.kalburator.canon')`
⇒ HTTP 500 generalException EVERY time; the same filter with the
microsoft.graph prefix (or any other string) ⇒ 200. Carrier persistence
verified end-to-end: nav POST → `$expand=extensions` read-back delivers
the row (create echo remains a wire-lie).

Backend consequence (fixed same commit): `kCanonExtensionId` and the
expanded-listing filter switched to the microsoft.graph prefix; mock +
suites re-pinned.

### O78 — RESOLVED (IP.2, 2026-09-01) — incidence-parity recon, 2026-08-29: the calendar canon catalogue drifted from the shared VTODO emitter — three keys are emitted into `{calendar,canon}` that the calendar catalogue never declares

`src/calendar/icalcanonstages.cpp:56` promotes VTODO through
`Kalburator::Todo::todoFieldsToCanon()` and `:83` demotes through
`Kalburator::Todo::canonObjectToVtodoBytes()`. The calendar domain
**shares** the todo emitter; it does not have its own. The vtodo-parity
campaign added `providerExtrasDigest` (O74), `seriesSplitOf` (W3), and
`completionAnchor` (W4) to that shared emitter and catalogued them only in
`todocanonproperties.cpp`. `makeCalendarCanonCatalogue()`
(`calendarcanonproperties.cpp`) declares none of the three — verified: zero
occurrences of `providerExtrasDigest`, `seriesSplitOf`, or
`completionAnchor` under `src/calendar/`.

Two consequences, both live on `fc1ae61`:

1. `CanonJsonDiffer` compares catalogued ids only, so a change confined to
   any of the three on a `{calendar,canon}` vtodo-kind record never dirties
   the diff.
2. **The merger silently discards them.**
   `src/shape/canonjsonmerger.cpp:29` starts `QJsonObject out = t` and
   overrides only ids in `m_properties`. An uncatalogued key therefore
   takes the **target's** value unconditionally on every merge, and the
   differ never surfaced the change that would have exposed it.

**Live blast radius, stated honestly (verified 2026-08-29).** Only one of
the three keys costs anything today:

- `completionAnchor` — **live**, narrowly. `vtodocanonfields.cpp:373`
  gates it on `X-ORG-REPEATER` being present on the source, so it bites an
  org-repeater VTODO arriving through a CalDAV *calendar*.
- `seriesSplitOf` — **latent**. Only `todoseriessplitter.cpp:203`
  (`splitSeriesAtInstant()`) writes it, and W3 landed that host-invoked and
  unwired by decision — its only callers are in
  `tests/todo/tst_todo_series_split.cpp`. Becomes live when W3 is wired to
  the engine.
- `providerExtrasDigest` — **benign now**. Calendar ignoring it leaves that
  leg exactly where it was pre-O74, which is O80's gap rather than a new
  regression; a stale merged digest is derived and recomputed at the next
  promote.

So this is a small fix with a large lesson. It IS a regression introduced
by the vtodo-parity campaign — which correctly scoped itself to todo but
edited an emitter the calendar domain also uses — and it is NOT an
emergency. Do not let the second fact excuse the first: the value of
fixing it is that the instance stops masking the class.

Root cause is the class, not the instance: a canon catalogue and its
emitters are two independent sources of truth about the same key set with
nothing enforcing agreement. `tests/calendar/tst_calendar_kind_dispatch.cpp:176-186`
is the drift's own tombstone — it hand-lists exactly four union keys
(`due`, `completed`, `percentComplete`, `relatedTo`), was never updated,
and passed green throughout.

Owned by the incidence-parity campaign: **IP.1** pins it red, **IP.2**
closes the instance, **IP.3** closes the class (contributed catalogues).
See `docs/campaign/incidence-parity/PLAN.md`.

**RESOLVED 2026-09-01 by IP.2** — `calendarcanonproperties.cpp` now
declares all three keys in its "Union across iCalendar component kinds"
block, matching `todocanonproperties.cpp`'s kind and display name exactly.
IP.1's `QEXPECT_FAIL` is removed and
`calendarCatalogueDeclaresVtodoKeys()` is green. Three new slots in
`tests/shape/tst_canonjson_diff_merge.cpp` pin the merger and differ
halves against the **real** `calendarCanonPropertyIds()` (not a hand-listed
set — a hand list is what let the drift survive). The *class* remains open
until **IP.3**: nothing yet forces a catalogue to track its emitter. See
`docs/campaign/incidence-parity/2026-09-01-ip2-return-receipt.md`.

### O79 — OPEN — incidence-parity recon, 2026-08-29: VEVENT alarm promote corrupts absolute-trigger and END-related VALARMs to a bogus `offset: 0` — and three more call sites read the same row shape

`src/calendar/eventcanonfields.cpp:374` reads
`alarm->startOffset().asSeconds()` unconditionally.
`KCalendarCore::Alarm`'s `hasTime()` / `hasEndOffset()` /
`hasStartOffset()` are mutually exclusive (probe-confirmed 2026-08-28 under
W5), so `startOffset()` on an absolute-trigger (`TRIGGER;VALUE=DATE-TIME:`)
or END-related (`TRIGGER;RELATED=END:`) alarm returns a meaningless
default that promote writes into canon as `offset: 0`. The alarm silently
becomes a zero-offset start-relative alarm.

This is the exact bug W5 fixed on the todo leg
(`vtodocanonfields.cpp:394-409` branches on trigger form; the comment at
`:389-392` names it "a real bug fix bundled in"). It is
**Reversible→silently-wrong**, not a declared loss — nothing in the
calendar loss profile declares it, because the VEVENT side was outside
vtodo-parity's charter and nobody compared until now.

**Fixing promote alone would make VEVENT round-tripping worse.** Four sites
read the row shape and all must move in one commit:

| Site | Defect |
|---|---|
| `eventcanonfields.cpp:366-379` promote | unconditional `startOffset()` (above) |
| `eventcanonfields.cpp:662-675` demote | unconditional `setStartOffset()`; ignores `at`/`related`/`repeatCount` — an `at`-carrying row would demote to an alarm with no trigger at all |
| `mseventcanonstages.cpp:1211-1232` | `a.value("offset").toInt()` ⇒ **0** on an `at`-shaped row, which then passes `offsetSecs <= 0 && offsetSecs % 60 == 0` and maps to `reminderMinutesBeforeStart: 0` — an absolute alarm silently becomes "remind at start" instead of falling to the carrier |
| `googlecanonstages.cpp:345-367` | same reader shape |

The last two are latent today and go live the moment promote is fixed.

VEVENT's canon `alarms` shape also never received W5's REPEAT/DURATION
pairing (`repeatCount`/`repeatIntervalSecs`) or the unified `related` key —
it is still exactly the pre-W5 todo shape. VJOURNAL takes no VALARM per
RFC 5545 and is correctly unaffected.

Owned by incidence-parity **IP.4** (shared `alarmshape` module +
`describeAlarmRow()` so a vendor leg asks rather than inferring from a
defaulted zero).

### O80 — OPEN — incidence-parity recon, 2026-08-29: calendar and contacts differs are still blind to provider-extras-only edits (the O74 follow-through O74 itself predicted)

`calendardomaindefinition.cpp:33` and `contactsdomaindefinition.cpp:29`
construct `CanonJsonDiffer` over their catalogued ids only, and neither
catalogue declares `providerExtrasDigest` — verified: zero occurrences
under `src/calendar/` or `src/contacts/`. O74's own text predicted this
("Same shape presumably holds for any domain whose differ is
catalogue-scoped"); the prediction was never turned into a tracked item.

Consequence: a sync whose only change is a vendor X-property or
provider-extras edit on an **event, journal, or contact** does not dirty
the differ and does not propagate. Byte-level/raw-bytes backends (CalDAV
blob view) are unaffected — the gap is specific to canon-diff-mediated
sync. VJOURNAL is included: `journalcanonfields.cpp:102-111` stashes
`providerExtras["x-ical"]` with no digest.

**Scope is exactly three domains.** `note` (`TextDiffer`), `outline`
(`OutlineDiffer`) and `blob` (`RecordDifferBlob`) do not use
`CanonJsonDiffer` and are structurally immune.

The fix must NOT be a fourth/fifth/sixth copy of the todo call sites.
`CanonEnvelope::canonicalDigest()` is already domain-neutral; the lift is
an envelope-level `stampProviderExtrasDigest(obj, volatileKeys)` used
everywhere, with the three todo sites retrofitted onto it. The
volatile-key filter is per-vendor and mandatory — an unfiltered digest is
spuriously always-dirty and worse than none (todo's lists: Google `etag`;
MS `@odata.etag`/`lastModifiedDateTime`/`@odata.context`; calendar and
contacts lists must be derived from real captured payloads, not assumed to
transfer).

Owned by incidence-parity **IP.5**.

### O81 — OPEN — incidence-parity recon, 2026-08-29: VEVENT promote has no malformed DTSTART/DTEND coercion (the W6.2 twin)

`src/calendar/eventcanonfields.cpp` contains no DATE-vs-DATE-TIME
reconciliation — zero hits for `coerc`/`malformed`, against rules (a)/(b)
at `vtodocanonfields.cpp:229-278`. A VEVENT with `DTSTART;VALUE=DATE` and
a DATE-TIME `DTEND` (or the reverse) promotes today with a type-mismatched
pair.

**W6.2's rule (a) must not be mirrored blindly.** It resolves the mismatch
by letting DUE's type win — recorded in the VP.f return receipt as a
*deliberate divergence* from tasks.org's symmetric rule, adopted because
the vtodo-parity response doc was binding on that point. No such document
constrains VEVENT, and DTSTART/DTEND is a different relationship from
DTSTART/DUE: DTEND is a bound derived from DTSTART, so the symmetric
argument is weaker and DTSTART-wins is likely correct. Probe
KCalendarCore, decide on evidence, and write the rule into a contract doc
before implementing.

Owned by incidence-parity **IP.7b**.

### O82 — OPEN — incidence-parity recon, 2026-08-29: VEVENT demote still unconditionally re-emits RANGE=THISANDFUTURE (the W3 twin)

`src/calendar/eventcanonfields.cpp:594-596` executes
`event->setThisAndFuture(range == QStringLiteral("thisAndFuture"))`
unconditionally on demote. Re-emitting `RANGE=THISANDFUTURE` is
write-hostile on real servers; `vtodocanonfields.cpp:740` now refuses to
(VP.e), and `recurrenceRange` became a `Degraded` loss row on the todo
edges.

Flagged in `docs/campaign/vtodo-parity/STATUS.md`'s VP.e row as "VEVENT-side
twin bug flagged, NOT fixed" — correctly out of that campaign's todo-only
scope. Promoted to its own numbered finding here because it is the fifth
instance of one pattern (todo fixed, calendar's known-identical bug left
open) and a STATUS-doc note has outgrown the load.

Confirmed still true on `fc1ae61`. Note that VP.e had to rewrite a pinned
test that asserted the old behaviour
(`vtodoRoundTripPreservesThisAndFutureRange` →
`vtodoDemoteNeverEmitsThisAndFutureRange`) — grep for the event-side twin
before assuming none exists.

Owned by incidence-parity **IP.7a**.

### O83 — OPEN — incidence-parity recon, 2026-08-29: VTODO is the poorest-covered incidence kind in the library — poorer than VJOURNAL — and every drop is undeclared

`src/todo/vtodocanonfields.cpp` has **zero** references to `revision()`,
`secrecy()`, `url()`, `organizer()`, `attendees()`, `attachments()`, or
`color()`. All seven are valid on a VTODO per RFC 5545. All seven are
promoted for VEVENT (`eventcanonfields.cpp:163,226,303,321,328,339,385`).
Four of the seven — SEQUENCE, CLASS, COLOR, URL — are promoted even for
VJOURNAL (`journalcanonfields.cpp:64,91,92,94`), the least-attended kind.

So a VTODO loses SEQUENCE, CLASS, URL, ORGANIZER, ATTENDEE, ATTACH and
COLOR on **both** its canon paths (`{todo,canon}` and `{calendar,canon}` —
they share one emitter, see O78), while a VJOURNAL carrying the same
properties keeps four of them.

**None of these drops is declared in any loss profile.** They cannot be:
`LossProfile::affected` is keyed by `PropertyId`, and an uncatalogued key
has no id to key on. A consumer therefore has no way to learn about them —
which violates the EEE doctrine's "loud about limits" clause
(`docs/campaign/eee/2026-08-24-reconnaissance-assessment-and-roadmap.md`
Part IV). Undeclared loss is the defect here, as much as the loss itself.

Related: O41's literal-presence CREATED/LAST-MODIFIED guard exists in
three independent copies and was fixed late in one — `journalcanonfields.cpp:53`
records that journal "never got the same guard." Same root cause, same fix.

Owned by incidence-parity **IP.6** (`incidencecommonfields` extraction
first as a pure no-behaviour-change commit, then the missing VTODO fields
as a separate commit, then honest loss declarations on the Google Tasks
and MS To-Do legs).

### O84 — OPEN — IP.2, 2026-09-01: `CanonJsonMerger` erases `_canon.kind`, so a merged calendar VTODO or VJOURNAL demotes as a **VEVENT**

Found while building IP.2's merger regression slot; logged and **not
fixed**, per PLAN.md §1's "no fix while passing through" prohibition.

`CanonJsonMerger::merge()` finishes with
`CanonEnvelope::stampEnvelope(out, m_domain, mergedUid)`
(`src/shape/canonjsonmerger.cpp:60`) — the **3-arg** overload, no `kind`.
`stampEnvelope` builds a *fresh* `_canon` object
(`src/shape/canonenvelope.cpp:27-32`) and inserts `kind` only when the
argument is non-empty, so it does not merely fail to set the kind — it
**erases** the one the record arrived with.

`CanonToICalStage::transform()` then reads `CanonEnvelope::kind(obj)` and
treats an absent kind as `vevent` for v1 back-compat
(`src/calendar/icalcanonstages.cpp:85`). A merged `{calendar,canon}` VTODO
therefore demotes to a VEVENT.

**Verified empirically, not inferred** (2026-09-01): merging a
kind-tagged `vtodo` record through the live calendar merger
(`calendardomaindefinition.cpp:38` builds it with
`calendarCanonPropertyIds()`) and feeding the result to
`CanonToICalStage` produced:

```
BEGIN:VCALENDAR
...
BEGIN:VEVENT
DTSTAMP:20260901T184859Z
UID:t-4
SUMMARY:edited
TRANSP:O...
```

**Blast radius.** Strictly the `calendar` domain, and only on paths that
merge — i.e. conflict resolution with a baseline. `todo`, `contacts`,
`note`, `outline` and `blob` are single-kind domains whose demote stages do
not kind-dispatch, so the erased key has nothing to change there. Within
`calendar` it hits **both** VTODO and VJOURNAL; VEVENT is unaffected
because absent-kind already means vevent. This is strictly worse than O78:
O78 dropped three field values, O84 changes the component type of the
whole record.

The other `stampEnvelope` callers are correct: the vendor event legs
(`googlecanonstages.cpp:529`, `mseventcanonstages.cpp:801`) deliberately
omit the kind because they only ever carry VEVENTs, and
`icalcanonstages.cpp:65` omits it for vevent on purpose to keep v1
baselines byte-stable.

Pinned by `mergerPreservesIncidenceKind()` in
`tests/shape/tst_canonjson_diff_merge.cpp` — landed as a two-assertion
`QEXPECT_FAIL` (consequence first: the demoted bytes are not `BEGIN:VTODO`;
then the symptom: `kind(o)` is empty). When the fix lands, both XPASS and
the slot must lose its `QEXPECT_FAIL`s rather than be deleted.

**Not yet owned by an item.** The natural home is **IP.3**, which already
touches the catalogue/envelope seam and gates IP.4–IP.7; the fix itself is
likely one line (thread `CanonEnvelope::kind(t)`, falling back to
`kind(s)`, into the stamp) plus a decision about whose kind wins when
source and target disagree — which is a real question, not a formality,
since a kind mismatch on one uid means something upstream is already
wrong. Whoever takes it should decide that explicitly and write it down.


### O85 — OPEN — incidence-parity pre-flight audit, 2026-09-02: every VALARM round-tripped through canon comes back **disabled**

Found by the 2026-09-02 pre-flight audit
(`docs/campaign/incidence-parity/2026-09-02-preflight-audit.md` §2.3), not
by an item passing through. Logged, not fixed — owned by **IP.4**.

`KCalendarCore::Alarm::enabled()` defaults to **false** on a
default-constructed alarm (probe-confirmed: `probes/kcalendarcore-probe.cpp`
section C). All four alarm seams construct `new Alarm(...)` and never call
`setEnabled(true)`, and no promote ever records `enabled()`:

| Site | File |
|---|---|
| VEVENT promote / demote | `src/calendar/eventcanonfields.cpp:366-379`, `:662-675` |
| VTODO promote / demote | `src/todo/vtodocanonfields.cpp` (W5 block) |

Measured round trip, both kinds:

```
SOURCE enabled=1 → demoted carries X-KDE-KCALCORE-ENABLED:FALSE → reparsed enabled=0
```

**Blast radius, stated honestly.** `X-KDE-KCALCORE-ENABLED` is a KDE
extension; non-KDE clients ignore it and the `TRIGGER` still stands, so a
CalDAV server and a phone will still fire the reminder. For Akonadi /
KOrganizer round trips the reminder silently stops firing. It also makes
demoted bytes gratuitously differ from source bytes for every
alarm-bearing incidence.

Note this is **independent of O79**: VP.f's W5 corrected the VTODO trigger
form and still leaves every VTODO alarm disabled. Fixing O79 without O85
would leave the same user-visible symptom. IP.4 must close both.

### O86 — OPEN — incidence-parity pre-flight audit, 2026-09-02: KCalendarCore 6.29.0 serializes `GEO` corrupt, so we emit malformed iCal

**Upstream**, not ours — reproduces with no libkalburator in the picture
(`probes/kcalendarcore-probe.cpp` section A). Owned by **IP.6**.

```cpp
todo->setGeoLatitude(1.5f); todo->setGeoLongitude(2.5f);
// accessors read back 1.5 / 2.5 correctly
ICalFormat().toICalString(todo)  →  GEO:2.5;<uninitialized bytes>
```

The latitude slot receives the **longitude**; the longitude slot receives
uninitialized memory whose bytes are not valid UTF-8 and differ between
runs. `Event` and `Todo` are affected identically. libical then refuses its
own output on re-parse:

```
icalvalue_new_from_string cannot parse value string (GEO) for '2.5;...'
```

**What this costs us today.** `src/todo/vtodocanonfields.cpp:443-447`
promotes `GEO` into canon and `:793-798` demotes it back, so the VTODO leg
writes malformed `GEO` lines to real servers, and VTODO
promote→demote→promote is **not a fixpoint** — the audit measured
`canon-stable=NO` for VTODO, with `geo` present before and absent after.
The VEVENT leg never promotes `geo` at all (the calendar catalogue declares
it; `eventcanonfields.cpp` never emits it), so it merely drops it.

**Not fixable in an emitter.** IP.6 must choose deliberately and record the
choice: either hand-serialize the `GEO` line (bypassing
`ICalFormat::toICalString`, in the style of the existing
`stripICalPropertyLine` post-processing) or stop emitting `geo` and declare
it `Dropped`. Do not "fix" it by round-tripping through the broken
accessor pair. Re-verify against the installed kcalendarcore version first
— this is a property of 6.29.0, and an upgrade may retire it.

### O87 — OPEN — incidence-parity pre-flight audit, 2026-09-02: VJOURNAL's undeclared drops, including `RECURRENCE-ID` identity aliasing

The VJOURNAL twin of O83, and worse in one respect. Owned by **IP.10**.

Measured (`probes/incidence-audit-probe.cpp` section 2): a maximal
RFC 5545 VJOURNAL loses seven properties across
`{calendar,canon}`, none of them declared in any loss profile:

```
ATTACH, ATTENDEE, EXDATE, ORGANIZER, RECURRENCE-ID, RELATED-TO, RRULE
```

`src/calendar/journalcanonfields.cpp` has no handling for any of them —
zero references to `recurrence`, `recurrenceId`, `organizer`, `attendees`,
`attachments`, or `relatedTo`.

**`RECURRENCE-ID` is the serious one.** Dropping it means a detached
journal instance and its master promote to canon objects that differ in no
identifying way — two records collapse onto one uid. That is identity
corruption, not field loss, and it puts VJOURNAL below VTODO on the very
axis (composite exception identity) that vtodo-parity's W1 was built to
fix. `RRULE`/`EXDATE` dropping means a recurring journal is silently
flattened to a single entry.

Note the interaction with **O88**: `canonToVjournalLoss()` returns an empty
profile commented *"VJOURNAL maps its full field-set; no non-reversible
loss to declare"*. That comment is false, and the function is dead code
besides — so nothing warns the user. IP.9 (O88) must land before IP.10 can
declare these honestly.

PLAN.md's IP.6 acceptance says only "VJOURNAL keeps every field it has
today". That was written before this measurement and is now insufficient:
VJOURNAL needs gap-closing of its own, not just regression protection.

### O88 — OPEN — incidence-parity pre-flight audit, 2026-09-02: one edge-level loss profile serves three incidence kinds; `canonToVjournalLoss()` is dead code

Structural. Owned by **IP.9**, which gates IP.4/IP.6/IP.10.

`CalendarStockShapes::edges()`
(`src/calendar/calendarstockshapes.cpp:92-96`) registers **one**
`{calendar,canon} → {calendar,ical}` edge, carrying `canonToIcalLoss()`.
That profile is entirely event-shaped: `onlineMeeting`, `eventType`,
`typedProperties`, `guestsCan*`, `allowNewTimeProposals`, `hideAttendees`,
`locked`, `privateCopy`, `freeBusyStatus`, `responseRequested`.

But the edge is **kind-polymorphic** — `CanonToICalStage::transform()`
dispatches on `_canon.kind` to three different emitters
(`icalcanonstages.cpp:81-88`). So:

- `materializedLoss()` (`syncengine.cpp:4635`, `:4675`) runs the *event*
  profile over VTODOs and VJOURNALs. A user demoting a VTODO is warned
  about `guestsCanModify` and told nothing about losing `ATTENDEE`,
  `ORGANIZER`, `SEQUENCE`, `CLASS`, `URL`, `COLOR`, `ATTACH`.
- `canonToVjournalLoss()` (`journalcanonfields.cpp:214`) exists, is
  declared in the header, returns an empty profile — and has **zero call
  sites**. Grep-confirmed. It is dead code whose comment asserts something
  false.
- The convergence matrix inherits the same distortion: it reports the
  calendar `ical` leg as though every record on it were a VEVENT.

**The design mismatch, stated plainly:** the loss-profile system's unit is
the *edge*, but the calendar `ical` encoding is a **union of three
schemas**. Either the edge splits per kind, or `LossProfile` gains a kind
dimension, or the profile becomes a function of the record rather than a
constant of the edge. IP.9 must pick one and justify it — this is the
decision that makes O83's and O87's "declare the drops honestly" acceptance
criteria actually expressible. Until it lands, any item that "declares a
loss" for VTODO or VJOURNAL has nowhere truthful to put it.

### O89 — OPEN — incidence-parity pre-flight audit, 2026-09-02: VTODO has two canonical representations, selected by transport metadata

The consumer-visible one. Owned by **IP.11**; needs PlanStan ratification
(`docs/2026-09-02-incidence-parity-planstan-report.md`).

A VTODO reaches one of two canonical shapes depending on where it lives:

| Path | Catalogue | Loss profile | Gets |
|---|---|---|---|
| `{todo,canon}` | 27 keys, `todocanonproperties.cpp` | `canonToVtodoLoss()` — thorough, 10 declared rows | all of vtodo-parity W1–W7 |
| `{calendar,canon}` | 46 keys but event-shaped | `canonToIcalLoss()` — event-only (O88) | 7 undeclared drops (O83), O84, O86 |

Which one is decided by transport metadata, not by the data:

- `MultiProtocolDavProvider::backendSpecs()` demuxes into a `todo` spec
  **only** when a collection advertises `VTODO` in its
  `supported-calendar-component-set`
  (`src/sync/multiprotocoldavprovider.cpp:214-226`). A server that does not
  advertise `contentTypes` takes the "legacy shape" branch and every
  component — VTODOs included — rides `{calendar,canon}`.
- `LocalBackend`, `DecSyncBackend`, `OrgBackend` and `AkonadiBackend` each
  return only `{calendar, ical}` from `nativeShapes()`. They never demux
  under any configuration, so a VTODO in a local `.ics`, in Akonadi, in
  DecSync or in an org file **always** takes the impoverished path.

So the same task, synced from a well-advertising Radicale, gets W1's
composite exception identity, W4's completion anchors and O74's extras
digest; synced from a local file it gets none of them and loses its
`ORGANIZER` silently.

**The demux itself is sound** — the two filtered views are disjoint
(`VEVENT`+`VJOURNAL` vs `VTODO`), so nothing is double-counted, and the
"transport grouping never crosses a domain boundary" rectification rule
holds. The defect is that the *fallback* is silent and the non-DAV backends
have no route to the good representation at all.

This is a contract question, not just a bug: closing it either promotes
`{calendar,canon}` VTODO to full parity (making the two representations
equivalent) or routes all VTODOs to `{todo,canon}` (making one of them
disappear). The second changes which domain a consumer sees a task in.
**libkalburator should not choose alone** — see IP.11.

### O90 — OPEN — incidence-parity pre-flight audit, 2026-09-02: demote is not a pure function of canon (attendee `X-UID`)

Low severity, cheap fix, recorded so it is not rediscovered. Owned by
**IP.12**.

`KCalendarCore::ICalFormat` stamps a heap-address-derived `X-UID` parameter
into every serialized `ATTENDEE`:

```
process A:  ATTENDEE;CN=A;...;CUTYPE=INDIVIDUAL;X-UID=93826400444256:mailto:a@example.com
process B:  ATTENDEE;CN=A;...;CUTYPE=INDIVIDUAL;X-UID=94004632973840:mailto:a@example.com
```

Stable within a process (two demotes of the same canon are byte-identical),
different across processes. So `demote(canon)` is not a function of `canon`
alone.

**Do not dramatise this.** It does *not* cause a write storm today: the
differ works on canon, which has no `X-UID`, and the engine's skip cache
compares each backend's own `contentHash` of the bytes actually *stored*
(`syncengine.cpp:3700-3712`), so an unchanged record is never rewritten.
What it does cost is real but narrow — demoted output is not reproducible
across runs, servers accumulate meaningless per-process identifiers, and
any future byte-pin or content-addressed optimisation over demoted bytes is
impossible. The likely fix is a post-serialization parameter strip in the
style of the existing `stripICalPropertyLine` calls.
