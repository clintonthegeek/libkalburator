# First-principles audit — what the sync engine and backends SHOULD do, and where the code diverges

**Date:** 2026-07-05
**Author:** Claude (Fable 5), independent re-derivation session
**Provenance:** commissioned as a trust-but-verify pass over the D1 threading
campaign after the T1.5 stall blocker
(`docs/campaign/archive/2026-07-05-d1-t1.5-stall-blocker-analysis.md`). Nothing in that document was
taken on faith; every claim below was re-verified by reading the code on
`feature/d1-threading` @ `6a4af95` and, where marked, by executing tests.
**Line numbers** are against that revision; every reference names the
enclosing symbol — if a line drifts, grep the symbol.

**Verdict up front:** the D1 relocation architecture is sound and should
proceed, but the campaign's bug inventory was materially incomplete. This
audit found **one data-stranding correctness bug with no threading component
at all (A1)**, a second change-masking bug in the same subsystem (A2), **two
unmarshaled cross-thread call sites the viability audit missed (B2, B3)** —
one of which is undefined behavior in production *today*, pre-D1 — a per-cycle
memory leak (C1), a doubled fetch pipeline (C2), an unbounded-freeze /
permanent-sync-wedge failure mode (B4), and an unmanaged re-entrancy hazard
that is the most plausible root cause of the historical N5 cache corruption
(B7). Findings are grouped by theme, each with severity, evidence, and
verification status (**READ** = verified by code reading; **EXEC** = verified
by running code).

---

## 1. The first-principles model

Derived from what the system is for — converge N replicas of calendar
collections across unreliable transports without losing changes and without
blocking a GUI — before looking at how the code does it. Four roles:

### 1.1 Backend — a single-threaded, asynchronous I/O service

- **Thread-affine:** all backend state (QNAM, SQLite stores, caches, maps)
  lives on the backend's thread; every public entry executes there.
- **Asynchronous:** operations return a handle immediately and complete via
  signals/continuations. A backend method **never runs a nested event loop**
  — a nested loop on the backend thread executes *other queued calls in the
  middle of the current operation*, which is concurrency with none of the
  protection (see B7).
- **Serialized:** operations against the same collection execute in FIFO
  order (an op queue), so a fetch can never interleave with a push.
- **Owns its cache-validity token:** a backend that maintains a content
  cache (CTag/ETag, fingerprint) commits that token **atomically with the
  data observation it validates** — at fetch completion, verified against
  what actually materialized. `RemoteCalendarBackend` already does exactly
  this (the N5 fix). This token answers one question only: *"is my cache a
  faithful copy of the collection at this token?"*

### 1.2 Engine worker — the pipeline state machine

- Runs the per-mapping pipeline (fetch → promote → diff → merge → apply →
  baseline) on a dedicated thread. No I/O of its own.
- Waits on backends only via **cancellable, timeout-bounded awaits** of
  operation handles. `Qt::BlockingQueuedConnection` is acceptable only for
  short, non-I/O calls; anything that can touch a network or a large
  directory must be awaited as an operation so cancellation and timeouts can
  interrupt it.

### 1.3 Engine facade — the GUI-thread surface

- Intake and reporting only: validate the request, enqueue, hand back a
  future, forward progress/completion signals. **Never calls a backend,
  never blocks.** The skip-unchanged pre-pass is *step 0 of the pipeline*
  (worker-side), not a facade pre-pass — it does I/O, so it cannot live on
  the caller's thread.

### 1.4 Sync-progress bookkeeping — owned by the engine, scoped per mapping

