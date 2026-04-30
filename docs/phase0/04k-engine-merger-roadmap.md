# Phase D / E / F / G — engine merger roadmap

**Date:** 2026-04-28 (updated 2026-04-29 with F slice + F.0 prerequisite)
**Status:** D.0, D, E, F.0, F1, F2 landed. Phase G next.

## At-a-glance phase status

| Phase | Status | Tag |
|---|---|---|
| D.0 — Tests-first | ✅ landed 2026-04-28 | `v0.9-phase-d0-tests-first` |
| D — Compose | ✅ landed 2026-04-29 | `v0.10-phase-d-compose` |
| E — Transcoding-into-backends | ✅ landed 2026-04-29 | `v0.11-phase-e-transcoding-backends` |
| F.0 — Test gap closure | ✅ landed 2026-04-29 | `v0.12-phase-f0-test-gaps` |
| F1 — Unify (engine + adapter, threading verbatim) | ✅ landed 2026-04-30 | `v0.13-phase-f1-unify` |
| F2 — Threading API redesign | ✅ landed 2026-04-30 | `v0.14-phase-f2-threading` |
| G — Opaque + plugin | ⬜ not started | `v0.15-phase-g-opaque-plugin` |

**Phase F slice rationale:** the original Phase F bundled the
engine-shape pivot (single `SyncEngine` over `IDomainAdapter`)
with the threading-API redesign (`QFuture`-based public API,
cancellation tokens, operation handles). Brainstorm 2026-04-29
sliced these apart on the same principle Phases D and E used:
preserve threading verbatim while the structural change lands,
then redesign threading on top of a stable unified engine.
Cancellation has zero current test coverage; bundling its
introduction with the engine collapse would put two unverified
changes in the same tag. F1 lands the engine collapse with
existing-threading tests as the safety net; F2 introduces the
new threading API with new TDD-style cancellation tests.

**Phase F.0 rationale:** the audit conducted during the F-scoping
brainstorm found that backend write/fetch error propagation was
only tested in PlanStan's `tst_sync_error_recovery`. Phase E
nearly broke this contract; we got lucky that `verify-all.sh`
runs PlanStan. F.0 moved the contract into a library-side test.

**Cross-repo coordination:** the master plan lives in
`~/dev/refactor-engine-merger/ROADMAP.md`. That doc explains the
spectrum of options that was considered and the chosen path; this
doc restates the technical plan for the libkalburator-side audience
without the cross-repo orchestration material.

**Sibling docs in this directory:**

- `04h-blob-layer-design.md` — the blob layer's architectural
  placement, including the `╳` non-coupling diagram. Phase D's job
  is to *replace* that `╳` with composition.
- `04i-blob-baseline-store-design.md` — current state of
  baseline-store storage, fold into Phase D's storage carve-up.
- `04j-engine-conflict-wiring-design.md` — existing conflict-store
  wiring; informs how the unified engine handles conflicts in
  Phase F.
- `04a-followups.md` — Audit 1 (BaselineStore split), Audit 2
  (IDMappingStore merge), Audit 3 (conflict handler dispatch),
  Audit 4 (AsyncFileWriter — note the revision: `QSaveFile` was
  rejected as a perf tradeoff). Most of these become Phase D
  prerequisites bundled into Phase D's scope.
- `04c-phase-c-plan.md` — the existing `SyncStore` carve-up plan,
  parts of which fold into Phase D.
- `../2026-04-28-honest-assessment.md` — the read-through that
  motivated this refactor.

## Goal

Unify the two parallel sync engines that currently coexist in
libkalburator:

- **Upper calendar layer** (`SyncBackend`, `SyncCoordinator`,
  `SyncWorker`, `CalendarManager`, `SyncStore`) — calendar-typed,
  KCalendarCore-bound, the original PlanStan extraction.
- **Lower blob layer** (`IBlobBackend`, `BlobSyncEngine`,
  `BlobBaselineStore`) — generic byte-level sync, three weeks old,
  consumed by Wild Palms plugins for non-calendar Palm data.

Today the two are **explicitly disconnected** (the `╳` in
`04h-blob-layer-design.md`). They share types
(`BackendRecord`, `CollectionInfo`, `ConflictPolicy`) but have
separate engines, separate backends, separate storage. The merger
brings them into a single architecture.

## The phases

### Phase D.0 — Tests-first (prerequisite)

The calendar layer has zero library-owned tests today (only the blob
layer does — 3 executables in `tests/blob/`, plus an `IcsFeedFetcher`
test added 2026-04-28). Refactoring the calendar engine without
contract-level tests at the libkalburator boundary is too risky.
Phase D.0 closes that gap before any structural change.

**Scope:**

