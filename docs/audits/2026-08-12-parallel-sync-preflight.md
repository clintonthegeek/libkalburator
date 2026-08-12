# Parallel-Sync Pre-Flight Audit: Registry/Shape Concurrency Safety

**Date:** 2026-08-12
**Branch:** `parallel-sync`
**Task:** Task 0 of the parallel-sync campaign (read-only audit, no production
code changes)

## Question

The parallel-sync design (Task 2 onward) replaces `SyncEngine`'s single
`SyncEngineWorker` on one `QThread` with a pool of N workers on N threads,
all running `dispatchSync()` concurrently for different mappings within one
sync run. Every worker holds the *same* `BackendRegistry *m_registry`
(`src/engine/syncengine_p.h:408`) and the *same*
`const Kalburator::Shape::ShapeRegistries &m_shape`
(`src/engine/syncengine_p.h:401`) — both injected once at `SyncEngine`
construction and shared by reference/pointer across all N workers.

This audit answers one question: **do the exact call paths
`SyncEngineWorker::dispatchSync()` uses on `m_registry` and `m_shape` mutate
any shared state at sync time?** If yes, N threads calling them
concurrently is a data race, and the pool design (Task 2) must change to
account for it before any code is written. This is a go/no-go gate for
everything downstream.

**Scope extension (2026-08-12, same day):** the original pass covered
`BackendRegistry` and `ShapeRegistries` only. After the NO-GO finding was
confirmed, the controller extended this audit to cover the remaining four
pointers `SyncEngineWorker::setDependencies()` also hands identically to
all N workers — `ISyncHost *host`, `ICalendarCollection *collection`,
`BaselineStore *baselineStore`, `IMassDeleteGuard *massDeleteGuard` — see
"Remaining shared worker state" below, added after the original
`BackendRegistry`/`ShapeRegistries` sections and folded into the amended
`Verdict`.

`SyncEngineWorker::dispatchSync()` is defined at
`src/engine/syncengine.cpp:2238`. All `m_shape`/`m_registry` accesses cited
below were confirmed to be inside `SyncEngineWorker` methods (`dispatchSync`
at line 2238 and `unifiedContinueAfterConflicts` at line 3037), not
`SyncEngine` methods — `SyncEngine` itself holds a separate, non-const
`Kalburator::Shape::ShapeRegistries &m_shape` (`src/engine/syncengine.h:541`)
which is the DI owner used only at composition/plugin-load time, not the
object referenced concurrently by the N workers.

## BackendRegistry

**Verdict: SAFE** (for the read path `dispatchSync()` actually uses; see
condition below for the write path, which is out of scope of the hot loop
but real).

`src/sync/backendregistry.h` / `src/sync/backendregistry.cpp`, full read.

**Q1 — Does `backendInstance(const QString&)` mutate any member (lazy
construction, cache insert, LRU touch, `QHash`/`QMap::operator[]` on a
missing key)?**

No.

```cpp
// backendregistry.cpp:24-27
SyncBackendBase* BackendRegistry::backendInstance(const QString &backendId) const
{
    return m_instances.value(backendId, nullptr);
}
```

`m_instances` is a `QMap<QString, SyncBackendBase*>` (`backendregistry.h:106`,
not a `QHash`, but the same insert-on-miss hazard applies to
`QMap::operator[]` too). `backendInstance()` uses `.value(key, default)`,
which — for both `QMap` and `QHash` — is a pure lookup with no side effect
on the container when the key is absent; it never touches `operator[]`.
Every call site in `SyncEngineWorker::dispatchSync()` and its helpers
(`src/engine/syncengine.cpp:1562, 1577, 1659, 1760-1763, 1945-1948,
2149-2152, 2248-2251, 3063-3066`) calls `backendInstance()`, never
`operator[]` or any mutating accessor. Confirmed safe for N concurrent
readers.

**Q2 — Does any other method mutate the instance map, and can it be called
while `isSyncing()` is true?**

