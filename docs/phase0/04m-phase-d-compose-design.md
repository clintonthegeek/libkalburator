# Phase D — Compose (design)

**Date:** 2026-04-28
**Status:** Landed 2026-04-29 on tag `v0.10-phase-d-compose` (libkalburator HEAD `6cbd849`). Approved 2026-04-28 by user via brainstorming session.
Implementation plan in `04m-phase-d-compose-plan.md` (sibling).
**Phase tag on completion:** `v0.10-phase-d-compose`.
**Gates:** Phase D.0 complete (`v0.9-phase-d0-tests-first`).

## Goal

Phase D of `04k-engine-merger-roadmap.md`: replace the deliberate
`╳` non-coupling between the calendar layer and the blob layer with
**composition**. Calendar engine internally delegates byte-level
fetch/store/hash to the blob engine; calendar engine continues to
drive incidence-typed orchestration (3-way merge, conflict
detection, transcoding) on top.

Bundled in: the SyncStore carve-up that Audit 1 of `04a-followups.md`
defers to Phase C. Storage cannot mix calendar-typed baselines with
blob-typed baselines while we're trying to compose engines, so the
carve-up rides with this phase.

## Decisions made during the brainstorm

The brainstorm resolved five forks. Recorded here so that the plan,
the implementation, and any future revision pass have a single
authoritative source for "why we're doing it this way."

### 1. Delegation depth: thin delegation + one end-to-end blob path

The roadmap text mixes two shapes — *"calendar engine continues to
drive on top of the blob view"* (calendar drives, blob is a
fetch/store API) versus *"blob engine drives at least one calendar
code path end-to-end"* (suggests `BlobSyncEngine` actually runs).
Settled: **both, in different paths.**

- **Subsequent syncs (have a baseline).** `SyncWorker` keeps full
  orchestration. Bytes move through the blob view
  (`static_cast<IBlobBackend*>(backend)`); 3-way merge runs against
  `CalendarBaselineStore` (full iCal text); conflict + transcoding
  unchanged. `BlobSyncEngine` is *not* invoked here in Phase D.
- **First sync (no baseline yet).** `SyncWorker` detects the
  no-baseline case and dispatches via `BlobSyncEngine::mirror(...)`
  (one-way mappings) or `BlobSyncEngine::twoWayNaive(...)` (two-way
  mappings). After the blob engine finishes the byte-level transfer,
  `SyncWorker` post-processes to populate `CalendarBaselineStore`
  with the resulting iCal text so the next sync has a baseline.

This satisfies the roadmap success criterion literally — `BlobSyncEngine`
drives a real, end-to-end calendar code path — while keeping the diff
to subsequent-sync paths bounded. It also gives Phase F (Unify) a
working precedent for what blob-engine-driven calendar sync feels
like.

### 2. Bridge ownership: inheritance, hoisted to `SyncBackend`

Open Question 1 had three options: inheritance, member, view.
Settled: **inheritance, hoisted one level to `SyncBackend`**, so the
type system enforces "every calendar backend is also a blob backend."

```cpp
class SyncBackend : public QObject, public IBlobBackend {
    // … existing calendar-typed surface unchanged
};
```

Every concrete `SyncBackend` (`LocalBackend`, `RemoteBackend`,
`OrgBackend`, `MockBackend`, `AkonadiBackend`, `DecSyncBackend`,
`SubscriptionBackend`, `HolidaySubscriptionBackend`) gains an
`IBlobBackend` implementation. Read-only backends (the
subscription pair, possibly Holiday) implement writes as
`return false` / `throw NotSupported`; this maps cleanly onto the
existing `BackendCapabilities::canCreate` flag.

**Why hoist instead of inheriting on each concrete backend?** A
single inheritance edge means one place to reason about the
calendar/blob relationship; no risk of one concrete backend forgetting
the inheritance and being silently uncoercible to `IBlobBackend*`.
The diff also gets smaller — eight subclass declarations stay as
they are; only the base class header changes.

**The recordId mapping per backend.** `IBlobBackend::loadRecord(recordId)`
needs each backend to define what a recordId *is* in its world:

| Backend | `recordId` definition |
|---|---|
| `LocalBackend` | iCal UID (filename without `.ics`) |
| `RemoteBackend` | calendar-relative href (already what CalDAV serves) |
| `OrgBackend` | iCal UID derived from `:ID:` org property |
| `MockBackend` | iCal UID (already keyed this way) |
| `AkonadiBackend` | Akonadi `Item::Id` rendered as a string |
| `DecSyncBackend` | iCal UID |
| `SubscriptionBackend` | iCal UID |

Documented in each backend's blob view implementation.

### 3. Storage filename: rename `.planstan-sync.db` → `.kalburator-sync.db`

Library-neutral name; PlanStan branding drops out. There is **no
user-facing migration debt** on either side (confirmed during
brainstorm) — the rename is just a name change with no compatibility
shim required. SQLite file substrate stays the same; the carved-up
stores all live inside it.

### 4. SyncStore carve-up: full split, class dissolves at end of phase

Audit 1 names splits for calendar baselines, conflicts, CTags, and
ID mappings. Three concerns are silent in Audit 1; resolved during
brainstorm:

| Today's `SyncStore` concern | Phase D destination |
|---|---|
| Version hashes (per `(backendId, calendarId, uid)`) | `BlobBaselineStore` (key shape generalized to per-record) |
| Calendar baselines (iCal text per `(mappingId, uid)`) | `CalendarBaselineStore` (new) |
| Property baselines (JSON per `(mappingId, calendarId)`) | `CalendarBaselineStore` (new) |
| CTags (per `(backendId, calendarId)`) | `RemoteBackend` private state |
| Local fingerprints (per `(backendId, calendarId)`) | `LocalBackend` private state |
| Conflict tracking | `ConflictStore` (already at lower layer; reused) |
| ID mappings | `IDMappingStore` (already exists; reused) |

`SyncStore` itself dissolves at the end of Phase D — the class is
deleted once the last caller migrates.

The version-hash decision is the load-bearing one: putting the
per-record content-hash in the *blob* layer means the unified engine
in Phase F has a single canonical source for "did this record
change?" regardless of whether it's a calendar incidence or any other
domain.

### 5. Change-detection API shape: keep `modifiedSince(QDateTime)` for D

`IBlobBackend::modifiedSince(collectionId, since)` is timestamped.
CalDAV CTags and local directory fingerprints are opaque-marker shaped,
not timestamps. Two viable answers:

- **(i)** Keep `modifiedSince(QDateTime)` in Phase D. Each backend
  implements it however it wants — `RemoteBackend` consults its
  cached CTag and returns empty quickly when the server's CTag
  matches; `LocalBackend` consults its cached directory fingerprint
  and returns empty when unchanged. The timestamp argument becomes a
  hint, not a contract.
- **(ii)** Add `collectionChangeMarker(collectionId) -> QString` plus
  `recordsSinceMarker(collectionId, marker)`. Cleaner abstraction but
  expands `IBlobBackend`'s surface during a phase already changing
  a lot.

Settled: **(i) for Phase D, (ii) for Phase F.** Don't disturb
`IBlobBackend`'s shape during Phase D; the leak is small. Phase F is
already redesigning the unified engine API and is the natural place
to introduce the opaque-marker shape.

## Architecture

### Layered shape after Phase D

```
┌────────────────────────────────────────────────────────────┐
│ Calendar layer (KCalendarCore-bound)                       │
│   SyncCoordinator → SyncWorker                             │
│       │                                                    │
│       │ first-sync mirror ──► BlobSyncEngine.mirror(...)   │
│       │                       (or twoWayNaive for two-way) │
│       │                                                    │
│       │ subsequent syncs ──► IBlobBackend (blob view)      │
│       │   for fetch/store + per-record hash skip;          │
│       │   3-way merge driven here against                  │
│       │   CalendarBaselineStore                            │
│       ▼                                                    │
│   storage: CalendarBaselineStore (iCal text + property JSON)│
└──────┬─────────────────────────────────────────────────────┘
       │ inheritance: SyncBackend : public IBlobBackend
       ▼
┌────────────────────────────────────────────────────────────┐
│ Blob layer (generic byte-level)                            │
│   BlobSyncEngine → IBlobBackend impls                      │
│   storage: BlobBaselineStore (now keyed per-record incl.   │
│            version hashes), ConflictStore, IDMappingStore  │
└────────────────────────────────────────────────────────────┘

Backend-private state (no longer in shared SyncStore):
  - RemoteBackend → CTag cache  (consulted in modifiedSince)
  - LocalBackend  → directory-fingerprint cache (same)

Storage substrate: single SQLite file at .kalburator-sync.db
SyncStore class: dissolved.
```

