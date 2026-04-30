# Phase F2 — Threading API redesign — design

**Date:** 2026-04-30
**Status:** Authored 2026-04-30. Implementation plan to follow in
`04q-phase-f2-threading-plan.md` (sibling).
**Phase tag:** `v0.14-phase-f2-threading`.
**Gates:** Phase F1 complete (`v0.13-phase-f1-unify`).

## Goal

Phase F2 of `04k-engine-merger-roadmap.md`: redesign the unified
`SyncEngine`'s threading and async surface on top of the stable
F1 structure. The roadmap's "Threading & async / Phase F target
shape" section is the canonical statement of intent and is
restated and refined here.

The single load-bearing principle: **one engine, one threading
contract, all consumers adapt.** The F1 transitional facade
`SyncEngine::runBlobTwoWay` / `runBlobMirror` exists only because
F1 preserved threading verbatim; it is not architecture, and F2
retires it. PlanStan and WildPalms both go through one entry point
— a `QFuture`-based `runSync` — and the engine never has two
parallel async surfaces again.

**F2 explicitly does redesign:**

- `SyncEngine`'s public async surface (`QFuture<SyncResult>` /
  `QFuture<QList<SyncResult>>` returns; cancellation via
  `QFuture::cancel()`).
- `SyncBackend`'s I/O contract (operation-handle-only; sync
  overloads removed).
- The cancellation propagation story end-to-end, with new
  TDD-style cancellation tests (no current coverage per FINDINGS).
- The signal contract: streaming events stay (`itemFetched`,
  `progressChanged`, `conflictDetected`, `phaseChanged`);
  completion signals (`syncCompleted`, `allSyncsCompleted`) and
  the per-write `writeFinished` signal retire in favour of the
  `QFuture` and `Operation` handles respectively.

**F2 explicitly does NOT redesign:**

- The worker-thread model itself. The F1 shape — public methods on
  the caller thread, a private `SyncEngineWorker` QObject on a
  dedicated `QThread`, queued-connection dispatch, queued-signal
  forwarding — survives verbatim. The F1 finding "worker-class
  collapse: file boundary, not QObject boundary" is load-bearing
  and not relitigated.
- `IBlobBackend`'s synchronous I/O surface. `IBlobBackend` remains
  synchronous; six WildPalms plugin backends implement it, and
  ramifying that to operation handles is its own ramifying change.
  Phase G is the natural place — when `IBlobBackend` becomes the
  primary surface and `SyncBackend` dissolves into adapters.
- `IDomainAdapter::diff` / `merge` / `applyChanges`. These remain
  synchronous; they execute on the worker thread as plain method
  calls. Diff/merge are CPU-bound but bounded (tens of ms on a
  1000-record sync).
- The mapping registration surface. `SyncEngine::setSyncMappings` /
  `loadSyncMappings` continue to be the way mappings reach the
  engine. WildPalms's `SyncRunner` registers (or replaces) mappings
  per HotSync session, then runs them through `runSync`.
- The two known WildPalms order-dependent test flakes
  (`tst_pluckerbackendplugin`, `tst_calendar_v2`). These pre-date
  F2 and are unrelated.

## Decisions made during the brainstorm

The 2026-04-30 brainstorm settled five forks. Recorded here so
the plan, the implementation, and any future revision pass have a
single authoritative source.

### 1. Scope — (a)+(b)+(c)+(e) in; (d) deferred

The roadmap's "Threading & async" section listed five pieces under
the Phase F target shape: the QFuture-based public API, cancellation
plumbing, operation-handle standardisation, concurrent-mapping
execution, and conflict pause/resume kept verbatim. The brainstorm
sliced these:

- **(a) `QFuture<SyncResult>` public API** — in.
- **(b) Cancellation via `QFuture::cancel()`** — in. Co-developed
  with new tests because no current coverage exists (audit
  confirmed; FINDINGS notes the gap).
- **(c) `SyncBackend` operation-handle standardisation** — in.
  Tightly coupled to (a): you cannot return a `QFuture<SyncResult>`
  cleanly while half the backend write surface is sync-void. The
  scope is **`SyncBackend` only**; `IBlobBackend` is explicitly
  out (see decision 3).
- **(d) Concurrent-mapping execution** — **deferred** to F2.1 or
  folded into G. F2 keeps the sequential queue. Open question 7
  in the roadmap stays open.
- **(e) Conflict pause/resume kept on existing signal-yield
  pattern** — in. Verbatim. No redesign of the pause-resume
  mechanism itself.

**Settled.** This matches the F1/F.0 pattern of "one structural
pivot per tag" — F2 lands the threading-API pivot with stable
mapping-execution semantics; concurrent execution is a separate
pivot when it lands.

### 2. The synchronous one-shot facade retires

F1 Task 6 added `SyncEngine::runBlobTwoWay` and `runBlobMirror`
as a synchronous calling-thread facade replacing the deleted
`BlobSyncEngine::twoWayWithBaseline` / `mirror`. WildPalms's
`SyncRunner` and four `tst_*_v2.cpp` test files consume them.

The brainstorm rejected the framing that this facade exists as
target architecture. Two options considered:

- **(P) Keep one synchronous and one async surface in parallel.**
  WildPalms continues to call the synchronous facade; PlanStan
  calls the new `QFuture` API. Two threading contracts, two
  consumer patterns coexisting permanently.
- **(Q) Single `QFuture`-based surface; consumers adapt.** The
  facade is removed. WildPalms's `SyncRunner` migrates to
  `engine.runSync(mappingId).waitForFinished()` for its blocking
  conduit context. The conduit framework already deals in blocking
  primitives; `.waitForFinished()` is exactly the
  synchronous-when-needed escape hatch.

