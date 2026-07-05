# Phase D1 execution plan — DAV I/O off the GUI thread (→ v0.83)

**Date:** 2026-07-04
**Status:** Stage 1 IN PROGRESS, BLOCKED on a newly-found gap — T1.1-T1.4
landed and green (158→159 tests). T1.5's GUI-stall probe is written and
correctly identifies a real, unfixed stall: `SyncEngine::prepareSyncFastPath()`
runs synchronously on the caller's thread (before the worker thread starts)
and blocks on relocated backends' network I/O via `BlockingQueuedConnection`
— relocating backends doesn't help because the caller still waits
synchronously regardless of which thread does the work. Full detail + fix
options: `docs/campaign/FINDINGS.md` O16. Needs a decision before Stage 1's
gate can close — see §9 checklist for the specific blocked item.
Checklist in §9 is the live state; update it in the
same commit as the work it describes.
**Repos:** libkalburator (primary) + PlanStan (consumer adoption).
**Baseline revisions for all file:line references below:** libkalburator
`main` @ `928f318`, PlanStan `master` @ `ca17648b`. **Line numbers drift** —
every reference below also names the enclosing symbol; if a line doesn't
match, grep the symbol and trust the symbol.
**Parent docs:** roadmap `docs/campaign/2026-07-03-sync-convergence-roadmap.md`
§"Phase D1"; invariants `docs/campaign/INVARIANTS.md` (read before any
change). Roadmap §5 gets a one-line status update when a stage lands; *this*
doc carries the task-level checklist.

---

## 0. Read this first — what you are doing and why it will work this time

Sync backends (LocalBackend, RemoteCalendarBackend, provider-created
per-calendar DAV backends) live on the consumer's GUI thread. The engine
worker marshals every read/classify/apply to them via
`Qt::BlockingQueuedConnection`, so all CalDAV network I/O and LocalBackend's
full-directory parse execute **on the GUI thread** — the 120 s soft-freeze
(finding N7). D1 relocates the backends to a dedicated I/O `QThread`.

Threading sync has failed before. Know the history so you don't repeat it:

- **Two streaming-progress attempts (Jan 2025, both reverted)** bolted
  progress signals + `processEvents()` onto a synchronous architecture and
  never moved I/O anywhere. Postmortems:
  `PlanStan/docs/complete/postmortem_sync_progress_attempt{,_2}_2025_01.md`.
  Their codified rules bind you: **no `processEvents()`, ever** — if you feel
  the need, the architecture is wrong; feature branch + commit per increment.
- **F2 (Apr 2026, landed)** redesigned the async API surface (QFuture,
  cancellation) but deliberately left backend I/O on the main thread.
- **v0.72 `onOwnerThread` fix** (`434c7d4`) made the op API
  thread-relocatable. **`6579dfb`** made GenericSqliteBackend thread-safe via
  per-thread connections. These are your two proven building blocks.

A 2026-07-04 viability audit (three deep sweeps of both repos) concluded D1
is viable because everything on the path is already thread-agnostic:

- The engine reaches backends **only** via
  `QMetaObject::invokeMethod(backend, …, Qt::BlockingQueuedConnection)`,
  resolved dynamically against `backend->thread()` at each call (~19 sites in
  `syncengine.cpp`). Moving a backend retargets every site automatically.
  **No engine changes are expected in D1.**
- Every backend→PlanStan signal connection (`connectBackendSignals`,
  `collectioncontroller.cpp:1550+`) is default/auto type → auto-promotes to
  queued. No `Qt::DirectConnection` exists on any backend signal in PlanStan.
- `SyncMassDeleteGuard` (`PlanStan/src/controllers/syncmassdeleteguard.cpp:53-72`)
  is already thread-aware (same-thread fast path, else BlockingQueued to
  qApp). Works unchanged.
- The user-edit save path never touches backends (staging via
  `IncidenceMutator` → `StagingController` in-memory maps).
