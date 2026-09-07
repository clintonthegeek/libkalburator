# Phase K.0 working notes — first-hand code findings

Working file. Will be folded into the design doc at K.0.7. Not durable
documentation; intermediate scratch. Last update: 2026-05-09.

## Headline shift in mental model

The audits said "the engine's abstraction is calendar-typed all the
way down." The code says something subtler: **the domain-abstraction
layer already exists and is partially wired**. Phase G's "shape
pipeline" + `Shape::DomainPlugin` + `Shape::IRecordWriter` +
`Shape::DomainRegistry` are real, used in `unifiedContinueAfterConflicts`,
and the engine drives writes through `plugin->createWriter()` already
(syncengine.cpp:2327-2336, 2393, 2411).

What was *not* done in Phase G/Ia.5/Ib.5 is **retiring the legacy
calendar-typed surface on `SyncBackend`**. That surface still exists,
still requires every backend to satisfy it (often as no-op stubs),
and is still reached for in the engine fast-path
(prepareSyncFastPath's `qobject_cast<RemoteCalendarBackend*>` /
`qobject_cast<LocalBackend*>`).

So Phase K is **less greenfield, more "finish the lift"**:

1. Move calendar-typed virtuals off `SyncBackend` and into the
   `Kalburator::Calendar::CalendarDomainPlugin`.
2. Make `IBlobBackend` (+ `nativeShapes()`) the actual public backend
   interface.
3. Generalize the fast-path's qobject_casts into a capability
   interface (CTag-supporting, fingerprint-supporting, linearizable,
   etc.) — addresses the user's "sync-situation flexibility" ask.
4. Unify `CalendarBaselineStore` + `BlobBaselineStore` (or scope
   the calendar one as a thin domain facade).
5. Add the contacts witness — ContactsDomainPlugin + RemoteContactsBackend
   already exist, the witness is wiring them through the unified path
   in tests/ without KCalendarCore.

## Confirmed first-hand against the code

### `SyncBackend` is calendar-typed at the base — confirmed

`libkalburator/src/calendar/syncbackend.h`:

- Lives in `src/calendar/` (placement is itself a tell).
- Includes `<KCalendarCore/MemoryCalendar>`, `<Incidence>`, `<Recurrence>`.
- Pure virtuals over calendar types:
  - `storeCalendars(QString, QList<MemoryCalendar*>)` (line 150)
  - `startSync(QString, MemoryCalendar*, QList<Incidence::Ptr>, ...)` (line 154)
  - `pushItems(QString, QList<Incidence::Ptr>, TranscodingPlan)` (line 216)
- Calendar-typed signals:
  - `itemLoaded(MemoryCalendar*, Incidence::Ptr, QString)` (557)
  - `calendarLoaded(MemoryCalendar*)` (562)
  - `itemFetched(QString, Incidence::Ptr)` (618)
  - `typeViolationDetected(..., CalendarType, CalendarType)` (606)
- Calendar-shaped capability getters:
  - `discoveredCalendarType` / `discoveredColor` / `discoveredWritable`
  - `calendarColor` / `calendarDescription`
  - `getRawIcs` / `setRawIcs`
  - `analyzeRecurrenceLoss(Incidence::Ptr)`
  - `RecurrenceCapabilities` / `RecurrenceLossInfo` structs
- **Inherits `QObject` AND `IBlobBackend`** (line 117):
  ```cpp
  class SyncBackend : public QObject, public IBlobBackend
  ```
  This means `IBlobBackend` is already the real backend interface.
  `SyncBackend` is calendar-typed scar tissue layered on top.

### `IBlobBackend` is the proper generic substrate

It already provides what we need:
- `loadRecords(collectionId)` / `loadRecord(id)` returning `BackendRecord`
- `createRecord(collectionId, BackendRecord)` / `updateRecord` / `deleteRecord`
- `availableCollections()`, `collectionInfo(id)`, `createCollection(info)`
- `modifiedSince(collectionId, since)` / `deletedSince(collectionId, since)`
- `beginBatch` / `commitBatch` / `rollbackBatch`
- `supportsBatch`, `supportsDeleteTracking`