**Settled: (Q).** The merger's whole point is convergence; (P)
freezes the parallel-engine pattern at the API layer instead of at
the implementation layer. WildPalms's per-collection one-shot
shape is preserved — register a mapping, run it, read the result —
just through the unified entry point. F1's facade was a migration
shape, not a destination.

### 3. `IBlobBackend` stays synchronous in F2

The roadmap's backend-contract bullet ("every I/O method returns
an `Operation` handle, standardise the half-finished pattern
already in `SyncBackend`") names `SyncBackend` specifically.
`IBlobBackend` is not named. Three options:

- **(α) F2 makes `IBlobBackend` async too.** All six WildPalms
  plugin backends (`PalmBackend`, `WebcalBlobBackend`,
  `PluckerBlobBackend`, `ContactsBlobBackend`, `TodoBlobBackend`,
  `MemoBlobBackend`) plus the libkalburator-side `LocalBlobBackend`
  / `MockBlobBackend` grow operation-returning shapes. Adapters
  and the engine `await(...)` blob ops the same way.
- **(β) F2 keeps `IBlobBackend` synchronous.** The engine's worker
  thread calls `IBlobBackend::loadRecords` / `createRecord` / etc.
  inline. Cancellation has a small gap inside long
  `IBlobBackend` calls.
- **(γ) F2 grows async variants alongside the sync ones, opt-in
  per backend.** Ramifying API; backends pick which to implement;
  engine has fallback logic.

**Settled: (β).** Six external plugin sub-repos already absorbed
the F1 Task 10 ripple (`IBlobBackend` no longer QObject); growing
the interface again in F2 would compound the ripple without a
matching benefit at this phase. The cancellation gap inside long
synchronous `IBlobBackend` calls already exists today and is not
a regression. Phase G is the natural place to revisit, when
`IBlobBackend` becomes the primary surface and `SyncBackend`
dissolves. (γ) is YAGNI — once the engine's `await(...)` pattern
exists, retrofitting blob ops is mechanical.

### 4. `QThreadPool` dispatch for CPU-bound work is YAGNI for F2

The roadmap's internals bullet says "CPU-bound work (diff, hash,
transcode) dispatches to `QThreadPool::globalInstance()`
(configurable)." Two options:

- **(i) F2 wires diff/hash/transcode through `QThreadPool`.**
  Adapter calls become `QtConcurrent::run` from the worker thread;
  the worker awaits via `QFutureWatcher` or a small adapter helper.
- **(ii) F2 leaves diff/hash/transcode inline on the worker.** No
  `QThreadPool` dispatch; the worker thread runs adapter methods
  directly. The "configurable" knob doesn't exist yet.

**Settled: (ii).** Profiling has not motivated this. The F1 design
note on calendar parsing cost is accurate (tens of ms on a
1000-record sync against SQLite + network costs in seconds), and
generalising that to all diff/hash/transcode work doesn't change
the conclusion. Adding `QThreadPool` dispatch is a five-line
change inside the worker once a real workload demands it; doing it
now is design-for-hypothetical-need.

### 5. `IDomainAdapter` stays synchronous

`IDomainAdapter::diff` / `merge` / `applyChanges` /
`fetchRecords` / `loadBaselines` / `saveBaselines` remain
synchronous as they are today. The engine's worker thread calls
them as plain method calls.

This is consistent with decisions 3 and 4: the worker thread is
the single locus of CPU-bound and SQLite-bound adapter work.
Async-ising the adapter surface would force every adapter to
manage its own cancellation, which loses the "single token,
checked at every operation-handle boundary" property that makes
the cancellation contract auditable.

## Open questions resolution

The roadmap carried eight open questions. F1 resolved Q3 / Q5 /
Q6 (calendar journal stays inside `CalendarDomainAdapter`;
`ICalendarCollection` stays as adapter concern; `DataDomain` enum
survives as caller-side metadata until G). F2 touches one:

| Q | Topic | F2 resolution |
|---|---|---|
| Q7 | Concurrent-mapping execution | **Still deferred** to F2.1 or folded into G. F2 keeps the sequential queue. The new public API is shaped so concurrent execution is a non-breaking addition: `runSync()` already returns `QFuture<QList<SyncResult>>`, and a future implementation can populate the list via `QtConcurrent::mapped` over `m_syncMappings` without changing the signature. |

The remaining open questions (Q4 `DecSyncBackend`, Q8 `.planstan-sync.db`)
are unrelated to threading and remain deferred.

## FINDINGS resolved by F2

Two findings against the F1 surface get a natural fix here:

- **"`SyncEngine::runSync(mappingId)` is leaky"** (2026-04-28).
  Single-mapping form double-runs because `processNextMapping`
  iterates from index 0. F2's redesign splits the worker entry
  points into `processSingleMapping` (used by `runSync(mappingId)`)
  and `processQueue` (used by `runSync()`). The shared
  `m_currentMappingIndex` field driving both goes away. The bug
  class becomes structurally impossible.

- **"Wrapper commit() lost error detection when switching from
  pushItems to storeItems"** (2026-04-29). The temporary
  `connect`-to-`writeFinished` pattern in
  `CreateIncidenceItem::commit()` and
  `UpdateIncidenceItem::commit()` is fragile (compiler doesn't
  catch silent loss of error reporting). F2's operation-handle
  pattern replaces it: callers `await(backend->pushItems(...))`
  and read `SyncOperation::state() == Failed` + `errorString()`
  directly. The fragile pattern goes away with `writeFinished`.