The decision *"can this mapping be skipped?"* requires knowing **which
revision of each side the mapping last successfully consumed**. That is
per-*mapping* state (two mappings sharing a source collection progress
independently) and it is *engine* state (only the engine knows whether the
mapping's run succeeded end-to-end). Therefore:

- The fetch step reports the revision token it observed **atomically with
  the records** it returned.
- The engine persists `(mappingId, side) → token` in its own store (the
  BaselineStore already plays exactly this role for record baselines) **only
  when the mapping completes successfully**.
- The fast path compares a fresh token (cheap backend query, worker-side)
  against that per-mapping stored token.
- The engine **never writes tokens back into the backend**. The backend's
  cache-validity token and the engine's sync-progress token are different
  facts with different lifecycles; conflating them is the root of A1/A2/A3
  below.

Everything in §§2–4 is a diff of the code against this model.

---

## 2. Theme A — the skip-token subsystem is unsound (no threading required)

The current design stores one token per (backend, collection) inside the
backend (`CTagStore` / `FingerprintStore`), written by **two independent
writers** — the backend itself at fetch time, and the engine's
`persistRevision` at mapping-completion time — and read by the engine's
`prepareSyncFastPath()` as if it meant "this mapping is up to date." It
means no such thing.

### A1 — HIGH — a failed apply phase strands changes behind the skip (READ)

`RemoteCalendarBackend::fetchItems` commits the fresh CTag to the persisted
store **at fetch time**, once every listed item has materialized
(`remotecalendarbackend.cpp:1598-1607` and `:1791-1794`). That is correct
*for the content cache* — the cache really is faithful at that token. But
the engine's skip check (`syncengine.cpp:744-758`, `checkSide` in
`prepareSyncFastPath`) compares fresh-vs-stored token with **no reference to
whether the mapping's last run succeeded**:

```
unchanged = !outRevision.isEmpty() && !stored.isEmpty() && outRevision == stored;
eligibleToSkip = sourceCovered && targetCovered && sourceUnchanged && targetUnchanged;
```

Failure sequence, all steps verified in code:

1. Server collection changes (ctag `R1` → `R2`).
2. Cycle N runs the mapping: fetch succeeds and **commits `R2`**; the apply
   phase then fails having written nothing to the target (server 5xx on
   PUT, disk full before the first write, transient auth failure —
   `dispatchSync` fails the mapping, `onWorkerSyncCompleted` sees
   `result.success == false`, so `persistRevision` is skipped — but the
   backend's own `R2` commit already happened and nothing rolls it back).
3. Cycle N+1 fast path: source fresh `R2` == stored `R2` → `sourceUnchanged`.
   Target wrote nothing, so its fingerprint also matches →
   `targetUnchanged`. **Mapping skipped.**
4. The `R1→R2` delta is never applied to the target until some *further*
   server change bumps the ctag. On a quiet calendar: indefinitely.

The same conflation bites the multi-mapping case: mapping A's fetch of
shared collection X commits X's token backend-globally; mapping B (same
source X, different target) that fails its own run still sees
"source unchanged" next cycle courtesy of A's commit.

`skipUnchangedMappings` defaults **on** in PlanStan (campaign track C), so
this is live in the shipping configuration. It requires no threads, no
races, and no D1 — one failed apply phase on an otherwise healthy account.

**Fix (per §1.4):** the fetch step returns `(records, token)`; the engine
persists the token per `(mappingId, side)` in BaselineStore on mapping
success only; the fast path compares against that. The backend's own
fetch-time commit stays — for the content cache, which is what it is
actually correct for. `ChangeDetection::cachedCollectionRevision` /
`primeRevisionCache` disappear from the engine's vocabulary.

### A2 — HIGH — LocalBackend's post-write re-hash masks concurrent foreign edits (READ)

`persistRevision` (`syncengine.cpp:1183-1203`, inside
`onWorkerSyncCompleted`) re-queries the **live** revision after the mapping
finishes and primes it as "seen". For `LocalBackend`,
`primeRevisionCache` writes **directly into the persisted FingerprintStore**
(`localbackend.cpp:188-192`) — no staging, no verification, unlike the
remote side's N5 discipline.

The fingerprint is a hash of the whole directory (`(name|mtime|size)`
tuples). The post-write re-hash therefore includes **any foreign
modification made to the directory between the fetch scan and the
completion callback** — a user editing an .ics in a text editor, another
sync tool, anything — and stamps it as already-synced. Next cycle: fresh
hash == primed hash → skip → the foreign edit is invisible until a *second*
local change perturbs the fingerprint. The exposure window is the entire
diff+apply phase of the mapping.