Yes, two methods mutate `m_instances`, and there is no guard preventing
them from running during a sync:

```cpp
// backendregistry.cpp:11-15
void BackendRegistry::registerBackendInstance(const QString &backendId, SyncBackendBase *backend)
{
    m_instances[backendId] = backend;     // operator[] — inserts/overwrites
    emit backendInstanceRegistered(backendId);
}

// backendregistry.cpp:17-22
void BackendRegistry::unregisterBackendInstance(const QString &backendId)
{
    if (m_instances.remove(backendId)) { ... }
}
```

Both are called exclusively from `ProviderManager`
(`src/sync/providermanager.cpp:275` and `:299`), which is reached from:
`ProviderManager::addProvider()` (:103), `::removeProvider()` (:128),
`::onProviderConnectionStateChanged()` (:229), and
`::onProviderCollectionsChanged()` (:245) — i.e. user-driven account
add/remove/reconnect actions, surfaced in the consuming app (PlanStan) via
`CollectionController::removeProvider()`
(`~/dev/PlanStan/src/controllers/collectioncontroller.cpp:1558`) and the
topology Apply path (`~/dev/PlanStan/src/sync/topology/synctopologywidget.cpp:2761,2795`).

Grepping `src/engine/**` and `src/sync/syncruncoordinator.cpp` for any
guard: `SyncRunCoordinator::runSync()` checks `m_engine->isSyncing()`
(`src/sync/syncruncoordinator.cpp:43`) but only to reject *starting a second
sync run* — nothing in `BackendRegistry`, `ProviderManager`, or the
`SyncEngine`/`SyncEngineWorker` pair checks `isSyncing()` before allowing a
provider add/remove. **This hazard is pre-existing**, not introduced by
parallel-sync: even today, with one worker thread, `backendInstance()` runs
on the worker thread while `registerBackendInstance`/`unregisterBackendInstance`
can run on the GUI thread with zero synchronization between them. Going
from 1 to N reader threads does not create a *new* race here — the
existing race between the (GUI-thread) writer and any (worker-thread)
reader is the same shape regardless of how many readers there are.

**Q3 — Is there any `QMutex` already guarding it?**

No. `grep -n "QMutex" src/sync/backendregistry.h src/sync/backendregistry.cpp`
returns nothing.

**Condition:** `backendInstance()` itself performs no mutation and is safe
for N concurrent worker threads reading it during a sync run. The
unguarded, pre-existing race between GUI-thread provider add/remove and any
sync-thread read is real but not created or worsened by this campaign —
carry it forward as a documented, separately-tracked hazard (candidate:
`ProviderManager` should reject add/remove while `isSyncing()`, or
`BackendRegistry` should take a `QMutex`), not a blocker for Task 2.

## ShapeRegistries

**Verdict: UNSAFE** — one concrete, in-hot-path mutation was found.

`ShapeRegistries` (`src/shape/shaperegistries.h:21-25`) is a plain struct
bundling three registries: `TransformationRegistry transformation`,
`DomainRegistry domain`, `DomainOperationsRegistry operations`. Each was
read in full (`.h` + `.cpp`).

**Q1 — Do the lookup paths `dispatchSync()` uses (differ/merger
acquisition, `srcToCanon`/`tgtToCanon` pipeline compilation, `DomainOperations`
lookup) mutate anything — memoisation caches, lazily-compiled pipelines,
`mutable` members?**

`grep -rn "mutable" src/shape/*.h` returns exactly one hit:
`src/shape/transformationregistry.h:133`:

```cpp
// transformationregistry.h:118-133
/// Internal: mark a domain frozen. Called by compile() on its
/// successful non-identity branch. The frozen set is logically a
/// "has-been-queried" cache: once a compile() consults the edge
/// graph for a domain, that graph is fixed. Hence `freeze()` is
/// `const` and `m_frozenDomains` is `mutable`, in the standard
/// pattern of caching the result of a logically-pure query.
void freeze(const DomainId& d) const;
...
mutable QSet<DomainId> m_frozenDomains;
```