## Components

### `SyncEngine` — public API

Lives at `src/engine/syncengine.{h,cpp}` (unchanged from F1 — file
location is stable). The public API changes:

```cpp
namespace Kalburator::Sync {

class SyncEngine : public QObject
{
    Q_OBJECT
public:
    // ── Run, async ──

    /// Run sync for one mapping. Future completes with the result.
    /// The future supports QFuture::cancel() to request cancellation;
    /// see "Cancellation" below.
    QFuture<SyncResult> runSync(
        const QString& mappingId,
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    /// Run sync for all enabled mappings, in order. Future completes
    /// with the per-mapping result list (one entry per enabled mapping).
    QFuture<QList<SyncResult>> runSync(
        SyncBehavior behavior = SyncBehavior::Unmonitored);

    // ── Conflict resolution (unchanged from F1) ──

    void resumeAfterConflictResolution(
        ConflictResolution resolution,
        const QString& mergedIcal = QString());

    // ── State (unchanged from F1) ──

    bool isSyncing() const;
    SyncResult lastSyncResult() const;

    // ── REMOVED in F2 ──
    // void runSync(...);                      // void-returning forms
    // void cancelSync();                      // → QFuture::cancel()
    // BlobSyncResult runBlobTwoWay(...);      // synchronous facade
    // BlobSyncResult runBlobMirror(...);

signals:
    // ── Streaming (preserved) ──
    void itemFetched(const QString& mappingId, const QString& recordId);
    void progressChanged(int phase, int current, int total);
    void conflictDetected(const SyncConflict& conflict);
    void phaseChanged(SyncPhase phase);
    void transcodingWarning(const QString& mappingId, const QString& details);

    // ── REMOVED in F2 ──
    // void syncCompleted(...);                // → QFuture::isFinished
    // void allSyncsCompleted(...);
};

} // namespace Kalburator::Sync
```

Callers that need a signal-shaped completion channel construct a
`QFutureWatcher<SyncResult>` (or `<QList<SyncResult>>`), set its
future to `runSync()`'s return, and connect to
`QFutureWatcher::finished`. PlanStan's `SyncProgressManager`
takes this shape.

Callers that need a synchronous-when-needed pattern call
`runSync(id).waitForFinished(); auto r = future.result();`.
WildPalms's `SyncRunner` takes this shape.

The two `runSync` overloads share an internal driver method on
`SyncEngineWorker`; the public methods differ only in how they
wire the `QFutureInterface` into the worker's queue.

### `SyncEngineWorker` — private companion

Lives at `src/engine/syncengine.h` / `.cpp` (already unified into
the same TU per F1 Task 8). New in F2:

- A `std::atomic<bool> m_cancelled{false}` member, set when the
  worker observes the in-flight `QFuture` being cancelled (see
  "Cancellation" below).
- A `QFutureInterface<SyncResult>* m_currentSingleIface` and
  `QFutureInterface<QList<SyncResult>>* m_currentMultiIface`
  pointer pair (only one populated per run).
- Two driver methods: `processSingleMapping(const QString& id,
  SyncBehavior)` and `processQueue(SyncBehavior)`. Neither
  consults the other's state. The shared `m_currentMappingIndex`
  field that today drives both forms goes away.
- A small `await<Op>(Op*)` template helper that runs an inner
  `QEventLoop` until the operation reports finished or
  cancellation is observed; cancellation invokes the operation's
  own `cancel()` and re-enters the loop briefly waiting for the
  operation to actually settle.

The worker emits streaming signals (`progressChanged`,
`itemFetched`, etc.) the same way it does today; they're queued
to main-thread receivers. The completion path is different: the
worker calls `m_currentSingleIface->reportResult(result)` then
`reportFinished()` (or `reportCanceled() + reportFinished()` on
cancellation). Multi-mapping is the same shape against the
`QList<SyncResult>`-typed interface: the worker accumulates
results in a local `QList<SyncResult>` as each mapping completes,
emits per-mapping progress via the streaming signals, and at the
end calls `reportResult(allResults)` once followed by
`reportFinished()`. The future carries one logical result (the
list); per-mapping incremental progress is a streaming-signal
concern, not a future-result concern.

### `SyncOperation` base + subclasses — `SyncBackend` contract

Lives at `src/calendar/syncoperation.{h,cpp}`. The file already
exists with a `SyncOperation` base and three concrete subclasses
(`FetchOperation`, `PushOperation`, `DeleteOperation`). The
operation-handle pattern is half-adopted: the async methods
(`fetchItems`, `pushItems`, `deleteItems`) return these types,
but engine and adapter code still calls the parallel synchronous
methods (`loadItems` [`[[deprecated]]`], `storeItems`,
`updateItem`) along with the per-write `writeFinished` signal.
Phase E added `storeItems`/`updateItem` specifically to thread a
`TranscodingPlan` parameter that `pushItems` did not yet accept.

F2's job on the operation contract:

1. Standardise the `SyncOperation` base — explicit `State` enum
   (`Pending` / `Running` / `Succeeded` / `Failed` / `Cancelled`),
   virtual `cancel()` with idempotent default, deterministic
   `finished` signal that fires exactly once regardless of
   terminal state. Where the existing class already implements
   this (it carries lifecycle plumbing today), the change is
   inspection + locking down a contract; where it doesn't, the
   change is mechanical.
