# Phase E — Transcoding into backends (design)

**Date:** 2026-04-29
**Status:** Approved 2026-04-29 by user via brainstorming session.
Implementation plan in `04n-phase-e-transcoding-plan.md` (sibling).
**Phase tag on completion:** `v0.11-phase-e-transcoding-backends`.
**Gates:** Phase D complete (`v0.10-phase-d-compose`).

## Goal

Phase E of `04k-engine-merger-roadmap.md`: move
`TranscodingRegistry` invocation off `SyncWorker::applyChanges` and
into each calendar backend's write path. The engine becomes
capability-blind during normal writes; transcoding re-emerges as a
conflict-resolution concern only in Phase G.

The roadmap's success criterion is precise: **`SyncWorker.cpp`
contains no `TranscodingRegistry` references** after this phase.
Today, two call sites (`syncworker.cpp:1166–1199`) are the entire
calendar-side transcoding surface, gated by a string comparison
`sourceType != targetType` derived from `backend->backendType()`.
Capabilities objects exist on every backend but are not consulted
by the transcoding path.

Bundled in: conversion of `src/calendar/CMakeLists.txt`,
`src/blob/CMakeLists.txt`, and `src/transcoding/CMakeLists.txt`
from `file(GLOB ...)` to explicit source lists, removing the
AUTOMOC-timestamp footgun documented in `FINDINGS.md` (entry of
2026-04-28). Phase E adds one new file (`transcodingrouter.{h,cpp}`)
and one near-empty header (`transcodingplan.h`); the cleanup is
self-contained and small.

## Decisions made during the brainstorm

The brainstorm resolved seven forks. Recorded here so the plan,
the implementation, and any future revision pass have a single
authoritative source for "why we're doing it this way."

### 1. Routing architecture: hybrid (router decides, backend coerces)

Three options were considered:

- **(A) Pure backend-internal.** Each backend's write methods call
  `TranscodingRegistry::instance().transcodeIncidence(sourceType,
  myType, item)` directly. Engine has zero `TranscodingRegistry`
  references, but every backend duplicates the registry-call
  boilerplate, and backends gain source-awareness they shouldn't
  carry long-term.
- **(B) Router shim consulted by engine, coerces in-engine.**
  `SyncWorker` calls `TranscodingRouter::route(sourceCaps,
  targetCaps, item) → (transcoded, warnings)`, then passes the
  transcoded item to the backend. Smallest migration; backends are
  unchanged. Cost: transcoding stays outside the backend, so Phase
  F has to redo this work.
- **(C) Hybrid — router decides, backend coerces.** `SyncWorker`
  calls `TranscodingRouter::plan(sourceType, targetType) →
  TranscodingPlan` (a small value object). The Plan flows into
  `target->storeItems(...)` etc. as a parameter; the backend
  executes the plan inside its write method.

**Settled: (C) hybrid.** It is the only option that satisfies both
roadmap goals literally — `SyncWorker.cpp` has no
`TranscodingRegistry` reference (it talks to `TranscodingRouter`
instead), and the actual coercion sits in the backend's write path
where Phase G's plugin model wants it. (B) is tempting for its
simplicity but leaves transcoding outside the backend, which means
Phase F has to redo the work. (A) duplicates registry-call
boilerplate across eight backends and gives backends source
awareness that doesn't belong there.

### 2. Signal origin: backend emits, engine forwards

`transcodingWarning(calendarId, uid, warnings)` originates today in
`SyncWorker` (`syncworker.h:151`) and is forwarded through
`SyncCoordinator::transcodingWarning` (slot at
`synccoordinator.cpp:923`). Under (C), the backend is the only
party that knows what got coerced.

Two options:

- **(C-1) Backend emits, engine forwards.** Add a
  `transcodingWarning` signal to the `SyncBackend` interface;
  backend emits during plan execution; `SyncWorker` connects to
  each backend's signal and re-emits its own.
- **(C-2) Backend returns warnings synchronously.** Write methods
  return `QStringList warnings` (or a `WriteResult` struct);
  `SyncWorker` collects and emits.

**Settled: (C-1).** Symmetric with how other backend signals
already bubble up; the D.0 contract test asserts on
`SyncCoordinator::transcodingWarning` regardless of origin, so it
keeps passing. (C-2) would force return-type changes on three
methods that have non-`SyncWorker` callers.