- `CalDavContentCache` already lazy-opens its SQLite connection with a
  per-instance UUID name → naturally relocatable.
- Backends are constructed **unparented** on the provider path
  (`caldavprovider.cpp:172`) → `moveToThread`-eligible.

What remains is the finite blocker list this plan works through, stage by
stage. Each stage is independently landable and independently valuable; a
failed later stage never strands a half-broken earlier one.

---

## 1. Non-negotiable invariants (violating any of these is how this attempt fails)

1. **Backends must never land on the engine worker thread.** Every
   BlockingQueuedConnection site in `syncengine.cpp` self-deadlocks when
   target thread == calling thread. The I/O thread is a **third** thread:
   GUI ≠ engine-worker ≠ backend-I/O.
2. **`SyncEngine` and `BaselineStore` do not move.** The worker marshals
   baseline access to the `m_baselineStoreAnchor` (the engine itself, on its
   creation thread) — `syncengine_p.h:119-132, 358-364`. D1 moves backends
   only.
3. **The I/O thread must run a Qt event loop** (default `QThread::start()` →
   `run()` → `exec()` is fine; never override `run()` without `exec()`).
   KDAV jobs and `davSyncRequest`'s nested `QEventLoop` depend on it.
4. **No `processEvents()`. No new nested event loops on the GUI thread.**
5. **`startSync(…, MemoryCalendar *calendar, …)` implementations may read
   only `calendar->id()`** — the calendar object stays GUI-owned. Both
   current implementations already comply
   (`remotecalendarbackend.cpp:869` / `localbackend.cpp:457`); Stage 1
   documents this as a contract comment so nobody adds a traversal later.
6. **Anything crossing the boundary by value must be owned or cloned.**
   Staged `Incidence::Ptr`s are shared with the GUI model — clone at the
   marshal boundary (Stage 2, T2.2).
7. **Thread affinity of lazily-created members = thread of first use.** All
   backend configuration (`setDbPath`, `setCacheDir`, …) must happen either
   before the move or via the marshal helper — never as a direct cross-thread
   call after the move.
8. Campaign-wide rules from `INVARIANTS.md` still apply (extend, don't fork;
   fail loud; RED-first tests; update status docs in the landing commit).

---

## 2. Build & test (both repos)

**libkalburator:**
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -- -j 8        # -j 8 ONLY — GCC ICEs under all-core load
ctest --test-dir build -j 8
```
Baseline: **157+ tests green** at v0.82/D0. Two pre-existing flakes are NOT
yours to fix and NOT a gate failure: `tst_sync_convergence`
`remoteEditFetchesExactlyOneChangedItem` (multiget-count assertion, flaky on
unmodified main) and `tst_engine_single_mapping_cancel` (segfaults only under
`ctest -j` contention; passes standalone). Anything else red = stop and fix.

**PlanStan:** build with `/project:build`, rebuild single files with
`/project:rebuild <file>` (never ad-hoc make; never `--parallel`). For
end-to-end work against the lib working tree:
```bash
cd ~/dev/PlanStan
cmake -B build-dev -DPLANSTAN_DEV_BUILD=ON \
      -DPLANSTAN_LIBKALBURATOR_SOURCE_DIR=$HOME/dev/libkalburator
```
Tests: `WAYLAND_DISPLAY=wayland-0 ctest` from the build dir; **do not force
`QT_QPA_PLATFORM=wayland`** (several tests pin themselves to offscreen).
Driving the real app against live data: set `PLANSTAN_NONINTERACTIVE=1`
(mass-delete guard denies by default, no modal).

Live CalDAV: Radicale at `http://localhost:5232/testuser1/`
(testuser1/password1) — disposable, abuse freely.

**Branch discipline:** libkalburator work on `feature/d1-threading` off
`main`; PlanStan work on `feature/d1-io-thread` off `master`. Merge `--no-ff`
when a stage's gate is green. Commit per task, not per stage.

