# Phase 0 — Merged interface sketch for libkalburator

**Status:** First draft, design-gate for Phase 1.

This document reconciles the PlanStan and Wild Palms inventories
(`01-inventory-planstan.md`, `02-inventory-wildpalms.md`) into a
single layered library surface. Every class / method carries a
**provenance marker**:

- `[PS]` — from PlanStan's `libs/sync/`
- `[WP]` — from Wild Palms' `src/sync/` or `qsynccore/`
- `[REC]` — reconciled from both; shape chosen during Phase 0
- `[NEW]` — invented during Phase 0 to cover a gap

## The layered architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Upper layer — calendar-typed                               │
│                                                             │
│  ICalendarBackend, ICalendarSyncCoordinator,                │
│  CalDAVBackend, LocalCalendarBackend, OrgBackend,           │
│  AkonadiBackend, DecsyncBackend, SubscriptionBackend,       │
│  HolidayBackend, MockCalendarBackend                        │
│                                                             │
│  KCalendarCore::Incidence::Ptr in/out.                      │
│  PropertyTranscoder, RruleTranscoder, CalendarJournal.      │
│                                                             │
│  Links: KCalendarCore, KF6::DAV, KF6::KIOCore, KF6::Holidays│
│         optional KPim6::AkonadiCore                         │
└─────────────────────────────────────────────────────────────┘
             ▲
             │ implemented on top of
             │