### 3. Routing semantics: preserve string-based gate

The roadmap text says transcoders fire "when capabilities()
indicate loss is possible" — capability-aware routing. Today the
gate is purely `sourceType != targetType`; capabilities are
defined but not consulted by the transcoding path.

Three options:

- **(S1) Move-only, preserve string semantics.** Router takes
  `(sourceType, targetType)` and behaves like today.
- **(S2) Upgrade to capability-gap routing.** Router computes a
  capability diff; transcoders register against capability fields
  rather than type-string pairs. Bigger; touches transcoder
  authoring (`PropertyTranscoder::appliesTo`).
- **(S3) Hybrid: caps-or-type, registry decides.** Both
  registration paths supported; router runs the union.

**Settled: (S1).** The phase's stated risk is "the engine has to
be calendar-aware during normal writes" — the *location* of the
call blocks F and G, not the gate predicate. (S1) decouples the
structural move from the semantic upgrade and keeps the diff
small. (S2)/(S3) are deferred (see §7).

### 4. Scope: legacy synchronous methods only

After Phase D, calendar backends have two write surfaces:

- Legacy synchronous: `storeItems`, `updateItem`, `startSync`,
  `removeItem`.
- Newer operation-based: `pushItems` returning `PushOperation*`
  (plus `FetchOperation`, `DeleteOperation`).

`SyncWorker::applyChangesToBackend` — the only
`TranscodingRegistry` caller today — uses the legacy methods. The
operation API is targeted by Phase F's threading-API redesign.