---

## 3. Stage 1 — libkalburator enablement (no consumer changes; each task independently valuable)

### T1.1 — Shared, thread-affine QNAM in RemoteCalendarBackend

`davSyncRequest` (`remotecalendarbackend.cpp:206-251`) constructs a fresh
`QNetworkAccessManager` per call (`:228`) → a new TCP+TLS handshake per
request and the stray `QIODevice::read (QSslSocket): device not open`
warnings.

- Add member `QNetworkAccessManager *m_nam = nullptr;`. In `davSyncRequest`,
  lazily create: `if (!m_nam) m_nam = new QNetworkAccessManager(this);` and
  use it. Lazy + parented to `this` means: created on the backend's owning
  thread at first use, relocates with the backend if moved before first use,
  destroyed with the backend.
- Add at `davSyncRequest` entry:
  `Q_ASSERT(QThread::currentThread() == thread());` — every raw-DAV entry
  point must already run on the backend's thread (they're reached via
  BlockingQueued from the worker); this assert turns a future affinity bug
  into a loud debug failure instead of a silent race.
- Gate: full suite green. (Optionally observe the QSslSocket warnings gone in
  `tst_caldav_integration` output — informational, not a gate.)

### T1.2 — Lazy-open CTagStore

`CTagStore` (private class in `remotecalendarbackend.cpp`, ~`:60-100`) opens
its QSqlDatabase **eagerly in its constructor**, which runs inside
`RemoteCalendarBackend::setDbPath` (~`:363`). Whoever calls `setDbPath` owns
the connection's thread forever → cross-thread SQLite misuse once the backend
moves.

- Convert to the `CalDavContentCache` pattern: ctor stores `dbPath` only; an
  `ensureOpen()` (called at the top of every public method — `get`/`set`/
  `clear`/whatever exists) performs `addDatabase`/`open`/`ensureSchema` on
  first use. Keep the per-instance connection name
  (`CTagStore_%1_%2`, backendId + `this`).