The vertical bridge replaces the `╳`. The "compose" claim is
realised in the type system — `static_cast<IBlobBackend*>(syncBackend)`
is total, not optional.

### Component changes

#### A. Storage layer

- **`BlobBaselineStore`** (already exists at
  `src/blob/blobbaselinestore.{h,cpp}`): generalize key shape from
  flat `RecordId` to `(backendId, collectionId, recordId)` so it can
  host per-record version hashes for calendar backends. Existing
  blob-layer callers (`BlobSyncEngine`) get a backwards-compatible
  overload.
- **`CalendarBaselineStore`** (new at
  `src/calendar/calendarbaselinestore.{h,cpp}`): owns iCal-text
  baselines per `(mappingId, uid)` and property-JSON baselines per
  `(mappingId, calendarId)`. SQLite-backed, table prefix
  `calendar_baseline_*`, lives in the same `.kalburator-sync.db`
  file as the blob stores.
- **`SyncStore`** (existing at `src/calendar/syncstore.{h,cpp}`):
  during the overlap window, the class becomes a thin facade
  forwarding each method to its new home. Marked `[[deprecated]]`.
  Deleted entirely once no caller remains. Per the deprecation-with-
  overlap pattern in `OPERATIONS.md`.
- **Storage filename**: rename `.planstan-sync.db` to
  `.kalburator-sync.db`. The SQLite file path is built in
  `SyncStore::dbPathFor(...)` today — point the new stores at the new
  filename and let the (deprecated) `SyncStore` facade redirect to it.
  No legacy data migration; both consumers are pre-release.
- **CTag storage**: `RemoteBackend` gains a private `m_ctags`
  `QHash<CalendarId, QString>` plus persistence to a per-backend
  table inside `.kalburator-sync.db`. Public surface unchanged.
- **Local fingerprint storage**: same shape on `LocalBackend` —
  private `m_fingerprints` plus per-backend table.

#### B. Backend interface

- **`SyncBackend`** (existing at `src/calendar/syncbackend.{h,cpp}`):
  base class declaration changes from `class SyncBackend : public QObject`
  to `class SyncBackend : public QObject, public IBlobBackend`.
  No member changes; subclasses implement the inherited
  `IBlobBackend` pure virtuals.
- **Each concrete backend** gets implementations for
  `IBlobBackend::backendId()`, `displayName()`, `isAvailable()`,
  `availableCollections()`, `collectionInfo(...)`,
  `createCollection(...)`, `loadRecords(...)`, `loadRecord(...)`,
  `createRecord(...)`, `updateRecord(...)`, `deleteRecord(...)`,
  `modifiedSince(...)`, `deletedSince(...)`,
  `supportsDeleteTracking()`, `beginBatch()`, `commitBatch()`,
  `rollbackBatch()`, `supportsBatch()`. Most map directly onto
  existing calendar-typed methods; the recordId mapping (table in
  Decision 2) defines the per-backend translation. Read-only
  backends return `false` from create/update/delete.

#### C. Engine wiring

- **`SyncWorker`** (existing at `src/calendar/syncworker.{h,cpp}`):
  - Add a new method `dispatchFirstSync(mappingId, source, target)`
    that detects "no calendar baseline yet for this mapping" by
    consulting `CalendarBaselineStore` and routes through
    `BlobSyncEngine::mirror(...)` or `twoWayNaive(...)` based on
    `mapping.mode`. After completion, post-processes records to
    populate `CalendarBaselineStore`.
  - `fetchSourceRecords` / `fetchTargetRecords` now use the blob
    view — `static_cast<IBlobBackend*>(backend)->modifiedSince(...)`
    — instead of the deprecated `loadItems(...)` calendar-typed path.
  - Per-record hash skip: before invoking calendar-level diff, check
    `BlobBaselineStore.hash(record)` against `record.contentHash`;
    skip the merge for matches.
  - 3-way merge unchanged in behavior — same `IncidenceDiff` /
    `PropertyTranscoder` logic, just consults `CalendarBaselineStore`
    instead of `SyncStore`.