┌─────────────────────────────────────────────────────────────┐
│  Lower layer — generic blob sync                            │
│                                                             │
│  IBlobBackend, BackendRecord, BlobSyncEngine,               │
│  LocalBlobBackend, MockBlobBackend                          │
│                                                             │
│  Conflict framework:                                         │
│    ConflictRecord, ConflictStore, ConflictPolicy,           │
│    ConflictHandler, AutomaticConflictHandler                │
│                                                             │
│  Cross-cutting:                                              │
│    BaselineStore, IDMappingStore, AsyncFileWriter           │
│                                                             │
│  Links: Qt6::Core, Qt6::Sql                                 │
└─────────────────────────────────────────────────────────────┘
```

Wild Palms' Palm contacts / memos use **only** the lower layer.
Wild Palms' calendar / todo backends and PlanStan everything use
**both** layers. Pure reuse hosts that need only calendar
synchronisation can skip the lower layer entirely (it builds
transparently underneath).

## Lower layer — detailed surface

### `BackendRecord`   **[WP]**

Unchanged from Wild Palms. Generic blob identity: `id`, `type`,
`displayName`, `data` (`QByteArray`), `contentHash`, `lastModified`,
`isDeleted`. Virtual `description()` for logging.

### `IBlobBackend`   **[WP]** (Wild Palms' `SyncBackend` renamed for disambiguation)

Abstract `QObject`-based interface. Methods correspond directly to
Wild Palms' existing `Sync::SyncBackend`:

- `backendId()`, `displayName()`, `isAvailable()`
- `availableCollections()`, `collectionInfo()`, `createCollection()`
- `loadRecords()`, `loadRecord()`, `createRecord()`, `updateRecord()`,
  `deleteRecord()`
- `modifiedSince()`, `deletedSince()`, `supportsDeleteTracking()`
- `beginBatch()`, `commitBatch()`, `rollbackBatch()`, `supportsBatch()`
- Signals: `recordCreated`, `recordUpdated`, `recordDeleted`,
  `errorOccurred`, `progressUpdated`

Host-neutral; no PlanStan or Palm concepts.

### `BlobSyncEngine`   **[REC]** (PlanStan `SyncCoordinator` + Wild Palms `SyncEngine`)

Lower-layer coordinator. Drives two-way sync between `IBlobBackend`
instances, consulting `BaselineStore` and `ConflictStore`. Surface
merges PlanStan's sync-mapping and worker model with Wild Palms'
simpler driver model.

Pending decision in 04a-followups: whether the blob engine and
calendar engine are separate classes or the calendar engine extends
the blob engine. Leaning toward extension.

### Conflict framework — lifted from Wild Palms wholesale **[WP]**

All of `qsynccore/` except the Palm-fit `ConnectionBehavior` enum
moves into libkalburator's lower layer:

```cpp
namespace Kalburator::Sync {

enum class AutoResolveStrategy { None, SourceAlwaysWins,
    TargetAlwaysWins, NewerWins, OlderWins, LargerWins, DuplicateAll };
enum class PromptStrategy { Never, Always, WhenComplex,
    WhenDelete, OnFirstConflict };
enum class FallbackBehavior { Defer, Skip, UseDefault, Abort };

struct ConflictPolicy {
    AutoResolveStrategy autoResolve = AutoResolveStrategy::None;
    PromptStrategy promptStrategy = PromptStrategy::Always;
    int promptTimeoutSeconds = 60;
    ConflictDecision timeoutDecision = ConflictDecision::Skip;
    FallbackBehavior fallback = FallbackBehavior::Defer;
    bool allowBatchReview = true;
    bool showPreviewBeforeSync = false;
    int maxAutoResolvePerSync = 100;
    bool requireConfirmForDeletes = true;
    // NO ConnectionBehavior — that's Palm-specific
};

struct ConflictRecord { /* Wild Palms' existing shape */ };
class  ConflictStore : public QObject { /* Wild Palms' existing API */ };
class  ConflictHandler { /* abstract, Wild Palms' existing shape */ };
class  AutomaticConflictHandler : public ConflictHandler { /* ... */ };

} // namespace
```

Wild Palms' `PalmConflictHandler` (to be introduced during Phase 4)
lives in Wild Palms, inherits `ConflictHandler`, carries the
HotSync-connection-aware logic.

### `BaselineStore`   **[REC]** (WP has it first-class; PS had baseline logic embedded in `SyncStore`)

Pre-extract Wild Palms' `BaselineStore`; fold PlanStan's embedded
baseline code into it. Single authoritative 3-way-merge baseline
store shared by both layers.

### `IDMappingStore`   **[WP]**

Cross-backend ID mapping (UUID ↔ CalDAV href ↔ PalmID ↔ Akonadi
item ID). PlanStan has implicit version of this in its `SyncStore`;
Wild Palms' explicit store is cleaner.

### `AsyncFileWriter`   **[PS]**

PlanStan's atomic-write helper. Wild Palms should adopt — equally
useful for Palm backup file writes.

## Upper layer — detailed surface

### `ICalendarBackend`   **[PS]** (PlanStan's `SyncBackend`)

Incidence-typed backend interface. Extends / wraps `IBlobBackend`?
(Decision: **extends conceptually, but does not inherit** — the two
have different I/O types. Every `ICalendarBackend` can also expose an
`IBlobBackend` view for lower-layer tools that need it, via
`blobBackend()`.)

Methods:

- All of `IBlobBackend`'s identity + collection methods (as
  `ICalendarBackend::blobBackend()`)
- `loadIncidences()`, `loadIncidence(uid)` — returns
  `KCalendarCore::Incidence::Ptr`
- `createIncidence()`, `updateIncidence()`, `deleteIncidence()`
- `backendCapabilities()`, `recurrenceCapabilities()`
- `discoverCalendars()` — `DiscoveredCalendar` list

### Concrete upper-layer backends   **[PS]**

Moved from `libs/sync/` essentially unchanged:

- `LocalCalendarBackend` (PlanStan's `LocalBackend`, rename for
  disambiguation from `LocalBlobBackend`)
- `CalDAVBackend` (PlanStan's `RemoteBackend`)
- `OrgBackend`
- `AkonadiBackend` (behind `HAVE_AKONADI`)
- `DecsyncBackend` + supporting DecSync protocol code
- `SubscriptionBackend`, `HolidaySubscriptionBackend`
- `MockCalendarBackend` (ships as test-support)

### `ICalendarSyncCoordinator`   **[PS]** (`SyncCoordinator`)

Upper-layer coordinator. Runs the incidence-typed two-way sync
algorithm. Reuses `BlobSyncEngine` underneath for the transport
layer; adds incidence-diff, RRULE transcoding, and calendar-specific
conflict handling.

### `PropertyTranscoder`, `RruleTranscoder`, `TranscodingRegistry`   **[PS]**

Lift unchanged. These are X-property + RRULE preservation for lossy
backends (org-mode's limited recurrence support, CalDAV-vs-Akonadi
quirks).

### `CalendarJournal`   **[PS]**

Write-ahead journal for calendar sync operations. Lift unchanged; the
blob layer does not need a journal (its backends are atomic).

### `IncidenceDiff`, `SyncDiff`   **[PS]**

3-way diff primitives. Lift unchanged.

### `CalendarManager`, `LogicalCalendarBuilder`   **[PS]**

Calendar-lifecycle orchestration. Lift with `ICalendarHost` interface
narrowing (see below).

## Host interfaces

### `ICalendarHost`   **[REC]** (narrow of PlanStan's `ISyncHost`)

```cpp
class ICalendarHost {
public:
    virtual ~ICalendarHost() = default;

