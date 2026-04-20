# Phase 0 — Wild Palms `src/sync/` + `qsynccore/` inventory

**Source:** `~/dev/WildPalms/src/sync/` — 8 top-level files, +
`qsynccore/` subdir — 11 files, counted 2026-04-20.

## Conceptual shape

Wild Palms' sync abstraction is **format-agnostic**. Its `SyncBackend`
operates on `BackendRecord` — a generic blob (`id`, `type`, `data`,
`contentHash`, `lastModified`, `isDeleted`). Calendar entries, vCards,
markdown memos, and VTODO items all flow through the same interface.

This is the **single biggest design divergence from PlanStan**.
PlanStan's `SyncBackend` traffics in `KCalendarCore::Incidence::Ptr` —
calendar-typed all the way down.

**Implication for the library:** libkalburator must be **layered**:

1. **Lower layer** — generic record-sync primitives: `BackendRecord`-
   shaped fetch/stage/push/delete, conflict detection, journaling,
   change tracking. Format-neutral.
2. **Upper layer** — calendar-typed façade over the lower layer:
   `Incidence::Ptr` in/out, transcoding hooks, CalDAV/org-specific
   adapters.

Wild Palms uses **only** the lower layer for contacts / memos.
PlanStan uses **only** the upper layer (it has no non-calendar data).
Wild Palms' calendar + todo backends use the upper layer.

This is a cleaner reconciliation than "merge the two SyncBackends" —
each project's current abstraction corresponds to a different layer
in the unified library.

## Top-level `src/sync/` files