- **`SyncCoordinator`**: no behavioral change. Holds references to
  the new stores instead of `SyncStore*`.

### Data flow — first sync vs subsequent sync

**First sync (no calendar baseline yet for this mapping):**

```
SyncWorker::syncMapping(mapping)
   │
   ├─ CalendarBaselineStore.hasBaselines(mapping.id)?  ─► false
   │
   ├─ source := mapping.sourceBackend  (cast to IBlobBackend*)
   │  target := mapping.targetBackend  (cast to IBlobBackend*)
   │
   ├─ if mapping.mode == OneWayUpload:
   │    BlobSyncEngine::mirror(source, target, mapping.collectionId)
   │  else:  // FullSync / TwoWay
   │    BlobSyncEngine::twoWayNaive(source, target, mapping.collectionId)
   │
   ├─ on completion: harvest the resulting records and populate
   │    CalendarBaselineStore with their iCal text and property JSON.
   │
   └─ emit syncCompleted(mapping.id)
```

**Subsequent sync (baseline exists):**

```
SyncWorker::syncMapping(mapping)
   │
   ├─ source.modifiedSince(coll, lastSync)  ─► layer 1 short-circuit
   │     RemoteBackend: CTag matches?  yes → []  done.
   │     LocalBackend:  fingerprint matches?  yes → []  done.
   │
   ├─ for each surfaced record:
   │     hash == BlobBaselineStore.hash(record)?  ─► layer 2 skip
   │       yes → skip
   │       no  → proceed to calendar 3-way merge
   │
   ├─ calendar 3-way merge consults CalendarBaselineStore (iCal text)
   │    on records that survived layers 1 & 2.  Same merge / conflict /
   │    transcoding logic as today, just reading from the new store.
   │
   ├─ apply changes via blob view (createRecord/updateRecord/deleteRecord)
   │
   ├─ on success: update CalendarBaselineStore (new iCal text) and
   │              BlobBaselineStore (new content hashes).
   │
   └─ emit syncCompleted(mapping.id)
```

### What deliberately doesn't change

- **Threading model.** Calendar worker thread, blob layer
  synchronous, calendar worker calls blob inline. Phase F redesigns.
- **`TranscodingRegistry` call sites.** `SyncWorker::applyChanges`
  still invokes transcoders. Phase E moves them to backends.
- **`IBlobBackend`'s API surface** (no new methods). Phase F adds
  `collectionChangeMarker`.
- **The four D.0 test contracts.** They must still pass at the end
  of Phase D — same behavior, possibly slightly different storage
  call sites.
- **Conflict handling shape.** `SyncWorker::handleConflicts` still
  drives conflict signals + resume the same way; conflict storage
  moves from `SyncStore` to `ConflictStore` but the surface is
  unchanged.

## Sequencing

The phase lands in three groups, each independently buildable and
testable. The group order minimises in-flight churn — finish the
storage carve-up before starting the type-hierarchy change, finish
the type-hierarchy change before rewiring the engine.

### Group 1: storage carve-up (deprecation-with-overlap)

Goal: every concern that's in `SyncStore` today moves to its proper
home, while `SyncStore` continues to compile and serve callers as a
deprecated facade. Each step is a separate commit; libkalburator's
ctest stays green throughout.

1. Land `CalendarBaselineStore` skeleton (new file, new SQLite
   tables, no callers yet).
