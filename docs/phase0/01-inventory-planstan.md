# Phase 0 — PlanStan `libs/sync/` inventory

**Source:** `~/dev/PlanStan/libs/sync/` — 44 headers + 37 source files,
counted 2026-04-20.

## Link dependencies (today)

| PUBLIC | PRIVATE (`NOT PROJECT_IS_TOP_LEVEL`) |
|---|---|
| Qt6::Core, Qt6::Sql, Qt6::Network | Qt6::Xml |
| KCalendarCore, KF6::DAV, KF6::KIOCore, KF6::Holidays | KF6::I18n |
| `planstan-org-io`, `planstan-models`, `planstan-core` | `planstan-models` (redundant) |
| optional KPim6::AkonadiCore (`HAVE_AKONADI`) | |

## Classification legend

- **L** — lifts to `libkalburator` unchanged or almost-unchanged
- **LI** — lifts with **I**nterface extraction required on host side
- **R** — **R**elocate (already in core but conceptually sync-ful)
- **S** — **S**tays in PlanStan (domain-specific)
- **?** — ambiguous; decision recorded in `04-merged-interface-sketch.md`

## Core orchestration

| File(s) | Role | Class |
|---|---|---|
| `syncbackend.{h,cpp}` | Abstract base for all backends. Defines fetch/stage/push/delete contract, `BackendCapabilities`, `RecurrenceCapabilities`. Host-neutral. | **L** |
| `synccoordinator.{h,cpp}` | Core two-way-sync algorithm. Loads mappings, runs 3-way diff, drives workers. Holds `ISyncHost*`. | **LI** (ISyncHost narrowing) |
| `syncworker.{h,cpp}` | Per-mapping worker thread. | **L** |
| `syncstore.{h,cpp}` | Persistent baseline + sync-mapping storage. | **L** |
| `backendregistry.{h,cpp}` | Backend-ID → `SyncBackend*` map. | **L** |
| `syncoperation.{h,cpp}`, `synctransaction.{h,cpp}`, `synctransactionitem.{h,cpp}` | Operation + transaction record types (add/update/delete). | **L** |
| `createincidenceitem.{h,cpp}`, `updateincidenceitem.{h,cpp}`, `deleteincidenceitem.{h,cpp}` | CRUD operation wrappers. | **L** |
| `isyncrecord.h` | Abstract per-record interface. | **L** |

## Backend implementations

| File(s) | Role | Classification |
|---|---|---|
| `localbackend.{h,cpp}` | Flat-file iCal reader/writer. | **L** |
| `remotebackend.{h,cpp}` | CalDAV via KF6::DAV. | **L** |
| `orgbackend.{h,cpp}` | org-mode files via `planstan-org-io`. | **L** (drags `planstan-org-io` as dep) |
| `akonadibackend.{h,cpp}` | Optional Akonadi client. | **L** (behind `HAVE_AKONADI`) |
| `decsyncbackend.{h,cpp}` + `decsynclib.{h,cpp}` + `decsyncactivecontroller.{h,cpp}` + `decsynccontrollerstore.{h,cpp}` + `decsyncgarbagecollector.{h,cpp}` | PlanStan's in-tree DecSync protocol implementation. | **L** |
| `holidaysubscriptionbackend.{h,cpp}` | KF6::Holidays read-only holiday feeds. | **L** |
| `subscriptionbackend.{h,cpp}` | Generic read-only iCal-URL subscription. | **L** |
| `mockbackend.{h,cpp}` | Test-only fake backend. | **L** (ships as test-support) |

## Backend capabilities + discovery

| File(s) | Role | Classification |
|---|---|---|
| `backendcapabilities.{h,cpp}` | Declarative "what this backend can do" struct — recurrence support, attachment support, attendee support, etc. | **L** |
| `caldavcapabilitydiscovery.h` | Probes CalDAV server for supported features. | **L** |
| `discoveredcalendar.h` | Record type for calendars found on a backend during discovery. | **L** |

## Conflict detection