    // Backend lifecycle
    virtual ICalendarBackend* backendById(const QString &id) = 0;
    virtual QHash<QString, ICalendarBackend*> backends() = 0;

    // Incidence propagation — sync → host's model
    virtual bool applyIncidenceAddition(const QString &calendarId,
        const KCalendarCore::Incidence::Ptr &inc,
        bool stageForSync = true) = 0;
    virtual bool applyIncidenceRemoval(const QString &calendarId,
        const QString &uid, bool stageForSync = true,
        const QDateTime &recurrenceId = {}) = 0;
    virtual bool applyIncidenceUpdate(const QString &calendarId,
        const KCalendarCore::Incidence::Ptr &inc,
        bool stageForSync = true) = 0;

    // Calendar collection
    virtual ICalendarCollection* collection() = 0;

    // Calendar lifecycle
    virtual void unloadCalendar(const QString &calendarId) = 0;
    virtual void generateSyncMappingsFromLogicalCalendars() = 0;

    // NOTE removed from PlanStan's ISyncHost:
    //   - KalbConfigManager* kalbConfigManager()  → replaced with
    //     ISyncConfigStore below
    //   - SyncCoordinator* syncCoordinator()       → circular, drop
    //   - IIncidenceSource* incidenceSource()     → PlanStan-specific;
    //     use ICalendarCollection's incidence accessor
    //   - IIncidenceRegistry* incidenceRegistry() → same
};
```

### `ICalendarCollection`   **[NEW]**

Narrow of PlanStan's `Collection` covering sync-facing methods only.

```cpp
class ICalendarCollection {
public:
    virtual ~ICalendarCollection() = default;

    virtual QString id() const = 0;
    virtual QList<KCalendarCore::MemoryCalendar*> calendars() const = 0;
    virtual KCalendarCore::MemoryCalendar* calendar(const QString &calendarId) const = 0;
    virtual void addCalendar(KCalendarCore::MemoryCalendar *calendar) = 0;
    virtual void removeCalendar(const QString &calendarId) = 0;

    virtual QString calendarBackend(const QString &calendarId) const = 0;
    virtual void setCalendarBackend(const QString &calendarId,
                                    const QString &backendId) = 0;
    virtual QList<QString> calendarsForBackend(const QString &backendId) const = 0;

    // Identity / working-copy helpers
    virtual QString calendarIdForIncidence(
        const KCalendarCore::Incidence::Ptr &inc) const = 0;
    virtual KCalendarCore::MemoryCalendar* workingCalendar(
        const QString &calendarId) const = 0;
};
```

PlanStan's `Collection` implements this. Wild Palms writes its own
implementation (probably a thin wrapper over a flat `MemoryCalendar`
list keyed by backend).

### `ISyncConfigStore`   **[NEW]** (replaces `KalbConfigManager` surface)

```cpp
class ISyncConfigStore {
public:
    virtual ~ISyncConfigStore() = default;

    virtual QList<BackendConfiguration> backendConfigurations() const = 0;
    virtual QList<LogicalCalendar> logicalCalendars() const = 0;
    virtual QList<SyncMapping> syncMappings() const = 0;