```cpp
// transformationregistry.cpp:61-64
void TransformationRegistry::freeze(const DomainId& d) const
{
    m_frozenDomains.insert(d);
}

// transformationregistry.cpp:153-164
std::optional<Pipeline> TransformationRegistry::compile(Shape from, Shape to) const {
    auto result = compileImpl(from, to);
    if (result.has_value() && from.domain == to.domain
        && !(from == to) && !to.isAny() && !from.isAny()) {
        freeze(from.domain);          // <-- unsynchronized write
    }
    return result;
}
```

`compile()` is exactly the method `dispatchSync()` calls to build
`srcToCanon`/`tgtToCanon`/`canonToTgt`/`canonToSrc`:

```cpp
// syncengine.cpp:2373-2377 (SyncEngineWorker::dispatchSync)
const auto &reg = m_shape.transformation;
std::optional<Kalburator::Shape::Pipeline> srcToCanon = reg.compile(srcShape, canonical);
std::optional<Kalburator::Shape::Pipeline> tgtToCanon = reg.compile(tgtShape, canonical);
std::optional<Kalburator::Shape::Pipeline> canonToTgt = reg.compile(canonical, tgtShape);
std::optional<Kalburator::Shape::Pipeline> canonToSrc = reg.compile(canonical, srcShape);
```

and again in `SyncEngineWorker::unifiedContinueAfterConflicts()`
(`syncengine.cpp:3037`):

```cpp
// syncengine.cpp:3081-3084
const auto &treg = m_shape.transformation;
...
const auto canonToTgt = treg.compile(m_unifiedCanonical, tgtShape);
const auto canonToSrc = treg.compile(m_unifiedCanonical, srcShape);
```

So **every single mapping dispatched by every worker calls `compile()`
four times**, and the first non-identity compile for any given domain
mutates `m_frozenDomains` — a plain `QSet<DomainId>` — with no lock. Once N
workers are running concurrently, two workers dispatching mappings in the
same domain (the overwhelmingly common case — e.g. two calendar mappings)
can call `compile()` and hit `freeze()`'s `m_frozenDomains.insert(d)`
concurrently on the *same* `TransformationRegistry` instance (shared via
the single `const ShapeRegistries&` all N workers hold). `QSet::insert()`
has no internal synchronization; two concurrent writers to the same
`QSet` instance is a data race that can corrupt the hash table's internal
bucket state, not merely produce a stale read — this is the accidental-
mutation-via-const-method hazard the brief called out, materialized for
real.

All other lookup paths `dispatchSync()` touches are genuinely read-only:

- `DomainRegistry::definitionFor()` — `constFind()`, no insertion
  (`domainregistry.cpp:19-22`). Called at `syncengine.cpp:2349-2350`.
- `DomainOperationsRegistry::operationsFor()` — `m_byDomain.find(domain)`
  on a `const`-qualified method, which resolves to the const overload
  (read-only, no insertion) (`domainoperationsregistry.cpp:16-20`). Called
  at `syncengine.cpp:2361-2362` and `:3087-3088`.
- `TransformationRegistry::catalogueFor()`, `::edgesFrom()`,
  `::canonicalFor()`, `::canonicalSpine()`, and the private `::findEdge()`
  all use `constFind`/`value`/`equal_range` on `const`-qualified
  `QHash`/`QMultiHash` members — none mutate
  (`transformationregistry.cpp:46-54, 93-97, 176-178, 212-220`).
