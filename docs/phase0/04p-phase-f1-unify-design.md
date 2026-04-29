# Phase F1 — Unify (engine collapse + IDomainAdapter) — design

**Date:** 2026-04-29
**Status:** Approved 2026-04-29 by user via brainstorming session.
Implementation plan in `04p-phase-f1-unify-plan.md` (sibling).
**Phase tag on completion:** `v0.13-phase-f1-unify`.
**Gates:** Phase F.0 complete (`v0.12-phase-f0-test-gaps`).

## Goal

Phase F1 of `04k-engine-merger-roadmap.md`: collapse the two
parallel sync engines (`SyncCoordinator`/`SyncWorker` for
calendar; `BlobSyncEngine` for blob) into a single `SyncEngine`
parameterized at runtime over an `IDomainAdapter`. Calendar and
blob domain adapters; future vCard adapter slot. Engine ↔
adapter boundary is pure `BackendRecord`; engine has no
KCalendarCore dependency in its core path.

**F1 explicitly does NOT redesign threading.** The current
threading model (caller-thread API, dedicated worker `QThread`,
`QMetaObject::invokeMethod` dispatch, signal-based completion) is
preserved verbatim. That's the same proven pattern Phases D and E
used; F2 (`v0.14-phase-f2-threading`) is the next phase, where
`QFuture`-based public API, `QFuture::cancel()` propagation, and
operation-handle standardization land. F1's job is to land the
structural change against the same threading the test suite
already exercises.

## Decisions made during the brainstorm

The 2026-04-29 brainstorm settled five forks. Recorded here so
the plan, the implementation, and any future revision pass have
a single authoritative source.

### 1. Phase F slice — F1 (structure) + F2 (threading)

The original Phase F bundled engine-shape change with threading
redesign. Rejected: cancellation has zero current test coverage
(audit confirmed), so its API has to be co-developed with its
tests TDD-style. Bundling that with the engine collapse would put
two unverified changes in the same tag with no working safety net
for either. **Settled: two tags.** F1 lands engine collapse with
existing-threading tests as the safety net; F2 redesigns
threading on top of a stable unified engine.

### 2. `IDomainAdapter` is a runtime virtual interface

The roadmap left this open: "`SyncEngine<Adapter>` (or runtime
equivalent — a borrowed `IDomainAdapter*`)." Three options
considered:

