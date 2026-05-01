---
status: ideation — implementation walkthrough
date: 2026-04-30
phase: G (pre-design)
companion-to: 04r-phase-g-shape-pipeline-ideation.md, 04r-phase-g-walkthrough.md, 04r-phase-g-walkthrough-wildpalms.md
scope: scoped shape-pipeline migration sequencing
---

# Phase G — Migration walkthrough

**Status:** Ideation — implementation walkthrough. Companion to the
prior three Phase G ideation/walkthrough documents. This is a
*planning* artefact, not an architectural one: it answers
**"how do we land the scoped shape-pipeline architecture from
post-F2 in pieces that keep `verify-all` green and follow the
deprecation-with-overlap pattern?"**

The walkthrough takes as inputs:

- 22 inline architectural decisions across the Alice and Bob walks
- 14 substantive code components those decisions imply
- The current state of the three worktrees on `refactor/engine-merger`
  (post-F2, tag `v0.14-phase-f2-threading` applied)
- `OPERATIONS.md`'s deprecation-with-overlap pattern as the discipline

It produces:

- A **dependency graph** of the 14 components
- A **proposed slicing into 10 sub-phases** (G.1 through G.10) with
  explicit deps, exit criteria, and gotchas per slice
- A **test fate map** for the ~8000 lines of PlanStan backend tests
  (the answer to original question 8)
- A **tag strategy** with three checkpoint tags
- A total **time estimate** and **parallelism analysis**

If this walkthrough survives, the design doc (`04r-phase-g-design.md`)
and plan doc (`04r-phase-g-plan.md`) are next. If it surfaces a
landed-state we can't safely traverse, we reshape the slicing or the
architecture.

## Inputs grounded against current state

A handful of measurements taken on the actual worktrees:

- `dataDomain()` callsites across all three repos: **13**. Migration
  to `nativeShapes()` is bounded.
- `ISyncHost`: **70 lines, 12 virtual methods**. Calendar-typed
  (`applyIncidenceAddition`, `applyIncidenceRemoval`,
  `applyIncidenceUpdate`, `collection`, `incidenceSource`,
  `incidenceRegistry`, `unloadCalendar`,
  `generateSyncMappingsFromLogicalCalendars`). Plus generic
  (`backendById`, `backends`, `configStore`).
- `IDomainAdapter`: **97 lines**. Two implementations
  (`CalendarDomainAdapter`, `BlobDomainAdapter`).
- F1 facade: `runBlobTwoWay`, `runBlobMirror` on `SyncEngine`,
  documented at `syncengine.h:533, 545`.
- WildPalms uses F1 facade in **3 sites** within `syncrunner_wp.cpp`.
- PlanStan synchronous I/O callers (per F2 outcome doc): **~180
  test sites** + **1 production caller** (`convertCalendarToBackend`).
- WildPalms PalmCalendarBackend's legacy stubs already delegate to
  the operation API; deletion is mostly a base-class change.

## The 14 components

Numbered for cross-reference; not an ordering yet.

1. **Property catalogues per shape** — ID + property list per shape
2. **Shape value type + edge primitives** — `Shape`, `TransformationEdge`,
   `LossProfile`, `Pipeline` types
3. **TransformationRegistry** — static-init populated, hub-and-spoke
4. **Shape-typed differs/mergers** — `IRecordDiffer`, `IRecordMerger`
   keyed on canonical shape
5. **Backend interface migration** — `nativeShapes()`, `resourceId()`,
   retire `dataDomain()`
6. **Mapping shape + mapping-keyed baselines** — `BlobBaselineStore`
   re-keyed; `SyncMapping` may grow `mapping_id`
7. **BlobDomainAdapter dispatch** — registered for unified dispatch;
   `runSyncFuture` accepts blob-typed mappings
8. **F1 facade retirement** — `runBlobTwoWay`/`runBlobMirror` deleted
9. **Synchronous I/O retirement** — `loadItems`/`storeItems`/
   `updateItem`/`writeFinished` deleted from `SyncBackend`
10. **WildPalms `SyncRunner_wp` dissolution** — replaced by
    `HotSyncCoordinator` + per-Palm-DB SyncBackends with `resourceId()`
11. **Resource-aware MappingScheduler** — `runSyncFuture(QList<MappingId>)`
    overload; capacity-1 per resource scheduling;
    `CancellationReason`; `SyncEngineFuture` wrapper
12. **ISyncHost narrowing** — generic event sink; calendar-typed
    methods retire
13. **New domain plugins** — todo, contacts, memo plugins with
    canonical-shape edges
14. **New stock backends + universal sinks** — `AkonadiContacts`,
    `AkonadiNotes`, `AkonadiTasks`, `CardDAV`, `RawFiles`,
    `GenericSqlite`. Plus loss-profile reporting infrastructure and
    the `LossProfileDetailView` Qt widget.

## Dependency graph