| File(s) | Role | Classification |
|---|---|---|
| `conflictmanager.{h,cpp}` | Tracks conflicts, routes to `IConflictResolver`. | **L** (but reconciled with Wild Palms' `ConflictStore`; see `03-conflict-engine-audit.md`) |
| `iconflictresolver.h` | Abstract callback for host to resolve a conflict. | **L** |
| `iconflictpresenter.h` | Abstract callback for host to present UI. | **L** |
| `syncdiff.{h,cpp}` | 3-way diff between local / remote / baseline. | **L** |
| `incidencediff.{h,cpp}` | Field-by-field incidence comparison. | **L** |

## Transcoding + lossy-format preservation

| File(s) | Role | Classification |
|---|---|---|
| `propertytranscoder.{h,cpp}` | X-property preservation across backend round-trips. | **L** |
| `rruletranscoder.{h,cpp}` | RRULE translation for lossy backends (e.g. org-mode). | **L** |
| `transcodingregistry.{h,cpp}` | Per-backend transcoder registration. | **L** |
| `incidencesyncadapter.h` | Adapter interface between incidence model and sync. | **L** |

## Crash recovery + disk I/O

| File(s) | Role | Classification |
|---|---|---|
| `calendarjournal.{h,cpp}` | Write-ahead journal of pending sync operations; recovers after crash. | **L** |
| `asyncfilewriter.{h,cpp}` | Atomic background file writes. | **L** |

## Network discovery

| File(s) | Role | Classification |
|---|---|---|
| `syncthingdiscovery.{h,cpp}` | Discovers Syncthing-managed folders. | **L** (but consider if it belongs in a separate `libkalburator-syncthing` module) |
| `syncthingmonitor.{h,cpp}` | Watches Syncthing activity for change detection. | **L** (same) |

## Calendar-lifecycle management

| File(s) | Role | Classification |
|---|---|---|
| `calendarmanager.{h,cpp}` | CRUD orchestration for `KCalendarCore::MemoryCalendar` aggregates. Holds `ISyncHost*`. | **LI** (depends on `Collection` — needs `ICalendarCollection`) |
| `logicalcalendarbuilder.{h,cpp}` | Constructs `LogicalCalendar` config objects. | **L** |

## Host interface

| File(s) | Role | Classification |
|---|---|---|
| `isynchost.h` | Abstract contract the host (CollectionController) implements. | **LI** — surface needs narrowing (see below) |

### `ISyncHost` current methods, with verdict

```cpp
class ISyncHost {
    // Backend lifecycle — lift as-is
    virtual SyncBackend* backendById(const QString &id) = 0;
    virtual QHash<QString, SyncBackend*> backends() = 0;

    // Incidence propagation — lift as-is; the "apply" shape
    // is exactly what any host needs.
    virtual bool applyIncidenceAddition(...) = 0;
    virtual bool applyIncidenceRemoval(...) = 0;
    virtual bool applyIncidenceUpdate(...) = 0;

    // Calendar discovery — replace with ICalendarCollection
    virtual Collection* collection() = 0;                // PlanStan type
    virtual IIncidenceSource* incidenceSource() = 0;     // from planstan-core
    virtual IIncidenceRegistry* incidenceRegistry() = 0; // from planstan-core

    // Subsystem access — needs unpacking
    virtual KalbConfigManager* kalbConfigManager() = 0;  // PlanStan-specific
    virtual SyncCoordinator* syncCoordinator() = 0;      // internal to sync lib

    // Calendar lifecycle — lift as-is
    virtual void unloadCalendar(const QString &calendarId) = 0;

    // Sync mapping regeneration — lift as-is
    virtual void generateSyncMappingsFromLogicalCalendars() = 0;
};
```

**Narrowing plan for the library-side interface:**

- `collection()` returns `ICalendarCollection*` instead of PlanStan's
  concrete `Collection`. The interface's sync-facing surface is
  ~10–15 methods — audited in `04-merged-interface-sketch.md`.
- `IIncidenceSource` and `IIncidenceRegistry` live in `planstan-core`
  but are host-neutral. They can move into `libkalburator` or a
  tiny shared types library.
- `KalbConfigManager*` is PlanStan's config store. The library
  needs a narrower interface — probably `ISyncConfigStore` with
  methods for reading/writing sync mappings and backend
  configurations.
- `SyncCoordinator*` getter is circular (SyncCoordinator *is* the
  sync lib's own class). Remove from host interface; use direct
  injection.
- `DataDomain` enum + `CalendarType` enum currently live in
  `planstan-core` — **relocate to libkalburator**.

## Types currently in `planstan-core` that belong in libkalburator

| Type | Role | Action |
|---|---|---|
| `BackendConfiguration` | Persistent backend-config struct | **R** relocate |
| `LogicalCalendar` | Logical-over-physical calendar binding | **R** relocate |
| `SyncTypes.h` — enums: sync mode, binding role, etc. | Sync vocabulary | **R** relocate |
| `CalendarType` enum | Calendar type (hybrid, events, todos, etc.) | **R** or share via a new `libkalburator-types` micro-lib |
| `DataDomain` enum | Domain (events, todos, journals, contacts, memos) | **R** |

## Types that stay in PlanStan

| Type | Why |
|---|---|
| `PlanStanBackend` | Plugin-shaped — exposes .planstan project data. Implementation of the lib's `SyncBackend`. |
| `CollectionController` as `ISyncHost` impl | App-shell wiring; depends on `GlobalIncidenceModel`, `ProjectStore`, `PlanningEngine` etc. |
| `DialogConflictResolver` etc. (in `src/sync/`) | App UI for conflict resolution |
| `StagingController` (in `src/sync/`) | App-level staging UI state |

## Lines of code rough bucket

```
grep -rln '^' libs/sync/include libs/sync/src | xargs wc -l
```

Run this on check-in to phase 0; embed the number here. Anticipated
~25K LOC total, of which ~22K lifts cleanly and ~3K is the
`ISyncHost` / `Collection` interface work.

## Non-ships

`synctesthooks.h` — PlanStan-only test instrumentation. Stays in
PlanStan unless Wild Palms wants equivalent hooks for its own tests.

## Next document

`02-inventory-wildpalms.md` — same treatment for Wild Palms'
`src/sync/` + `src/sync/qsynccore/`.