2. Generalize `BlobBaselineStore`'s key shape to `(backendId,
   collectionId, recordId)`. Existing blob-layer callers use a
   convenience overload that defaults `backendId`/`collectionId` to
   the current sync's pair.
3. Introduce `RemoteBackend::CTagStore` (private, persisted to per-
   backend table). Migrate `RemoteBackend`'s callers from
   `SyncStore::ctag(...)` to `m_ctags.get(...)`.
4. Same for `LocalBackend::FingerprintStore`.
5. Switch `SyncStore::baseline(...)`, `setBaseline(...)`, etc. to
   forward to `CalendarBaselineStore`. Mark `[[deprecated]]`.
6. Same for `versionHash(...)` etc., forwarding to
   `BlobBaselineStore`.
7. Migrate every caller off `SyncStore::*` to its new home.
8. Rename SQLite file `.planstan-sync.db` → `.kalburator-sync.db`
   (path constants in one place; old path deleted).
9. Delete `SyncStore`. Strip the `[[deprecated]]` markers. Update
   PlanStan and WildPalms in their respective worktrees if they
   reference `SyncStore` directly.

### Group 2: backend inheritance hoist

Goal: `SyncBackend : public IBlobBackend`. Every concrete backend
implements the new pure virtuals.

10. Add `: public IBlobBackend` to `SyncBackend`. (The base header
    change forces every subclass to either implement or inherit a
    default `=delete`'d throw.)
11. Implement `IBlobBackend` methods on each concrete backend, one
    per commit. Order: `MockBackend` first (it's our test fixture
    and has the simplest internal storage), then `LocalBackend`,
    `RemoteBackend`, `OrgBackend`, `AkonadiBackend`, `DecSyncBackend`,
    `SubscriptionBackend`, `HolidaySubscriptionBackend`.
12. Add a small unit test per backend that exercises the new blob
    view: `tst_<backend>_blob_view` performs a `loadRecord` round-
    trip and a `createRecord` + `loadRecord` confirmation. Read-only
    backends test that writes return `false`.

### Group 3: engine wiring

Goal: `SyncWorker` uses the blob view; first-sync goes through
`BlobSyncEngine`.

13. Wire `SyncWorker::fetchSourceRecords` / `fetchTargetRecords` to
    the blob view. The four D.0 tests must still pass.
14. Add per-record hash skip using `BlobBaselineStore.hash(record)`.
    The four D.0 tests must still pass.
15. Add `dispatchFirstSync` and the no-baseline detection. Add
    `tst_calendar_first_sync_via_blob_engine` to verify
    `BlobSyncEngine::mirror` is invoked on the first-sync path and
    `CalendarBaselineStore` is populated afterward.
16. Final: run `verify-all.sh`. Update `CURRENT-STATUS.md`,
    `04m-status.md`, and tag `v0.10-phase-d-compose`.

## Test plan

### D.0 contracts to preserve

These four test executables exist as of Phase D.0 and must continue
to pass without modification of their *.cpp* files (CMake plumbing
may need to follow renames). Any test that fails during Phase D
indicates a real regression in observable engine behavior.

- `tst_calendar_sync_full` — full bidirectional sync.
- `tst_calendar_sync_oneway` — one-way upload sync.
- `tst_calendar_conflict` — conflict + 3-way merge, both modes.
- `tst_calendar_transcoding_warning` — transcoding-warning emission.

### New tests added during Phase D

- `tst_calendar_baseline_store` — unit test for `CalendarBaselineStore`:
  set baseline, get baseline, replace, remove, bulk operations,
  per-mapping clearing, persistence across re-open.
- `tst_blob_baseline_store_per_record_keys` — unit test for the per-
  record key shape extension to `BlobBaselineStore`.
- `tst_<backend>_blob_view` × 8 — one per concrete backend, verifies
  `IBlobBackend` view works via the inherited surface. Read-only
  backends test that writes return `false`.
- `tst_calendar_first_sync_via_blob_engine` — verifies the first-sync
  dispatch path: no calendar baseline → `BlobSyncEngine::mirror` (or
  `twoWayNaive`) is invoked → after completion,
  `CalendarBaselineStore` has the expected entries.
- `tst_calendar_subsequent_sync_uses_blob_view` — verifies subsequent
  sync calls `IBlobBackend::modifiedSince` and short-circuits on
  per-record hash equality. (Spy on `MockBackend`'s blob-view call
  log.)

### Acceptance criteria

1. `cmake --build build --target tests` succeeds.
2. libkalburator standalone `ctest` reports **all tests passing**.
   New test count: 9 (D.0) + 1 (`CalendarBaselineStore`) + 1 (blob
   baseline keys) + 8 (per-backend blob views) + 2 (first-sync,
   subsequent sync) = 21.
3. PlanStan worktree builds and tests at baseline (95 pass / 25 fail —
   pre-existing flakes).
4. WildPalms worktree builds and tests at baseline (73/73).
5. `scripts/verify-all.sh` exit clean.
6. Tag `v0.10-phase-d-compose` on libkalburator's
   `refactor/engine-merger`.

## Risks & gotchas

- **Read-only backends.** Subscription backends (and possibly
  `HolidaySubscriptionBackend`) don't write. Their `IBlobBackend`
  write methods must return `false` cleanly, not crash. Easy to miss
  in implementation; the per-backend `tst_*_blob_view` test catches
  it.
- **`recordId` definition drift.** Each backend defines `recordId`
  differently (UID vs href vs Akonadi Item::Id). Document the
  mapping in each backend's blob-view header comment, and make sure
  `IDMappingStore` callers don't conflate identifier kinds.
- **`BlobBaselineStore` key migration.** Existing blob-layer callers
  use the flat `RecordId` shape. The generalized shape adds
  `backendId`, `collectionId` — supply convenience overloads to
  avoid touching every blob-layer call site in this phase.
- **First-sync detection is per-mapping, not per-collection.** Two
  mappings can share collections but have different baseline state.
  Detection must be `CalendarBaselineStore.hasBaselines(mappingId)`,
  not `(collectionId)`.
- **Post-first-sync baseline harvest is the load-bearing step.** If
  `BlobSyncEngine::mirror` succeeds but the harvest step fails, the
  next sync will see "no calendar baseline" and re-dispatch the
  first-sync path, doing the work twice. Wrap harvest + mirror in a
  single transactional unit (best-effort: rollback if harvest fails;
  report a sync error).
- **Threading invariant for new stores.** `CalendarBaselineStore`
  must be safe to access from the `SyncWorker` thread. Use the same
  thread-affinity pattern as the existing blob stores (which are
  worker-thread-only).
- **Test isolation via SQLite path.** New tests must use
  `QTemporaryDir`-scoped paths for `.kalburator-sync.db`, identical
  to the D.0 pattern.
- **`SyncCoordinator::runSync(mappingId)` is leaky** (FINDING from
  D.0). Phase D's tests must continue to use the no-arg `runSync`
  path.
- **`TranscodingRegistry` singleton state** (FINDING from D.0). New
  tests in `tests/calendar/` follow the cleanup pattern.
- **`StubCalendarCollection` calendar-id matching** (libkalburator
  CLAUDE.md). New stub-host tests must satisfy the
  `setId(calendarId)` invariant.

## Open questions deferred

These are not blocked-by Phase D and are not resolved here.

- **OQ2 (transcoding routing).** Phase E moves transcoding decisions
  to backends. Phase D leaves `TranscodingRegistry` call sites
  untouched.
- **OQ3 (`CalendarJournal`).** Phase F.
- **OQ4 (`DecSyncBackend` placement in unified engine).** Phase F.
- **OQ5 (`ICalendarCollection` survival).** Phase F.
- **OQ6 (`DataDomain { Calendar, Project }` enum).** Phase G.
- **OQ7 (concurrent-mapping execution).** Phase F.
- **OQ8 (`.planstan-sync.db` migration).** Resolved here as
  rename-now (no migration debt). Question retired.
- **`collectionChangeMarker` opaque-marker API on `IBlobBackend`.**
  Phase F.
- **De-singletonising `TranscodingRegistry`.** Phase G.
- **`SyncCoordinator::runSync(mappingId)` leak.** Phase F (the
  unified-engine API redesign is the natural place to fix).

## Cross-references

- `04k-engine-merger-roadmap.md` — Phase D scope section, open
  questions list.
- `04h-blob-layer-design.md` — the `╳` non-coupling diagram this
  phase replaces.
- `04i-blob-baseline-store-design.md` — `BlobBaselineStore`'s
  current key shape; per-record extension lands here.
- `04a-followups.md` — Audit 1 (BaselineStore split) is the source of
  the carve-up plan; this phase executes it.
- `04j-engine-conflict-wiring-design.md` — conflict-store wiring
  from Phase B4; reused as-is in Phase D.
- `04l-phase-d0-test-harness-design.md` — the test harness this
  phase preserves and extends.
- `~/dev/refactor-engine-merger/FINDINGS.md` — D.0 findings (runSync
  leak, conflict signal policy, transcoding singleton, build flags,
  stale build dirs).
- `~/dev/refactor-engine-merger/OPERATIONS.md` — deprecation-with-
  overlap pattern used in Group 1.