```
                             ┌─[1] Property catalogues
                             │
                             ▼
[2] Shape + edge types ──► [3] TransformationRegistry ──► [4] Shape-typed differs
                                       │
                                       ├──► [13] New domain plugins (todo/contacts/memo)
                                       │            │
                                       ▼            ▼
                          [5] Backend interface ──► [6] Mapping-keyed baselines
                                  │                       │
                                  │                       ├──► [7] BlobDomainAdapter dispatch
                                  │                       │              │
                                  │                       │              ├──► [10] WildPalms SyncRunner dissolution
                                  │                       │              │              │
                                  │                       │              ▼              ▼
                                  ▼                       │         [8] F1 facade ◄──── (gated by 10)
                          [11] MappingScheduler ◄─────────┘
                                  │
                                  ▼
                          [12] ISyncHost narrowing  ◄──── (parallelizable)
                                  │
                                  ▼
                          [9] Synchronous I/O retirement (gated by test moves)

[14] New stock backends + universal sinks + loss UX — parallelizable post-[5,13]
```

Critical paths:

- **Architectural foundation chain:** [1]→[2]→[3]→[4]→[5]→[6]
- **Engine unification chain:** [6]→[7]→[10]→[8]
- **Cleanup chain:** [test moves]→[9], [12] independent

The cleanup work (deletions) gates on consumer migration; the
architectural work gates on having property catalogues and the
registry in place.

## Proposed slicing — 10 sub-phases

Each sub-phase is its own commit set on `refactor/engine-merger`,
ending with `verify-all.sh` green. Tags at three checkpoints
(see "Tag strategy" below).

### G.1 — Foundations (~2 weeks)

**Components landed:** [1] property catalogues, [2] shape+edge
types, [3] registry shell.

**Scope:**

- New header `src/shape/shape.h` with `Shape { DomainId, EncodingId }`,
  `LossProfile { lossless / intra-lossy / projection / degenerate }`,
  `PropertyId`, related value types
- New header `src/shape/transformationedge.h` with
  `TransformationEdge { from, to, loss, dropped, stage }` and
  `Pipeline` as a sequence of stages
- New `TransformationRegistry` static singleton with
  `registerEdge()`, `compile(Shape, Shape) → optional<Pipeline>`,
  `inspect(Shape, Shape) → LossProfile`
- New `IShape` (or similar) for property catalogues — each shape
  declares its list of `PropertyId`s, where each `PropertyId` is
  an enum-backed identifier with a name and a parser/serializer