The B5 comment above `persistRevision` (`syncengine.cpp:1161-1176`)
motivates the live re-query as avoiding a one-cycle skip lag from
*self*-writes; it never considers *foreign* writes in the window. The sound
version of the same optimization: the backend computes the expected
post-write fingerprint **incrementally from its fetch-time snapshot plus its
own write set** (it knows exactly which files it wrote; it stats just
those). A foreign edit to any other file then correctly differs next cycle.

### A3 — MEDIUM — the remote half of persistRevision is inert by design, so B5's stated goal was never achieved for remote (READ)

For `RemoteCalendarBackend`, `primeRevisionCache` only stages into the
in-memory `pendingCtag` (`remotecalendarbackend.cpp:745-761`, the N5 fix)
and `cachedCollectionRevision` reads only the persisted store
(`remotecalendarbackend.cpp:740-743`). Consequence: after any cycle that
pushed to the server, the persisted token is the *fetch-time* ctag, the
live server ctag is the *post-push* one, they differ, and the next cycle
does **not** skip — precisely the one-cycle lag B5 set out to remove.
The staged value is superseded at the next fetch (`:1444-1447`) before it
can ever be committed, so the engine's whole live-re-query dance buys the
remote side **nothing** — while costing one full extra CTag PROPFIND per
mapping per cycle **executed on the GUI thread** (see B1).

So the B5 "fix" is inert exactly where it would be safe (remote, N5-guarded)
and effective exactly where it is unsound (local, A2). It should be deleted
in favor of the §1.4 contract, not repaired.

### A4 — LOW — the skip gate's documented baseline condition is not implemented (READ)

`syncengine.h:274-283` and the roadmap describe the skip as requiring
"a stored baseline exists." `prepareSyncFastPath()` checks only
`if (!m_baselineStore) return;` (`syncengine.cpp:731`) — a null-pointer
check, not a per-mapping `baselinesForMappingV3(...).isEmpty()` check. A
collection pair with matching tokens but wiped/absent baselines (e.g. after
a baseline-store reset) skips instead of resyncing. Becomes moot under the
§1.4 rework (tokens then live in the BaselineStore, so "no baseline" and
"no token" coincide structurally).

---

## 3. Theme B — threading and blocking

### B1 — HIGH, KNOWN (O16) — fast path + persistRevision block the GUI thread (EXEC)

Re-verified by running `tst_backend_thread_relocation`: 212 ms stall against
a 200 ms injected latency (O16 measured 213 ms). Two engine-side code paths
run on the caller's (GUI) thread and block on backend I/O through the
T1.4 `runOnBackendThread` marshals: `prepareSyncFastPath()`
(`syncengine.cpp:724,753`) and `persistRevision`
(`syncengine.cpp:1191,1196`). Under §1 this is a *role* violation, not a
tuning problem: the facade is doing pipeline work. Two aggravations beyond
O16's writeup:

- **Unbounded, not just "one round-trip":** none of these paths has any
  timeout (see B4), so on a stalled connection the GUI freeze is bounded
  only by OS TCP behavior — minutes, not 200 ms.
- **Worse than pre-D1, not equal:** pre-D1 the remote check waited in
  `davSyncRequest`'s nested `QEventLoop`
  (`remotecalendarbackend.cpp:248-268`), which kept processing GUI events
  (measured: 14 ms heartbeat gap un-relocated vs 212 ms relocated). The
  marshaled `BlockingQueuedConnection` processes **nothing**. Shipping D1
  with B1 unfixed is a responsiveness *regression* for this phase. (The
  pre-D1 GUI-jank story is the **local** side: `calendarFingerprint`'s full
  directory stat/hash scan is plain unyielding C++ on the GUI thread.)

Fix: move the pre-pass and completion-time token persistence onto the
worker (blocker doc Option A); A1/A3's rework removes persistRevision
entirely, which is half of the surface.

### B2 — HIGH, LIVE TODAY PRE-D1 — runPropertyPhase calls backends directly from the worker thread (READ)