- Stub-`ISyncHost` integration tests in `tests/calendar/` covering
  the major sync flows (full sync, one-way, conflict detection,
  baseline 3-way merge, transcoding warning emission).
- Stub-`ICalendarCollection` test harness reusable across tests.
- A `MockCalendarBackend` library-owned mock equivalent to
  `MockBlobBackend`.

**Success:**

- libkalburator standalone `ctest` grows from 5 to ≥ 8 executables.
- The new tests run cleanly against unmodified `pre-engine-merger`.

**Gates:** none. Pure addition, no behavior change.

### Phase D — Compose

Calendar engine internally delegates byte-level fetch/store/hash to
the blob engine. Calendar backends gain a `blobBackend()` view (or
inherit `IBlobBackend` directly — see open question 1).

**Scope:**

- `IBlobBackend` view exposed by every concrete calendar backend
  (`LocalBackend`, `RemoteBackend`, `OrgBackend`, etc.).
- `SyncWorker::fetchSourceRecords` / `fetchTargetRecords` delegate
  to the blob view.
- Calendar engine continues to drive incidence-typed orchestration
  on top of the blob view.
- Phase C's `SyncStore` carve-up is bundled in here (you can't
  compose engines while storage mixes calendar baselines with blob
  baselines). Splits per Audit 1 in `04a-followups.md`:
  - `BlobBaselineStore` (already exists)
  - `CalendarBaselineStore` (iCal text baselines, carved out)
  - `IDMappingStore` (already exists, may need migration from
    `SyncStore::*idMapping*` callers)
  - `ConflictStore` stays where it is (already at lower layer)
  - CTags fold into `RemoteBackend`'s private state (per Audit 1)
- `.planstan-sync.db` filename — decide retire-vs-rename. **Note:**
  PlanStan users have on-disk databases at this name. A migration
  is required if renamed. Defer to Phase F if the migration is
  non-trivial.

**Success:**

- libkalburator's blob engine drives at least one calendar-layer
  code path end-to-end.
- All three repos pass tests at baseline (verify-all.sh in the
  cross-repo coordination folder).
- Either Phase C remainder is bundled (preferred) or has been
  separately landed on `main` before Phase D begins.

**Gates:** Phase D.0 complete.

### Phase E — Transcoding into backends

`PropertyTranscoder` and `RruleTranscoder` are invoked today in
`SyncWorker::applyChanges` — **on the engine main path**. As long
as that's true, the engine has to be calendar-aware during normal
writes, which blocks Phases F and G.

This phase moves transcoding decisions to the backend boundary:
each backend that has capability limits coerces or rejects on write.
The engine becomes capability-blind during normal writes; transcoding
re-emerges only as a conflict-resolution concern (Phase G).

**Scope:**

- Each calendar backend's `storeItems` / `updateItem` / `startSync`
  invokes the transcoder for itself when its `capabilities()`
  indicate loss is possible.
- `SyncWorker` no longer calls `TranscodingRegistry`.
- A *routing-decision* concern remains: some transcoding decisions
  need both source and target capabilities (see open question 2).
  Phase E must decide where this lives — likely a thin
  `TranscodingRouter` consulted by the engine before dispatch, with
  the actual coercion still in the backend.
- `transcodingWarning` signal continues to fire — moved to the
  backend.

**Success:**

- `SyncWorker.cpp` contains no `TranscodingRegistry` references.
- Transcoding-warning tests still pass (warnings still emitted,
  just from the backend now).

**Gates:** Phase D complete.

### Phase F — Unify

Single `SyncEngine` parameterized over an `IDomainAdapter`. Calendar
and blob adapters; future vCard adapter slot.

**Scope:**

- `IDomainAdapter` interface: serialize, deserialize, hash, diff,
  merge, describe-capabilities.
- `CalendarDomainAdapter` (uses `IncidenceDiff`, iCal text
  baselines).
- `BlobDomainAdapter` (identity serde, hash-equality diff,
  whole-record replace merge).
- `SyncEngine<Adapter>` (or runtime equivalent — a borrowed
  `IDomainAdapter*`).
- Old `BlobSyncEngine` and the calendar-side coordinator code
  become thin wrappers or are deleted.
- **Threading API decided here.** See "Threading & async" below.

**Success:**

- `BlobSyncEngine` and the calendar engine's orchestration are one
  class.
- Both consumers compile against the new API.
- Both consumers pass tests at baseline.

**Gates:** Phase E complete.

### Phase G — Opaque transport + plugin diff

Engine deals only in `BackendRecord`. Calendar diff/merge becomes
an `IRecordDiffer` / `IRecordMerger` registered for type
`"calendar"`. Adapters dissolve into typed plugins.

**Scope:**