**Settled: (W1) legacy only.** Add `TranscodingPlan` parameter to
`storeItems`, `updateItem`, `startSync`. `removeItem` is unchanged
(deletes don't transcode). Operation API is unchanged in this
phase; a header comment in `syncbackend.h` flags Phase F as the
moment to add equivalent plan-passing if the operation API
survives the threading-API redesign.

`startSync` is included defensively even though
`applyChangesToBackend` doesn't call it today: the SyncBackend
interface presents it as an alternative write entry, and a future
caller routing through `startSync` would silently skip transcoding
without the parameter.

### 5. Router lifecycle: per-engine instance, registry injectable

`TranscodingRegistry` is a process-wide singleton
(`transcodingregistry.h:47`); FINDINGS entry of 2026-04-28 flags
this as a test-isolation hazard and a Phase G de-singletonisation
candidate. The router we add inherits the question.

Three options:

- **(R1) Stateless singleton wrapper.** `TranscodingRouter::instance()`,
  delegates to `TranscodingRegistry::instance()`.
- **(R2) Per-engine instance, registry injectable.** Router is an
  owned object, constructed with a `TranscodingRegistry&`.
  Production code passes `TranscodingRegistry::instance()`; tests
  can pass a fresh registry per test.
- **(R3) Per-engine instance, registry singleton internally.** Like
  (R2) but no constructor parameter; loses the testability win.

**Settled: (R2).** The cost is one constructor parameter and one
`&` member; in exchange, every router-level test gets registry
isolation, and Phase G's per-engine registry rework can swap the
injected reference with zero call-site churn. The router lives on
`SyncCoordinator` (alongside the existing engine ownership graph),
constructed with `TranscodingRegistry::instance()` and passed by
reference into `SyncWorker` along with the rest of the per-worker
config.

### 6. Test strategy: black-box D.0 + white-box router

D.0 left `tst_calendar_transcoding_warning.cpp` as the integration
contract: register a lossy transcoder, modify an item, expect
`SyncCoordinator::transcodingWarning` with non-empty warnings. That
test must stay green.

**Settled: (T2) black-box + new white-box `tst_transcoding_router`.**
Add a small unit test for `TranscodingRouter::plan()`: registry +
sourceType + targetType in, plan out (empty when types match,
populated when types differ and matching transcoders are
registered). The unit test catches router-internal regressions; the
existing D.0 integration test catches "a backend forgot to wire the
plan into its write path." A per-backend smoke test (T3) was
considered but rejected — eight near-identical tests for moderate
incremental value.

### 7. Bundle CMake glob → explicit source lists

`src/calendar/CMakeLists.txt` and `src/blob/CMakeLists.txt` use
`file(GLOB CONFIGURE_DEPENDS ...)`. FINDINGS entry of 2026-04-28
documents that adding a new `Q_OBJECT` class via globbed sources
fails to invalidate `kalburator_autogen/timestamp`, producing a
confusing vtable link error on first build.

**Settled: bundled.** Phase E adds one new file
(`transcodingrouter.cpp`) and would re-trigger the same workaround
without the conversion. Phase F will churn these source lists
heavily during engine unification; converting glob → explicit
mid-Phase-F is worse than doing it as a small standalone commit
during Phase E. Three CMakeLists touched
(`src/calendar/`, `src/blob/`, `src/transcoding/` if it uses
glob — verified during plan authoring).

## Components

### `TranscodingPlan` — value type

Defined in `src/transcoding/transcodingplan.h` (alongside the
`executeTranscodingPlan` declaration; helper definition in the
sibling `.cpp`).

```cpp
struct TranscodingPlan {
    QList<PropertyTranscoder*> transcoders;  // borrowed; not owning
    QString routingDecision;                  // diagnostic only

    bool isEmpty() const { return transcoders.isEmpty(); }
};
```

Default-constructible to mean "no transcoding needed."
Cheap to copy by value. Borrowed pointers — the transcoders are
owned by the `TranscodingRegistry`. Lifetime contract: the plan
must not outlive the registry it was sourced from. In practice the
plan is built at the start of an `applyChanges` invocation,
consumed during the same invocation, and discarded — it never
leaves the worker thread, never outlives the registry.

### `TranscodingRouter` — instance class

Lives at `src/transcoding/transcodingrouter.{h,cpp}`. No
`Q_OBJECT`, no signals. Single public method:

```cpp
class TranscodingRouter {
public:
    explicit TranscodingRouter(TranscodingRegistry& registry);

    TranscodingPlan plan(const QString& sourceType,
                         const QString& targetType) const;

private:
    TranscodingRegistry& registry_;
};
```

Returns an empty plan when:

- `sourceType.isEmpty()` (unknown source — happens during certain
  internal pseudo-applies; existing `applyChangesToBackend`
  already handles this case by skipping).
- `targetType.isEmpty()` (same).
- `sourceType == targetType`.
- `registry_.findTranscoders(sourceType, targetType)` returns
  empty (no transcoder registered for this pairing).

Otherwise returns a plan with `transcoders` populated from
`findTranscoders` and `routingDecision` set to a diagnostic string
("source=X target=Y, N transcoders").

Lifetime: registry must outlive router. Documented in the header.

### `executeTranscodingPlan` — free helper

Lives at `src/transcoding/transcodingplan.{h,cpp}`. Centralises
the per-item plan execution so each backend's call site is two
lines, not a duplicated loop:

```cpp
struct TranscodingResult {
    KCalendarCore::Incidence::Ptr incidence;  // transcoded clone
    QStringList warnings;                       // empty if lossless
};

TranscodingResult executeTranscodingPlan(
    const TranscodingPlan& plan,
    const KCalendarCore::Incidence::Ptr& original);
```

If `plan.isEmpty()`, returns `{original, {}}` — no clone, no work.
Otherwise clones the incidence, walks `plan.transcoders` in order
applying each, accumulates warnings from each transcoder, returns
the transcoded clone and combined warnings.

### `SyncBackend` interface changes

`src/calendar/syncbackend.h`:

- New signal: `void transcodingWarning(QString calendarId,
  QString uid, QStringList warnings);` declared on the base.
- Three method signatures grow a `const TranscodingPlan& plan`
  parameter, defaulted to a static empty plan to keep
  non-`SyncWorker` callers source-compatible:
  - `storeItems(MemoryCalendar*, items, const TranscodingPlan& = {})`
  - `updateItem(MemoryCalendar*, item, icalData, const TranscodingPlan& = {})`
  - `startSync(collectionId, calendar, creates, updates, deletes, const TranscodingPlan& = {})`
- `removeItem` unchanged.
- New header comment near the operation-based API
  (`pushItems`/`PushOperation`) flagging Phase F.

### Backend implementations

Each of the eight concrete backends — `LocalBackend`,
`RemoteBackend`, `OrgBackend`, `MockBackend`, `AkonadiBackend`,
`DecSyncBackend`, `SubscriptionBackend`, `HolidaySubscriptionBackend`
— gets a small "execute plan" prologue in `storeItems`/`updateItem`/
`startSync`:

```cpp
void XBackend::storeItems(MemoryCalendar* cal,
                          const QList<Incidence::Ptr>& items,
                          const TranscodingPlan& plan)
{
    for (const auto& original : items) {
        auto [transcoded, warnings] = executeTranscodingPlan(plan, original);
        if (!warnings.isEmpty())
            emit transcodingWarning(cal->id(), original->uid(), warnings);
        // existing write logic, but with `transcoded` instead of `original`
    }
}
```

For backends with `canCreate == false` (the subscription pair, and
read-only branches in others), the plan parameter is accepted and
ignored — the existing read-only behavior is preserved.
`startSync` accepts the parameter but applies it to its
`stagedCreations` and `stagedUpdates` only.

### `SyncWorker::applyChangesToBackend`

`src/calendar/syncworker.cpp:1108–1210`:

- The two existing `TranscodingRegistry::instance().transcodeIncidence(...)`
  blocks (lines 1166–1199) are deleted, along with the local
  clone-and-warn machinery.
- The `sourceType`/`targetType` derivation (lines 1131–1145) is
  preserved; it now feeds `router_.plan(sourceType, targetType)`,
  called once per `applyChanges` invocation per direction.
- The resulting `TranscodingPlan` is captured by value in a local
  and passed by const-ref into the `CreateIncidenceItem` /
  `UpdateIncidenceItem` wrappers' apply paths, which forward it to
  `backend->storeItems` / `updateItem`.
- The local `transcodingWarning` emit is removed. Instead,
  `SyncWorker` connects to each backend's
  `SyncBackend::transcodingWarning` signal in the same place it
  connects other backend signals, and re-emits its own
  `SyncWorker::transcodingWarning`. The public
  `SyncCoordinator::transcodingWarning` contract stays exactly as
  D.0 pinned it.
- `SyncWorker` gains a `TranscodingRouter& router_` member,
  injected via the worker constructor (alongside the existing
  injected dependencies).

### `SyncCoordinator`

`src/calendar/synccoordinator.{h,cpp}`:

- Gains a `TranscodingRouter` member, constructed with
  `TranscodingRegistry::instance()` in the coordinator
  constructor.
- Passes the router by reference into the `SyncWorker` constructor
  alongside the existing injected dependencies.
- `onWorkerTranscodingWarning` slot is unchanged — the public
  contract is preserved.

## Test plan

### Existing tests that must stay green

- `tst_calendar_transcoding_warning` (D.0). Black-box integration:
  registers `ByDayStripTranscoder`, modifies an incidence, asserts
  `SyncCoordinator::transcodingWarning(calendarId, uid, warnings)`
  fires with non-empty warnings whose text contains "BYDAY".
  Under Phase E, the signal originates in `MockBackend`'s write
  path (after invoking the plan) instead of in `SyncWorker`. The
  test does not assert on origin — it asserts on the
  `SyncCoordinator` side of the forwarding chain — so it stays
  green provided the chain
  (`MockBackend → SyncWorker → SyncCoordinator`) is wired
  correctly.
- All other `tests/calendar/` tests pass unchanged.
- All `tests/blob/` tests pass unchanged (blob layer is not
  touched by Phase E).

### New tests added in this phase

- **`tst_transcoding_router`** — white-box unit test for
  `TranscodingRouter::plan()`:
  - Empty source type → empty plan.
  - Empty target type → empty plan.
  - Equal types → empty plan.
  - Differing types, no matching transcoder → empty plan.
  - Differing types, matching transcoder → plan contains that
    transcoder.
  - Each test owns its own `TranscodingRegistry` (constructed on
    the stack), proving (R2)'s testability win and avoiding the
    singleton-cleanup hazard documented in the FINDINGS entry.

### Acceptance criteria

- libkalburator standalone: 19 → 20 ctest executables (one new
  test added). All pass.
- Grep: `git grep TranscodingRegistry src/calendar/` returns no
  hits (was: two hits in `syncworker.cpp` before Phase E).
- PlanStan baseline: 96/120 (same as Phase D — Phase E does not
  touch the consumer's known-failing tests).
- WildPalms baseline: 73/73.
- `verify-all.sh` exit 0 on stable runs.

## Migration order (within Phase E)

The plan doc enumerates concrete tasks; this section sketches the
shape so the plan author has a starting point.

1. Add `TranscodingPlan` value type and `executeTranscodingPlan`
   helper. Pure addition; no caller change.
2. Add `TranscodingRouter` class + unit test
   `tst_transcoding_router`. Pure addition; not yet wired.
3. Convert three CMakeLists from `file(GLOB ...)` to explicit
   source lists. Self-contained; verifies build.
4. Add `SyncBackend::transcodingWarning` signal and the
   defaulted-parameter overloads on the three write methods. Base
   class only; subclasses unchanged at this step thanks to
   defaulted parameters. Compile-only check.
5. Wire each of the eight concrete backends to invoke
   `executeTranscodingPlan` and emit `transcodingWarning`. One
   commit per backend (or one batched commit, plan author's
   choice).
6. Wire `SyncCoordinator` and `SyncWorker`: instantiate the
   router, inject into worker, connect each backend's
   `transcodingWarning` to the worker's, delete the
   `TranscodingRegistry` calls in `applyChangesToBackend`.
7. Run all calendar tests; expect green.
8. Migrate PlanStan: should compile unchanged thanks to defaulted
   parameters. Run PlanStan tests; expect 96/120 (baseline).
9. Migrate WildPalms: should compile unchanged. Run WildPalms
   tests; expect 73/73.
10. Refresh baselines if any test counts shift, run
    `verify-all.sh`, tag `v0.11-phase-e-transcoding-backends` on
    libkalburator HEAD.

## Deferred / future work

Recorded explicitly so future phases inherit a clean handoff.

### Capability-aware routing (Phase F or later)

Phase E preserves the existing string-based gate
(`sourceType != targetType`). The roadmap envisions capability-
aware routing where transcoders fire based on `BackendCapabilities`
deltas (e.g., target lacks `RecurrenceCapabilities::supportsByDay`
→ run RRule transcoder, regardless of backend type).

Two upgrade paths exist (see brainstorm decision 3 / option S2,
S3): rewrite `PropertyTranscoder::appliesTo` to take a capability
diff, or add a parallel registration path. Neither is small; both
change which transcoders fire for real workloads, which is a
separate "is this still correct?" exercise. Phase F (Unify) is the
natural moment — `IDomainAdapter::describe-capabilities` is part
of that phase's API design.

Sketch for the future implementer: extend `TranscodingRouter::plan`
to accept `(sourceCaps, targetCaps)` alongside the type strings;
run both type-keyed and capability-keyed lookups; union the
results.

### Operation-based API plan-passing (Phase F)

`pushItems` / `PushOperation` etc. do not gain a `TranscodingPlan`
parameter in Phase E (W1). If the operation API survives Phase F's
threading-API redesign, it inherits the same plan-passing pattern.
A header comment near the operation-API declarations in
`syncbackend.h` flags this for the Phase F design author.

### `TranscodingRegistry` de-singletonisation (Phase G)

(R2) makes the router instance-owned and registry-injectable, but
the registry itself remains a process-wide singleton. FINDINGS
documents the test-isolation hazard. Phase G's plugin model is the
natural moment to make the registry per-engine (or per-plugin-host),
because plugin discovery and lifecycle become first-class then.
With (R2) in place, the call-site change in Phase G is "swap the
injected `TranscodingRegistry&` for a different instance" — zero
churn beyond the constructor.

### CalendarJournal, DecSyncBackend, ICalendarCollection, DataDomain

Open questions 3–6 in `04k-engine-merger-roadmap.md` are unchanged
by Phase E. They surface again in Phase F.

## Cross-references

- `04k-engine-merger-roadmap.md` — Phase E section, success criteria,
  open question 2.
- `04m-phase-d-compose-design.md` — the inheritance edge
  (`SyncBackend : public IBlobBackend`) that Phase D landed and
  Phase E builds on.
- `tests/calendar/tst_calendar_transcoding_warning.cpp` — the D.0
  contract that Phase E must keep green.
- `~/dev/refactor-engine-merger/FINDINGS.md` —
  "TranscodingRegistry is a process-wide singleton"
  (2026-04-28); "AUTOMOC timestamp not invalidated when adding
  Q_OBJECT class via globbed sources" (2026-04-28).