| File | Role | Classification |
|---|---|---|
| `syncbackend.h` — `Sync::SyncBackend` abstract base + `Sync::BackendRecord` | Generic blob-sync interface. Matches libkalburator's **lower layer** exactly. | **L-lower** |
| `localfilebackend.{h,cpp}` — `Sync::LocalFileBackend : Sync::SyncBackend` | Writes `BackendRecord`s to `.ics`/`.vcf`/`.md` files in a sync folder. | **L-lower** (or merge with PlanStan's `LocalBackend`; see §Reconciliation) |
| `syncengine.{h,cpp}` — coordinator | Drives sync across backends. Calls into `qsynccore/` for conflict detection. | **L** (reconciled with PlanStan's `SyncCoordinator`) |
| `syncstate.{h,cpp}` | Per-backend sync state tracking — last-sync timestamps, status. | **L** (merges with PlanStan's `SyncStore`) |
| `synctypes.h` | Shared enums: `CollectionInfo`, conflict decisions, etc. | **L** (merges with PlanStan's `SyncTypes.h` relocated from core) |
| `conduit.{h,cpp}` — `SyncConduitBase : public QObject, public ISyncConduit` | Wild Palms' conduit plugin base. Higher-level abstraction over `SyncBackend`. | **S** — stays in Wild Palms. This is Wild-Palms-specific plugin orchestration. |

## `src/sync/qsynccore/` — the conflict framework

Wild Palms already pre-extracted this into its own namespace. That
pre-extraction is a genuine head start.

| File | Role | Classification |
|---|---|---|
| `synccommon.h` | Shared types used across qsynccore. | **L** |
| `conflictrecord.{h,cpp}` — `ConflictRecord` | Data struct: source record, target record, detected fields, suggested resolution, timestamps. | **L** |
| `conflictstore.{h,cpp}` — `ConflictStore : QObject` | Persistent storage of unresolved conflicts; deferred-resolution workflow. | **L** |
| `conflictpolicy.{h,cpp}` — `ConflictPolicy` struct + handler interfaces | Configuration: `AutoResolveStrategy`, `PromptStrategy`, `FallbackBehavior`, `ConnectionBehavior`, safety limits. | **L-mixed** (see §Conflict-policy audit) |
| `baselinestore.{h,cpp}` — `BaselineStore` | Persistent 3-way-merge baseline storage. | **L** (merges with PlanStan's `SyncStore` baseline functionality) |
| `idmappingstore.{h,cpp}` — `IDMappingStore` | Cross-backend ID mapping (PalmID ↔ UUID ↔ CalDAV-href). | **L** (PlanStan needs this for cross-backend sync too) |

### Conflict-policy audit

`ConflictPolicy` mixes generic and Palm-fit concerns. Drilling in:

| Field / enum | Generic or Palm-fit | Action |
|---|---|---|
| `AutoResolveStrategy` — None / SourceWins / TargetWins / NewerWins / OlderWins / LargerWins / DuplicateAll | **Generic** — every sync scenario has these choices | **L** |
| `PromptStrategy` — Never / Always / WhenComplex / WhenDelete / OnFirstConflict | **Generic** | **L** |
| `FallbackBehavior` — Defer / Skip / UseDefault / Abort | **Generic** | **L** |
| `ConnectionBehavior` — KeepAlive / DisconnectAndDefer / TimeoutThenDefer | **Palm-fit** — HotSync session timeout has no meaning for CalDAV | **S** — stays in Wild Palms' Palm-backend subclass |
| `promptTimeoutSeconds`, `connectionTimeoutSeconds` | **Generic** (timeout is universal) but specific Palm values | **L** as generic; Palm uses Palm-specific defaults |
| `maxAutoResolvePerSync` safety limit | **Generic** | **L** |
| `requireConfirmForDeletes` | **Generic** | **L** |
| `allowBatchReview`, `showPreviewBeforeSync` | **Generic** | **L** |

**Conclusion:** ~90 % of `ConflictPolicy` lifts to the library. The
5–10 % that is Palm-fit (connection-behavior semantics) stays as a
subclass field or separate Palm-specific config struct attached to
`PalmBackend`.

### ConflictHandler: the interface pattern

Wild Palms exposes `ConflictHandler` abstract + `AutomaticConflictHandler`
concrete subclass. The library preserves this shape:

- Library ships `ConflictHandler` abstract + `AutomaticConflictHandler`
  concrete (implementing `AutoResolveStrategy` enum).
- Wild Palms ships a `PalmConflictHandler` subclass with
  HotSync-connection-aware logic (archive bits, category-ID
  remapping, secret-record handling, archived-delete semantics).
- PlanStan ships a `DialogConflictHandler` that wires to its existing
  `DialogConflictResolver`.
- Each backend can register a preferred handler; the library dispatches
  per-backend.

## Other Wild Palms components

| File | Role | Classification |
|---|---|---|
| `conduit.{h,cpp}` | `ISyncConduit` + `SyncConduitBase`. Wild Palms' plugin abstraction for Palm-database-shaped entities. | **S** — stays in Wild Palms as an upper-upper-layer abstraction specific to Palm database concepts. |

## What this inventory reveals

### Wild Palms is ahead on conflict framework

`ConflictStore` (persistent deferred resolution), `ConflictPolicy`
(declarative strategy selection), `IDMappingStore` (cross-backend ID
mapping) — PlanStan has none of these at this level of
sophistication. Adopting these from Wild Palms is a net win for
PlanStan.

### PlanStan is ahead on CalDAV, Akonadi, DecSync, transcoding

Wild Palms has `LocalFileBackend` only. CalDAV is a planned feature.
PlanStan has eight concrete backends plus rich `PropertyTranscoder` /
`RruleTranscoder` machinery.

### The layered architecture is the reconciliation

Wild Palms' `BackendRecord` ≠ PlanStan's `Incidence::Ptr`, but they
are not rivals — they are two layers of a library that accommodates
both calendar and non-calendar data.

### `localfilebackend` ≈ `LocalBackend` but not identical

Wild Palms' `LocalFileBackend` is `BackendRecord`-shaped. PlanStan's
`LocalBackend` is `Incidence::Ptr`-shaped. Reconciliation: Wild Palms'
`LocalFileBackend` becomes the **lower-layer** `LocalBlobBackend`;
PlanStan's `LocalBackend` becomes the **upper-layer** `LocalCalendarBackend`
built on top of it. Contacts and memos in Wild Palms go through the
blob layer; calendars go through the calendar layer.

## Non-ships

- `conduit.{h,cpp}` — Palm-database-conduit abstraction; stays in
  Wild Palms.
- The manifest-based conduit plugin loader — stays in Wild Palms.
- ShadowStan-specific conduit interfaces — stay in Wild Palms.

## Next document

`03-conflict-engine-audit.md` — detailed Palm-vs-generic separation
of the conflict engine, expanding on the ConflictPolicy table above.