### Engine uses `nativeShapes()` + `DomainPlugin` already — confirmed

`syncengine.cpp:2320-2336`:
```cpp
const Kalburator::Shape::Shape srcShape = srcBackend->nativeShapes().first();
const Kalburator::Shape::Shape tgtShape = tgtBackend->nativeShapes().first();
const auto canonToTgt = treg.compile(m_unifiedCanonical, tgtShape);
const auto canonToSrc = treg.compile(m_unifiedCanonical, srcShape);

auto *plugin = Kalburator::Shape::DomainRegistry::instance()
                   .findByDomain(srcShape.domain);
```

Engine consults `DomainRegistry`, finds the plugin, asks the plugin
to make a writer (`plugin->createWriter(backend)`), and applies records.
This is the right pattern. **The calendar-typed `SyncBackend` virtuals
are dead-end legacy** — the engine's modern path doesn't use them.

`syncengine.cpp:2363` has a `dynamic_cast<CalendarPluginWriter*>`. This is
**threading routing, not a domain leak** — calendar writer asserts it's
called from a non-backend thread (BlockingQueuedConnection internal),
generic writers run on the backend thread. Pretty, no. Architectural
leak, no — it's a writer-instance contract dispatch. Fixable by
moving the threading contract onto IRecordWriter as a virtual.

### Fast-path's qobject_casts are the real engine-level leak

`syncengine.cpp:611-720` (prepareSyncFastPath):

```cpp
if (auto *r = qobject_cast<RemoteCalendarBackend*>(base)) {
    remoteCalIdsByBackend[r].append(calId);
    ...
} else if (auto *l = qobject_cast<LocalBackend*>(base)) {
    fresh.sourceFingerprint = l->calendarFingerprint(...);
    ...
}
```

`RemoteCalendarBackend` despite the name is the CalDAV/CardDAV
transport (RemoteContactsBackend extends it for contacts). `LocalBackend`
is a local file store. The hard-coding to these two concrete types is
**the user's "sync-situation flexibility" issue, manifest**:

- A Palm backend doesn't fit either, so its mappings can never be
  fast-path-skipped.
- An Akonadi backend, an Evolution backend, an org-mode backend —
  all hit the same wall.
- The change-detection mechanism (CTag vs fingerprint) is hard-coded;
  there's no way for a backend to declare "I have my own change
  signal."

This is the right place to introduce a capability interface — what
the user asked for as a separate evaluation pass. The user's "almost
worth going over just that architecture alone" is correct.

### Calendar-typed types in public API

Verified from grep:

- `synctypes.h:393` — `struct ExecutionOverride`. Per-call direction
  override. Used by WildPalms's Copy Palm→PC button. **Not a Palm-ism
  in concept** — direction override is a real cross-consumer feature.
  Keep semantics, name is fine actually (no Palm-name in it).
- `syncenginefuture.h:13` — `enum class CancellationReason`. Has a
  `ResourceLost` variant the audit flagged. **Not a Palm-ism** —
  resource-lost is a generic transient I/O failure (network down,
  server gone, removable device unplugged). Keep.
- `IDMapping::sourceCategory` — not yet read first-hand. TODO.
- `qobject_cast<RemoteCalendarBackend*>` / `qobject_cast<LocalBackend*>`
  in fast-path — confirmed; covered above as the change-detection
  capability gap.

User's instruction: "semantics are fine, but we really want
libkalburator to be flexible enough to handle the constraints of
different sync situations." Read with the code: the *naming* is
fine, the *concepts* are fine, what's missing is the **abstraction
behind them**. ExecutionOverride is per-call; needs to remain.
ResourceLost is a real reason; needs to remain. The fast-path's
backend-type-specific code is what needs lifting into a capability
interface so other backends with their own affordances can plug in.

## Phase J Task 9 root cause — confirmed

`calendarplugin_writer.cpp:80-85`:
```cpp
KCalendarCore::MemoryCalendar *cal = m_collection->calendar(collectionId);
if (!cal) {
    qWarning() << "CalendarPluginWriter::apply - calendar not found:"
               << collectionId;
    return false;
}
```