`SyncEngineWorker::runPropertyPhase` (`syncengine.cpp:3043-3087`) — called
from `dispatchSync` at `:2104` on the **worker thread** — invokes
`ops->collectionProperties(src, …)` / `applyCollectionProperties(tgt, …)`
with **no marshaling whatsoever**. `CalendarDomainOperations`
(`calendardomainoperations.cpp:28-50`) turns those into direct virtual
calls on the backend: `calendarColor()` / `calendarDescription()`
(unsynchronized reads of `m_calendars`, racing the backend thread's own
mutations of that map during discovery/fetch) and
`syncBackend->updateCalendar(...)` — which for `RemoteCalendarBackend`
builds and sends a **PROPPATCH** (`remotecalendarbackend.cpp:1218+`),
i.e. network I/O from the worker thread on a QNAM owned by another thread.
Qt documents QNAM as usable only from its owning thread.

This is not a D1-created problem: **pre-D1 it is already a cross-thread
QObject access in production** (worker thread → GUI-thread backend). It
fires whenever a mapping's two sides disagree on calendar color or
description (the empty-props early-out at `:3058` is why it is quiet on
fresh test fixtures). Post-D1 the T1.1 `Q_ASSERT(currentThread ==
nam->thread())` upgrades it from silent race to loud debug crash. The
viability audit's "~19 call sites, all marshaled" inventory missed this
because the calls route through `DomainOperations`, not through a
`SyncBackendBase*` invokeMethod.

Fix: marshal each side onto its own backend's thread (read props on the
backend thread; apply on the backend thread), same pattern as every other
worker→backend touch.

### B3 — HIGH (post-D1) — dispatchFirstSync runs target-backend writes on the source backend's thread (READ)

`dispatchFirstSync`'s inline blob mirror (`syncengine.cpp:1786-1833`)
marshals **one** lambda onto `srcBackend`'s thread and, inside it, calls
`tgt->loadRecordsOrError(...)`, `tgt->createRecord(...)`,
`tgt->updateRecord(...)`, `tgt->deleteRecord(...)` — six target-backend
entry points executing on the **source's** thread. Pre-D1 (everything on
the GUI thread) and in T1.4's test (both backends deliberately moved to
one shared I/O thread) this is coincidentally same-thread. The moment
source and target have distinct affinities — per-backend I/O threads, or a
partial adoption that relocates only remotes — it is cross-thread access
to the target's QNAM/SQLite/caches. This is a 23rd unmarshaled site in the
same class as the three T1.4 found; it is invisible to grep because it
hides *inside* a correctly-marshaled lambda.

Fix: split into per-backend marshals (read source on source thread, read
target on target thread, write target on target thread) — or route
first-sync through the same RecordWriter path steady-state uses, which
already handles its own marshaling (`syncengine.cpp:2806-2830`).