- `IRecordDiffer` and `IRecordMerger` interfaces.
- Engine consults registered diff/merger by record type, falls
  back to last-write-wins / hash-equality when none is registered.
- `CalendarDomainAdapter` reshapes into a calendar-typed plugin.
- WP HotSync conduit primitives surface naturally — Phase G's
  primitive operations (sync one record, given source+target+
  baseline+policy) are exactly what `SyncConduitBase` wants.

**Success:**

- libkalburator engine has no direct dependency on KCalendarCore.
- Calendar support is loaded as a plugin from a sibling library or
  optional translation unit.
- Both consumers still work.

**Gates:** Phase F complete.

## Threading & async

**Decision: model is decided in Phase F**, when the unified engine's
API is defined. Phases D and E preserve current threading verbatim
(calendar worker thread, blob still synchronous, calendar worker
thread calls blob engine inline).

**Phase F target shape:**

- **Public API (recommended).**
  - `QFuture<SyncResult> runSync(mappingId)` — futures-based
    one-shot. Cancellable, awaitable, has progress.
  - Streaming signals (`itemFetched`, `progressChanged`,
    `conflictDetected`, `phaseChanged`) for real-time UI.
  - Conflict pause/resume keeps the existing signal-yield pattern
    — it works and is well-thought.
- **Internals.**
  - Engine main loop runs on a worker thread.
  - Network I/O stays async via the Qt event loop (no thread
    blocking).
  - File / SQLite / Palm device I/O wrapped in operation handles
    that post completion back to the worker.
  - CPU-bound work (diff, hash, transcode) dispatches to
    `QThreadPool::globalInstance()` (configurable).
  - Cancellation: `QFuture::cancel()` propagates a token the
    engine checks at every operation-handle boundary.
- **Backend contract.**
  - Every I/O method returns an `Operation` handle. Standardize
    the half-finished pattern that's already in `SyncBackend`
    (`FetchOperation*` / `PushOperation*` / `DeleteOperation*`).
  - Delete the deprecated synchronous overloads (`loadItems`
    etc.).

**Out of scope:**

- Native C++20 coroutines / `std::execution` / `QCoro`. Tempting
  but the dependency cost is real and `QFuture::then()` chains get
  ~80% of the benefit.
- Replacing Qt primitives with `std::execution`.

## Open questions (carried into Phase D / F design)

These don't block setup. They need to be **visible** during the
relevant phase's design pass.

1. **Bridge ownership pattern.** Calendar backend exposes
   `IBlobBackend` via:
   - (a) inheritance — calendar backend `: public IBlobBackend`
   - (b) member — calendar backend holds an `IBlobBackend*` member
   - (c) view — calendar backend implements `blobBackend()`
     returning a thin view object

   Default working assumption: **(a) inheritance**. May be
   overridden during Phase D design if a specific backend doesn't
   fit.

2. **Transcoding routing decisions.** Some decisions need both
   source and target capabilities (e.g. "RRULE has BYDAY, target
   is org-mode → coerce"). Backend-internal coercion can't see the
   source. Phase E must site this — likely a thin
   `TranscodingRouter` consulted by the engine before dispatch.

3. **`CalendarJournal`.** Write-ahead log of incidence operations
   for crash recovery. Does it become a generic operation journal
   in Phase F, or stay calendar-shaped?

4. **`DecSyncBackend`.** Sync protocol implementation, not a sync
   source. Where does it sit in the unified engine?

5. **`ICalendarCollection`.** Calendar-shaped host contract. Does
   it survive Phase F, become a typed view, or dissolve into the
   calendar plugin?

6. **`DataDomain { Calendar, Project }` enum.** Smell suggesting
   PlanStan's "Project" tasks were forced into the calendar
   pipeline. In Phase G this becomes the type discriminator that
   drives plugin lookup, OR it's deleted (Project becomes a kind
   of incidence).

7. **Concurrent-mapping execution.** Should N independent mappings
   run in parallel during `runSyncAll()`? Phase F decision.

8. **`.planstan-sync.db` migration.** PlanStan users have
   databases at this filename. Renaming requires a migration.
   Decide rename-vs-keep in Phase D; do migration in Phase F if
   rename.

## Why not a rewrite

Worth stating explicitly: the spectrum considered before choosing
this path included options as conservative as "stay parallel
forever" and as aggressive as "rewrite onto std::execution". The
chosen path (Compose → Transcode-into-backends → Unify →
Opaque+Plugin) was selected because each phase is **independently
valuable** and **independently revertible**. If the merger wedges
at any point — say Phase E surfaces a transcoding shape that won't
factor cleanly — the partial progress through Phase D still leaves
the library better than it started, and falling back to "stay
parallel" is fine. The library is not bad; it just has consolidation
debt.