- RED-first: extend/create a test that constructs the backend on the main
  thread, calls `setDbPath` on the main thread, `moveToThread`s the backend,
  then round-trips a CTag via `QMetaObject::invokeMethod(backend, …,
  BlockingQueuedConnection)` from the test thread. RED today (Qt warns
  "QSqlDatabasePrivate::database: requested database does not belong to the
  calling thread" and/or the read fails); GREEN after lazy-open. Simplest
  home: the new relocation test (T1.4) — fine to land T1.2 and T1.4 together.

### T1.3 — Lazy-open LocalBackend FingerprintStore

Identical pattern, identical fix: `FingerprintStore` in `localbackend.cpp`
(~`:32-50`) opens eagerly in its ctor from `LocalBackend::setDbPath`
(~`:144`). Convert to lazy-open exactly as T1.2. Covered by the LocalBackend
case in T1.4.

### T1.4 — Backend thread-relocation test (the lib-side proof)

New test, e.g. `tests/calendar/tst_backend_thread_relocation.cpp` (follow the
harness pattern in `tests/calendar/CMakeLists.txt` /
`docs/phase0/04l-phase-d0-test-harness-*.md`; use `FakeCalDavServer` from
`tests/sync/fakecaldavserver.{h,cpp}` for the remote cases). Cases:

1. **Remote, construct-then-move:** construct `RemoteCalendarBackend`
   unparented on the main thread; `setDbPath`/`setCacheDir` on the main
   thread (exercises T1.2 lazy-open); `moveToThread(ioThread)`; drive
   `loadCalendars` + `fetchItems` + `pushItems` + `deleteItems` from the main
   thread via `invokeMethod(…, BlockingQueuedConnection)` (mimicking the
   engine); assert results correct against FakeCalDavServer and
   `op->thread() == backend->thread()` (extends the v0.72
   `…_fromWorkerThread_opLivesOnBackendThread` cases in
   `tst_remotecalendarbackend_writepaths`).
2. **Local, construct-then-move:** same shape for `LocalBackend` against a
   temp-dir fixture: fetch, `startSync` staged writes (exercises the internal
   `AsyncFileWriter` from a relocated backend), fingerprint round-trip
   (exercises T1.3).
3. **Full engine run with relocated backends:** stub-`ISyncHost` integration
   test (pattern: `tests/calendar/` + `kalburator_add_calendar_integration_test`)
   where both mapping backends live on an I/O thread and
   `SyncEngine::runSync(SyncRequest)` completes with a correct result. This
   proves invariant §1.1 (three distinct threads) end-to-end.

### T1.5 — GUI-stall probe test (the D1 acceptance gate, built in the lib)

The roadmap gate: a heartbeat proves the main thread never stalls > 50 ms
through a full sync cycle over a latency-injected fake server.

- Add a latency hook to `FakeCalDavServer` (it's a `QTcpServer`): e.g.
  `setResponseDelayMs(int)` — delay each response via `QTimer::singleShot`
  (do NOT `msleep` the server's thread if it's the test main thread — give
  the fake server its own thread, or use the timer so the test's event loop
  stays live).
- Test: backends on an I/O thread (as in T1.4 case 3), fake server with
  ~200 ms per-response delay, a `QTimer` on the test main thread ticking
  every 10 ms recording `QElapsedTimer` gaps; run a full
  `runSync(SyncRequest)`; assert max observed gap < 50 ms.
- **Also assert the inverse is currently true** (optional but valuable
  RED-documentation): with backends left on the main thread, the same run
  produces gaps ≫ 50 ms. This pins that the test actually measures the
  freeze.

### T1.6 — Contract documentation + peripheral audit

- `syncbackend.h` class comment: "A backend may live on any thread. All
  public entry points are invoked on the backend's thread (the engine
  marshals via blocking-queued connections); configuration setters must be
  called before first use or marshaled. Lazily-created members acquire the
  affinity of first use."
- `startSync` doc comment: implementations may read **only**
  `calendar->id()` from the calendar parameter (invariant §1.5).
- `FilteredCollectionBackend` (`filteredcollectionbackend.cpp:34-45`)
  connects to `BackendRegistry::backendInstanceUnregistered` with
  `Qt::DirectConnection` assuming shared thread. Not on the DAV path — add a
  warning comment "not relocatable as-is; do not move FCB-wrapped backends
  off the registry's thread" and log it in `docs/campaign/FINDINGS.md`.
- Glance at `ProviderManager::~ProviderManager → disconnectAll →
  unregisterProviderBackends` (flagged in
  `docs/2026-06-11-remotecalendarbackend-first-sync-url-race-handoff.md`
  §134-138): with Stage 3's teardown order (I/O thread stopped **before**
  ProviderManager destruction) this is safe; add a comment saying so.

**Stage 1 gate:** full libkalburator suite green (modulo the two named
flakes); T1.4 + T1.5 green. Merge `feature/d1-threading` → `main` (`--no-ff`).
**Do not tag yet.** Update roadmap §5 + §9 checklist here in the merge commit.

---

## 4. Stage 2 — PlanStan preparation (backends stay on the GUI thread; strictly behavior-neutral)

Everything in this stage must leave the app behaving identically — auto
connections resolve to direct delivery while sender and receiver share a
thread, so marshaling introduced now is a no-op until Stage 3 flips the
thread. That is what makes this stage safely landable on its own.

### T2.1 — De-parent backends; make ownership explicit

`createBackendFromConfig` (`collectioncontroller.cpp:1226-1300+`) parents
every backend to CC: `new LocalBackend(calendarRootPath, this)` (`:1231`),
`new RemoteCalendarBackend(…, this)` (`:1240`), and the decsync/orgmode/
subscription/akonadi branches. `moveToThread` refuses parented objects.