If D1 standardizes on **one shared I/O thread for all backends** (the
current plan), B3 stays latent rather than active — but it must then be
written down as a hard invariant ("all sync backends share one I/O
thread"), because nothing in the code enforces it and per-backend threads
are the natural next step someone will take.

### B4 — HIGH — no timeouts anywhere ⇒ one stalled request permanently wedges sync, silently (READ)

`davSyncRequest` (`remotecalendarbackend.cpp:219-270`) sets no
`transferTimeout` and its nested loop has no watchdog; the engine's
blocking marshals and gate-await loops (`syncengine.cpp:2128-2141`) are
equally unbounded. Consequences compound:

1. A stalled connection (half-open TCP, unresponsive server) parks the
   backend thread indefinitely; the worker is parked in
   `BlockingQueuedConnection` behind it.
2. The mapping never completes; `m_isSyncing` never clears.
3. Every subsequent `runSync()` is rejected by the overlap guard
   (`syncengine.cpp:537-544`) — which returns a future that is
   **indistinguishable from a successful empty run** (finished,
   empty-list result, no error). PlanStan's 120 s cycle keeps "running"
   and silently does nothing forever. No log, no user-visible failure,
   until restart.
4. Cancellation cannot help (B5) and pre-D1 shutdown deadlocks (B6).

Fix: `setTransferTimeout` on the shared QNAM (T1.1 made this a one-line
change — one QNAM now exists); a per-operation deadline in the worker's
awaits; make the overlap-guard rejection distinguishable (failed future or
a `SyncResult` with an error), and log it.

### B5 — MEDIUM — cancellation cannot interrupt the waits that need it most; the gate never cancels its op (READ)

Cancellation reaches the worker as a **queued** event (`observeCancel`,
`syncengine.cpp:1459-1468`). The worker processes queued events only inside
the gate-await loops — not while parked in any of its ~20
`BlockingQueuedConnection` marshals, which is where the long operations
(`loadRecordsOrError` full fetches, the dispatchFirstSync mirror, batch
applies) actually run. So cancel takes effect only at phase boundaries;
against a slow or hung backend (B4) it never takes effect at all.
Additionally, when the gate loop *is* woken by cancellation, it returns
without calling `fetchOp->cancel()` (`syncengine.cpp:2143-2150`) — the
backend's job chain runs to completion in the background, wasting the
exact I/O the user cancelled. (`SyncEngineWorker::await<Op>` — the
carefully-documented helper that *does* call `op->cancel()` — has **zero
call sites**; the F2 Task 16 machinery it anchors is dead code.
`syncengine_p.h:269-300`.)

### B6 — MEDIUM — stopWorkerThread deadlocks if a backend call is in flight pre-D1; post-D1 it imposes an undocumented teardown order (READ)

`stopWorkerThread` (`syncengine.cpp:209-231`): `m_worker->cancel()` is a
direct cross-thread call that only sets a flag (it does **not** emit
`cancellationObserved`, so it wakes nothing); then `quit(); wait();`.
`QThread::quit()` cannot interrupt a `BlockingQueuedConnection` (a
semaphore wait, not an event loop). Pre-D1, backends live on the GUI
thread: if the worker is mid-marshal, it is waiting for the **GUI thread**
to run the lambda while the GUI thread is inside `wait()` — mutual wait,
deadlock on every mid-sync shutdown (CollectionController destructor).
Post-D1 the specific deadlock dissolves (the I/O thread services the
call), but only if teardown order is engine-first, I/O-thread-last —
an invariant PlanStan's Stage 3 must state and enforce, because reversing
it recreates the same hang.

### B7 — MEDIUM (design) — nested event loops on the backend thread admit uncontrolled re-entrancy; plausible root cause of the N5 corruption class (READ)

Every `davSyncRequest` call and `awaitOperation` spin a nested `QEventLoop`
**on the backend's thread** while a request is in flight. Nested loops
process *all* queued events — including other calls marshaled to that
backend. Any app-side backend use that overlaps a running sync (editor
save → CalDAV PUT, calendar-list refresh, provider mirroring) executes
**in the middle of** the in-flight operation's wait, interleaving
unguarded mutations of `pendingCtag`, `m_lastRawIcsByUid`, the shared
`EtagCache`, and the content cache. This is live today (nested loops on
the GUI thread) and survives D1 unchanged (same loops, I/O thread). A
half-updated EtagCache/content-cache pair produced by an interleaved PUT
is exactly the shape of the historical "CTag ahead of content cache" N5
bug — the campaign fixed its *symptom* (distrust + verified commit)
without this candidate *mechanism* ever being named.

Fix (D2 scope): backends serialize operations per collection (FIFO op
queue), and/or `davSyncRequest` becomes a continuation-style async call
like the KDAV job chains `fetchItems` already uses. §1.1's "no nested
loops" rule is the durable form.

---

## 4. Theme C — waste and leaks

### C1 — MEDIUM — the worker's gate FetchOperations leak, with their full fetched payload (READ)

The gate fetch (`syncengine.cpp:2122-2165` source, `:2245-2282` target)
obtains a `FetchOperation*` and never `deleteLater()`s it.
`registerOperation`'s finished-handler only **unregisters**
(`syncbackendbase.cpp:113-136`); it does not delete.
`RemoteCalendarBackend` creates ops **unparented**
(`remotecalendarbackend.cpp:1377`, `onOwnerThread(new FetchOperation(...))`)
— those are unreachable after the gate returns and leak until process
exit. `LocalBackend` parents ops to itself (`localbackend.cpp:693`) — those
accumulate as children until backend destruction. Both hold
`setFetchedItems(...)` payloads: **the entire collection's incidences,
retained per side, per mapping, per cycle** — ~720 leaked collection
snapshots per day at PlanStan's 120 s cadence. (The blob adapters'
own ops are fine — `loadRecords` deleteLater()s what it awaits,
`remotecalendarbackend.cpp:2203,2223`.)