2. Add a `TranscodingPlan` parameter to `pushItems` so updates and
   creates can flow through the existing `PushOperation` shape
   without a separate synchronous path.
3. Delete the synchronous overloads (`loadItems`, `storeItems`,
   `updateItem`) and the `writeFinished` signal once no callers
   remain.

There is no new `UpdateOperation`. Updates flow through
`pushItems` — `PushOperation` already handles a list of
incidences, and backends decide create-vs-update by whether the
UID exists. This matches the pre-Phase-E control flow that
`CreateIncidenceItem::commit()` and `UpdateIncidenceItem::commit()`
used (FINDINGS), restored after the Phase E synchronous detour.

`startSync` stays as it is for F2. It is a per-sync setup call,
not on the per-record hot path that needs cancellation
granularity, and the roadmap's "every I/O method returns an
Operation" line is interpreted as covering the per-record I/O
methods. If a future phase needs a `StartSyncOperation`, it can
be added without changing the shape of the rest of the contract.

Standardised contract:

```cpp
namespace Kalburator::Sync {

class SyncOperation : public QObject
{
    Q_OBJECT
public:
    enum State { Pending, Running, Succeeded, Failed, Cancelled };

    State state() const noexcept { return m_state.load(); }
    QString errorString() const { return m_errorString; }
    bool isFinished() const noexcept;  // Succeeded|Failed|Cancelled

    /// Best-effort cancellation. Default sets a flag the operation's
    /// run() body checks. Subclasses may override (e.g. to call
    /// QNetworkReply::abort()). Idempotent.
    virtual void cancel();

signals:
    void started();
    void progress(int current, int total);
    void finished();    // exactly once, regardless of terminal state

protected:
    void setState(State);          // emits started / finished as appropriate
    void setError(const QString&); // implies setState(Failed)
    bool cancelRequested() const noexcept;

private:
    std::atomic<State> m_state{Pending};
    std::atomic<bool>  m_cancelRequested{false};
    QString            m_errorString;
};

// Existing subclasses; F2 confirms the result-accessor shape and
// locks down the contract. No new sibling types in F2.
class FetchOperation  : public SyncOperation { /* items(), ctag() */ };
class PushOperation   : public SyncOperation { /* pushedIds(), failedIds() */ };
class DeleteOperation : public SyncOperation { /* deletedIds() */ };

} // namespace Kalburator::Sync
```

`SyncBackend`'s I/O surface becomes operation-only:

```cpp
class SyncBackend : public QObject, public IBlobBackend
{
    Q_OBJECT
public:
    // ── Operation-returning per-record I/O (existing shapes) ──
    virtual FetchOperation*  fetchItems(const QString &calendarId) = 0;
    virtual PushOperation*   pushItems(
        const QString &calendarId,
        const QList<KCalendarCore::Incidence::Ptr> &items,
        const TranscodingPlan &plan = TranscodingPlan{}) = 0;   // ← F2 adds plan
    virtual DeleteOperation* deleteItems(
        const QString &calendarId,
        const QStringList &uids) = 0;

    // ── Per-sync setup; stays as-is in F2 ──
    virtual void startSync(
        const QString &calendarId,
        const QMap<QString, QString> &stagedDeletions = {},
        const TranscodingPlan &plan = TranscodingPlan{}) = 0;

    // ── REMOVED in F2 ──
    // void loadItems(MemoryCalendar*, ...);   // already [[deprecated]]
    // void storeItems(MemoryCalendar*, ...);  // sync, replaced by pushItems
    // void updateItem(MemoryCalendar*, ...);  // sync, replaced by pushItems
    // void writeFinished(calId, ok, err) signal;   // → SyncOperation::finished
};
```

Operations are heap-allocated and parented to whatever owns
them (typically nullptr — the awaiter takes ownership and
deletes via `deleteLater()` after `finished` fires). The existing
operation-tracking helpers on `SyncBackend`
(`pendingOperations()`, `cancelOperationsFor(calId)`,
`cancelAllOperations()`) survive unchanged.

### Cancellation propagation — the contract

**Caller side.** Caller obtains a `QFuture<SyncResult>` (or
`<QList<SyncResult>>`) from `runSync(...)`. To cancel, caller
invokes `QFuture::cancel()`. This is the only cancellation
channel. There is no public `cancelSync()` slot.

**Observation.** `SyncEngine` constructs a `QFutureWatcher` for
each in-flight run and connects `QFutureWatcher::canceled` to a
private slot `SyncEngine::onCancelObserved()`, which posts a
queued-connection slot to the worker (`SyncEngineWorker::observeCancel()`).
The worker's slot sets `m_cancelled.store(true)` and emits
`cancellationObserved` (a private signal on the worker).
Concretely: there is one well-defined cancellation observation
point, queued onto the worker's event loop, which means
cancellation can never race the worker's hot path mid-instruction.

**Propagation in the hot path.** The worker's per-mapping driver
checks `m_cancelled.load()` at:

1. The top of every iteration over `m_syncMappings` (multi-mapping
   form only).
2. The top of every per-record iteration in the apply phase.
3. Every `await<Op>(...)` call: the inner `QEventLoop` is also
   connected to `cancellationObserved`, so `await` exits as soon
   as cancellation is observed even if the operation has not
   finished. When this happens, `await` calls `op->cancel()` and
   re-enters the loop briefly waiting for the operation's own
   teardown to settle (operations are not pre-emptible at the
   per-record level once started).
4. Each phase transition (fetch → diff → conflict → apply →
   baseline).

On observing cancellation, the worker:

- Stops dispatching new operations.
- Awaits any in-flight operation's natural settlement (via the
  brief re-entry loop in `await`).
- Populates a `SyncResult{success=false, cancelled=true, ...}`
  for any mapping that was mid-flight when cancellation arrived.
- For the multi-mapping form, fills queue slots that never
  started with `SyncResult{cancelled=true, skipped=true}`.
- Calls `m_currentSingleIface->reportCanceled()` (or the multi
  equivalent) followed by `reportFinished()`.

**Pause-resume interaction.** If `QFuture::cancel()` arrives while
the worker is in `QEventLoop::exec()` waiting for
`resumeAfterConflictResolution(...)`, the same
`cancellationObserved` connection wakes the conflict-pause loop.
The worker's pause path is:

```cpp
QEventLoop pauseLoop;
QObject::connect(this, &SyncEngineWorker::resumeReceived,
                 &pauseLoop, &QEventLoop::quit);
QObject::connect(this, &SyncEngineWorker::cancellationObserved,
                 &pauseLoop, &QEventLoop::quit);
pauseLoop.exec();
if (m_cancelled.load()) {
    // tear down via cancellation path; conflict left in
    // SyncConflictStore for the next run to pick up
    return CancelledOutcome;
}
// resumeReceived path: read the user's choice, continue
```

Conflicts that were detected but never resolved (because the
user cancelled instead of resolving) remain in the persistent
`SyncConflictStore` per existing semantics. The next sync run
picks them up.

### `SyncResult` — error model

The `SyncResult` struct grows two booleans:

```cpp
struct SyncResult {
    // Existing fields preserved verbatim:
    bool    success = false;
    QString errorMessage;
    int     itemsAdded = 0;
    int     itemsUpdated = 0;
    int     itemsDeleted = 0;
    int     conflictsDetected = 0;
    int     conflictsResolved = 0;
    QString mappingId;
    QDateTime startedAt;
    QDateTime finishedAt;

    // New in F2:
    bool    cancelled = false;   // true iff the run observed QFuture::cancel()
    bool    skipped   = false;   // true iff a queue slot never started
                                 //   (multi-mapping form, cancellation cut
                                 //   the queue short)
};
```