- Construct all of them with **no parent**.
- Ownership becomes fully manual: audit every path that inserts into
  `m_backends` (grep `m_backends.insert`) and every deletion path — the
  destructor loop (`~CollectionController`, `:213-217`), the reload/reset
  loop (`:374-386`), and any early-return in `loadCollectionFromFile` after
  backends exist. The parent link was a silent leak-stopper; without it,
  every exit path must delete non-provider-mirrored backends explicitly
  (`m_providerMirroredBackendIds` marks the ProviderManager-owned ones —
  never delete those).
- Provider-created backends are already unparented
  (`caldavprovider.cpp:172`, `std::make_unique`, owned by ProviderManager) —
  verify the multiproto/other providers follow the same shape.

### T2.2 — One door: marshal every direct backend call

Add to CC (or a small free-function header both controllers can use):

```cpp
// Fire-and-forget onto the backend's thread (direct if same thread).
static void invokeOnBackend(Kalburator::Sync::SyncBackend *b, std::function<void()> fn);
// → QMetaObject::invokeMethod(b, std::move(fn));  (AutoConnection)

// Blocking query on the backend's thread. ONLY for rare, user-initiated
// topology operations — NEVER on per-item or per-tick paths (it parks the
// GUI until the I/O thread services it, which mid-sync can be seconds).
template <typename T> T queryBackendBlocking(SyncBackend *b, std::function<T()> fn);
// → same-thread: call directly; cross-thread: invokeMethod(…, BlockingQueuedConnection)
```

Convert **all** direct backend calls outside the engine to go through these.
Complete call-site inventory (grep to reverify — lines drift):

| Site | Calls | Treatment |
|---|---|---|
| `stagingcontroller.cpp:249,307` (`StagingController::startSync`) | `deleteCalendar`, `startSync` | fire-and-forget; **clone staged incidences at the boundary** — `stagedCreations/Updates` hold `Incidence::Ptr`s shared with the GUI model; build the marshal lambda over `inc->clone()` copies (invariant §1.6). Pass the calendar pointer through unchanged (lib contract: only `id()` is read). Completion already returns via auto→queued `syncCompleted` (`:234-235`). |
| `itemloadingcoordinator.cpp:72` | `fetchItems(calendarId)` | fire-and-forget; results stream back via `itemFetched` (already queued-safe) |
| `itemloadingcoordinator.cpp:163` | `cancelOperationsFor` | fire-and-forget |
| `collectioncontroller.cpp:669,1733` | `loadCalendars` | fire-and-forget |
| `collectioncontroller.cpp:238,241` and `:1035,1038` (unload paths) | `hasPendingOperationsFor` + `cancelOperationsFor` | `hasPendingOperationsFor` → blocking query (rare, user-initiated); cancel → fire-and-forget |
| `collectioncontroller.cpp:419` | `setCalendarColor` | fire-and-forget |
| `addcalendarcommand.cpp:47` | `createCalendar` | fire-and-forget (its result comes back via `calendarCreated`/discovery signals) |
| `backenddiscoverycoordinator.cpp:221,270,302` | `setCalendarColor`, `createCalendar` | fire-and-forget |
| `collectioncontroller.cpp:1143-1221` (`convertCalendarToBackend`) | `supportsCalendarCreation`, `discoveredCalendar`, `createCalendar`, `pushItems` (+ nested QEventLoop `:1202-1205`), `deleteCalendar` | queries → blocking query; mutations → fire-and-forget; the existing nested loop keeps waiting on the (now queued) op `finished` signal — acceptable for D1, it's a rare explicit user action. Do not add a new nested loop anywhere else. |
| `collectioncontroller.cpp:1860-1869` (`initializeSyncInfrastructure`) | `setDbPath`, `setCacheDir` | route through `invokeOnBackend` for uniformity (safe either way after Stage 1 lazy-open; invariant §1.7) |

