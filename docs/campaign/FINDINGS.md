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
> **Sync-excellence campaign OPENED 2026-07-07.** The live phase plan is
> **`docs/campaign/2026-07-07-sync-excellence-phases.md`** (phases E1–E10 +
> CP-A/B/C; tags v0.85 optional mid-campaign, v0.90 at close). It owns the
> open findings **O26, O28** and the newly seeded **O29–O36** below (the
> hardening campaign's parked backlog plus the Discipline Log's accumulated
> debts, promoted to numbered findings).

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

### O42 — first sync of each app process never uses `sync-collection`: the fetch races the supported-report-set probe, and the capability is in-memory only (OPEN, found 2026-07-09 at CP-B; efficiency only — decide at CP-C's efficiency audit, candidates E10/E11)

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

### O45 — CalDAV create jobs against the H8 scratch-Radicale rig time out 100%, every retry, with zero successes (OPEN, found 2026-07-09 during E13's live spot-check; efficiency/correctness triage needed, not yet scoped to a phase)

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