### C2 — MEDIUM — every sync fetches everything twice and PROPFINDs the ctag ~4× (READ, corroborated by test logs)

Per mapping side, steady state: (1) engine fast path — CTag PROPFIND
(GUI thread); (2) worker gate `fetchItems` — CTag PROPFIND + list/multiget
into cache; (3) `loadRecordsOrError` → `loadRecords` → **`fetchItems`
again** (`remotecalendarbackend.cpp:2190-2224`) — another CTag PROPFIND,
cache-served; (4) `persistRevision` — another CTag PROPFIND (GUI thread).
The stall-probe log shows the pattern verbatim (two `Delta sync` lines —
`1 changed` then `0 changed … from cache` — bracketed by two
`fetchAllCtags` lines). For `LocalBackend` it is a **full directory
parse twice** — the gate's parse (results discarded into the leaked op,
C1) then `loadRecords`' parse. The gate exists solely as a cancellation
window (Phase Ib.5 Task 7 comment, `syncengine.cpp:2111-2120`); it costs
a full redundant fetch to provide it, and for local backends provides
nothing (C3). Under §1, "give me the records + token" is **one**
backend operation.

### C3 — MEDIUM — the cancellation gate doesn't gate LocalBackend at all (READ)

`LocalBackend::fetchItems` defers its body via `QTimer::singleShot(0,…)`
(`localbackend.cpp:697`), so the op returned through the
`BlockingQueuedConnection` is still **Pending**. The gate awaits only
`state() == Running` (`syncengine.cpp:2127,2250`) — Pending falls through,
no await, no cancellation window; the deferred full-directory parse then
runs anyway (into the leaked op, C1). The remote path passes the gate only
because its `invokeMethod(this, …)` happens to execute inline
(AutoConnection, same thread) and reaches `setState(Running)` before
returning — a coincidence of connection-type semantics, not a contract.
The check should be `!op->isFinished()`. This also means the cancellation
coverage the F2 campaign built was, for local backends, **only ever
exercised by MockBackend in tests** — the tests verify an execution model
production doesn't have.

### C4 — LOW — assorted dead/false/misplaced machinery (READ)

- `SyncEngineWorker::await<Op>` (`syncengine_p.h:269-300`): dead code, zero
  callers; its cancel-on-wake semantics silently never ship (B5).
- `localbackend.cpp:767`: "*processEvents() removed - sync runs in worker
  thread*" — false; that lambda runs on the **backend's** thread (the GUI
  thread pre-D1, where the whole-directory parse was one unyielding timer
  event — the likeliest actual mechanism of the historical local-mirror
  GUI jank). Also emits `itemFetched` **per incidence** — post-D1, a
  cross-thread queued-signal storm per fetch.
- `processSync` clears `m_cancelled` at dispatch (`syncengine.cpp:1494`):
  a cancel that lands between queue advance and worker start is erased;
  the cancelled queue runs one more full mapping before stopping.
- DecSync active controllers run their **entire sync synchronously on the
  caller's (GUI) thread** inside `driveQueue` (`syncengine.cpp:369-374`)
  — outside PlanStan's current usage, but a §1 violation to whoever
  enables it next.