### T2.3 — BackendFacts snapshot for hot-path reads

`ItemLoadingCoordinator::onItemFetched` calls `backend->shapeFor(calId)` and
`backend->backendType()` **per fetched item**
(`itemloadingcoordinator.cpp:263-264`) — after the move these become per-item
cross-thread reads of an I/O-thread object. Blocking queries here would
re-serialize the GUI on the I/O thread; naked reads are races. Neither is
acceptable:

- Add a GUI-side cache (owned by CC, or by ItemLoadingCoordinator):
  `backendType` per backendId captured at registration
  (`connectBackendSignals` time — it's immutable), `shapeFor` per calendarId
  captured when the calendar is registered/discovered (hook the same places
  the backend's calendar map is populated; `calendarDiscovered` arrival on
  the GUI thread is a natural point).
- Convert the other scattered const reads to the cache too:
  `incidenceeditordialog.cpp:586-588` (`recurrenceCapabilities`),
  `deleteincidencecommand.cpp:65,131`, `incidencemutator.cpp:143`,
  `backenddiscoverycoordinator.cpp:262`.

### T2.4 — Metatype audit

Queued delivery of backend signals requires registered metatypes. Today
delivery is direct, so gaps are latent. Grep every signal signature in
`syncbackend.h` / `syncbackendbase.h` for non-builtin types
(`KCalendarCore::Incidence::Ptr`, list/map typedefs, enums) and ensure
`qRegisterMetaType<>()` calls exist at app init (PlanStan `main.cpp`) or lib
init. Runtime canary: any "QObject::connect: Cannot queue arguments of type
'X'" line in Stage 3 testing = a miss here.

**Stage 2 gate:** PlanStan full suite green; run the real app
(`/project:build`, dev build) against a Radicale mirror collection and
confirm behavior is identical (backends still on the GUI thread — this stage
must be invisible). Merge to `master`. Update §9 checklist.

---

## 5. Stage 3 — the move (PlanStan)

### T3.1 — I/O thread + relocation

- CC owns `QThread m_backendIoThread;` (member, plain default QThread —
  invariant §1.3). Start it lazily before the first backend is created; name
  it (`setObjectName("planstan-backend-io")`) for debuggability.
- **Scope: only `LocalBackend` and `RemoteCalendarBackend` instances move**
  (config-declared and provider-created/multiproto). DecSync, OrgMode,
  Subscription, and Akonadi backends stay on the GUI thread — DecSync holds a
  pointer to the GUI-thread `SyncthingMonitor` (`collectioncontroller.cpp:
  1264`), and none of them are on the freeze path. This mixed placement is
  safe **by construction**: the engine and the Stage 2 marshal helper both
  key on `backend->thread()` per call.
- Relocate immediately after construction and signal wiring, before any use:
  config path in `createBackendFromConfig`/`loadCollectionFromFile`; provider
  path in `mirrorProviderBackends` (`collectioncontroller.cpp:1607-1660`)
  right after the borrow — verify the provider backend has no parent first
  (`Q_ASSERT(!backend->parent())`).
- **Kill switch:** env var `PLANSTAN_SYNC_IO_THREAD` — unset or `1` = move
  (default ON); `0` = leave everything on the GUI thread. One `if` at the
  relocation point. This is the fail-safe for live testing and the shipping
  fallback if Stage 3 verification finds something structural.

### T3.2 — Teardown order

`~CollectionController` (`collectioncontroller.cpp:141-223`) currently: stop
engine worker (`m_syncCoordinator->stopWorkerThread()`, `:158`) → delete
coordinators → delete non-provider backends → delete collection. New order:

1. Stop the engine worker first (unchanged — it calls into backends).
2. `m_backendIoThread.quit(); m_backendIoThread.wait();` — **before** any
   backend deletion and **before** ProviderManager teardown. Once the thread
   has finished, deleting its objects from the GUI thread is safe (standard
   Qt practice), including ProviderManager's `unique_ptr` deletions and the
   `:213-217` loop.
3. Everything else unchanged. Apply the same ordering to the reload/reset
   path (`:374-386`) if it survives Stage 2's ownership audit.

### T3.3 — Verification (the whole point)

1. Both suites green (lib suite unchanged from Stage 1; PlanStan
   `WAYLAND_DISPLAY=wayland-0 ctest`).
2. The Stage 1 stall probe (T1.5) stays green — it's the automated gate.
3. **Live end-to-end** (dev build against sibling lib checkout,
   `PLANSTAN_NONINTERACTIVE=1`, Radicale testuser1):
   - Create a local-mirror collection against 2-3 Radicale calendars with a
     few hundred events; initial sync completes; items appear in the views.
   - Idle cycles converge: by cycle 2, zero writes / zero conflicts / zero
     item fetches (the B5 fast path — watch the log; same procedure as the
     C4 close-out, narrated in
     `docs/campaign/archive/2026-07-03-sync-convergence-tracks-a-b-c.md`).
   - Edit an event on the server mid-session (curl PUT or second client);
     next cycle pulls it; it appears in the model live (D0 path).
   - **The GUI never freezes during any of this** — drag a window, scroll a
     view during a sync cycle. Subjective check on top of the probe.
   - Local edit → staged flush (`syncNow` path) → lands on server; verifies
     the marshaled `StagingController::startSync` + incidence cloning.
   - Kill switch check: same session with `PLANSTAN_SYNC_IO_THREAD=0`
     behaves like v0.82 (freeze back, correctness unchanged).
4. Watch stderr the whole time for: "Cannot queue arguments of type" (T2.4
   miss), "QObject: Cannot create children for a parent that is in a
   different thread", QSqlDatabase thread warnings, the T1.1 assert. Any of
   these = stop, fix, re-verify.

**Stage 3 gate:** all of the above. Merge to `master`. Update §9.

---

## 6. Stage 4 — release & consumption

1. **libkalburator:** update roadmap §5 (D1 done) + this doc's checklist +
   `CLAUDE.md` campaign status; tag **v0.83** (contents: D0 + D1; tag message
   per INVARIANTS §10 — no breaking API changes expected; WildPalms consumes
   on its own schedule and is unaffected until it opts into relocation).
2. **PlanStan:** bump `PLANSTAN_LIBKALBURATOR_GIT_TAG` to `v0.83`
   (`CMakeLists.txt:69`); clean build + suite green **without** the
   `SOURCE_DIR` override.
3. **Drop the D0 mitigation** per `docs/todo/sync-apply-phase-model-refresh.md`:
   remove the `reloadModelEligibleCalendars()` call in
   `CollectionController::createLogicalCalendar`; re-verify the
   create-a-collection flow shows items live (engine `recordChanged` now
   covers it); **delete that todo file** in the same commit.
4. **Close the freeze half of `docs/bugs/sequential-sync-performance.md`:**
   the 120 s freeze is fixed architecturally; the sequential-mapping
   parallelism half remains deliberately open (roadmap D2/deferred) — edit
   the file down to that remainder, don't delete it.
5. Add a `docs/todo/` note for the deliberately-deferred follow-up: collapse
   the legacy `syncNow → StagingController::startSync` push path into the
   engine path (one door for writes). Out of D1 scope by decision.
6. Update PlanStan `CLAUDE.md` campaign section (D1 done; next = D2 triage).
   Push both repos.

---

## 7. Abort / fallback ladder

- **Stage 1 trouble** (relocation test exposes deep KDAV/QNAM affinity
  problems that resist the lazy-init pattern): land whatever subset is green
  (T1.1 QNAM and T1.2/T1.3 lazy-open are each independently valuable), write
  findings into `docs/campaign/FINDINGS.md` + a status note in this doc, and
  stop. Consumer repos untouched.
- **Stage 2 lands, Stage 3 fails verification:** Stage 2 is behavior-neutral
  and stays merged. Ship with the kill switch defaulted OFF (flip the default
  in T3.1's one `if`) — no regression vs v0.82 — and record exactly what
  failed in FINDINGS + this doc before ending the session.
- **Never** leave a half-moved state merged: the relocation point is a single
  guarded block by design, so "moved" is one boolean, not a scatter.
- If you find yourself adding `processEvents()`, a mutex around a backend, or
  a `sleep` — stop. That is the failure signature of attempts 1 and 2.
  Re-read §0 and the postmortems.

---

## 8. Task-order summary (do them in exactly this order)

T1.1 QNAM → T1.2 CTagStore lazy → T1.3 FingerprintStore lazy → T1.4
relocation tests → T1.5 stall probe (+ FakeCalDavServer latency hook) → T1.6
contract/audit → **merge lib** → T2.1 de-parent/ownership → T2.2 marshal all
call sites → T2.3 facts snapshot → T2.4 metatypes → **merge PlanStan prep** →
T3.1 I/O thread + kill switch → T3.2 teardown → T3.3 verification → **merge
move** → Stage 4 release steps 1-6.

## 9. Checklist (LIVE — update in the same commit as the work)

Stage 1 (libkalburator, branch `feature/d1-threading`):
- [x] T1.1 shared lazily-created QNAM + entry assert
- [x] T1.2 CTagStore lazy-open
- [x] T1.3 FingerprintStore lazy-open
- [x] T1.4 relocation tests (remote / local / full-engine-on-io-thread) —
      also found and fixed two unmarshaled engine→backend calls
      (`prepareSyncFastPath`/`persistRevision` called `ChangeDetection`
      methods directly; now via a `runOnBackendThread()` helper)
- [ ] T1.5 GUI-stall probe + FakeCalDavServer latency hook (gate: <50 ms) —
      **written, RED**: probe correctly measures a ~213ms stall even with
      backends relocated. Root cause is NOT a T1.1-T1.4 gap — it's
      `prepareSyncFastPath()` itself running synchronously on the caller's
      thread before the worker thread starts. See FINDINGS.md O16 for full
      detail + fix options. Blocked pending a decision on which fix
      direction to take.
- [ ] T1.6 threading contract docs + FCB/ProviderManager audit notes
- [ ] Stage 1 gate: full suite green; merged --no-ff to main; roadmap §5 touched
      — **cannot close while T1.5 is RED**

Stage 2 (PlanStan, branch `feature/d1-io-thread`):
- [ ] T2.1 backends de-parented; ownership audit of all insert/delete paths
- [ ] T2.2 marshal helper + all 10 call-site groups converted (incl. staged-incidence cloning)
- [ ] T2.3 BackendFacts snapshot; hot-path reads converted
- [ ] T2.4 metatype registrations
- [ ] Stage 2 gate: suite green; live app behavior identical; merged

Stage 3 (PlanStan):
- [ ] T3.1 I/O thread, relocation of Local/Remote backends, PLANSTAN_SYNC_IO_THREAD kill switch
- [ ] T3.2 teardown ordering (dtor + reset path)
- [ ] T3.3 full verification (suites, stall probe, live Radicale checklist, stderr clean)
- [ ] Stage 3 gate: merged

Stage 4 (release):
- [ ] roadmap §5 + lib CLAUDE.md updated; tag v0.83
- [ ] PlanStan pin → v0.83; clean-build green
- [ ] D0 mitigation dropped; sync-apply-phase-model-refresh.md deleted
- [ ] sequential-sync-performance.md trimmed to the parallelism half
- [ ] docs/todo entry for legacy staging-path collapse
- [ ] PlanStan CLAUDE.md updated; both repos pushed