    virtual void persistBackendConfigurations(const QList<BackendConfiguration>&) = 0;
    virtual void persistLogicalCalendars(const QList<LogicalCalendar>&) = 0;
    virtual void persistSyncMappings(const QList<SyncMapping>&) = 0;
};
```

PlanStan's `KalbConfigManager` implements this. Wild Palms writes its
own (probably backed by a QSettings profile).

### `IConflictResolver`, `IConflictPresenter`   **[PS]**

Keep Wild Palms' naming consistent: these exist as both-project
interfaces already; keep shape unchanged.

## Types that live at the library root

| Type | From | Role |
|---|---|---|
| `BackendConfiguration` | [PS, relocate from core] | Persistent backend config |
| `LogicalCalendar` | [PS, relocate from core] | Logical-over-physical calendar binding |
| `SyncMapping` | [PS] | Mapping definition: source + target + policy |
| `BackendCapabilities` | [PS] | What a backend can/can't do |
| `RecurrenceCapabilities` | [PS] | Finer-grained RRULE feature support |
| `DiscoveredCalendar` | [PS] | Record of a calendar found during discovery |
| `CollectionInfo` | [WP] | Lower-layer collection description |
| `CalendarType` enum | [PS, relocate from core] | Hybrid / events / todos / etc. |
| `DataDomain` enum | [PS, relocate from core] | events / todos / journals / contacts / memos |
| `ConflictDecision` enum | [WP] | User/auto decision outcome |

## What does NOT enter libkalburator

- PlanStan's `PlanStanBackend` — plugin into the library, stays in
  PlanStan.
- PlanStan's `CollectionController` as an `ICalendarHost` impl —
  app-shell, stays in PlanStan.
- PlanStan's dialog-based `IConflictResolver` + `IConflictPresenter`
  impls — UI, stay in PlanStan.
- PlanStan's `StagingController` (in `src/sync/`) — app-level UI,
  stays in PlanStan.
- Wild Palms' `PalmBackend` — plugin into the library, stays in Wild
  Palms.
- Wild Palms' `PalmConflictHandler` — subclass of
  `ConflictHandler`, stays in Wild Palms.
- Wild Palms' `SyncConduitBase` + manifest plugin loader — Palm-shaped
  orchestration on top, stays in Wild Palms.

## Open questions deferred to `04a-followups.md` / `00-open-questions.md`

- Does `ICalendarSyncCoordinator` inherit from `BlobSyncEngine`, or
  compose it, or stand alone?
- Namespace: `Kalburator::Sync::*` vs `Kal::Sync::*` vs flat.
- Symbol-renaming: every project has its own `SyncBackend` —
  internal namespacing chosen during Phase 1 must be chosen here.
- Per-calendar conflict handler registration API shape.
- DecSync — is it upper layer (calendar-typed) or lower layer
  (blob-typed)? Currently calendar-only in PlanStan, but DecSync's
  design is blob-shaped and could serve contacts / memos too.

## Sanity check

A minimal Wild Palms Full Sync Mode session:

1. Wild Palms constructs `PalmBackend : ICalendarBackend` for its
   Palm device, a `CalDAVBackend` for the user's CalDAV server, a
   `LocalCalendarBackend` for the user's local iCal folder.
2. Wild Palms implements `ICalendarCollection` + `ICalendarHost` +
   `ISyncConfigStore` over its own model.
3. Wild Palms constructs an `ICalendarSyncCoordinator`, registers
   the three backends, registers its `PalmConflictHandler` for the
   PalmBackend and an `AutomaticConflictHandler(NewerWins)` for the
   other two.
4. Wild Palms calls `coordinator.runSync()`.

No PlanStan code is loaded. The library does its job. PlanStan in its
own process does the equivalent against its own `ICalendarHost`
implementation. Both users see consistent data because both apps'
library instances use the same `BaselineStore` on disk.

## Confidence check

- **High:** the layered architecture (blob + calendar). The
  inventory reveals this naturally; both projects will benefit.
- **High:** the conflict framework lifts from Wild Palms.
- **High:** the concrete calendar backends lift from PlanStan.
- **Medium:** `ICalendarCollection` surface — 10–15 methods, need
  to audit `libs/sync` + `libs/calendar-views` actual calls to
  confirm.
- **Medium:** `ISyncConfigStore` — need to see `KalbConfigManager`'s
  actual usage pattern in the coordinator.
- **Lower:** DecSync layering decision. Safe default: keep it
  calendar-typed for now; revisit if contacts sync via DecSync
  becomes a real ask.