- The overlap-guard's rejected future is indistinguishable from a
  successful no-op run (part of B4's silent-wedge chain).

---

## 5. What is already right (do not churn)

- The engine/worker split, the queued signal spine, the per-mapping FIFO
  queue, and dynamic `backend->thread()` resolution — the D1 premise that
  relocation retargets the ~20 marshaled sites **is** true for those sites
  (T1.4 case 3 proves it end-to-end, EXEC).
- The operation-handle API (`SyncOperation` + KDAV continuation chains in
  `RemoteCalendarBackend::fetchItems`) is the §1.1 shape already; the async
  machinery exists, it's the *consumption* that regressed to blocking.
- The N5 verified-commit discipline (commit ctag only when every item
  materialized; distrust ctag-match-with-empty-cache) is exactly the §1.1
  cache-token contract. A1's fix builds on it rather than replacing it.
- N4 chunked multiget, the mass-delete guard shape (worker blocks on a
  guard that marshals to GUI — correct direction), T1.1's shared QNAM +
  thread assert, T1.2/T1.3 lazy-open stores, and the T1.4
  `runOnBackendThread` same-thread guard are all keepers.

---

## 6. Recommended shape of the fix, in dependency order

Severity says A1/A2 (silent data stranding, live today) outrank the D1
stall (B1, visible jank). Practically they meet in the same place: the
§1.4 token rework **deletes** `persistRevision` (half of B1's surface) and
relocates the fast path to the worker (the other half). Suggested order:

1. **T1.6 — worker-side fast path** (B1): `startWorkerThread()` before the
   pre-pass; pre-pass runs on the worker via queued dispatch; results land
   in an engine slot that re-checks `m_cancelled` and calls
   `processQueue()`. Closes O16; T1.5's probe flips green as the gate.
2. **T1.7 — token contract rework** (A1, A2, A3, A4): fetch returns
   `(records, token)`; engine persists per-`(mappingId, side)` tokens in
   BaselineStore on success only; `persistRevision` deleted;
   `LocalBackend::primeRevisionCache`/`cachedCollectionRevision` reduced to
   the backend-internal cache role or removed. RED test first: the A1
   failed-push scenario (fetch commits, apply fails, next cycle must NOT
   skip).
3. **T1.8 — marshaling completeness** (B2, B3): per-backend marshals in
   `runPropertyPhase` and `dispatchFirstSync`; add the "all sync backends
   share one I/O thread" invariant to the plan §1 *or* fix B3 structurally.
4. **T1.9 — bounded waits + honest failure** (B4, B5, B6): QNAM
   `transferTimeout`; worker await deadline + `op->cancel()` on wake; gate
   condition `!isFinished()` (C3); distinguishable overlap-rejection;
   teardown-order invariant documented in Stage 3.
5. **T1.10 — single-fetch pipeline + op lifetime** (C1, C2): gate and
   loadRecords collapse into one awaited fetch per side;
   `deleteLater()` every awaited op; delete dead `await<>`.
6. **D2 backlog:** backend op-queue serialization / async `davSyncRequest`
   (B7); C4 cleanups; DecSync off the GUI thread if ever used.

Steps 1–5 are each RED-first, independently landable, and none touches the
diff/merge/conflict core.

---

## 7. Cross-references

- `docs/campaign/FINDINGS.md` — O16 (= B1 here); **O17–O24 added by this
  audit's commit**, one per A1/A2/A3/B2/B3/B4/C1+C2/C3.
- `docs/campaign/archive/2026-07-05-d1-t1.5-stall-blocker-analysis.md` — the T1.5
  blocker this audit was commissioned to verify; its §4 narrative is
  confirmed (EXEC), its Option A endorsed, its fix menu subsumed by §6.
- `docs/campaign/2026-07-04-d1-threading-execution-plan.md` — §9 checklist
  now points here for the Stage 1 unblock decision.
- PlanStan `docs/bugs/sync-nonconvergence-vtimezone-corruption-and-dav-transport.md`
  — N4/N5/N7 lineage; §3's B7 names a candidate mechanism for N5's
  corruption class.
- `docs/campaign/INVARIANTS.md` — extend-don't-fork, RED-first, no
  `processEvents()`: all §6 steps comply (no nested loops added, no
  mutexes around backends, no sleeps).