- `TransformationRegistry::inspect()` (used at `syncengine.cpp:1768`) calls
  the private `compileImpl()`, **not** `compile()` — it deliberately
  bypasses `freeze()` (`transformationregistry.cpp:166-170`, and the header
  comment at `:114-116`: "`compile()` freezes the source domain; `inspect()`
  must not"). Safe.
- Differ/merger acquisition: `dd->createCanonicalDiffer()` /
  `dd->createCanonicalMerger()` (`syncengine.cpp:2692-2693`) are `const`
  virtual factory methods (`domaindefinition.h:31-32`) whose every concrete
  implementation (`calendardomaindefinition.cpp:30-39`,
  `contactsdomaindefinition.cpp:26-33`, and the todo/note/outline/blob
  equivalents) does a plain `std::make_unique<...>(...)` — a fresh heap
  allocation per call, with no shared/cached/static state. The resulting
  `unique_ptr`s are stored in the *calling worker's own*
  `m_unifiedDiffer`/`m_unifiedMerger` members (`syncengine_p.h:395-396`),
  never shared across workers.

**Q2 — Are the compiled pipeline objects returned by value/unique_ptr per
call, or shared?**

By value: `compile()` returns `std::optional<Pipeline>`
(`transformationregistry.h:80`), a value type, copied out to each caller.
Each `dispatchSync()` invocation gets its own independent `Pipeline`
instances (`srcToCanon`/`tgtToCanon`/`canonToTgt`/`canonToSrc` are local
variables) — no shared mutable pipeline object across workers. This part
is safe; the only problem is the `freeze()` side effect inside `compile()`
itself, which happens before the value is returned.

## Remaining shared worker state (scope extension, appended 2026-08-12)

The controller extended Task 0's scope after the first pass to close a gap
I flagged myself: `BackendRegistry` and `ShapeRegistries` were not the only
state N `SyncEngineWorker` instances will share. `SyncEngineWorker::
setDependencies()` (`src/engine/syncengine_p.h:154-159`) takes six pointers;
every one will be handed the *identical* value across all N workers. Two
are already covered above (`BackendRegistry *registry`,
`ShapeRegistries` via the ctor). This section covers the remaining four:
`ISyncHost *host`, `ICalendarCollection *collection`,
`Kalburator::Storage::BaselineStore *baselineStore`, and
`Kalburator::Conflict::IMassDeleteGuard *massDeleteGuard`. Same evidence
standard: file:line, read the code, don't rely on grep alone.

### ICalendarCollection — SAFE (dead on the worker side)

`grep -rn "m_collection\b" src/engine/*.cpp src/engine/*.h` finds `m_collection`
assigned in `SyncEngineWorker::setDependencies()`
(`syncengine.cpp:1483`) and declared at `syncengine_p.h:410`, but **never
dereferenced anywhere in `SyncEngineWorker`'s own code** — the only other
hits are `SyncEngine`-side (lines 297, 636, 1009, all inside `SyncEngine`
methods defined before line 1449, i.e. the engine/GUI-thread owner, not the
worker). The worker's own code confirms this is deliberate, at
`syncengine.cpp:3172-3174`:

```cpp
// The converged writers (DefaultBlobWriter) ignore the host
// MemoryCalendar; do not source it. prepareForApply remains a no-op hook
// on the RecordWriter interface. (m_collection / setCollection stay for
// CalendarManager's separate use.)
```

The pointer is passed to every worker but genuinely unused by any worker
logic — vestigial. Zero risk from N workers holding it, because none of
them ever touch it.

### BaselineStore — SAFE (marshaled, verified exhaustively — no bypass found)

`BaselineStore` is documented as thread-affine at its own declaration,
`src/storage/baselinestore.h:24-26`: *"Not thread-safe. Callers must
serialize access to a given instance."* `SyncEngineWorker` addresses this
via `m_baselineStoreAnchor` (`syncengine_p.h:412-418`, comment: *"thread
anchor used to marshal BaselineStore access back to the thread that owns
it"*).

I greped every `m_baselineStore` occurrence in `syncengine.cpp` (28 hits)
and classified each by enclosing class using the function-boundary map
(`SyncEngine` methods end at line 1448; `SyncEngineWorker` methods start at
1449). The `SyncEngine`-side hits (lines 233, 302, 496-521, 637, 1010,
1196-1211, 1482) are the engine's own separate `m_baselineStore` member
(`syncengine.h`), accessed directly on the engine-owning thread — a
different variable, out of scope here (not shared across N workers; each
`SyncEngine` has exactly one).

The `SyncEngineWorker`-side hits are at lines 1992-1994, 2144, 2215-2216,
2676-2677, 2713, 2757, 3207-3208, 3478-3479, 3503, 3520 — ten call sites
across `dispatchFirstSync`, `harvestBaselinesAfterFirstSync`,
`dispatchSync`, and `unifiedContinueAfterConflicts`. I read every one.
**All ten** follow the identical pattern — copy the raw pointer into a
local (`bbs`), then dereference `bbs->` only inside a lambda passed to
`QMetaObject::invokeMethod(m_baselineStoreAnchor, lambda,
Qt::BlockingQueuedConnection)`. Representative example
(`syncengine.cpp:1992-1997`):

```cpp
if (m_baselineStore && m_baselineStoreAnchor) {
    bool hasExistingBaselines = false;
    Kalburator::Storage::BaselineStore *bbs = m_baselineStore;
    const QString mappingId = request.mapping.id;
    QMetaObject::invokeMethod(m_baselineStoreAnchor, [bbs, mappingId, &hasExistingBaselines]() {
        hasExistingBaselines = !bbs->baselinesForMappingV3(mappingId).isEmpty();
    }, Qt::BlockingQueuedConnection);
}
```

The bare `m_baselineStore` reads outside a lambda (e.g. `syncengine.cpp:2144`,
`if (!m_baselineStore) { ... return; }`) only test the pointer's nullness —
never dereference what it points to. I found **no bypass**: no site where
`m_baselineStore->something()` is called directly on the worker thread
outside the marshal. This directly answers the controller's specific
concern.

Because all N workers marshal onto the *same* `m_baselineStoreAnchor`
object (one `BaselineStore`, one owning thread, shared by construction —
`SyncEngine::setupWorkerConnections()` hands every worker the same
anchor), N concurrent `BlockingQueuedConnection` calls correctly serialize
on that one thread's event loop, each blocking its calling worker thread
until its own lambda finishes. Safe by construction under the N-worker
pool as currently written elsewhere in this file — no change needed here.

### IMassDeleteGuard — SAFE (marshaled, by documented interface contract)

`m_massDeleteGuard->confirmMassDelete(...)` is called directly from the
worker thread at `syncengine.cpp:3220-3222`, inside `dispatchSync`'s
`resolveMassDeleteGuard` lambda. Unlike `BackendRegistry`, this interface
**does** document its threading contract, at
`src/engine/imassdeleteguard.h:24-27`:

```cpp
/// Threading: the engine calls `confirmMassDelete` from a worker thread.
/// Concrete implementations that need to interact with a GUI must
/// marshal to the UI thread themselves (Qt::BlockingQueuedConnection
/// or equivalent).
```

i.e. libkalburator deliberately pushes the marshaling obligation onto the
consumer, by contract, rather than doing it itself (unlike `BaselineStore`,
where the *engine* owns the marshal via the anchor). I verified PlanStan's
concrete implementation, `PlanStan::SyncMassDeleteGuard`
(`~/dev/PlanStan/src/controllers/syncmassdeleteguard.cpp:53-72`), honors
this:

```cpp
bool SyncMassDeleteGuard::confirmMassDelete(...) {
    if (QThread::currentThread() == qApp->thread())
        return confirmOnGuiThread(...);
    bool allowed = false;
    QMetaObject::invokeMethod(qApp, [&]() {
        allowed = confirmOnGuiThread(...);
    }, Qt::BlockingQueuedConnection);
    return allowed;
}
```

The object carries no mutable member state beyond its `QObject` base
(`syncmassdeleteguard.h:18-36`— no data members at all besides the QObject
machinery), so it is trivially reentrant even before considering the
marshal: N workers calling `confirmMassDelete` concurrently each
independently marshal to `qApp`'s thread, which serializes them via its
own event loop (same shape as the `BaselineStore` anchor pattern) — safe.

One non-safety note worth carrying to Task 2/3: if N workers propose a
mass delete simultaneously, N modal `KMessageBox` dialogs will appear
sequentially (each nested `exec()` blocks the GUI thread until dismissed
before the next one shows) — a UX pile-up, not a data race. Consider
flagging this for a future task, not a blocker here.

**Caveat (contract, not a code defect):** this safety depends entirely on
every consumer implementation upholding the documented contract.
libkalburator cannot enforce it from the interface side. I verified
PlanStan's implementation only — WildPalms' `IMassDeleteGuard`
implementation was not inspected (out of this repo) and should be
spot-checked by whoever owns that integration before N-worker rollout
there.

### ISyncHost — SAFE WITH CONDITIONS (consumer-implementation-dependent; contract is undocumented in the interface itself)

`grep -n "m_controller->" src/engine/syncengine.cpp` restricted to
`SyncEngineWorker` methods (line ≥ 1449) finds exactly two methods called
on the worker thread: `syncStarted()` once
(`syncengine.cpp:1772`) and `recordChanged()` three times
(`syncengine.cpp:3340, 3344, 3348`, one per `ChangeKind`).

Unlike `IMassDeleteGuard`, `ISyncHost`'s header
(`src/calendar/isynchost.h`) has **no threading-contract comment at all** —
neither on the class nor on `syncStarted`/`recordChanged` individually.
This is a real gap: a consumer implementing `ISyncHost` today has no
documented signal that these virtuals can be called from a worker thread,
let alone from N of them concurrently. `IMassDeleteGuard`'s doc comment
(added deliberately per its own text) is the model libkalburator should
apply here too — see remediation below.

Given the contract is undocumented, safety currently rests entirely on
what consumer implementations happen to do. I traced PlanStan's
`CollectionController` (the only consumer available in this workspace):

- `syncStarted(const QString&, const LossProfile&)` — `CollectionController`
  does **not** override this two-argument virtual (confirmed via
  `~/dev/PlanStan/src/controllers/collectioncontroller.h`: the only
  `syncStarted` declared there is an unrelated single-argument Qt signal,
  not an `ISyncHost` override). Calls therefore hit the interface's
  default no-op body (`isynchost.h:65-66`, empty `{}`). An empty virtual
  call touches no shared state — trivially safe for any N.

- `recordChanged(const QString&, const QString&, ChangeKind)` — genuinely
  overridden (`~/dev/PlanStan/src/controllers/collectioncontroller.cpp:2388-2497`)
  and does real work. I read the full body. It:
  1. Reads `m_syncCoordinator->syncMappings()` — `SyncEngine::syncMappings()`
     returns `const QList<SyncMapping>&` (a live reference into
     `SyncEngine::m_syncMappings`, `syncengine.h:248`), iterated directly
     on the calling worker thread with no lock
     (`collectioncontroller.cpp:2421-2422`).
  2. Falls back to `m_kalbConfigManager->syncMappings()` similarly
     (`:2425`).
  3. Reads `m_backends.value(mapping.sourceBackend)` — `CollectionController`'s
     own `QHash<QString, SyncBackend*>`, again a direct unguarded read on
     the worker thread (`:2443`).
  4. Re-reads the record via `PlanStan::queryBackendBlocking(backend, ...)`
     (`:2471-2472`) — this **is** properly marshaled, per PlanStan's own
     documented pattern (`CLAUDE.md`: "Never call a sync-backend method
     directly from the GUI thread — route through
     PlanStan::invokeOnBackend / queryBackendBlocking"), onto the shared
     `planstan-backend-io` thread, which serializes concurrent callers the
     same way the `BaselineStore` anchor does.
  5. The two model-mutating tail calls (`onItemDeleted`/`onItemFetched`,
     which touch `GlobalIncidenceModel`/`MemoryCalendar`) are explicitly
     marshaled to the GUI thread via `QMetaObject::invokeMethod(...,
     Qt::QueuedConnection)`, with inline comments citing the exact prior
     incident this guards against
     (`collectioncontroller.cpp:2452-2457`: *"recordChanged is invoked by
     SyncEngine as a direct virtual call on the engine WORKER thread
     ... hop to the GUI thread rather than mutating them from the
     caller's thread (the O44 cross-thread mutation bug)"*).

So the correctness-critical part — the actual model mutation — is already
properly marshaled, and `recordChanged`'s own local logic holds no
instance-mutable state of its own (only local variables), so the method is
safe to enter reentrantly from N different worker threads simultaneously.
The three unmarshaled reads (1-3 above) are safe **only** under the same
unenforced assumption already flagged for `BackendRegistry`: that
`SyncEngine::m_syncMappings`, `KalbConfigManager`'s mappings, and
`CollectionController::m_backends` are not mutated by the GUI thread while
a sync is in flight. This is not new — it is the identical shape of
pre-existing gap as the `BackendRegistry::registerBackendInstance`
finding above (same absence of an `isSyncing()` guard on the relevant
Apply-time/add-provider paths: `~/dev/PlanStan/src/sync/topology/
synctopologywidget.cpp:2458, 3161` call `setSyncMappings()` with no sync
guard). Going from 1 to N *reader* threads does not create a new race here
— it's the same race, just with more potential readers, which was already
unguarded.

**Verdict for ISyncHost: SAFE WITH CONDITIONS.** Condition: (1) consumers
must not override `syncStarted`/`recordChanged` in a way that mutates
instance state directly on the calling thread without marshaling — PlanStan
already follows this discipline (verified above), but the interface does
not document or enforce it; (2) the pre-existing "mappings/backends must
not mutate mid-sync" assumption (shared with the `BackendRegistry`
finding) must continue to hold, and grows marginally more exposed
(more concurrent readers hitting the same unguarded window) as N grows,
though it does not change in kind.

## Verdict

**Overall: NO-GO for Task 2 as currently scoped** — one concrete,
in-hot-path data race blocks it. All six subjects `SyncEngineWorker`
shares across N workers, one line each:

| Subject | Verdict |
|---|---|
| `BackendRegistry` (read path: `backendInstance()`) | SAFE |
| `BackendRegistry` (write path: register/unregister) | SAFE WITH CONDITIONS — pre-existing unguarded race with sync-thread readers, not worsened by N; track separately, not a Task 2 blocker |
| `ShapeRegistries` (`TransformationRegistry::compile()`/`freeze()`) | **UNSAFE** — real unsynchronized concurrent write to `mutable QSet<DomainId> m_frozenDomains`, hit on every mapping; this is the NO-GO |
| `ShapeRegistries` (all other lookup paths, differ/merger factories, `Pipeline` returns) | SAFE |
| `ICalendarCollection` | SAFE — dead/unused on the worker side |
| `BaselineStore` | SAFE — marshaled via `m_baselineStoreAnchor` at all 10 worker-side call sites, no bypass found; verified exhaustively |
| `IMassDeleteGuard` | SAFE — marshaled by documented interface contract, verified in PlanStan's concrete implementation; WildPalms' implementation not checked |
| `ISyncHost` | SAFE WITH CONDITIONS — PlanStan's `recordChanged` override correctly marshals its mutating tail, but the interface itself documents no threading contract (unlike `IMassDeleteGuard`), and three of its reads share the same pre-existing unguarded-mutation-window assumption as `BackendRegistry` |

The only blocking finding remains `TransformationRegistry::compile()`'s
`freeze()` race. Everything else is either genuinely safe or safe under a
pre-existing, unenforced, not-campaign-introduced assumption that should
be tracked but does not block Task 2.

## If NO-GO — remediation needed (sized)

Single-file, single-class fix, low risk: guard
`TransformationRegistry::m_frozenDomains` against concurrent access.
Concretely, in `src/shape/transformationregistry.h`/`.cpp`:

- Add a `mutable QMutex m_frozenDomainsMutex;` alongside `m_frozenDomains`.
- Take a `QMutexLocker` in `freeze()` around the `insert()`.
- Take the same lock in the four call sites that currently do
  `m_frozenDomains.contains(...)` unguarded (`registerShape`,
  `declareCanonical`, `appendCanonicalVersion`, `registerEdge`) — these
  only run once at startup (`PluginManager::loadInProcess()`, called once
  from `AppController::init()` in the consuming app, verified via
  `~/dev/PlanStan/src/app/appcontroller.cpp:47,81` — never during a sync),
  so the added lock has no measurable cost there, and protects them
  against the theoretical (currently unreachable, but not asserted-against)
  case of a plugin being loaded while a sync is mid-flight.

This does not change `compile()`'s return value, its identity/non-identity
branching, or any caller's semantics — `freeze()` is purely a
"has-been-queried" enforcement flag for the post-init dynamic-registration
guard (see the class comment at `transformationregistry.h:56-60`), not
data that affects sync correctness. Estimated size: ~10-15 lines, one
file, one new unit test asserting concurrent `compile()` calls from
multiple threads don't corrupt/crash (candidate: spin N `QThread`s calling
`compile()` on the same domain simultaneously under TSan or a stress-loop
assertion). This fix should land as its own task before or as part of
Task 2's pool wiring, not deferred — the race is on the primary code path
of every sync mapping, not an edge case.

No remediation needed for `BackendRegistry` for Task 2 to proceed; its
separate write-side race (see above) should get its own tracking item
(`docs/todo/` or `docs/bugs/`) but is out of scope for this gate.

**Not a blocker, but recommended before or alongside Task 2 — document
`ISyncHost`'s threading contract.** Trivial, doc-only change:
`src/calendar/isynchost.h` should get a comment on `syncStarted` and
`recordChanged` (and any other lifecycle virtual reachable from
`dispatchSync`) modeled on `IMassDeleteGuard`'s existing one
(`imassdeleteguard.h:24-27`) — e.g. *"Called from a worker thread; under
the N-worker pool, may be called concurrently from multiple worker
threads. Implementations that mutate GUI-affine state must marshal to
their own thread themselves."* This costs nothing functionally (PlanStan's
implementation already complies, per the trace above) but closes the gap
that let this go unverified for WildPalms, and gives future consumers a
contract to implement against instead of needing to reverse-engineer it
from this audit. Pair with a note in the same header pointing at this
audit doc for the underlying analysis. Estimated size: comment-only, under
10 minutes.

## Suite baseline at branch point

Build: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DKALBURATOR_BUILD_TESTS=ON && cmake --build build -j 8` — succeeded clean, zero build errors.

`cd build && ctest` result: **172/173 passed** (99% pass rate). Total test
time 323.54 sec.

FAILED:

```
82 - tst_calendar_canon_roundtrip (Failed)
```

Detail from `Testing/Temporary/LastTest.log`: 13/14 subtests in that binary
passed; the one failure is
`TestCalendarCanonRoundtrip::canonPersonalClassificationProducesPrivateAndStash`
at `tests/calendar/tst_calendar_canon_roundtrip.cpp:412`:

```
FAIL!  : TestCalendarCanonRoundtrip::canonPersonalClassificationProducesPrivateAndStash() 'output.contains("X-CANON-CLASSIFICATION:personal")' returned FALSE. (classification=personal must stash verbatim value in X-CANON-CLASSIFICATION)
```

This is a deterministic unit-level assertion (no network/Radicale
dependency, no timing dependency) — not the kind of live-server-state flake
previously catalogued for this codebase. `parallel-sync` is confirmed at
the exact tip of `main` (`git merge-base main parallel-sync` ==
`git rev-parse main` == `git rev-parse parallel-sync` ==
`3fcb84257efd087f783b2a80e2facf00fc73d0cd`), so this failure is pre-existing
on `main` itself, not something this branch introduced. It was not
previously known to this audit and was not investigated further or fixed,
per this task's read-only scope; it should be triaged separately
(`docs/bugs/`). No other failures.