- **(A) Compile-time template `SyncEngine<Adapter>`.** Zero vtable
  cost; needs type erasure anyway for heterogeneous mappings;
  Q_OBJECT + templates is hostile (moc doesn't template).
- **(B) Runtime `IDomainAdapter*` virtual interface.** Single
  concrete `SyncEngine` class; vtable on diff/merge calls is
  irrelevant for I/O-bound sync work; matches existing codebase
  idioms (`SyncBackend`, `IBlobBackend`, `ISyncHost`,
  `ICalendarCollection` are all virtual interfaces).
- **(C) Hybrid.** YAGNI.

**Settled: (B).** Phase G's plugin model wants runtime registration
by record type; (A) is a redesign away from G, (B) is a 5-line
edit away. moc + Q_OBJECT works straightforwardly. None of (A)'s
"performance" advantages survive examination — sync is bound by
SQLite + network, not adapter dispatch.

### 3. Engine ↔ adapter boundary is pure `BackendRecord`

Three options for the type that flows across the engine/adapter
seam:

- **(a) Pure `BackendRecord`.** Engine sees `(id, hash, bytes,
  metadata)` only. Adapter parses iCal / interprets bytes
  internally; `CalendarDomainAdapter` may cache parsed
  `Incidence::Ptr` keyed by record id if profiling later
  warrants.
- **(b) Opaque payload handle.** `BackendRecord` plus a typed
  payload pointer the adapter materializes during fetch and
  consumes during diff/merge. Engine never inspects it.
- **(c) Templated payload.** Conflicts with decision 2.

**Settled: (a).** It's already how `BlobSyncEngine` works — F1
extends that pattern to calendar rather than introducing a third
payload-handle concept. It's exactly where Phase G ends up. Calendar
parsing cost is bounded (~tens of ms on a 1000-record sync against
SQLite + network costs in seconds); if hot, the adapter caches
internally without touching the engine. Option (b) is calendar-
knowledge-leaking-in-disguise — the engine has to plumb the
payload through even if it never reads it.

### 4. F.0 prerequisite: error-recovery test + failure triage only

F.0 lands ahead of F1 (`v0.12-phase-f0-test-gaps`). Its scope
was settled by the same brainstorm:

- (i) Library-side error-recovery test (move PlanStan's
  `tst_sync_error_recovery` pattern into libkalburator). Real
  coverage we lacked.
- (ii) Triage of the 24 PlanStan baseline "failures." Result:
  zero hide engine bugs (22 are EXCLUDE_FROM_ALL/flag-gated
  unrelated tests; 2 are environmental). Documented in FINDINGS.

Other audit-identified gaps (cancellation tests, adapter-boundary
tests, separable diff/merge tests, decoupling existing tests from
`SyncCoordinator` naming) cannot be written today because the APIs
don't exist yet — they will be co-developed with F1/F2 TDD-style.

### 5. WildPalms keeps a one-shot blob API surface

`WildPalms/src/runtime/syncrunner_wp.cpp:266` instantiates a
`BlobSyncEngine` per device sync and calls `twoWayWithBaseline()`
in a tight inner loop. Its blob backends are plugin-created
on-the-fly (no registered backend id), so the mapping-based
`runSync(mappingId)` API is awkward for it.

Two options:

- **(P) Keep one-shot signatures on the unified engine.** The
  new `SyncEngine` exposes both `runSync(mappingId, …)` (for
  configuration-driven sync) AND
  `runBlobTwoWay(IBlobBackend*, IBlobBackend*, …)` /
  `runBlobMirror(…)` (for ad-hoc one-shot calls). The latter
  internally constructs a synthetic mapping and runs it through
  the unified flow with `BlobDomainAdapter`.
- **(Q) Migrate WildPalms to mapping-based.** Plugins would
  register their backends in a `BackendRegistry` per device sync
  and create `SyncMapping`s on the fly.

**Settled: (P).** (Q) reshapes consumer code beyond what F1 needs
to cover, expanding scope. (P) is mechanical for WildPalms (rename
`BlobSyncEngine` → `SyncEngine` and the method names from
`twoWayWithBaseline` → `runBlobTwoWay`) and preserves the engine
collapse goal. The synthetic-mapping construction is internal
glue; it's not a real architectural concession. Phase G can revisit
once the plugin model is in place.

## Open questions resolution

The roadmap carried eight open questions. Phase E resolved Q1 (via
Phase D inheritance pattern) and Q2 (transcoding routing). F1
resolves the remainder where they're in scope, defers the rest.

| Q | Topic | F1 resolution |
|---|---|---|
| Q3 | `CalendarJournal` fate | **Calendar-shaped, lives inside `CalendarDomainAdapter`.** F2 may revisit if the threading API needs operation logging for crash recovery. F1: just move the existing class behind the adapter's facade — its consumers (`SyncWorker::applyChanges`) become adapter-internal. |
| Q5 | `ICalendarCollection` fate | **Stays as a calendar-adapter concern.** Engine never sees it. The adapter holds an `ICalendarCollection*` (set by the engine via the adapter's setter) and uses it during `applyChanges`. Phase G dissolves it into the calendar plugin. |
| Q6 | `DataDomain` enum fate | **Adapter type IS the discriminator.** F1 keeps `DataDomain` as caller-side metadata (PlanStan treats some mappings as "Project" tasks); the engine maps mapping → adapter via a registration mechanism (see component design below). G can delete the enum entirely when plugin lookup makes it redundant. |
| Q7 | Concurrent-mapping execution | **Defer to F2.** F1 preserves serial mapping execution. Concurrent execution is a threading-API concern. |
| Q8 | `.planstan-sync.db` rename | **Defer indefinitely.** No rename in F1. The migration cost is not zero (PlanStan users have data); the renaming benefit is purely cosmetic. Revisit if a different reason motivates it. |

## Components

### `IDomainAdapter` — virtual interface

Lives at `src/engine/idomainadapter.h`. New top-level directory
`src/engine/` for the unified engine and adapter interface;
calendar and blob adapters live in their respective domain
directories.

```cpp
namespace Kalburator::Sync {

class SyncBackend;       // calendar + blob (Phase D inheritance)
class IBlobBackend;      // blob only
struct BackendRecord;
struct SyncMapping;

/// Per-domain glue between the engine and its backends. Borrowed
/// pointer; engine does not own. The adapter is constructed by
/// the consumer (PlanStan, WildPalms) and registered with the
/// engine before sync runs.
class IDomainAdapter
{
public:
    virtual ~IDomainAdapter() = default;

    /// Discriminator. "calendar", "blob", or future "vcard".
    /// Used for diagnostics and for Phase G's plugin lookup.
    virtual QString domainType() const = 0;

    /// Fetch records from a backend's collection. The default
    /// dispatches to IBlobBackend::loadRecords; calendar-typed
    /// adapters override to apply iCal-aware fetch logic on top.
    virtual QList<BackendRecord> fetchRecords(
        SyncBackend* backend,
        const QString& calendarId) = 0;

    /// Compute the diff between source / target / baseline. The
    /// returned DiffResult is BackendRecord-shaped and can be
    /// inspected without re-parsing.
    virtual SyncDiff diff(const QList<BackendRecord>& source,
                          const QList<BackendRecord>& target,
                          const QList<BackendRecord>& baseline,
                          const BackendCapabilities& sourceCaps,
                          const BackendCapabilities& targetCaps) const = 0;

    /// Resolve conflicts in the diff according to policy and
    /// produce the merged record set.
    virtual SyncMerge merge(const SyncDiff& diff,
                            ConflictResolution policy) const = 0;

    /// Apply the merged result to the destination backend. Returns
    /// the new baselines (BackendRecord per applied write) on
    /// success. On failure, errorMessage is populated and the
    /// returned baselines list reflects what was actually written
    /// before the error (for partial-failure recovery).
    virtual ApplyResult applyChanges(
        const SyncMerge& merge,
        SyncBackend* destination,
        const QString& calendarId,
        const TranscodingPlan& plan = TranscodingPlan{}) = 0;

    /// Load and save baselines. Adapter holds its own baseline
    /// store; engine calls these around the diff/apply flow.
    virtual QList<BackendRecord> loadBaselines(
        const QString& mappingId) const = 0;
    virtual bool saveBaselines(
        const QString& mappingId,
        const QList<BackendRecord>& baselines) = 0;
};

} // namespace Kalburator::Sync
```

`SyncDiff`, `SyncMerge`, `ApplyResult` are simple value types in
`src/engine/syncdiff.h` (engine-internal). They carry lists of
`BackendRecord` operations annotated with type
(create/update/delete/conflict). Engine inspects them only at the
control-flow level (counts, conflict presence) — never at the
payload level.

### `CalendarDomainAdapter` — concrete, calendar-typed

Lives at `src/calendar/calendardomainadapter.{h,cpp}`. Owns:

- `CalendarBaselineStore*` (borrowed; engine sets it).
- `ICalendarCollection*` (borrowed; engine sets it during sync).
- `TranscodingRouter&` (borrowed; injected by engine).
- Optional internal cache of `Incidence::Ptr` keyed by
  `BackendRecord::id` for diff/merge fast path. Initially absent;
  add only if profiling shows iCal re-parsing is hot.

`fetchRecords()` calls `backend->loadRecords(calendarId)` (the
`IBlobBackend` view from Phase D's inheritance). `diff()` uses
`IncidenceDiff` against parsed `Incidence::Ptr`. `merge()` produces
a `SyncMerge` with `BackendRecord`-shaped operations. `applyChanges()`
absorbs the body of today's
`SyncWorker::applyChangesToBackend` — including the post-Phase-E
`writeFinished`-capture pattern that's currently in
`CreateIncidenceItem::commit()` / `UpdateIncidenceItem::commit()`.
The wrapper classes `CreateIncidenceItem` and `UpdateIncidenceItem`
become adapter-internal helpers (their public API surface goes
away — only PlanStan tests reference them today, and those tests
will migrate during the consumer-migration tasks).

### `BlobDomainAdapter` — concrete, blob-typed

Lives at `src/blob/blobdomainadapter.{h,cpp}`. Owns:

- `BlobBaselineStore*` (borrowed; engine sets it).

`fetchRecords()` calls `backend->loadRecords(collectionId)`.
`diff()` is hash-equality (compare `BackendRecord::contentHash`).
`merge()` is last-write-wins or whatever conflict policy says.
`applyChanges()` calls `IBlobBackend::createRecord` /
`updateRecord` / `deleteRecord`.

Absorbs the body of today's `BlobSyncEngine::twoWayWithBaseline`
and `mirror` operations. The free functions stop existing as a
separate class.

### `SyncEngine` — single concrete class

Lives at `src/engine/syncengine.{h,cpp}` (moved + renamed from
`src/calendar/synccoordinator.{h,cpp}`). The public API of today's
`SyncCoordinator` is preserved verbatim — same signal names, same
slots, same `SyncBehavior` enum, same `runSync(mappingId)` and
`runSync(behavior)` forms. PlanStan call sites change one type
name (`SyncCoordinator` → `SyncEngine`) and one include path
(`#include "synccoordinator.h"` → `#include "syncengine.h"`).

New on `SyncEngine`:

```cpp
/// Register an adapter for a domain. Engine stores by
/// IDomainAdapter::domainType(). Mappings are routed to
/// adapters via the SyncMapping's domain field (see Q6).
void registerAdapter(IDomainAdapter* adapter);

/// One-shot blob-typed sync. Internal: constructs a synthetic
/// SyncMapping with domain="blob", drives the unified flow with
/// BlobDomainAdapter, returns the result. Replaces
/// BlobSyncEngine::twoWayWithBaseline / mirror for ad-hoc
/// callers (WildPalms's syncrunner_wp.cpp). The behavior is
/// equivalent to today's BlobSyncEngine; only the type names
/// change.
BlobSyncResult runBlobTwoWay(IBlobBackend* a,
                             IBlobBackend* b,
                             const QString& collectionId,
                             const QString& mappingId,
                             BlobBaselineStore* baseline,
                             QSyncCore::ConflictHandlerRegistry* handlers,
                             QSyncCore::ConflictStore* conflicts,
                             const QSyncCore::ConflictPolicy& policy);

BlobSyncResult runBlobMirror(IBlobBackend* source,
                             IBlobBackend* target,
                             const QString& collectionId);
```

Internal: the body of today's `SyncWorker` (1647 LOC) is split.
Calendar-specific bits (incidence parsing, transcoding plan
construction, conflict signal forwarding for calendar-shaped
events) become `CalendarDomainAdapter`. Generic orchestration
bits (mapping loop, worker-thread dispatch, fetch → diff →
merge → apply → baseline progression, signal emission) stay as
`SyncEngine` private slots / methods. The split is mechanical —
each concern has a clear home — but the result is a smaller
engine and a focused calendar-only adapter.

`SyncWorker` as a separate class **goes away**. Its contents
relocate to `SyncEngine`'s private members. The
`SyncCoordinator` ↔ `SyncWorker` thread boundary becomes the
`SyncEngine`'s caller-thread ↔ worker-thread boundary —
preserved verbatim per decision 1.

### `BlobSyncEngine` — deleted

After `BlobDomainAdapter` and `SyncEngine::runBlobTwoWay` /
`runBlobMirror` land, `BlobSyncEngine` has zero callers. Its
`.h` and `.cpp` are removed. Its tests (`tst_blobsyncengine`)
either retarget to the new one-shot API or get replaced by an
equivalent test against `SyncEngine`.

### `BlobBaselineStore` — single-table

Today the store has two tables (per FINDINGS 2026-04-28: flat
`blob_baselines` keyed `(mapping_id, record_id)` for
`BlobSyncEngine`, and triple `blob_baselines_triple` keyed
`(backend_id, collection_id, record_id)` for the calendar
side's per-record version hashes from Phase D). With
`BlobSyncEngine` deleted in F1, the flat-keyed table and its API
go away — only `blob_baselines_triple` (renamed to
`blob_baselines`) survives. The flat-keyed call sites in
`WildPalms/src/runtime/syncrunner_wp.cpp` migrate to the triple
keying (`backendId = pluginId`, `collectionId = col.id`).

This is a mechanical rename + caller migration. The data
migration is `DROP TABLE blob_baselines_old; ALTER TABLE
blob_baselines_triple RENAME TO blob_baselines;` once we're
sure nothing reads the flat table.

**Risk:** WildPalms users have on-disk DBs with both tables.
Dropping the flat table means existing-baseline blob syncs hit
"first sync" semantics on F1 upgrade. Acceptable: WildPalms
plugins currently treat first-sync as full re-mirror, and the
worst case is one redundant sync per device on first launch
post-upgrade.

### `SyncBackend` and `IBlobBackend` — unchanged

Phase D's `class SyncBackend : public QObject, public IBlobBackend`
inheritance survives. Adapters call into the backends through these
interfaces. `IBlobBackend` is still pure (no QObject); calendar
backends still have the calendar-shaped `storeItems` /
`updateItem` / `startSync` / `removeItem` write methods that the
calendar adapter dispatches to.

`SyncBackend::transcodingWarning` signal (added in Phase E) stays
on the base; the calendar adapter connects to it and forwards
through the engine's public `transcodingWarning`.

## Test plan

### Existing tests that must stay green

All 21 libkalburator tests, all 73 WildPalms tests, PlanStan
unchanged at 96/120. The five integration tests in
`tests/calendar/` (sync_full, sync_oneway, conflict, transcoding_warning,
first_sync_via_blob_engine, subsequent_sync_uses_blob_view,
sync_error_recovery) will need source-level updates because they
hold concrete `SyncCoordinator*` pointers — but the public
behavior they verify is preserved. The recommended migration is a
deprecated alias `using SyncCoordinator = SyncEngine;` early in
F1, applied only inside libkalburator's own tests, removed at F1
completion. (Production callers in PlanStan get the actual rename.)

### New tests added in this phase

- **`tst_calendar_domain_adapter`** — unit test for
  `CalendarDomainAdapter::diff()` and `merge()` in isolation,
  given hand-constructed `BackendRecord` lists. Six methods:
  empty inputs; create-only diff; update detection; delete
  detection; conflict detection; merge applies policy correctly.
- **`tst_blob_domain_adapter`** — unit test for
  `BlobDomainAdapter::diff()` and `merge()`. Four methods:
  hash-equality detection; create-only; update; delete.
- **`tst_engine_unified_boundary`** — integration test for
  `SyncEngine::runSync()` against MockBackend. Verifies that
  `runSync` works for both calendar and blob mappings driven
  through the same engine, and that signal contracts are the
  same. Three methods: calendar mapping path; blob mapping
  path; mixed (calendar + blob mappings in one engine, run
  through `runSyncAll`).
- **`tst_engine_blob_one_shot`** — `SyncEngine::runBlobTwoWay`
  parity test. Mirrors `tst_blobsyncengine`'s scenarios against
  the new API. Confirms WildPalms's call site has no behavior
  drift.

### Acceptance criteria

- libkalburator standalone: 21 → 25 ctest executables (four new).
  All pass.
- `git grep "SyncCoordinator" src/` returns hits only inside
  `synccoordinator.h`/`.cpp` deletion-pending stubs (during
  migration) or zero hits (after).
- `git grep "BlobSyncEngine" src/` returns zero hits.
- `git grep "SyncWorker" src/` returns zero hits (class deleted).
- PlanStan baseline: 96/120 (unchanged — F1 doesn't touch the 24
  noise items).
- WildPalms baseline: 73/73.
- `verify-all.sh` exit 0.

## Migration order (within F1)

The plan doc enumerates concrete tasks; this section sketches the
shape so the plan author and the implementer have a starting
point.

**Group 0 — Prep (small):**

1. Add `src/engine/` directory; populate with `idomainadapter.h`,
   `syncdiff.h`, value types. No callers; just compiles.
2. Add `using SyncCoordinator = SyncEngine;` deprecated alias
   (initially `using SyncEngine = SyncCoordinator;` until the
   class is renamed). Ensures F1 work can reference the new name
   immediately even before the rename lands.

**Group 1 — Adapter extraction:**

3. Write `BlobDomainAdapter`. Body absorbed from
   `BlobSyncEngine::twoWayWithBaseline` and `mirror`. Test:
   `tst_blob_domain_adapter`. `BlobSyncEngine` still exists.
4. Write `CalendarDomainAdapter`. Body absorbed from
   `SyncWorker::applyChangesToBackend` and the diff/merge bits
   currently scattered through `SyncWorker`. Test:
   `tst_calendar_domain_adapter`. `SyncWorker` still exists.
5. Both adapters compile and pass their unit tests; engine still
   uses old paths.

**Group 2 — Engine collapse:**

6. Rename `SyncCoordinator` → `SyncEngine`. Move file to
   `src/engine/`. Update libkalburator-internal call sites and
   tests (most just need `SyncCoordinator → SyncEngine` and
   `synccoordinator.h → syncengine.h`).
7. Add `IDomainAdapter*` member array + `registerAdapter()` slot
   to `SyncEngine`. Mappings route to adapter by domain type
   (default "calendar" for back-compat).
8. Replace the body of `SyncWorker::applyChangesToBackend` with
   `m_calendarAdapter->applyChanges(...)`. Replace the diff/merge
   sections with `m_calendarAdapter->diff/merge`. Confirm
   `tst_calendar_*` still green.
9. Add `runBlobTwoWay` / `runBlobMirror` one-shot methods on
   `SyncEngine` (delegate to `BlobDomainAdapter`).
10. Test: `tst_engine_unified_boundary`,
    `tst_engine_blob_one_shot`. Should pass.

**Group 3 — `SyncWorker` collapse:**

11. Inline `SyncWorker`'s remaining body into `SyncEngine`'s
    private members. The QThread + `QMetaObject::invokeMethod`
    plumbing moves verbatim.
12. Delete `src/calendar/syncworker.{h,cpp}`. Confirm no
    references remain.
13. Confirm all 21 (now 25) tests still pass.

**Group 4 — `BlobSyncEngine` deletion:**

14. Update `WildPalms/src/runtime/syncrunner_wp.cpp` to use
    `Kalburator::Sync::SyncEngine` and `runBlobTwoWay()` /
    `runBlobMirror()`. Mechanical type/method rename. Confirm
    WildPalms tests pass.
15. Delete `src/blob/blobsyncengine.{h,cpp}`. Confirm no
    references remain.
16. Migrate `tst_blobsyncengine` to test `SyncEngine`'s
    one-shot API instead, OR delete it as redundant with
    `tst_engine_blob_one_shot`. Plan author's call.

**Group 5 — `BlobBaselineStore` consolidation:**

17. Drop the flat-keyed `blob_baselines` table and API. Migrate
    the triple-keyed table to that name.
18. Update `WildPalms/src/runtime/syncrunner_wp.cpp` to use the
    triple-keyed shape (`backendId = pluginId`,
    `collectionId = col.id`). Test: WildPalms full sync works.

**Group 6 — Consumer migrations:**

19. PlanStan: `SyncCoordinator` → `SyncEngine` rename across all
    src and test files. The deprecated alias goes away here.
20. WildPalms: should be unchanged after Group 4. Verify.

**Group 7 — Cleanup:**

21. Run `verify-all.sh` until exit 0 (with baseline refresh after
    libkalburator test count grows).
22. Update phase docs (`04p-...-design.md` Status,
    `04p-...-plan.md` Status, `04k-engine-merger-roadmap.md`
    table, `CURRENT-STATUS.md`, `FINDINGS.md` if non-obvious
    learnings emerged).
23. Tag `v0.13-phase-f1-unify` (user runs the tag command).

## Deferred / future work

Recorded explicitly so future phases inherit a clean handoff.

### Threading API redesign — Phase F2

Per decision 1. F2 introduces:

- `QFuture<SyncResult>`-based public `runSync` API.
- `QFuture::cancel()` token propagation; engine checks at every
  operation boundary.
- Operation-handle standardization on `SyncBackend` (formalize
  the half-finished `FetchOperation*` / `PushOperation*` pattern
  that's already there).
- Concurrent-mapping execution (Q7).
- Wrapper transactional semantics restoration (the 30s timeout +
  full pushItems error semantics that Phase E partially lost).
- Capability-aware transcoding routing (Phase E preserved
  string-based; F2 extends `TranscodingRouter` with capability
  diff input).

F2 design doc is `04q-phase-f2-threading-design.md` (not yet
written). Brainstorm before F2 begins.

### Plugin model — Phase G

`IDomainAdapter` becomes `IRecordDiffer` / `IRecordMerger`
plugins registered by record type. Engine consults the registry
at runtime; calendar support loads as a sibling library or
optional translation unit. The KCalendarCore dependency lifts off
the engine's core path.

`TranscodingRegistry` de-singletonisation lands here too — the
per-engine instance becomes per-plugin-host.

### `DataDomain` enum deletion — Phase G

Once plugin-driven adapter lookup is in place, the enum is
redundant.

## Cross-references

- `04k-engine-merger-roadmap.md` — Phase F1 entry in the
  at-a-glance status table; F slice rationale.
- `04m-phase-d-compose-design.md` — Phase D's
  `SyncBackend : public IBlobBackend` inheritance, which F1
  builds on.
- `04n-phase-e-transcoding-design.md` — Phase E's
  `TranscodingRouter` (per-engine instance, registry-injectable)
  and `TranscodingPlan` value type. F1 carries them forward
  unchanged; the calendar adapter holds the router.
- `04o-phase-f0-test-gaps-design.md` — F.0's
  `tst_calendar_sync_error_recovery` is the contract F1 must
  preserve for backend-write error propagation.
- `~/dev/refactor-engine-merger/FINDINGS.md` — "BlobBaselineStore
  has two storage tables" (2026-04-28); "Wrapper commit() lost
  error detection" (2026-04-29); "TranscodingRegistry is a
  process-wide singleton" (2026-04-28); "IBlobBackend must be a
  pure interface" (2026-04-28).
- `WildPalms/src/runtime/syncrunner_wp.cpp:266` — the
  `BlobSyncEngine` consumer that drives the (P) decision
  in §2.5.