Existing predicates (`success` true means "ran to completion
without errors") are unchanged. Cancellation is a third state
distinct from success and failure: `success == false &&
cancelled == true && errorMessage.isEmpty()`. Skipped is a
fourth: `success == false && skipped == true && errorMessage.isEmpty()`.

The field set above is the proposed shape. Plan Group 0
includes a `git grep` of `SyncResult` callers across all three
worktrees to confirm none assume zero-value invariants the new
fields would violate (e.g. "`errorMessage` empty implies success");
if any do, the predicate set is documented inline in the struct's
header during the same task.

### Backend implementations — concrete operation classes

Each existing concrete `SyncBackend` (`LocalBackend`,
`RemoteBackend`, `OrgBackend`, `MockBackend`, `AkonadiBackend`,
`DecSyncBackend`, `HolidaySubscriptionBackend`,
`SubscriptionBackend`) implements `fetchItems` / `pushItems` /
`deleteItems` returning the standardised `SyncOperation`
subclasses. Most already do; F2 makes the contract uniform:

- **Synchronous-internal backends** (`LocalBackend`, `OrgBackend`,
  `MockBackend`): `fetchItems` / `pushItems` / `deleteItems`
  return a `SyncOperation` whose `run()` is invoked via
  `QMetaObject::invokeMethod(..., QueuedConnection)` immediately,
  executes synchronously inside the slot, and emits `finished`.
  The operation is a thin wrapper around the pre-F2 synchronous
  body. For `pushItems`, the body now invokes the shared
  transcoding helper (Phase E logic), so updates and creates
  flow through the same path.

- **Asynchronous-internal backends** (`RemoteBackend`,
  `DecSyncBackend`, `AkonadiBackend`): the operation's lifetime
  spans real async work (`QNetworkReply` for `RemoteBackend`;
  DecSync watcher callbacks; KIO jobs for `AkonadiBackend`).
  These already have natural cancellation hooks
  (`QNetworkReply::abort()`, etc.) which the operation's
  `cancel()` override invokes.

The transcoding logic that Phase E moved into
`storeItems`/`updateItem` migrates into a shared private helper
invoked by `PushOperation::run()` (which now handles both creates
and updates with the new `TranscodingPlan` parameter). No
semantic change; `transcodingWarning` continues to fire from the
backend the same way it did post-Phase-E.

### `MockBackend` — failure injection cleanup

The FINDINGS-noted asymmetry (`OnPush` vs `OnStoreItems` checked
differently across `pushItems` and `storeItems` paths) collapses
naturally: the operation-handle methods are the only path, and a
single `setFailurePoint(FailurePoint, mode)` API drives them
symmetrically. The two ways to inject a "push failure" become
one. The fixup applied in commit `438e545` becomes redundant and
can be simplified.

### `IDomainAdapter`, `CalendarDomainAdapter`, `BlobDomainAdapter`

Unchanged in shape; `applyChanges` no longer calls
`backend->storeItems(...)` / `updateItem(...)` synchronously —
it calls `await(backend->pushItems(...))` and friends through a
helper passed in by the engine (so the adapter stays agnostic of
the awaiter implementation).

The adapter's call signature for `applyChanges` does not change
publicly; the `SyncBackend*` parameter is the same. Internally,
the adapter's body is rewritten to use the operation-handle
methods. Adapter callers (the engine) pass the awaiter via a
constructor or a setter; the adapter does not own its own event
loop.

### `IBlobBackend` and concrete blob backends

Unchanged. The six WildPalms plugin backends and the
libkalburator-side `LocalBlobBackend` / `MockBlobBackend` keep
their synchronous methods. Adapters call them inline from the
worker thread.

## Test plan

### Existing tests that must stay green

- **libkalburator** — 23 tests (the F1 baseline). Several need
  source-level updates because they hold concrete `SyncEngine*`
  pointers and call `runSync(behavior)` (void) + connect to
  `allSyncsCompleted`. Migration is mechanical:
  - `tst_calendar_sync_full`, `tst_calendar_sync_oneway`,
    `tst_calendar_conflict`, `tst_calendar_transcoding_warning`,
    `tst_calendar_first_sync_via_blob_engine`,
    `tst_calendar_subsequent_sync_uses_blob_view`,
    `tst_calendar_sync_error_recovery` — replace
    `engine.runSync(behavior); waitForSignal(allSyncsCompleted);`
    with `engine.runSync(behavior).waitForFinished();`.
  - `tst_engine_blob_one_shot`, `tst_engine_unified_boundary`
    — migrate from `runBlobTwoWay`/`runBlobMirror` to
    `runSync(id).waitForFinished()` against a one-mapping engine
    configured with the relevant blob backends. The adapter and
    mapping setup may require a small test helper.
  - `tst_synctransaction` — uses concrete `LocalBackend*` and
    calls `storeItems` / `updateItem` directly (per FINDINGS
    "Virtual function default arguments must be redeclared on
    overrides for concrete-type callers"). Migrates to
    `await(localBackend->pushItems(...))` with the new
    `TranscodingPlan` parameter. The defaulted-parameter problem
    survives because `pushItems` keeps its default `plan = {}`,
    but the migration consolidates the calls onto one method.
  - `tst_mockbackend` — collapses `OnPush` vs `OnStoreItems`
    coverage into one path.
  - Adapter unit tests (`tst_calendar_domain_adapter`,
    `tst_blob_domain_adapter`) — unaffected; they exercise
    `diff` / `merge` which remain synchronous.
  - Baseline-store tests
    (`tst_blob_baseline_store_per_record_keys`,
    `tst_blobbaselinestore`) — unaffected.

- **PlanStan** — 96/120 baseline preserved. The four
  `EXCLUDE_FROM_ALL` sync-workflow tests
  (`tst_sync_conflicts`, `tst_sync_caldav_conflicts`,
  `tst_sync_error_recovery`, `tst_sync_dialog`) each touch the
  engine API and migrate. The remaining 22 non-running tests
  per FINDINGS triage stay non-running (unrelated subsystems).

- **WildPalms** — 73/73 baseline preserved. Two known
  order-dependent flakes (`tst_pluckerbackendplugin`,
  `tst_calendar_v2`) continue to bite intermittently per
  existing FINDINGS; F2 does not introduce a third.

### New tests added in this phase

Single new test executable, `tst_engine_cancellation.cpp`, in
`libkalburator/tests/calendar/` (uses the existing stub-`ISyncHost`
integration harness from D.0). Suite organised so the cancellation
contract reads as one document. Cases:

- **C1 — Cancel before start.** `QFuture` cancelled before the
  worker observes it. Verify `reportCanceled` + `reportFinished`
  fire, no operations dispatched, returned `SyncResult` has
  `cancelled == true` and `skipped == true` (depending on form).
- **C2 — Cancel during fetch.** `QFuture::cancel()` invoked while
  a `FetchOperation` is in flight (configure `MockBackend` to
  block its fetch via a synchronisation primitive). Verify the
  operation's `cancel()` is invoked, the worker tears down without
  entering apply phase, no partial writes.
- **C3 — Cancel during apply.** Cancel mid-loop. Verify the
  in-flight operation completes (per-record granularity stops
  dispatching new ops; the current one finishes), partial result
  reflected in the final `SyncResult`, no further ops dispatched.
- **C4 — Cancel during conflict pause.** Cancel while worker is
  in `QEventLoop::exec()` waiting for
  `resumeAfterConflictResolution`. Verify wait-loop exits, no
  further work, conflicts left in the persistent
  `SyncConflictStore`.
- **C5 — Cancel multi-mapping mid-queue.** Cancel after mapping 2
  of 5 finishes. Verify mappings 3–5 land in the result list as
  `{cancelled, skipped}` sentinels; mappings 1–2 carry their real
  results.
- **C6 — Idempotent cancel.** Cancelling an already-cancelled
  future is a no-op.
- **C7 — Cancel after finished.** Cancelling an already-finished
  future is a no-op (Qt's `QFutureInterface` semantics already
  guarantee this; assert here documents the contract).

Plus a handful of positive `QFuture` tests in the same file:

- `runSync(id).waitForFinished()` returns a sane `SyncResult`
  with `cancelled == false`.
- `runSync()` (multi-mapping) returns a `QList<SyncResult>` of
  the right length (one entry per enabled mapping).
- `QFutureWatcher::finished` fires exactly once.
- `QFuture::progressValue()` ticks during the run (covers the
  worker's `setProgressValue` calls).

The cancellation suite is the load-bearing TDD work. The positive
tests document the new API surface and catch regressions.

### Acceptance criteria

- libkalburator standalone: 23 → 24 ctest executables (one new:
  `tst_engine_cancellation`). All pass.
- All existing libkalburator tests (the 23 from F1) continue to
  pass after their mechanical migration.
- `git grep "loadItems\\|storeItems\\|updateItem\\|writeFinished" libkalburator/src/`
  returns zero hits (the synchronous overloads and the
  `writeFinished` signal are deleted in Group 4). `pushItems`
  survives as the operation-returning async method and continues
  to appear, now carrying a `TranscodingPlan` parameter.
- `git grep "runBlobTwoWay\\|runBlobMirror" libkalburator/src/ libkalburator/tests/`
  returns zero hits.
- `git grep "syncCompleted\\|allSyncsCompleted\\|cancelSync"
  libkalburator/src/`
  returns zero hits.
- PlanStan baseline: 96/120 (per F.0 triage; F2 doesn't shift
  this).
- WildPalms baseline: 73/73 (modulo the two known flakes).
- `verify-all.sh` exit 0.

## Migration order (within F2)

The plan doc enumerates concrete tasks; this section sketches the
shape so the plan author and the implementer have a starting
point.

### Group 0 — Prep

1. Add the cancellation TDD scaffolding: `tst_engine_cancellation.cpp`
   stub with C1 written against the current API (will compile but
   skip until the new API is in place). Locks the contract before
   the implementation drifts.

### Group 1 — Operation contract

2. Standardise the `SyncOperation` base in
   `src/calendar/syncoperation.{h,cpp}`: lock down the `State`
   enum, virtual `cancel()` contract, idempotency, and
   `setError`/`setState` semantics. Existing subclasses
   (`FetchOperation`, `PushOperation`, `DeleteOperation`)
   conform; gaps are filled where the existing implementation is
   under-specified. No new sibling types.
3. Add a `TranscodingPlan` parameter to `pushItems` on
   `SyncBackend` and propagate to every concrete backend.
   Implementations move the transcoding helper out of
   `storeItems`/`updateItem` into the body of
   `PushOperation::run()` (shared private helper). The sync
   `storeItems`/`updateItem` paths still exist; they delegate to
   `pushItems` internally so behaviour is preserved.
4. Update `MockBackend` first (because tests pin behaviour):
   collapse the `OnPush`/`OnStoreItems` failure-injection
   asymmetry, ensure `pushItems` honours the same combined check.
   Migrate `tst_mockbackend` to the unified shape.
5. Bring the remaining concrete backends (`LocalBackend`,
   `RemoteBackend`, `OrgBackend`, `AkonadiBackend`,
   `DecSyncBackend`, `HolidaySubscriptionBackend`,
   `SubscriptionBackend`) into conformance with the standardised
   contract. Per backend, the diff is bounded — most already
   implement the operation methods.
6. Both old (sync) and new (async) surfaces coexist for the
   duration of Group 1, gated behind the engine's not yet
   calling the new methods. `verify-all.sh` green at end of group.

### Group 2 — Engine async API

7. Add the new `runSync(...)` overloads returning `QFuture`s
   alongside the existing void forms. Internally both routes go
   through the same worker driver. Verify-all passes.
8. Migrate the engine's worker to call `pushItems` (with
   `TranscodingPlan`) and the other operation-handle methods on
   `SyncBackend` exclusively. The synchronous `loadItems` /
   `storeItems` / `updateItem` paths become dead code at the
   engine layer (they still exist as delegating shims from
   Group 1 step 3, but no engine code calls them).
9. Add cancellation propagation: `QFutureWatcher` on the engine,
   `cancellationObserved` signal on the worker, `m_cancelled`
   atomic, `await(...)` helper on the worker, conflict-pause
   loop wired to the cancellation channel.
10. Implement C1–C7 in `tst_engine_cancellation`; gate Group 2
    completion on the suite passing.

### Group 3 — Consumer migration + signal cleanup

11. Migrate libkalburator's own `tests/calendar/` to use
    `runSync(...).waitForFinished()` instead of
    `runSync(behavior); waitForSignal(allSyncsCompleted)`.
    Migrate `tst_engine_blob_one_shot` and
    `tst_engine_unified_boundary` to the unified entry point.
12. Migrate PlanStan: `SyncProgressManager` to `QFutureWatcher`;
    the four `EXCLUDE_FROM_ALL` sync-workflow tests; any other
    call sites of `runSync(behavior)` / `cancelSync()` /
    `syncCompleted` / `allSyncsCompleted`. Run PlanStan ctest.
13. Migrate WildPalms: `syncrunner_wp.cpp` from `runBlobTwoWay` /
    `runBlobMirror` to `runSync(id).waitForFinished()`. Migrate
    the four `tst_*_v2.cpp` files plus `tst_palmbackend_roundtrip`.
    Watch for the FINDINGS pattern of plugin-sub-repo ripple;
    verify-all is the only reliable signal.

### Group 4 — Cleanup

14. Delete the void `runSync(...)` overloads from `SyncEngine`.
    Delete `cancelSync()`. Delete the `syncCompleted` /
    `allSyncsCompleted` signals.
15. Delete the synchronous methods on `SyncBackend`: `loadItems`
    (already `[[deprecated]]`), `storeItems`, `updateItem`, and
    the `writeFinished` signal. Their delegating-shim bodies
    have no callers post-Group 3.
16. Delete `SyncEngine::runBlobTwoWay` / `runBlobMirror` from the
    header and the implementation. The bodies have no callers
    post-Group 3.
17. Refresh baselines (`baselines/libkalburator-worktree-ctest.txt`)
    to include `tst_engine_cancellation`. Update CLAUDE.md
    references to the new API (in particular the
    "`SyncEngine::runSync(behavior)` (no `mappingId` arg) — use
    this and wait on `allSyncsCompleted`" guidance, which
    becomes "use `runSync(...).waitForFinished()`"). Update
    FINDINGS to mark the leak and the `writeFinished`
    wrapper-fragility entries
    `[RESOLVED in v0.14 — see commit SHA]`.

### Group 5 — Doc + tag

18. Update `04q-phase-f2-threading-design.md` status line to
    "landed YYYY-MM-DD". Update `CURRENT-STATUS.md`,
    `ROADMAP.md`, `FINDINGS.md`. Tag `v0.14-phase-f2-threading`
    on libkalburator's HEAD per `ROADMAP.md`'s convention. Tag
    landing requires user authorisation per project ground
    rules.

## Risks & gotchas

- **Cross-repo verify-all is the only reliable signal.** F1's
  Tasks 10/13 surfaced this twice — IBlobBackend changes rippled
  to six WildPalms plugin sub-repos that the libkalburator-side
  build doesn't see. F2's `SyncBackend` operation-handle change
  is similarly cross-repo (PlanStan tests hold concrete
  `LocalBackend*`; WildPalms's `syncrunner_wp.cpp` is in scope).
  Every Group's exit gate is `verify-all.sh exit 0`, not
  "libkalburator green."

- **Forward declarations of removed method names.** `cancelSync`,
  `runBlobTwoWay`, `runBlobMirror`, `syncCompleted`,
  `allSyncsCompleted` may appear in consumer headers as
  forward-decl-style references (e.g. signal declarations in
  derived classes; `Q_OBJECT`-meta references). Run
  `git grep` across all three repos for each name as Group 3
  begins. The F1 Task 13 finding ("Phase F1 Task 13 caught a
  missed BackendPluginManager type ref") is the load-bearing
  precedent.

- **Operation cancellation contract for async backends.**
  `RemoteBackend`'s push operation wraps `QNetworkReply`. Calling
  `op->cancel()` triggers `QNetworkReply::abort()`, which emits
  `finished()` with an error. The operation's state must
  transition to `Cancelled`, not `Failed`, when `cancel()` was
  the cause. This requires a small flag inside the operation
  ("we asked for the cancel; map the resulting error to
  Cancelled"). Easy to get wrong; the cancellation tests should
  cover this for at least one async backend (extend C2/C3 with a
  `RemoteBackend` variant once it's wired).

- **Per-record cancellation in the apply phase.** The contract
  says "an in-flight operation completes; no further ops
  dispatched." If a `PushOperation` internally batches N items,
  it does not stop halfway. This is a deliberate trade —
  pre-empting a batched push would require stricter contracts
  on every backend's batching logic, and most backends don't
  have a natural mid-batch-cancel. The C3 test pins the
  contract explicitly.

- **`QFuture::cancel()` semantics.** Qt's `QFuture::cancel()`
  sets the cancel flag synchronously, but the cancellation is
  not synchronously observed by the producer. The producer
  (here: `SyncEngine`) sees it via `QFutureWatcher::canceled`
  on its own thread, then propagates to the worker via queued
  connection. There is therefore a small window between
  `cancel()` returning and the worker tearing down. Callers
  must use `QFuture::isFinished()` (or the watcher's
  `finished` signal) to know teardown is complete. Document
  this in the public-API doxygen on `runSync(...)`.

- **`waitForFinished` on a future from this engine.** Calling
  `runSync(id).waitForFinished()` from the engine's *caller*
  thread is fine — the worker runs on a separate thread, and
  the caller blocks until the worker finishes. Calling it from
  the worker thread itself (e.g. inside an adapter) would
  deadlock. The adapter contract forbids this, but the doxygen
  on `runSync` must say it explicitly.

- **The two known WildPalms flakes
  (`tst_pluckerbackendplugin`, `tst_calendar_v2`).** Per
  FINDINGS, these are unrelated destructor-order issues that
  pre-date F2 and bite intermittently in full-suite runs.
  Re-run on regression; if a second run is green, treat as
  noise. F2 does not block on fixing them.

- **Nested `QEventLoop` on the worker thread.** The `await<Op>(...)`
  helper runs an inner `QEventLoop::exec()` on the worker thread
  while the worker's outer thread loop is itself running. This is
  by design — cancellation signals must process during await —
  but it means adapter code invoked between yield points cannot
  assume single-flight execution. Anything queued to the worker
  while `await` is in its inner loop runs at the next event-loop
  iteration, including before `await` returns. The cancellation
  contract relies on this property; other code in the worker hot
  path must tolerate it. The C2 / C3 / C4 cancellation tests
  exercise the nested-loop teardown explicitly.

## Cross-references

- `04k-engine-merger-roadmap.md` — "Threading & async" section is
  the canonical statement of intent F2 implements.
- `04p-phase-f1-unify-design.md` — F1 design; decisions 1, 5, and
  the FINDINGS-derived gotchas (worker-class boundary, AUTOMOC
  invalidation, deprecation-shim pattern, cross-repo verify-all)
  are load-bearing precedents.
- `~/dev/refactor-engine-merger/FINDINGS.md` —
  "`SyncEngine::runSync(mappingId)` is leaky",
  "Wrapper commit() lost error detection when switching from
  pushItems to storeItems",
  "MockBackend missing failure injection on updateItem and OnPush
  in storeItems",
  "Worker-class collapse: file boundary, not QObject boundary".
  All four are F2 inputs.
- `~/dev/refactor-engine-merger/CURRENT-STATUS.md` — F2 status
  flips from "next" to "in flight" when the plan lands and the
  first task is dispatched.