The calendar writer demands the host has registered a `MemoryCalendar`
for the target id. In the palm→caldav direction, no MemoryCalendar
exists for the CalDAV target id (only the Palm side has one), so
the write silently fails. **This is fixable in two ways:**

1. Quick fix — host registers MemoryCalendars for *all* sides of every
   mapping. Doesn't solve the architectural problem.
2. Phase K fix — calendar writer becomes an IRecordWriter that doesn't
   need a host-resident MemoryCalendar; it just parses BackendRecord
   bytes, applies via SyncTransaction directly to the backend. The
   `MemoryCalendar` was only there because legacy `applyChangesToBackend`
   in `CalendarDomainAdapter` worked through the host's calendar.
   The unified path doesn't need it for correctness — it's a leftover.

## Sync-situation flexibility — what the engine already has vs. what's missing

The user wants the engine to flex along these axes (their list):
- Palm linearity
- ETag/CTag affordances in CalDAV/CardDAV
- Parallelism where efficient
- Baselines
- Various conflict-handling scenarios

What the engine **already** has:

- **Per-backend conflict handlers**: `ConflictHandlerRegistry`. Backends
  register their own handlers; engine consults them.
- **Conflict policies per-mapping**: `SyncMapping.conflictPolicy`
  (SourceWins / TargetWins / AskUser / Newest).
- **Resource-aware cancellation**: `cancelWithReason(reason, resourceId)`.
  Cancels in-flight + skips pending mappings touching a lost resource.
- **Resource-aware FIFO scheduling**: `MappingScheduler` (G.6 Task 44).
- **Per-call direction override**: `ExecutionOverride`.
- **Cross-thread cancellation observation**: F2 `await<Op>` model + the
  conflict-pause yield + `cancellationObserved` signal.
- **Two-phase fast-path skip**: PROPFIND CTag for remote, fingerprint
  for local.
- **Two-tier baseline**: `CalendarBaselineStore` (calendar-shaped) and
  `BlobBaselineStore` (generic, hash-keyed).

What the engine is **missing**:

- **Capability declaration on backends.** Today the fast-path discovers
  capabilities by qobject_cast on concrete types. Should be
  `IBlobBackend::changeDetection()` returning a small struct/enum
  describing what the backend supports (none, fingerprint, ctag,
  per-record-etag, custom-signal, ...).
- **Linearity hint.** Palm device can serve only one client at a time;
  multiple Palm-targeting mappings cannot run in parallel. There's
  no "linearization key" on backends today; the scheduler doesn't
  know to serialize.
- **Per-backend baseline strategy.** `BlobBaselineStore` is generic but
  hash-keyed; `CalendarBaselineStore` is calendar-shaped. Need a
  unified model where a domain plugin declares its baseline shape
  (canonical-record blob, hash, structured property bag, …) and the
  store handles it generically. The split-brain is split because
  calendar baseline carries property-level state for property-sync
  that the blob baseline doesn't.
- **Generalized "this backend uses this conflict signal" path.**
  E.g. CalDAV's ETag is a per-record change signal; CardDAV similar;
  Palm has its own delete-tracking mechanism. The engine should
  consult a `IChangeDetection` rather than special-case CTag.

## Files I still need to read first-hand

- `src/shape/domainplugin.h` — what's the contract?
- `src/shape/domainregistry.h`
- `src/shape/irecordwriter.h`, `irecorddiffer.h`, `irecordmerger.h`
- `src/contacts/contactsdomainplugin.h` — witness substrate
- `src/blob/iblobbackend.h` — full interface
- `src/blob/blobdomainplugin.h`
- `src/types/backendrecord.h` — universal item type
- `src/types/synctypes.h:393` — ExecutionOverride and IDMapping
- `src/engine/syncenginefuture.h` — CancellationReason enum + values
- `src/calendar/calendarbaselinestore.h` and `src/blob/blobbaselinestore.h`
  to compare baseline strategies
- `src/sinks/rawfilesbackend.h` + `genericsqlitebackend.h` — how do
  they fake calendar virtuals?
- PlanStan's actual surface (K.0.3)
- WildPalms's adapter layer + plugin contract (for K.7 design later)
