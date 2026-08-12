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

## Verdict

**NO-GO for Task 2 as currently scoped** — not because the overall design
is wrong, but because one specific, narrow, pre-existing method has a real
data race that N-way concurrent `dispatchSync()` calls will hit on
essentially every sync run: `TransformationRegistry::compile()`'s
unsynchronized write to `mutable QSet<DomainId> m_frozenDomains` via
`freeze()`.

`BackendRegistry::backendInstance()` and all other `ShapeRegistries` lookup
paths dispatchSync uses are confirmed read-only and safe for N concurrent
readers. `BackendRegistry`'s write side (`registerBackendInstance`/
`unregisterBackendInstance`) has an unguarded pre-existing race with any
sync-thread reader, but it is not new or worsened by adding more reader
threads — track separately, does not block Task 2.

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