- Initial population: only the canonical calendar shape registered
  (`(calendar, ical)` with ICalendar property catalogue derived from
  `KCalendarCore::Incidence`'s property set; identity edge only)
- New tests:
  - `tst_shape_value_type` — Shape comparison, hashing
  - `tst_transformation_registry` — register, compile, inspect
  - `tst_property_catalogue_calendar` — calendar shape catalogue

**No retirement.** No deprecation. No behavioural change. The
registry exists; nothing consumes it yet. Existing engine continues
to work via `IDomainAdapter`.

**Exit criteria:**

- libkalburator tests: 26 + 3 new = 29/29 pass
- PlanStan: unchanged (96/120 baseline)
- WildPalms: unchanged (73/73 baseline)
- `verify-all.sh` green

**Gotchas:**

- Property catalogue scope: hand-written for v1, not auto-derived
  from KCalendarCore (introspection isn't there). Document this
  decision; auto-derivation is a future enhancement
- Static-init order: TransformationRegistry must initialize before
  any backend reads it. Use `Q_GLOBAL_STATIC` or accept that domain
  plugins register at construction time

### G.2 — Calendar plugin (~1.5 weeks)

**Components landed:** part of [4] shape-typed differs (calendar
only); part of [13] domain plugins (calendar only).

**Scope:**

- New `src/calendar/calendarplugin.{h,cpp}` —
  `KalburatorDomainCalendar` registers calendar edges:
  - `(calendar, ical) ↔ (calendar, ical)` identity
  - the existing `PropertyTranscoder`-based transformations
    re-expressed as edges (one per capability profile)
- New `src/shape/icalendarrecorddiffer.{h,cpp}` — the existing
  `IncidenceDiff` logic, refactored to implement `IRecordDiffer`
  for `(calendar, ical)`
- Existing `CalendarDomainAdapter` becomes a thin wrapper that
  routes through the registry; bug-for-bug equivalent for now
- Existing tests in `tests/calendar/` continue to pass against the
  new wiring (the contract is preserved)
- New tests:
  - `tst_calendar_plugin_edges` — edges registered correctly,
    pipelines compile in both directions
  - `tst_icalendar_record_differ` — moved from existing engine tests

**No retirement** of `IDomainAdapter` yet — only wiring goes through
the registry. Ensures bisectable behaviour.

**Exit criteria:**

- All 23 pre-existing `tests/calendar/` tests pass against the
  registry-mediated path
- `IncidenceDiff` continues to be the diff for calendar; just
  invoked through the registry
- `verify-all.sh` green

**Gotchas:**

- `PropertyTranscoder` and `RruleTranscoder` need to be re-expressed
  as edges. The current "transcoder per capability profile" model
  becomes "edge per (canonical, native-with-profile)" pairs. This
  may surface that some profiles don't have a clean edge; document
  and elide if so
- The calendar plugin is the proof-of-concept that the registry
  shape works for a real domain. If anything breaks here, fix the
  registry, not the domain plugin

### G.3 — Backend interface migration (~2 weeks)

**Components landed:** [5] backend interface migration;
foundation for [10].

**Scope:**

- Add `SyncBackend::nativeShapes() const` returning
  `QList<Shape>`, default returns `{ Shape::Unknown }` (a sentinel
  meaning "I haven't been migrated yet")
- Add `SyncBackend::resourceId() const` returning a per-instance
  unique value by default
- Override `nativeShapes()` and `resourceId()` on every concrete
  `SyncBackend`:
  - `LocalBackend`, `RemoteBackend`, `OrgBackend`, `AkonadiBackend`,
    `DecSyncBackend`, `HolidaySubscriptionBackend`,
    `SubscriptionBackend`, `MockBackend` → all
    `{ (calendar, ical) }`, default resourceId
  - `PalmCalendarBackend` (WildPalms) → `{ (calendar, palm-datebook) }`,
    `resourceId()` returns Palm device serial
- Mark `dataDomain()` `[[deprecated]]`
- Migrate the 13 callsites of `dataDomain()` to the new API
- Delete `dataDomain()` and `DataDomain` enum

**Notable callsite migrations:**

- `libkalburator/src/types/iincidencesource.h` — was using
  `DataDomain` for type tagging; switch to a different mechanism or
  drop if redundant
- `libkalburator/src/calendar/syncbackend.h` — base class
- `libkalburator/src/engine/syncengine.cpp` — engine routing
- `PlanStan/src/controllers/collectioncontroller.cpp` and
  `itemloadingcoordinator.cpp` — minor consumer updates
- `WildPalms/src/palm/calendar/palmcalendarbackend.{h,cpp}` —
  declaration update

**Exit criteria:**

- `dataDomain()` has zero callsites
- `nativeShapes()` is what callers use to introspect a backend's
  shape capability
- All tests still pass
- `verify-all.sh` green

**Gotchas:**

- `DataDomain { Calendar, Project }` was originally a type
  discriminator for a hypothetical project-task pipeline that never
  fully landed (per the original ROADMAP open questions). Migrating
  shouldn't lose anything functional; verify by checking that the
  `Project` value isn't actively consulted in production
- WildPalms's `palmcalendarbackend.cpp` declares `DataDomain::Calendar`;
  the change is a one-liner to return `{ (calendar, palm-datebook) }`

### G.4 — Mapping-keyed baselines (~1.5 weeks)

**Components landed:** [6] mapping shape + mapping-keyed baselines.

**Scope:**

- Schema migration in `BlobBaselineStore`:
  - Add `blob_baselines_v3` table keyed `(mapping_id, record_id)`
    with canonical-shape bytes
  - Migration path from `blob_baselines_triple` (the post-F1
    schema): for each row, look up which mapping uses
    `(backend_id, collection_id)`. If unambiguous, copy with
    `mapping_id` filled in. If ambiguous (multiple mappings
    share the same backend+collection), copy to all matching
    mappings (the worst case is one redundant baseline per
    mapping; the next sync will refresh)
- Update `BlobBaselineStore` API to be mapping-keyed
- Update all callers in the engine (pre-F1 the calendar baseline
  store was separate; F1 collapsed it; G.4 now extends the
  collapsed store)
- Tests:
  - `tst_blob_baseline_store_v3_migration` — the migration path
  - `tst_blob_baseline_store_v3_keyspace` — mapping-keyed reads/writes
  - All existing baseline-using tests continue to pass

**Schema migration discipline (per FINDINGS lesson):**

- Use `PRAGMA user_version` to gate the migration
- Inspect `sqlite_master.sql` if needed to disambiguate states
- Never `DROP TABLE IF EXISTS` against a name that exists in both
  before and after states
- Migration is idempotent (running twice is safe)

**Exit criteria:**

- Existing user databases migrate cleanly to v3 schema
- New tests pin the migration paths
- `verify-all.sh` green

**Gotchas:**

- The migration touches stored user data. Conservative behaviour:
  if migration fails, log and treat as a fresh sync (forfeits the
  baseline; next sync rebuilds it). Document the recovery story
- Same lesson as Phase F1 Task 11 (commit `5489a10`) about
  per-open migrators — verify behaviour on the *second* open
- `tst_blob_baseline_store_v3_migration` should explicitly run
  open-then-reopen-then-migrate sequences to catch the F1 footgun

### Tag checkpoint #1: `v0.15-phase-g-foundations`

After G.1+G.2+G.3+G.4. The new architecture exists; existing
behaviour preserved. This is a defensible "we could stop here and
ship" state, even though we're not stopping.

### G.5 — New domain plugins (~2 weeks)

**Components landed:** rest of [4] differs; rest of [13] domain
plugins (todo, contacts, memo).

**Scope:**

- `KalburatorDomainTodo` plugin — registers `(todo, ical-vtodo)`
  canonical, edges to `(todo, org)`, `(todo, todotxt)`,
  `(todo, palm-todo)`. `IRecordDiffer<todo, ical-vtodo>` is the
  existing iCal diff but applied to VTODO components
- `KalburatorDomainContacts` plugin — registers `(contacts, vcard)`
  canonical, edge to `(contacts, palm-address)`. Property
  catalogue derived from KContacts::Addressee.
  `IRecordDiffer<contacts, vcard>` is new code (vCard property-aware
  diff)
- `KalburatorDomainMemo` plugin — registers `(memo, plaintext)`
  canonical, edge to `(memo, palm-memo)`. Property catalogue is
  small (body, category). Differ is essentially text equality
- Property catalogues for each new shape
- Tests:
  - `tst_domain_todo_plugin`
  - `tst_domain_contacts_plugin`
  - `tst_domain_memo_plugin`
  - One end-to-end `tst_pipeline_compose` exercising todo's
    `(palm-todo) → (ical-vtodo) → (org)` two-step pipeline

**No new backends yet.** The plugins exist; their edges are
registered; nothing in the stock library produces or consumes the
non-canonical shapes for todo/contacts/memo because the backends for
those shapes don't exist yet.

**Exit criteria:**

- Three new plugins registered, edges inspectable
- New tests pass
- `verify-all.sh` green

**Gotchas:**

- `KContacts::Addressee` introspection: similar to KCalendarCore,
  hand-written catalogue
- The vCard differ is real new logic; no existing implementation to
  port. Likely use `KContacts::VCardConverter` for parse/serialize
  and a property-by-property diff
- Memo differ is trivial; verify via tests that category changes
  are diffed correctly

### G.6 — BlobDomainAdapter dispatch + MappingScheduler (~2 weeks)

**Components landed:** [7] BlobDomainAdapter dispatch; [11]
resource-aware MappingScheduler.

**Scope:**

- Register `BlobDomainAdapter` with the unified dispatch path; the
  worker's `processSync` learns to route blob-typed mappings
  through the blob path (today the worker hardcodes calendar)
- New `MappingScheduler` in `SyncEngine`:
  - Reads each mapping's `(source.resourceId(), target.resourceId())`
  - Groups by resource graph; capacity-1 per resource initially;
    sequential within a connected component; sequential across
    components for now (no cross-group concurrency yet)
- New `runSyncFuture(QList<MappingId>)` overload
- New `CancellationReason` enum
- New `SyncEngineFuture` wrapper around
  `QFuture<QList<SyncResult>>` exposing `cancelWithReason(...)`
- Mark `runBlobTwoWay` and `runBlobMirror` `[[deprecated]]`
- Tests:
  - `tst_engine_blob_through_unified_dispatch` — a blob mapping
    runs through `runSyncFuture`
  - `tst_mapping_scheduler` — resource graph, queueing,
    cancellation propagation
  - `tst_cancellation_reason` — ResourceLost short-circuits queued
    siblings

**Critical:** F1 facade is *deprecated* but still callable. WildPalms
continues using it. PlanStan tests for blob-via-facade still work.

**Exit criteria:**

- Blob mappings runnable through `runSyncFuture`
- F1 facade marked deprecated, compiler warnings in callers
- New tests pass
- `verify-all.sh` green

**Gotchas:**

- The `MappingScheduler` is new code in `SyncEngine`. It replaces
  ad-hoc per-mapping dispatch with a queue. Make sure the QFuture
  semantics from F2 still hold (single completion, single
  cancellation, results survived)
- The blob worker path may have tangles with the calendar worker
  path — F1 collapsed them at the file boundary, not the QObject
  boundary (per FINDINGS). Verify the unified dispatch keeps the
  threading model

### G.7 — WildPalms SyncRunner dissolution (~3 weeks)

**Components landed:** [10] WildPalms transformation; gates [8].

**Scope:**

- New `WildPalms/src/runtime/hotsynccoordinator.{h,cpp}` —
  device-connected → `runSyncFuture(palmMappingIds)`
- New `WildPalms/src/palm/contacts/palmcontactsbackend.{h,cpp}` —
  exposes Palm AddressDB as a contacts backend with shape
  `(contacts, palm-address)`, `resourceId()` returning device serial
- New `WildPalms/src/palm/memo/palmmemobackend.{h,cpp}` — similar
  for MemoDB
- New `WildPalms/src/palm/todo/palmtodobackend.{h,cpp}` — similar
  for ToDoDB
- Existing `WildPalms/src/palm/calendar/palmcalendarbackend` —
  already shape-aware after G.3; resourceId added
- Existing Palm Plucker plugin remains as-is (sui generis shape;
  only valid sink is Shape::Any, which lands in G.8)
- Per-profile mapping registry in WildPalms config — UI for
  configuring which Palm DBs sync to which Akonadi/remote
  collections
- `WildPalms/src/runtime/syncrunner_wp.{h,cpp}` deleted
- WildPalms's existing `tst_*_v2.cpp` tests continue to pass
  (they don't use F1 facade directly; they used the V2 plugin
  ABI which `HotSyncCoordinator` accommodates)
- New WildPalms tests:
  - `tst_hotsync_coordinator` — device-connected wiring
  - `tst_palm_contacts_backend`, `tst_palm_memo_backend`,
    `tst_palm_todo_backend`
  - These are WildPalms tests because the backends are
    consumer-provided per the test ownership rule

**Exit criteria:**

- WildPalms HotSync flow works end-to-end with the new architecture
- F1 facade has zero callers
- `verify-all.sh` green; WildPalms baseline holds (73/73 modulo
  flakes)

**Gotchas:**

- The 6 WildPalms plugin V2 backends (`PalmBackend`, `WebcalBlob`,
  `PluckerBlob`, etc.) currently use `IBlobBackend` directly. After
  G.6 the engine accepts them through the unified dispatch; do they
  still pass through unchanged, or do they need shape declarations
  too? Likely: each gets a shape (e.g., `(plucker, palm-plucker)`,
  `(webcal, ical)`, etc.) and the existing blob baseline behaviour
  survives via `BlobDomainAdapter`'s registered edges
- Shape declarations for the V2 plugin backends are a concrete
  exercise; they may surface that the plugin V2 ABI needs a
  `nativeShapes()` method too. Probably yes; add it
- This phase is the largest; can be sliced internally if needed

### G.8 — F1 facade deletion + universal sinks (~1.5 weeks)

**Components landed:** [8] F1 facade retirement; part of [14]
universal sinks.

**Scope:**

- Delete `runBlobTwoWay`, `runBlobMirror`, `BlobSyncStats`,
  `BlobSyncResult` (or relocate the result types if any caller
  still uses them — verify clean delete)
- New `src/sinks/rawfilesbackend.{h,cpp}` — `Shape::Any` sink with
  filename-encoded shape metadata
- New `src/sinks/genericsqlitebackend.{h,cpp}` — `Shape::Any` sink
  with table-per-shape schema generation from property catalogues
- Tests:
  - `tst_rawfiles_backend` — write+read round-trip preserves shape
  - `tst_generic_sqlite_backend` — schema generation, multi-shape
    coexistence in one DB
  - `tst_universal_sink_as_source` — RawFilesBackend used as source
    after a backup mirror

**Exit criteria:**

- F1 facade gone
- Universal sinks tested
- `verify-all.sh` green

**Gotchas:**

- F1 facade deletion is the final retirement of the F1 transitional
  layer. Any leftover comments referring to "facade" or "F1
  transitional" need cleanup
- `BlobSyncResult` may be embedded in `SyncEngineFuture`'s result
  type — relocate or refactor the result schema

### Tag checkpoint #2: `v0.15.5-phase-g-engine-unified`

After G.5+G.6+G.7+G.8. The engine is fully unified; F1 facade gone;
WildPalms operates as a peer multi-PIM consumer. This is a major
shippable milestone.

### G.9 — ISyncHost narrowing + sync I/O retirement (~3 weeks)

**Components landed:** [12] ISyncHost narrowing; [9] synchronous
I/O retirement; bulk of test moves (the question 8 work).

**Scope split into G.9.a and G.9.b for clarity, both landing in
this phase:**

**G.9.a — ISyncHost narrowing:**

- New 7-method `ISyncHost`:
  - `syncStarted(MappingId, LossProfile)`
  - `syncFinished(MappingId, SyncResult)`
  - `recordChanged(MappingId, RecordId, ChangeKind)`
  - `resolveConflict(MappingId, RecordId, src, tgt, baseline)
    → ConflictResolution`
  - `progressChanged(MappingId, current, total, msg)`
  - `phaseChanged(MappingId, Phase)`
  - `errorOccurred(MappingId, msg)`
- Mark old methods `[[deprecated]]` initially (deprecation overlap)
- PlanStan implements new interface in `PlanStanSyncHost`;
  PlanStan's existing per-incidence handling moves into a new layer
  that consumes `recordChanged` and parses the bytes itself
- WildPalms implements new interface in `WildPalmsSyncHost`
- All engine calls into `ISyncHost` migrate to new methods
- Old methods deleted
- Tests pinned

**G.9.b — Synchronous I/O retirement (the test moves):**

- The ~8000 lines of PlanStan backend tests get sorted into 4
  buckets per the test fate map below
- Mass migration in carefully-sized commits: ideally one
  test-file-per-commit (~10-20 commits)
- Once all 180-ish call sites are migrated, delete `loadItems`,
  `storeItems`, `updateItem`, `writeFinished` from `SyncBackend`
- Update the one PlanStan production caller
  (`convertCalendarToBackend`) to operation-handle pattern
- Update `palmcalendarbackend.{h,cpp}` to drop the legacy stub
  overrides (they were only there because the base declared them)

**Exit criteria:**

- `ISyncHost` is 7 generic methods
- Synchronous I/O API gone
- All tests pass at new locations
- `verify-all.sh` green

**Gotchas:**

- This is the largest single phase by lines-of-code touched
- Test moves can be parallelized across multiple subagent sessions
  (per `OPERATIONS.md`'s subagent guidance — mechanical migrations
  are exactly the right use case)
- The synchronous I/O deletion is *the* deletion gate; once it
  lands, downstream cannot revert
- Each test file's destination depends on the test fate map (see
  next section). Keep the test fate spreadsheet next to this
  walkthrough during execution

### G.10 — Loss profile UX + new stock backends (~2 weeks)

**Components landed:** rest of [14] new stock backends + loss
profile reporting.

**Scope:**

- `LossProfile` plumbed through `ISyncHost::syncStarted` and the
  per-mapping configuration
- New Qt widget `LossProfileDetailView` in a new sibling library
  `libkalburator-qtwidgets/` (or in `libkalburator` if we don't
  want to fragment the build; decision deferred)
- `WhenLossWouldOccur::Abort/Warn/Proceed` field on `SyncMapping`
- New stock backends (compile-time gated by KF6 module presence):
  - `AkonadiContactsBackend` — `(contacts, vcard)` native
  - `AkonadiNotesBackend` — `(memo, plaintext)` native
  - `AkonadiTasksBackend` — `(todo, ical-vtodo)` native
  - `CardDAVRemoteBackend` — `(contacts, vcard)` native
- PlanStan and WildPalms gain UI for selecting these backends in
  mapping configuration
- Tests for each new stock backend (in libkalburator, per the test
  ownership rule)

**Exit criteria:**

- All new stock backends usable in mapping configurations
- Loss profile UX renders meaningfully
- `verify-all.sh` green

**Gotchas:**

- The `libkalburator-qtwidgets` sibling library decision is real:
  `libkalburator` core is currently widget-free; introducing Qt
  widgets there couples the library to QtWidgets. A sibling library
  keeps the core independent. Decide in the design doc.
- Some new stock backends require system dependencies
  (KContacts for vCard parsing, libkdav2 for CardDAV) — gate
  builds behind `KALBURATOR_HAVE_*` flags per existing convention

### Tag checkpoint #3: `v0.16-phase-g-shape-pipeline`

After G.9+G.10. The full shape-pipeline architecture is shipped.
This is the major phase tag, marking Phase G complete.

## Test fate map (the question 8 work)

The ~8000 lines of PlanStan backend tests sort into four fate
buckets:

### Bucket A — Move to libkalburator, keep largely as-is

These tests genuinely test libkalburator's stock backends and
should live in libkalburator. They migrate to `libkalburator/tests/`
in domain-organized directories (`tests/calendar/backends/`,
`tests/todo/backends/`, etc., mirroring the eventual layout).

Ownership rule: stock backend → libkalburator owns the tests.

| File | Destination | Notes |
|---|---|---|
| `tst_orgbackend.cpp` (1775) | `libkalburator/tests/calendar/backends/tst_orgbackend.cpp` | Calendar tests; some lines retire if testing deprecated sync I/O directly. ~70-80% of lines survive |
| `tst_orgbackend_external.cpp` (1806) | `libkalburator/tests/calendar/backends/` | Same |
| `tst_decsyncbackend.cpp` (1429) | `libkalburator/tests/calendar/backends/` | Same |
| `tst_localbackend.cpp` (537) | `libkalburator/tests/calendar/backends/` | Same |
| `tst_remotebackend.cpp` (1021) | `libkalburator/tests/calendar/backends/` | Same |

**Estimated surviving line count:** ~5000-5500 lines move,
~1500-2000 lines retire as redundant.

### Bucket B — Rewrite during move

These tests exercise contracts that the architecture changes.
They get rewritten to test the new operation API + shape-aware
contracts.

| File | Destination | Notes |
|---|---|---|
| `tst_backend_signals.cpp` (1007) | `libkalburator/tests/calendar/backends/tst_backend_signal_contract.cpp` | Tests `SyncBackend` signal contract; rewrites to test `SyncOperation::finished` and the new `ISyncHost::recordChanged` semantics |
| `syncbackend_test_framework.h` (helper) | `libkalburator/tests/shared/syncbackend_test_framework.h` | Helper for backend tests; updated for operation-handle pattern |

**Estimated rewrite cost:** ~1500 lines; ~3-5 days of focused work.

### Bucket C — Stay in PlanStan

These tests genuinely test PlanStan's app behavior, even if they
touch sync. Per the test ownership rule, they belong in PlanStan.

| File | Notes |
|---|---|
| `tests/sync-workflow/*` (4 files, EXCLUDE_FROM_ALL) | Already PlanStan-flavored end-to-end UI flows; they exercise libkalburator through PlanStan's UI layer |
| `tests/integration/tst_calendarcrud.cpp` (small) | PlanStan-app integration test |
| `tests/localbackend/tst_localbackend.cpp` (small) | Possibly redundant with libkalburator's; review and delete if so |

**Estimated retained-in-PlanStan:** ~500-1000 lines.

### Bucket D — Retire as redundant

Some test logic is duplicated between the engine-level tests
(`tests/calendar/`) and the backend-level tests; after migration,
some of the backend-level tests are redundant with the new
end-to-end pipeline tests in libkalburator.

| Examples | Notes |
|---|---|
| Tests that just verify "backend's storeItems writes to backend's storeFile and emits writeFinished" | Now covered by `tst_*_backend` operation contract tests |
| Tests that verify error propagation through deprecated sync I/O | Now covered by operation-handle tests |
| Tests that exercise behaviour gated by deprecated APIs | Retire |

**Estimated retirement:** ~1000-1500 lines.

### Test fate analysis summary

| Bucket | Action | Approx LoC | Effort |
|---|---|---|---|
| A | Move + minor edits | ~5000-5500 | ~1 week parallel |
| B | Rewrite during move | ~1500 | ~3-5 days |
| C | Stay in PlanStan | ~500-1000 | ~1 day audit |
| D | Retire | ~1000-1500 | ~1 day delete |

**Total:** ~3 weeks of focused effort, parallelizable into ~2 weeks
real-time with subagent assistance per `OPERATIONS.md`. Folded
into G.9.b above.

### Test organization in libkalburator post-migration

Proposed directory structure:

```
libkalburator/tests/
  shared/                     — shared helpers
    syncbackend_test_framework.h
    stub_synchost.h
    mock_backend.h
  calendar/
    backends/                 — calendar-domain backend tests
      tst_orgbackend.cpp
      tst_decsyncbackend.cpp
      tst_localbackend.cpp
      tst_remotebackend.cpp
      tst_orgbackend_external.cpp
    plugin/
      tst_calendar_plugin_edges.cpp
    differs/
      tst_icalendar_record_differ.cpp
    (existing engine integration tests)
      tst_calendar_sync_full.cpp        — already here pre-G
      tst_calendar_conflict.cpp         — already here pre-G
      ...
  contacts/
    backends/
      tst_akonadi_contacts_backend.cpp  — added in G.10
      tst_carddav_backend.cpp           — added in G.10
    plugin/
      tst_contacts_plugin_edges.cpp     — added in G.5
    differs/
      tst_vcard_record_differ.cpp       — added in G.5
  memo/
    plugin/
      tst_memo_plugin_edges.cpp         — added in G.5
  todo/
    plugin/
      tst_todo_plugin_edges.cpp         — added in G.5
    differs/
      tst_todo_record_differ.cpp
  shape/                      — shape primitives
    tst_shape_value_type.cpp
    tst_property_catalogue.cpp
  registry/
    tst_transformation_registry.cpp
    tst_pipeline_compose.cpp
  engine/                     — engine integration
    tst_engine_blob_through_unified_dispatch.cpp
    tst_mapping_scheduler.cpp
    tst_cancellation_reason.cpp
    (existing engine tests)
      tst_engine_cancellation.cpp       — already here pre-G
      tst_engine_unified_boundary.cpp   — already here pre-G
  journal/
    tst_blob_baseline_store.cpp         — already here pre-G
    tst_blob_baseline_store_v3_migration.cpp  — added in G.4
    tst_blob_baseline_store_v3_keyspace.cpp   — added in G.4
  blob/
    tst_mockblobbackend.cpp             — already here pre-G
    tst_localblobbackend.cpp            — already here pre-G
  sinks/
    tst_rawfiles_backend.cpp            — added in G.8
    tst_generic_sqlite_backend.cpp      — added in G.8
    tst_universal_sink_as_source.cpp    — added in G.8
```

Layout principles:

- Domain-flavored top-level directories (`calendar/`, `contacts/`,
  `memo/`, `todo/`)
- Cross-cutting concerns in their own dirs (`shape/`, `registry/`,
  `engine/`, `journal/`, `blob/`, `sinks/`, `shared/`)
- Each domain has subdirs `backends/`, `plugin/`, `differs/` as
  applicable
- The pre-existing `tests/calendar/`, `tests/journal/`, `tests/blob/`
  layout is preserved and extended

## Tag strategy

Three checkpoint tags, in order:

1. **`v0.15-phase-g-foundations`** after G.4 — architectural
   primitives in place; existing behaviour preserved. Defensible
   stopping point if scope shifts.
2. **`v0.15.5-phase-g-engine-unified`** after G.8 — engine fully
   unified, F1 facade gone, WildPalms transformed to peer
   consumer. Major shippable milestone; PlanStan and WildPalms
   both pass tests against unified engine.
3. **`v0.16-phase-g-shape-pipeline`** after G.10 — full
   shape-pipeline architecture shipped. This is Phase G's
   completion tag.

Three tags is more than the original Phase G's "one tag" but
each represents a real shippable state. The middle tag in
particular is load-bearing: if implementation ambition shifts,
G.9 and G.10 can defer to a Phase H or be subsumed into ongoing
maintenance, and the project ships meaningfully at
`v0.15.5-phase-g-engine-unified`.

## Time estimate

| Sub-phase | Estimate | Parallelizable? |
|---|---|---|
| G.1 | 2 weeks | No (foundation) |
| G.2 | 1.5 weeks | No (depends on G.1) |
| G.3 | 2 weeks | Partially (callsite migration is parallel) |
| G.4 | 1.5 weeks | No (depends on G.3) |
| G.5 | 2 weeks | Partially (one plugin per subagent if dispatched) |
| G.6 | 2 weeks | No |
| G.7 | 3 weeks | Partially (multiple Palm DBs in parallel) |
| G.8 | 1.5 weeks | Yes (universal sinks ⊥ facade deletion) |
| G.9 | 3 weeks | Yes (test moves heavily parallelizable) |
| G.10 | 2 weeks | Yes (each new backend ⊥ others) |
| **Total** | **20.5 weeks** | with parallelism ≈ **15-17 weeks** |

This is meaningfully longer than my earlier 10-14 estimate. The
walkthrough surfaced real work (G.4's schema migration, G.7's
WildPalms transformation, G.9.b's test moves). 15-17 weeks of
calendar time with disciplined execution and subagent help on
mechanical migrations (test moves, callsite updates).

If the schedule needs to compress, the natural cut is **defer G.10
to a Phase H**. Tags 1 and 2 still ship; new domains and universal
sinks land later. Project completes Phase G at
`v0.15.5-phase-g-engine-unified` after ~13-15 weeks. Future Phase H
adds the new domains as a smaller, focused effort.

## Gotchas surfaced by the walkthrough

Inline gotchas (per phase):

- **G.1:** Property catalogue auto-derivation deferred; static-init
  order
- **G.4:** Schema migration on stored user data; idempotent
  migration; second-open verification
- **G.6:** MappingScheduler's threading interaction with F2's
  worker model
- **G.7:** WildPalms V2 plugin ABI may need `nativeShapes()`
  addition
- **G.8:** F1 facade's result types may be embedded in callers'
  signatures
- **G.9:** Largest phase; mechanical work; subagent-heavy
- **G.10:** Sibling library decision for QtWidgets

Cross-cutting gotchas:

- **`verify-all.sh` baselines need refreshing at each tag.** The
  PlanStan baseline shifts as test files move out; the libkalburator
  baseline grows. Refresh in the same commit as the tag.
- **`OPERATIONS.md`'s 5-step deprecation pattern** is the per-API
  discipline; multiplied across 14 components and 10 sub-phases
  this is ~50-70 deprecation cycles. The pattern is well-trodden
  but the volume is real.
- **The MEMORY.md / CLAUDE.md / phase-status doc cycle** continues
  per phase. Each sub-phase G.1-G.10 gets its own status update
  in `04r-phase-g-status.md` (to be created at design-doc time).

## Architectural break-glass triggers — none hit in this walk

The migration is dense but tractable. No phase requires a structural
revisit of the architecture; every sub-phase is a routine application
of the deprecation-with-overlap pattern across a known scope.

The riskiest single phase is **G.4** (schema migration on stored
user data); the largest phase by LoC is **G.9** (test moves +
ISyncHost narrowing). Both have well-understood mitigation paths.

## Recommendation

**Graduate to design-doc authoring.** The architecture is validated
(Alice + Bob walks); the implementation slicing is grounded
(this walk). The remaining concretion items — exact property
catalogues, exact `LossProfile` shape, exact `ISyncHost` post-bend
signature, the `libkalburator-qtwidgets` sibling-library decision —
are design-doc work, not walkthrough work.

The next move: write `04r-phase-g-design.md` and
`04r-phase-g-plan.md` as a pair. The design doc consolidates the 22
inline architectural decisions and answers the remaining concretion
questions; the plan doc takes this walkthrough's slicing and
expands it into a task-level checklist with explicit deps and
success criteria per task.

After both documents land and are reviewed, code commits begin on
G.1.

## Cross-references

- `04r-phase-g-shape-pipeline-ideation.md` — architectural
  exploration
- `04r-phase-g-walkthrough.md` — Alice's todos walk (architecture
  validation, intra-domain + universal sinks)
- `04r-phase-g-walkthrough-wildpalms.md` — Bob's HotSync walk
  (architecture validation, single-occupancy + multi-PIM)
- `04q-phase-f2-threading-outcome.md` — F2 closure that defined
  current state
- `~/dev/refactor-engine-merger/CURRENT-STATUS.md` — Phase G
  design exploration in flight
- `~/dev/refactor-engine-merger/ROADMAP.md` — original Phase G
  framing; this walkthrough's slicing supersedes the
  single-tag plan there
- `~/dev/refactor-engine-merger/OPERATIONS.md` —
  deprecation-with-overlap pattern referenced throughout
- `~/dev/refactor-engine-merger/FINDINGS.md` — schema migration
  lesson (cf. G.4) and other cross-cutting lessons cited inline
