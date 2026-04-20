# Phase 0 follow-ups — discovered during Phase 1 execution

Small corrections and deferrals identified while running the
extraction against the real codebase.

## `ICalendarCollection` surface is 6 methods, not 12

The Phase 0 draft in `04-merged-interface-sketch.md` listed 12
methods. Auditing actual `libs/sync/` usage showed only 6 are
called:

- `id()`, `calendar(id)`, `calendars()`, `addCalendar(cal)`
- `setCalendarColor(id, color)`, `setCalendarVisible(id, visible)`

The other 6 drafted methods (`removeCalendar`, `calendarBackend`,
`setCalendarBackend`, `calendarsForBackend`, `calendarIdForIncidence`,
`workingCalendar`) are used elsewhere in PlanStan but never from
`libs/sync/`. They stay Collection-specific.

This makes the reuse surface for Wild Palms' side narrower — good.

## `ISyncConfigStore` surface is 8 methods, name `save()` chosen

Audited actual `libs/sync/` calls against `KalbConfigManager`.
8 methods used:

- `addLogicalCalendar`, `updateLogicalCalendar`, `removeLogicalCalendar`,
  `logicalCalendar(id)`
- `backendConfig(id)`
- `hasSyncMappings()`, `syncMappings()`
- `save()` (thin wrapper over `saveCollectionConfig()` — renamed to
  decouple from PlanStan's legacy method name)

## `ISyncHost` narrowing landed slightly differently than sketched

The sketch proposed dropping `IIncidenceSource*` and `IIncidenceRegistry*`
from `ISyncHost`. In practice these are already abstract interfaces
(from `libs/core`) that a reuse host can trivially implement over
whatever incidence model it has. Kept them on `ISyncHost` unchanged.

The circular `SyncCoordinator*` getter was indeed dropped.

## Phase 1.3 (relocate types out of `libs/core`) — deferred into Phase 3

The Phase 0 plan said to relocate `BackendConfiguration`,
`LogicalCalendar`, `SyncTypes`, `CalendarType`, `DataDomain` from
`libs/core` to `libs/sync` (and eventually `libkalburator`).

**Deferred.** Reasoning:

1. 65 source files consume these headers. Relocating in-tree (Phase 1)
   means 65 files churn for the intra-repo move, and then churn a
   second time in Phase 3 when the files physically move to
   `~/dev/libkalburator/`. Bundling both relocations into Phase 3 is
   a single consistent churn.
2. Phase 1's end state is functional without the relocation: the
   library interfaces (`ICalendarCollection`, `ISyncHost`,
   `ISyncConfigStore`) are in place and narrow enough to support the
   smoke test in Phase 2.
3. The Phase 1.4 standalone-build check (`PROJECT_IS_TOP_LEVEL` for
   `libs/sync/`) can tolerate the in-tree header locations because
   `libs/core` is a declared PUBLIC dep of `libs/sync` already.

Phase 3's scope is therefore: (a) copy source files to
`~/dev/libkalburator/src/`, (b) move the five type headers from
`libs/core/include/` to `libkalburator/src/types/`, (c) update 65
consumer include paths in PlanStan to the new library's public
headers. All in one pass.

## Phase 1.4 result

Running `cmake -S libs/sync -B /tmp/sync-standalone` (the Phase 1.4
standalone-configure test) **succeeds** — the `PROJECT_IS_TOP_LEVEL`
gating is correct.

Running the subsequent `cmake --build /tmp/sync-standalone` **fails**
with missing-header errors for `calendartype.h`,
`backendconfiguration.h`, `logicalcalendar.h`, `synctypes.h` — all of
which currently live in `libs/core/`. Exactly as anticipated by the
Phase 1.3 deferral: true standalone build unblocks in Phase 3 when
those headers move to `libkalburator/src/types/` alongside the rest
of the library.

Phase 1.4 therefore reports:
- ✅ configure step works standalone
- ⚠ build step gated on Phase 3 type relocation (expected)

## Phase 2 consumer smoke-test scope confirmed

The smoke test writes a minimal stub host implementing
`ICalendarCollection` + `ISyncHost` + `ISyncConfigStore` using
`MemoryCalendar` as the calendar store, drives a `LocalBackend` and
optionally a `RemoteBackend` against the local Radicale server, and
asserts round-trip X-property preservation.

Whether to include `RemoteBackend` (needs live Radicale) as optional
is TBD during Phase 2 execution — may gate that behind
`-DPLANSTAN_ENABLE_CALDAV_TESTS=ON` as other tests do.

---

## Phase B pre-work audits (2026-04-20)

These four questions were called out as gating the Phase B
(`qsynccore/` merge upstream) work in Wild Palms'
`docs/plans/2026-04-20-libkalburator-integration-design.md`. All four
were audited 2026-04-20 by reading both sides' code. Decisions below
are **provisional maintainer-set defaults**; revisit any of them if
Phase B execution turns up contradicting evidence. All four audits
are `[WP-driven]` in provenance (the WP integration plan forced them).

### Audit 1 — BaselineStore schema reconciliation  **[WP-driven]**

**Sources audited:**

- WP: `~/dev/WildPalms/src/sync/qsynccore/baselinestore.{h,cpp}`
- libkalburator (ex-PlanStan): `~/dev/libkalburator/src/sync/syncstore.{h,cpp}`
  — "Baseline Storage" section (lines ~163–219 in `syncstore.h`) and
  "Property Baselines" section.

**The two designs store different things.**

| Dimension | WP `BaselineStore` | PS `SyncStore` baselines |
|---|---|---|
| What's stored per record | Content **hash** only (caller supplies algorithm) | Full **iCal text** for the incidence (plus property-JSON for calendar metadata) |
| Storage backend | In-memory `QMap<RecordId, QString>`; JSON blob persistence via `toJson`/`fromJson` | SQLite, per-mapping transactions |
| Keying | Flat `RecordId` (single sync pair) | Per-`mappingId` / per-`uid` — one baseline per (mapping, uid) |
| Purpose | "Did this record change since last sync?" (cheap change-detection) | "What did this incidence look like at last sync?" (3-way merge: baseline vs current-source vs current-target, field-by-field) |
| Granularity | Record-level (whole-record hash) | Record-level + calendar-property-level (two tables) |
| Ancestor for 3-way merge? | **No** — hash doesn't let you recover content | **Yes** — you can diff baseline against current to locate conflicting fields |

**Decision: keep both, at different layers.**

- **Lower blob layer** keeps WP's hash-only `BaselineStore` shape
  (renamed `BlobBaselineStore`). Cheap, format-agnostic, all a generic
  blob sync needs. Palm contacts/memos and other non-calendar data
  use this.
- **Upper calendar layer** keeps PlanStan's iCal-text baseline — this
  is the 3-way-merge storage needed for `IncidenceDiff` /
  `PropertyTranscoder` to work. Stays in the renamed
  `CalendarBaselineStore` (carved out of today's `SyncStore` during
  Phase C).
- **SyncStore itself dissolves** at Phase C. Its six concerns split
  as follows:
  1. Identity mapping → `IDMappingStore` (lower layer, merged with
     WP's store — see Audit 2).
  2. Version hashes → stays upper-layer (`CalendarVersionStore` or
     folded into `CalendarBaselineStore` — defer to Phase C detail).
  3. Baseline storage (iCal text) → `CalendarBaselineStore` (upper).
  4. Property baselines (calendar metadata) → `CalendarBaselineStore`.
  5. CTag tracking → CalDAV-backend-internal cache, not a top-level
     store. Move into `CalDAVBackend`'s private state.
  6. Conflict tracking → merges with WP's `ConflictStore` (upper
     layer calls into lower-layer conflict store; see Audit 3).

**Implication for Phase B:** Phase B lifts WP's `BaselineStore`
verbatim as `BlobBaselineStore` in the lower layer. PlanStan's
existing baseline logic stays in `SyncStore` for now (untouched in
Phase B). The carve-up of `SyncStore` is explicitly Phase C work —
don't attempt during Phase B, as the layered-directory split is the
natural moment to do it.

**Migration concern (post-v1.0):** existing PlanStan users have
SQLite databases with iCal-text baselines at `.planstan-sync.db`.
The Phase C reshuffle must preserve on-disk schema compatibility or
ship a migration. Flagged for Phase C design.

### Audit 2 — IDMappingStore coverage  **[WP-driven]**

**Sources audited:**

- WP: `~/dev/WildPalms/src/sync/qsynccore/idmappingstore.{h,cpp}` +
  `synccommon.h` (`IdMapping` struct).
- PS: `~/dev/libkalburator/src/sync/syncstore.h` "Identity Mapping"
  section.

**Feature comparison:**

| Dimension | WP `IdMappingStore` | PS `SyncStore` identity mapping |
|---|---|---|
| Keying | Flat `RecordId ↔ RecordId` for a single sync pair | Per-`backendId`-qualified: `(backendId, sourceUid) → targetId`. Optional `calendarId` context. |
| Multi-backend expressiveness | No — one source/target pair per mapping instance | Yes — same source UID can map to different target IDs across multiple backends |
| Reverse lookup | Yes (dual map) | Yes (separate query method) |
| Per-record metadata | `sourceCategory`, `targetCategories`, `lastSynced`, `archived` | None — it's strictly the (sourceUid, targetId) pair plus calendarId |
| Persistence | JSON array, written wholesale | SQLite, per-row transactions |
| Bulk operations | `mapIds`/`removeBySource`/`removeByTarget` | Same, plus `allIdMappings(backendId)` batch accessor |

**Shape decision: PlanStan's shape wins as the primary store.**

PS's per-backend-qualified key is strictly more expressive. WP's
flat pair is a degenerate case where `backendId` is implicit (there's
only one remote).

**But WP has real functionality PS lacks:**

- `sourceCategory` + `targetCategories` — Palm record category tracking
- `archived` — soft-delete semantics for mappings where the target was
  archived but the source still exists

**Decision: merge, preserving both sides' concerns.**

Library-level `IDMappingStore` shape:

```cpp
struct IDMapping {
    QString backendId;              // [PS] required
    QString sourceUid;              // [REC]
    QString targetId;               // [REC]
    QString calendarId;             // [PS] optional
    QDateTime lastSynced;           // [WP]
    QString sourceCategory;         // [WP] optional, for Palm/category backends
    QStringList targetCategories;   // [WP] optional
    bool archived = false;          // [WP]
};

class IDMappingStore {
    // PS surface as primary API
    QString targetIdForSourceUid(backendId, sourceUid) const;
    QString sourceUidForTargetId(backendId, targetId) const;
    void setIdMapping(backendId, sourceUid, targetId,
                      calendarId = QString());
    // ... (all 5 PS methods)

    // WP-contributed category + archive methods, optional to call
    void updateCategories(backendId, sourceUid,
                          sourceCategory, targetCategories);
    void setArchived(backendId, sourceUid, bool archived);
    IDMapping getMapping(backendId, sourceUid) const; // returns full struct
};
```

**Backend:** SQLite (following PS's model). JSON persistence (WP's
model) is cheap but can't survive concurrent access across multiple
sync sessions and doesn't scale to per-mapping partial updates.

**Name:** `IDMappingStore` (capital D — PS's convention and the
established design-doc spelling). WP's `IdMappingStore` (lowercase d)
renames on merge. Trivial churn, noted so the rename isn't rediscovered
surprise mid-Phase-B.

**Layer:** **lower**. Mapping is backend-agnostic — blob-layer
backends (Palm contacts/memos) need this just as much as calendar
backends. Put in `src/blob/` (or equivalent) at Phase C.

**Implication for Phase B:** Phase B folds WP's `IdMappingStore`
into the library at the chosen `IDMappingStore` shape (strict
superset of both inputs). PlanStan's existing identity-mapping calls
into `SyncStore` become calls into the new `IDMappingStore` — this
is a simultaneous library-and-PlanStan commit. WP doesn't consume
yet (that's Phase E).

### Audit 3 — Conflict handler dispatch when a conduit is active  **[WP-driven]**

**Sources audited:**

- WP: `~/dev/WildPalms/src/sync/conduit.h` — `SyncContext`,
  `SyncConduitBase::resolveConflictWithHandler()`,
  `applyConflictDecision()`, `applyResolvedConflicts()`.
- Upstream design: `04-merged-interface-sketch.md` §"Conflict
  framework" and `00-open-questions.md` §4 resolution.

**Key finding from `conduit.h`:**

`SyncContext` carries `ConflictHandler*`, `ConflictStore*`, and
`ConflictPolicy` fields (lines 46–48). The conduit itself calls
`resolveConflictWithHandler()` during its own `syncRecord()` path
(lines 476–480). The conduit **owns the dispatch moment** — it
decides when two records are in conflict and calls the handler.

This is NOT the same as the library-side dispatch model from
`00-open-questions.md` §4, which was specified as
`coordinator->registerConflictHandler(backendId, handler)` — i.e.
the **coordinator** dispatches by backend ID.

**These don't conflict; they operate at different layers.**

**Decision: two-tier dispatch.**

- **Coordinator-driven dispatch** (library layer): when
  `CalendarSyncEngine` / `BlobSyncEngine` detects a conflict
  between records from two backends, it consults
  `registerConflictHandler(backendId, handler)` to pick the handler.
  The coordinator is the dispatch point when the library owns the
  sync session end-to-end (this is what PlanStan does today).

- **Conduit-driven dispatch** (WP upper-upper layer): when a
  `SyncConduitBase` subclass owns the sync session (Palm-database-
  shaped orchestration), **the conduit drives**. It calls
  `context->conflictHandler->handleConflict(...)` directly. The
  library's registry is consulted by the conduit when populating
  `context->conflictHandler` at session start — the conduit picks
  the registered handler for the backend it's syncing to.

**API shape:**

```cpp
// Library side — coordinator-driven
coordinator->registerConflictHandler("caldav", dialogHandler);
coordinator->registerConflictHandler("palm", palmHandler);
coordinator->setDefaultConflictHandler(autoHandler);

// Library side — registry accessor (used by conduits)
ConflictHandler* registry->handlerFor(const QString &backendId);

// WP-side conduit, early in sync(context):
context->conflictHandler =
    context->coordinator->registry()->handlerFor(
        context->backend->backendId());
```

**No library change required beyond adding
`coordinator->registry()` as a public accessor** — the handler
registry already has to exist (per `00-open-questions.md` §4); it
just needs to be query-able by external orchestrators, not only
driven internally.

**Palm-specific layer:** `PalmConflictHandler` (WP-side) subclasses
`ConflictHandler`, adds HotSync-connection-aware fields
(`ConnectionBehavior`, timeout). Registered via
`coordinator->registerConflictHandler("palm", palmHandler)`. The
conduit + coordinator paths both end up calling the same handler
instance — no duplication.

**Implication for Phase B:** Phase B adds `ConflictHandler` abstract
+ `AutomaticConflictHandler` concrete + the registry with public
accessor. Phase E wires WP's `SyncConduitBase` to query it. No WP
involvement needed during Phase B beyond the API contract.

### Audit 4 — AsyncFileWriter unification  **[WP-driven]**

**Sources audited:**

- PS (in libkalburator): `~/dev/libkalburator/src/sync/asyncfilewriter.h`
- WP: grepped `~/dev/WildPalms/src/` for `QSaveFile` / atomic / rename
  / `.tmp`. Hits: `src/palm/kpilotdevicelink.h`, `deviceworker.h`,
  `devicesession.h`, `tickleworker.h`, `sync/syncbackend.h` (only in
  a doc comment about "batch operations for atomic commits" —
  **not** file-level atomicity), `plugins/install/installconduit.cpp`.
  WP's `src/sync/localfilebackend.cpp` does **not** use QSaveFile.

**What these actually are:**

- **PS `AsyncFileWriter`** — a Qt-thread-based worker that queues
  `QByteArray` writes OR `KCalendarCore::Incidence::Ptr` instances
  (deferred serialization: the serialization runs in the worker
  thread, not the caller). Purpose: prevent UI freeze during bulk
  writes in sync. Calendar-typed in its `Incidence::Ptr` overload.
  Not atomic (no `QSaveFile`) — it's concurrency, not durability.
- **WP's atomic writes in `palm/` code** — QSaveFile-based
  tempfile+rename for HotSync session artifacts (device-log dumps,
  session state files). Purpose: durability across a crash mid-sync.
  Single-threaded, main-thread-blocking. Not about concurrency.
- **WP's `LocalFileBackend`** — appears to do naked `QFile::write`
  without atomicity. Candidate for hardening.

**These solve three different problems:**

| Problem | Owner today |
|---|---|
| Off-thread bulk writes (don't freeze UI) | PS `AsyncFileWriter` |
| Atomic single-file writes (durability) | WP's Palm session code, via `QSaveFile` |
| Local-backend file writes | WP's `LocalFileBackend` — currently neither atomic nor off-thread |

**Decision: not a single unification.**

- **Upstream `AsyncFileWriter` into libkalburator** as-is (it's
  already there). Rename: drop the `KCalendarCore::Incidence::Ptr`
  overload down into the upper calendar layer only; keep the bare
  `QByteArray` queue in the lower blob layer. Result:
  `Kalburator::Sync::BlobAsyncFileWriter` (generic) +
  `CalendarAsyncFileWriter : BlobAsyncFileWriter` that adds the
  incidence-overload.
- **Make both atomic.** Add `QSaveFile` inside the worker's write
  path. This is a small upstream fix and closes the durability gap
  for both PS and WP.
- **WP's Palm session atomicity code stays WP-side.** It's
  Palm-domain-specific (HotSync log format, pilot-link integration)
  and doesn't belong in a generic library.
- **WP's `LocalFileBackend` dissolves** — its role is taken over by
  `LocalBlobBackend` in the library, which uses the upgraded
  `BlobAsyncFileWriter`. Durability + concurrency for free.

**Implication for Phase B:** the rename / layering is Phase C work.
**Audit-4 revision (2026-04-20, mid-Phase-B):** the "add QSaveFile
to AsyncFileWriter" plan was reverted after reading
`asyncfilewriter.cpp` lines 114–116, which contain a deliberate
comment explaining why `QSaveFile` was rejected:

> // Use QFile instead of QSaveFile for bulk writes - much faster
> // (QSaveFile does fsync per file which is very slow for 500+ files)
> // For sync operations, corrupted files get fixed on next sync anyway

This is a considered tradeoff: atomicity vs. throughput on bulk
sync. Sync is idempotent, so a torn file gets rewritten on the next
run — the "self-healing" property makes atomicity less critical
than it would be for general-purpose writes.

**Revised decision:** do **not** add `QSaveFile` unconditionally.
If WP (or future consumers) need atomicity for specific write paths,
add an opt-in `atomic=true` flag to `queueWrite` rather than
flipping the default. Deferred past Phase B — no consumer has
actually asked for it.

**Lesson for future audits:** read `.cpp` alongside `.h`. The header
tells you what the API is; the implementation tells you what the
*tradeoffs behind the API* are. Missing this nearly had me regress
PS's sync throughput.

---

## Summary — Phase B unblocked

All four gating audits resolved. Phase B scope is now:

1. Copy WP's `qsynccore/` files into `libkalburator/src/sync/`
   (flat; layered split is Phase C).
2. Rename `IdMappingStore` → `IDMappingStore`; merge schema per
   Audit 2 decision. Back with SQLite.
3. Strip `ConnectionBehavior` from `ConflictPolicy`.
4. Keep WP's `BaselineStore` (lower layer, hash-only); PS's baseline
   code stays in `SyncStore` untouched. Full carve-up is Phase C.
5. Add `ConflictHandler` + `AutomaticConflictHandler` + registry
   with public `coordinator->registry()` accessor (Audit 3).
6. ~~Add `QSaveFile` to `AsyncFileWriter` worker.~~ **Dropped
   2026-04-20** — existing non-atomic writes are a deliberate
   performance tradeoff (see Audit-4 revision above).
7. Coordinated PlanStan commit: PS's existing
   `SyncStore::setIdMapping` / `sourceUidForTargetId` callers move
   to the new `IDMappingStore`.

No open questions remain. Execute.

