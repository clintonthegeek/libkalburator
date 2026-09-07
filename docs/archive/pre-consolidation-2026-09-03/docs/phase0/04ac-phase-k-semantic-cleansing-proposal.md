# Phase K — semantic cleansing proposal

**Status:** Proposal — 2026-05-09. Sibling document to
`04ab-phase-k-engine-generalization-design.md`. Not yet folded into
the K plan; awaiting user review.

**Question this proposal answers:** the K design doc handles the
*structural* generalization (lifting calendar virtuals off the
backend base, capability interfaces for sync-situation flexibility).
This proposal handles the *vocabulary* — the names of classes,
interfaces, and namespaces. Names that lie about what a thing does
are a permanent tax on every reader. Code is read more than written.

This proposal stands on its own — even if Phase K is abandoned, the
semantic problems are real — but the rename pass should be **woven
into** the K phases (specifically K.4 and K.5), since those phases
already touch all the affected code. Doing the renames as a
separate post-K cleanup would mean reading and modifying the same
files twice.

---

## 1. Why vocabulary matters

The library was extracted from PlanStan, a calendar app, and
generalized into a multi-domain sync engine. Its evolution is
visible in the names: things that started life as
calendar-and-PlanStan-specific kept their names as their meaning
broadened, and now those names misrepresent the architecture.

Four categories of harm:

1. **Names that mismatch intent.** `IBlobBackend` is *not* an
   interface for binary blobs anymore — it's the genuinely-generic
   record-level backend interface. Every reader has to learn
   "ignore the name, this is the real abstraction." Multiply across
   readers and time and it costs.
2. **Names that overload.** `Kalburator::Sync` is currently the
   namespace for the engine, the backend interfaces, the data types,
   the baseline stores, the conflict types, the providers, the ID
   mapping store, and the host contract. Seven distinct concepts
   under one tag means readers have to scope every name mentally.
3. **Names whose location lies.** `ISyncHost` lives at
   `src/calendar/isynchost.h` despite the class's own docstring
   saying "G.9.a narrows this interface to ~7 generic methods.
   Calendar-typed methods are deprecated and will be deleted in
   Task 67." A directory placement that contradicts the file's
   own commentary is a tell that the migration was abandoned
   mid-flight.
4. **Prefix incoherence.** The codebase mixes `I`-prefixed
   interfaces (`.NET` style: `IBlobBackend`, `IRecordWriter`,
   `IProvider`), `Q`-prefixed namespaces (`QSyncCore` — meaning
   unclear, since *every* type in this library is Qt-based), bare
   abstract classes (`DomainPlugin` is pure-virtual but unprefixed),
   and project-prefixed class names that duplicate their namespace
   (`KalburatorDomainContacts` inside `Kalburator::Contacts`).
   Mixing three interface conventions in one library is itself
   the problem — readers have to learn to ignore the prefix
   pattern because it doesn't carry information.

The structural Phase K work in `04ab-` removes calendar leaks from
the *behavior*. This proposal removes them from the *vocabulary*.
Both are necessary — names that lie about behavior aren't fixed by
fixing the behavior alone.

---

## 2. Inventory — what's misnamed and how

### 2.1 Critical — names that mismatch intent

#### `Sync::IBlobBackend` is the generic backend interface

Lives at `src/blob/iblobbackend.h`. Pure virtual. Methods:
`loadRecords / loadRecord / createRecord / updateRecord /
deleteRecord / modifiedSince / deletedSince / availableCollections /
collectionInfo / createCollection / batch …`. Operates on
`BackendRecord` (universal record type). No calendar, no contacts,
no blob-specific assumption. **It is the actual generic backend
abstraction in this codebase.**

The "Blob" prefix dates to a moment when this interface was
specifically for binary opaque payloads. That moment is gone. Other
domains (calendar, contacts, memo, todo) all use `BackendRecord`
and route through this same interface. The name lies.

**Rename:** `Sync::IBlobBackend` → `Sync::ISyncBackend`. (The name
`SyncBackend` is currently held by the calendar-typed legacy class;
it's freed by Phase K.4's lift, and is the most descriptive name
for a generic sync backend interface in a generic sync library.)

#### `Sync::SyncBackend` is calendar-typed

Lives at `src/calendar/syncbackend.h`. Includes KCalendarCore,
declares calendar-typed pure virtuals, has 660 lines of mostly
calendar-shaped surface. Per K.4 it slims to ~80 lines of QObject +
operation-tracking + generic signals.

**Rename (per K.4):** `Sync::SyncBackend` → `Sync::SyncBackendBase`.
File moves from `src/calendar/syncbackend.{h,cpp}` to
`src/sync/syncbackendbase.{h,cpp}`. The "Base" suffix reflects that
this is the QObject-derived concrete base class that backends
inherit; the pure-virtual interface is the renamed `ISyncBackend`.

**The pair:**
- `ISyncBackend` (pure virtual interface, no QObject) — replaces `IBlobBackend`.
- `SyncBackendBase` (QObject + ISyncBackend; operation tracking; generic signals) — replaces `SyncBackend`.

This is a clean Qt pattern: pure-virtual interface for type
contracts, concrete base class for shared QObject machinery.

#### `BlobBaselineStore` is generic

Already covered in K.5. Renaming to `BaselineStore`. Moves from
`src/journal/` to `src/sync/`. Class name `BlobBaselineStore` →
`BaselineStore`.

The `journal` directory currently has both `BaselineStore` (which
is *not* a journal) and `IDMappingStore` (which *is* a per-record
identity-translation journal). Recommend splitting:
- `src/sync/baselinestore.{h,cpp}` (current `journal/blobbaselinestore`)
- `src/sync/idmappingstore.{h,cpp}` (current `journal/idmappingstore`)
- Or: `src/storage/baselinestore.{h,cpp}` and `src/storage/idmappingstore.{h,cpp}` — `storage` is the more descriptive directory name.

I'd lean **`src/storage/`** — these are persistent stores, not engine
or backend code. Currently the directory is misnamed "journal" (a
journal implies append-only history; these are mutable key-value
stores).

#### `Sync::QSyncCore::*` sub-namespace is misleading

Currently contains `ConflictPolicy`, `ConflictHandlerRegistry`,
`ConflictStore`, `ConflictRecord`. The "Q" prefix usually means
"Qt-based" or "Qt-flavored," but **everything in libkalburator is
Qt-based** — `Q` doesn't distinguish. The name appears to be a
historical artifact from a separately-developed `qsynccore` library
that was lifted into libkalburator wholesale.

Files involved:
- `src/conflict/conflictpolicy.h` (`namespace Kalburator::Sync::QSyncCore`)
- `src/conflict/conflicthandlerregistry.h` (same)
- `src/conflict/conflictstore.h` (same)
- `src/conflict/conflictrecord.h` (same)
- `src/journal/baselinestore.cpp` (uses `Kalburator::Sync::QSyncCore`)

External use: `SyncEngine::conflictRegistry()` returns
`Kalburator::Sync::QSyncCore::ConflictHandlerRegistry*`. This is
visible to consumers, so it's a public-API rename.

**Rename:** collapse `Kalburator::Sync::QSyncCore::*` →
`Kalburator::Conflict::*`. The classes' purpose is conflict handling;
`Conflict` is the right namespace tag. The directory `src/conflict/`
already matches.

#### `ICalendarCollection` is calendar-shaped, lives in `Sync`

`src/types/icalendarcollection.h`, namespace `Kalburator::Sync`.
Per its docstring: "Host contract for the sync engine's view of a
calendar collection." Methods are calendar-typed (`MemoryCalendar*`).
After K.4 the engine no longer needs this interface (writer doesn't
look up MemoryCalendar from a host-resident collection any more).
The interface remains useful to the **calendar plugin** for its own
lookups.

**Move and rename:** `Kalburator::Sync::ICalendarCollection`
(`src/types/icalendarcollection.h`) →
`Kalburator::Calendar::ICalendarRegistry`
(`src/calendar/icalendarregistry.h`). The "Collection" name was
overloaded with the generic `CollectionInfo` concept;
`ICalendarRegistry` is more accurate (it's a registry that maps
calendarIds to MemoryCalendars).

#### `ISyncHost` lives in `src/calendar/`

`src/calendar/isynchost.h`, namespace `Kalburator::Sync`. Per the
file's own docstring: "G.9.a narrows this interface to ~7 generic
methods. Calendar-typed methods are deprecated and will be deleted
in Task 67." The generic methods (`syncStarted`, `syncFinished`,
`recordChanged`, `resolveConflict`, `progressChanged`,
`phaseChanged`, `errorOccurred`) are already in place. The
deprecated calendar-typed methods are still present (Task 67 was
deferred indefinitely).

**Move + clean:** `Kalburator::Sync::ISyncHost`
(`src/calendar/isynchost.h`) → `Kalburator::Sync::ISyncHost`
(`src/sync/isynchost.h`). Delete the deprecated calendar-typed
methods in the same change. This is the actual long-overdue Task
67 from G.9.a.

The class name `ISyncHost` is fine — generic, descriptive.

### 2.2 Medium — inconsistent class naming

#### `KalburatorDomain*` redundant prefix

```
src/contacts/contactsdomainplugin.h   class KalburatorDomainContacts
src/blob/blobdomainplugin.h            class KalburatorDomainBlob
```

The class name `KalburatorDomainContacts` lives inside namespace
`Kalburator::Contacts`. The leading `Kalburator` is redundant with
the namespace, the trailing word reverses order with the file name
(`contactsdomainplugin.h`), and neither matches the calendar
plugin's name (`Kalburator::Calendar::CalendarDomainPlugin`).

**Rename:**
- `Kalburator::Contacts::KalburatorDomainContacts` → `Kalburator::Contacts::ContactsDomainPlugin`
- `Kalburator::Blob::KalburatorDomainBlob` → `Kalburator::Blob::BlobDomainPlugin`

Matches `Kalburator::Calendar::CalendarDomainPlugin` (already
correctly named) and matches the file names. **One pattern across
all domain plugins.**

Check for similar elsewhere: search `Kalburator::Memo::`,
`Kalburator::Todo::` for parallel violations and apply the same
rule.

#### `Sinks::*` namespace name

`src/sinks/` directory contains `RawFilesBackend` and
`GenericSqliteBackend`, both universal multi-shape storage
backends. "Sinks" suggests one-way write; these read and write.

Two options:
- **Keep** `Kalburator::Sinks` with a clarifying header comment.
  Inertia argument: it's a small directory, the metaphor (data sink
  that absorbs any shape) is defensible.
- **Rename** to `Kalburator::Universal` or `Kalburator::AnyShape`
  or `Kalburator::Storage`. Cleaner, but adds churn that may not
  pay back.

**Recommendation:** rename to `Kalburator::Universal`. Two reasons:
(a) `nativeShapes()` returning `Shape::Any()` is the defining
characteristic of these backends; "universal" describes that
directly; (b) once `BaselineStore` and `IDMappingStore` move to
`src/storage/`, that name is taken, and `Storage` would be
ambiguous. `Universal` is unambiguous.

`src/sinks/` → `src/universal/`.

### 2.3 Medium — namespace overload

`Kalburator::Sync` currently owns:

- Engine: `SyncEngine`, `SyncEngineWorker`, `SyncEngineFuture`,
  `MappingScheduler`, `ExecutionOverride`, `CancellationReason`
- Backend interfaces & base: `IBlobBackend`, `SyncBackend`,
  `BackendRegistry`, `BackendCapabilities`, `BackendRecord`,
  `CollectionInfo`, `RecurrenceCapabilities`, `RecurrenceLossInfo`
- Sync data types: `SyncMapping`, `SyncResult`, `SyncStats`,
  `SyncDiff`, `SyncChange`, `ConflictResolution`, `ConflictType`,
  `ConflictInfo`, `SyncMode`
- Conflict: `ConflictManager`, `ConflictHandlerRegistry` (via
  `QSyncCore`), `ConflictPolicy`, `ConflictStore`, `ConflictRecord`,
  `SyncConflictStore`
- Storage: `BlobBaselineStore`, `CalendarBaselineStore`,
  `IDMappingStore`, `IDMapping`
- Host contract: `ISyncHost`, `ISyncConfigStore`,
  `ICalendarCollection`
- Operations: `SyncOperation`, `FetchOperation`, `PushOperation`,
  `DeleteOperation`, `SyncTransaction`
- Providers: `IProvider`, `ProviderManager`, `CalDavProvider`,
  `CardDavProvider`
- Capabilities & metadata: `BackendCapabilities`,
  `CalendarBackendBinding`, `DiscoveredCalendar`, `CalendarType`,
  `CalendarMetadataManager`, `LogicalCalendar`

That's eight conceptually-distinct concept clusters. The
`Kalburator::Sync` tag stops disambiguating anything once you
have a hundred classes inside it.

**Proposal — split into purposeful sub-namespaces:**

| Sub-namespace | Owns | From `src/` |
|---|---|---|
| `Kalburator::Engine` | SyncEngine, SyncEngineWorker, SyncEngineFuture, MappingScheduler, EngineDiff | `src/engine/` |
| `Kalburator::Sync` | SyncMapping, SyncResult, SyncStats, SyncDiff, SyncChange, SyncMode, ConflictResolution, ConflictType, ConflictInfo, ExecutionOverride, CancellationReason | `src/types/` (most of synctypes.h; possibly rename to `synctypes.h`) |
| `Kalburator::Backend` | ISyncBackend (was IBlobBackend), SyncBackendBase (was SyncBackend), BackendRegistry, BackendRecord, BackendCapabilities, CollectionInfo | `src/backend/` (new dir, absorbs blob/iblobbackend, blob/localblobbackend, blob/mockblobbackend, calendar/backendregistry, calendar/backendcapabilities, types/backendrecord, types/collectioninfo) |
| `Kalburator::Conflict` | ConflictPolicy, ConflictHandlerRegistry, ConflictStore, ConflictRecord, ConflictManager, SyncConflictStore (was QSyncCore::*) | `src/conflict/` |
| `Kalburator::Storage` | BaselineStore (was BlobBaselineStore), IDMappingStore, IDMapping | `src/storage/` (renamed from journal/) |
| `Kalburator::Provider` | IProvider, ProviderManager, CalDavProvider, CardDavProvider, CalDavConfigWidget, CardDavConfigWidget, *capability discoveries* | `src/provider/` (renamed from sync/) |
| `Kalburator::Operation` | SyncOperation, FetchOperation, PushOperation, DeleteOperation | (within `src/sync/` or `src/operation/`) |
| `Kalburator::Calendar` | CalendarDomainPlugin, CalendarPluginWriter, ICalendarRegistry (was ICalendarCollection), CalendarManager, CalendarBackendBinding, DiscoveredCalendar, CalendarType, RecurrenceCapabilities, etc. | `src/calendar/` |
| `Kalburator::Contacts` | ContactsDomainPlugin (was KalburatorDomainContacts), RemoteContactsBackend, vCard transformations & differs/mergers | `src/contacts/` |
| `Kalburator::Blob` | BlobDomainPlugin (was KalburatorDomainBlob) | `src/blob/` |
| `Kalburator::Memo`, `Kalburator::Todo` | (existing) | `src/memo/`, `src/todo/` |
| `Kalburator::Universal` | RawFilesBackend, GenericSqliteBackend (was Sinks) | `src/universal/` (renamed from sinks/) |
| `Kalburator::Shape` | (unchanged) — DomainPlugin, IRecordWriter, IRecordDiffer, IRecordMerger, Shape, TransformationRegistry, etc. | `src/shape/` |
| `Kalburator::Transcoding` | (unchanged) — TranscodingPlan, TranscodingRouter, TranscodingRegistry, RruleTranscoder, etc. | `src/transcoding/` |
| `Kalburator::Discovery` | (existing) | `src/discovery/` |
| `Kalburator::Sync::Host` *or* `Kalburator::Host` | ISyncHost, ISyncConfigStore | `src/host/` (new) or `src/sync/host/` |

This is a meaningful restructure but an honest one: each sub-namespace
gets a single conceptual job. The `using namespace Kalburator::Sync;`
patterns in PlanStan (with their `TODO(phase-c-cleanup)` markers)
naturally resolve into more-specific imports.

**Most important corollary**: `Kalburator::Sync` shrinks to *only*
the data types describing a sync. That's its purest meaning. The
engine moves to `Engine`, backends to `Backend`, etc.

### 2.4 Critical — prefix and capitalization conventions

The codebase mixes three incompatible interface-naming conventions:
- **`I` prefix** (Microsoft / .NET): `IBlobBackend`, `IRecordWriter`,
  `IRecordDiffer`, `IRecordMerger`, `ISyncHost`, `ISyncConfigStore`,
  `ICalendarCollection`, `IProvider`, `IBlobBackend`.
- **`Q` prefix** (Qt-flavored, but actually misleading because
  *everything* in this library is Qt-based): `QSyncCore` namespace
  (already covered for removal).
- **No prefix** for everything else, including some pure-virtual
  abstract classes that *should* be flagged as abstract:
  `DomainPlugin` (pure virtual, no prefix), `SyncOperation` (abstract
  base, no prefix).

This is genuinely incoherent. Qt itself does not use the `I` prefix;
Qt uses **`QAbstract*`** for QObject-derived abstract bases (e.g.
`QAbstractItemModel`, `QAbstractButton`, `QAbstractFileEngine`) and
**no prefix** for non-QObject pure-virtual interfaces (e.g.
`QIODeviceBase`, `QRunnable`). Mixing `I` prefixes into a Qt
codebase reads as "we couldn't decide what framework we're in."

**Convention to adopt across libkalburator:**

| Kind | Convention | Rationale |
|---|---|---|
| QObject-derived abstract base (pure virtuals + shared signals/machinery) | `Abstract*` prefix, no `I` | Qt convention. Reads as `AbstractSyncBackend`, `AbstractSyncOperation`. |
| Non-QObject pure-virtual interface (returned from factories, dispatched via `dynamic_cast`, no QObject machinery) | **No prefix.** Descriptive name. | Qt 6 convention (`QIODeviceBase`, `QRunnable`). Pure-virtual is an implementation detail, not part of the role. |
| Capability interface (small side-interface, opt-in, dispatched via `dynamic_cast`) | **No prefix.** Descriptive name. | Capability is the role; whether it's an interface is an implementation detail. |
| Concrete class | No prefix | Standard. |

**Drop the `I` prefix from every interface.** Names below assume
this convention.

#### Capitalization for spec-named types (`vCard*`, `iCal*`)

C++ class names start with uppercase by convention. The spec names
`vCard` and `iCal` are written with a lowercase first letter in
their own specifications, but C++ class names should not. Current
state is mixed:

- `vCard3to4Transformation` — lowercase first letter, violates C++
  naming convention.
- `vCardDiffer`, `vCardMerger`, `vCardProperties` — same.
- `iCalProperties`, `iCalRecordDiffer`, `iCalRecordMerger` — likely
  same (file names are lowercase; need to check class name).

KCalendarCore precedent: `KCalendarCore::ICalFormat`. The standard
upper-camel-case form is `ICal*` and `VCard*`.

**Convention:** spec-named classes use upper-camel: `ICalFormat`,
`VCard4Properties`, `VCard3To4Transform`. The acronym `ICAL` /
`VCARD` is not used (Qt convention is `Url` not `URL`, `Json` not
`JSON`).

#### Backend names — drop the calendar-specific qualifier

`Sync::RemoteCalendarBackend` is the abstract CalDAV/CardDAV
transport base; `RemoteContactsBackend` extends it. The "Calendar"
in the parent name lies — it handles contacts too.

**Rename:**
- `Sync::RemoteCalendarBackend` → `Backend::AbstractDavBackend`
  (CalDAV/CardDAV shared base).
- New concrete subclass `Backend::CalDavBackend` (calendar-shape).
- `Sync::RemoteContactsBackend` → `Backend::CardDavBackend`
  (contacts-shape, sibling of `CalDavBackend`).

Capitalization: `CalDav` and `CardDav` per Qt conventions (`Url`
not `URL`, etc.). Some currently-existing files spell it `CalDAV` /
`CardDAV` — those normalize to `CalDav` / `CardDav` in class names
but `caldav` / `carddav` in file names (lowercase, as today).

#### Should `IBlobBackend` and `SyncBackend` collapse to ONE class?

Currently:
- `IBlobBackend` is pure-virtual, no QObject. Defined in
  `src/blob/iblobbackend.h`.
- `SyncBackend` is QObject + inherits IBlobBackend. Adds
  calendar-typed virtuals, signals, operation tracking.

The split was justified at the time by the comment in
`iblobbackend.h`: "The split is necessary so `SyncBackend`, which
already inherits QObject, can add IBlobBackend as a second base
without creating a QObject diamond."

After K.4's calendar lift, `SyncBackend` slims to `SyncBackendBase`:
QObject + the `IBlobBackend` virtuals + operation tracking +
generic signals. **There is no longer any reason for two classes.**
Every concrete backend (`LocalBackend`, `RemoteCalDavBackend`,
`RemoteCardDavBackend`, `RawFilesBackend`, etc.) is QObject-derived
already. Zero non-QObject implementations exist; nothing breaks if
the abstract base is QObject-derived.

**Recommend collapsing:** drop the interface/base split entirely,
have a single `Backend::AbstractSyncBackend` (QObject + pure
virtuals + shared machinery). Concrete backends inherit it
directly. This is the **Qt-idiomatic** pattern: see
`QAbstractItemModel`, which is QObject + pure virtuals; concrete
models inherit it.

Result: `IBlobBackend` and `SyncBackend` both retire; `AbstractSyncBackend`
takes their place. Both names freed; neither resurrected.

This is a stronger position than §2.1 took (which proposed
`ISyncBackend` + `SyncBackendBase` as a pair). The single-class
form is cleaner and matches Qt convention. **§3 rename table
below adopts this.**

### 2.5 Low — docstring rot

Already covered in K.6 of the structural design:
- `ExecutionOverride` docstring drops "WildPalms's Tools-menu Copy
  Palm→PC" framing.
- `CancellationReason::ResourceLost` docstring drops "Palm cradle
  disconnect."
- `IDMapping::sourceCategory` docstring drops "Palm-shaped backends
  only."

Stays as K.6 work.

---

## 3. Concrete rename table — flat list for grep verification

Conventions adopted (per §2.4):
- **Drop `I` prefix everywhere.** Pure-virtual non-QObject interfaces
  use bare descriptive names.
- **`Abstract*` prefix** for QObject-derived abstract bases
  (Qt-idiomatic).
- **Drop `Q` prefix** from the `QSyncCore` namespace.
- **Spec-name capitalization** normalized: `vCard*` → `VCard*`,
  `iCal*` → `ICal*`, `CalDAV` / `CardDAV` → `CalDav` / `CardDav`
  in class names.
- **`IBlobBackend` and `SyncBackend` collapse** into a single
  QObject-derived `Backend::AbstractSyncBackend` (per §2.4 final
  recommendation).

### 3.1 Backend layer

| Current | Renamed/moved to |
|---|---|
| `Sync::IBlobBackend` (`src/blob/iblobbackend.h`) | **collapsed** into `Backend::AbstractSyncBackend` (`src/backend/abstractsyncbackend.h`) |
| `Sync::SyncBackend` (`src/calendar/syncbackend.h`) | **collapsed** into `Backend::AbstractSyncBackend` (`src/backend/abstractsyncbackend.h`) |
| `Sync::BackendRegistry` (`src/calendar/backendregistry.h`) | `Backend::BackendRegistry` (`src/backend/backendregistry.h`) |
| `Sync::BackendCapabilities` | `Backend::BackendCapabilities` |
| `Sync::BackendRecord` (`src/types/backendrecord.h`) | `Backend::BackendRecord` (`src/backend/backendrecord.h`) |
| `Sync::CollectionInfo` (`src/types/collectioninfo.h`) | `Backend::CollectionInfo` (`src/backend/collectioninfo.h`) |
| `Sync::RemoteCalendarBackend` (`src/calendar/remotecalendarbackend.h`) | `Backend::AbstractDavBackend` + concrete `Backend::CalDavBackend` (separated) |
| `Sync::RemoteContactsBackend` (`src/contacts/remotecontactsbackend.h`) | `Backend::CardDavBackend` |
| `Sync::LocalBackend` (`src/calendar/localbackend.h`) | `Backend::LocalCalendarBackend` (the calendar-typed local store) — disambiguates from `Backend::LocalBlobBackend` |
| `Sync::LocalBlobBackend` (`src/blob/localblobbackend.h`) | `Backend::LocalBlobBackend` (file moves; name unchanged) |
| `Sync::MockBlobBackend` | `Backend::MockBlobBackend` (test fixture; unchanged name, file moves) |
| `Sinks::RawFilesBackend` (`src/sinks/`) | `Universal::RawFilesBackend` (`src/universal/`) |
| `Sinks::GenericSqliteBackend` | `Universal::GenericSqliteBackend` |

New backend capability interfaces (introduced by K.1, no prefix):
- `Backend::ChangeDetection` — collection-revision capability.
- `Backend::ResourceLinearization` — linearization-key capability.
- `Backend::RecordRevision` — per-record revision capability (K.5+, optional).

(All non-QObject pure-virtual; bare names per the convention.)

### 3.2 Engine

| Current | Renamed/moved to |
|---|---|
| `Sync::SyncEngine` (`src/engine/syncengine.h`) | `Engine::SyncEngine` (same file) |
| `Sync::SyncEngineWorker` | `Engine::SyncEngineWorker` |
| `Sync::SyncEngineFuture` | `Engine::SyncEngineFuture` |
| `Sync::MappingScheduler` | `Engine::MappingScheduler` |
| `Sync::CancellationReason` | `Engine::CancellationReason` |
| `Sync::SyncOperation` (abstract base) | `Engine::AbstractSyncOperation` |
| `Sync::FetchOperation` | `Engine::FetchOperation` |
| `Sync::PushOperation` | `Engine::PushOperation` |
| `Sync::DeleteOperation` | `Engine::DeleteOperation` |
| `Sync::EngineDiff` (`src/engine/enginediff.h`) | `Engine::EngineDiff` |

### 3.3 Conflict

| Current | Renamed/moved to |
|---|---|
| `Sync::QSyncCore::ConflictPolicy` (`src/conflict/conflictpolicy.h`) | `Conflict::ConflictPolicy` (same file) |
| `Sync::QSyncCore::ConflictHandlerRegistry` | `Conflict::ConflictHandlerRegistry` |
| `Sync::QSyncCore::ConflictStore` | `Conflict::ConflictStore` |
| `Sync::QSyncCore::ConflictRecord` | `Conflict::ConflictRecord` |
| `Sync::ConflictManager` | `Conflict::ConflictManager` |
| `Sync::SyncConflictStore` | `Conflict::SyncConflictStore` |

### 3.4 Storage (was Journal)

| Current | Renamed/moved to |
|---|---|
| `Sync::BlobBaselineStore` (`src/journal/blobbaselinestore.h`) | `Storage::BaselineStore` (`src/storage/baselinestore.h`) |
| `Sync::IDMappingStore` (`src/journal/idmappingstore.h`) | `Storage::IDMappingStore` (`src/storage/idmappingstore.h`) |
| `Sync::IDMapping` | `Storage::IDMapping` |
| `Sync::CalendarBaselineStore` | (deleted by K.5; subsumed into `Storage::BaselineStore`) |

### 3.5 Provider (was within `Kalburator::Sync` via `src/sync/`)

| Current | Renamed/moved to |
|---|---|
| `Sync::IProvider` (`src/sync/iprovider.h`) | `Provider::Provider` (`src/provider/provider.h`) |
| `Sync::ProviderManager` | `Provider::ProviderManager` |
| `Sync::CalDavProvider` | `Provider::CalDavProvider` |
| `Sync::CardDavProvider` | `Provider::CardDavProvider` |
| `Sync::CalDavConfigWidget` | `Provider::CalDavConfigWidget` |
| `Sync::CardDavCapabilityDiscovery` | `Provider::CardDavCapabilityDiscovery` |

Note: `Provider::Provider` (interface) reads slightly redundantly,
but every alternative tested worse. `Provider::Service` would
imply server-side; `Provider::Backend` collides with the backend
namespace. Accept the redundancy.

### 3.6 Host (was within `src/calendar/`)

| Current | Renamed/moved to |
|---|---|
| `Sync::ISyncHost` (`src/calendar/isynchost.h`) | `Host::SyncHost` (`src/host/synchost.h`) — calendar-typed methods deleted (long-overdue Task 67) |
| `Sync::ISyncConfigStore` | `Host::SyncConfigStore` (same dir) |

### 3.7 Calendar domain

| Current | Renamed/moved to |
|---|---|
| `Sync::ICalendarCollection` (`src/types/icalendarcollection.h`) | `Calendar::CalendarRegistry` (`src/calendar/calendarregistry.h`) |
| `Calendar::CalendarDomainPlugin` | (unchanged; correctly named) |
| `Calendar::CalendarPluginWriter` | `Calendar::CalendarRecordWriter` (matches `Shape::RecordWriter` after I-drop) |
| `Sync::iCalRecordDiffer` (file `icalrecorddiffer.h`) | `Calendar::ICalRecordDiffer` (capitalize spec prefix) |
| `Sync::iCalRecordMerger` | `Calendar::ICalRecordMerger` |
| `Sync::iCalProperties` (file `icalproperties.h`) | `Calendar::ICalProperties` |

### 3.8 Contacts domain

| Current | Renamed/moved to |
|---|---|
| `Contacts::KalburatorDomainContacts` | `Contacts::ContactsDomainPlugin` |
| `Contacts::vCard3to4Transformation` | `Contacts::VCard3To4Transform` |
| `Contacts::vCardDiffer` | `Contacts::VCardDiffer` |
| `Contacts::vCardMerger` | `Contacts::VCardMerger` |
| `Contacts::vCardProperties` | `Contacts::VCardProperties` |

### 3.9 Other domains

| Current | Renamed/moved to |
|---|---|
| `Blob::KalburatorDomainBlob` | `Blob::BlobDomainPlugin` |
| `Memo::*` | (audit during K.5.5; apply same conventions) |
| `Todo::*` | (audit during K.5.5; apply same conventions) |
| `Todo::iCalVTodoMerger` | `Todo::ICalVTodoMerger` |

### 3.10 Shape layer (no prefix; non-QObject interfaces)

| Current | Renamed/moved to |
|---|---|
| `Shape::IRecordWriter` (`src/shape/irecordwriter.h`) | `Shape::RecordWriter` (`src/shape/recordwriter.h`) |
| `Shape::IRecordDiffer` | `Shape::RecordDiffer` |
| `Shape::IRecordMerger` | `Shape::RecordMerger` |
| `Shape::DomainPlugin` | (unchanged; correctly bare-named) |

### 3.11 What stays as `Kalburator::Sync::*`

After the split, `Kalburator::Sync` becomes purely the **value
vocabulary** of sync operations:

- `SyncMapping`, `SyncResult`, `SyncStats`, `SyncDiff`, `SyncChange`,
  `SyncMode`, `ConflictResolution`, `ConflictType`, `ConflictInfo`,
  `ExecutionOverride`, `TranscodingPlan`.

These describe sync operations *as data* — what to sync, what
changed, what conflicted, what resulted. That's a coherent
namespace meaning. Approximately `synctypes.h` plus a few
neighbours.

---

## 4. How this folds into Phase K

**Recommendation: weave the renames into K.4 and K.5.** Adding a
separate "K.4.5 — semantic cleansing" phase would mean re-touching
every backend file twice (once for the K.4 lift, once for the
namespace move). Doing them together is one pass per file.

### Adjustments to K.4 (SyncBackend lift, now also the I-drop / collapse)

K.4 in the original design just lifted calendar virtuals off
`SyncBackend`. With the convention adopted, K.4 also collapses
`IBlobBackend` and `SyncBackend` into a single `Backend::AbstractSyncBackend`:

- `src/blob/iblobbackend.h` and `src/calendar/syncbackend.{h,cpp}`
  collapse into `src/backend/abstractsyncbackend.{h,cpp}`. Class
  name: `Backend::AbstractSyncBackend` (QObject + pure virtuals +
  shared signals/operation tracking).
- All concrete backends inherit `AbstractSyncBackend` directly. The
  current `: public QObject, public IBlobBackend` two-base pattern
  goes away.
- `src/calendar/backendregistry.{h,cpp}` → `src/backend/backendregistry.{h,cpp}`.
- `src/calendar/backendcapabilities.{h,cpp}` → `src/backend/backendcapabilities.{h,cpp}`.
- `src/types/backendrecord.h` → `src/backend/backendrecord.h`.
- `src/types/collectioninfo.h` → `src/backend/collectioninfo.h`.
- `src/calendar/remotecalendarbackend.{h,cpp}` splits into
  `src/backend/abstractdavbackend.{h,cpp}` (the shared CalDAV /
  CardDAV transport base) plus `src/backend/caldavbackend.{h,cpp}`
  (the calendar-shape concrete).
- `src/contacts/remotecontactsbackend.{h,cpp}` → `src/backend/carddavbackend.{h,cpp}`.
- `src/calendar/localbackend.{h,cpp}` → `src/backend/localcalendarbackend.{h,cpp}`.
- `src/blob/localblobbackend.{h,cpp}` → `src/backend/localblobbackend.{h,cpp}`.
- New capability interface files: `src/backend/changedetection.h`,
  `src/backend/resourcelinearization.h`. Bare names (no `I` prefix).

K.4 verification gates (extended):
- `grep -rn '<KCalendarCore' src/backend/` returns empty.
- `grep -rn 'class IBlobBackend\b\|class SyncBackend[^B]' src/ tests/` returns empty.
- `grep -rn 'Kalburator::Sync::IBlobBackend\|Kalburator::Sync::SyncBackend' src/ tests/` returns empty.
- `grep -rn 'IBlobBackend\b' src/ tests/` returns empty.

Forwarding shims (one phase-tag's worth): leave
`src/blob/iblobbackend.h` and `src/calendar/syncbackend.h` as
one-line `#include "../backend/abstractsyncbackend.h"` files with
`using IBlobBackend = Backend::AbstractSyncBackend;` and `using
SyncBackend = Backend::AbstractSyncBackend;` aliases. PlanStan and
WildPalms get cutover commits in their own worktrees in the same
K.4 task group; shims delete in K.6.

### Adjustments to K.5 (baseline unification + storage move)

Already changes:
- `BlobBaselineStore` → `BaselineStore` (class rename).

Add to K.5:
- File moves from `src/journal/` to `src/storage/`.
- `IDMappingStore` and `IDMapping` move with it (storage is the
  proper home; journal was a misnomer — these are mutable key-value
  stores, not append-only journals).
- Namespace becomes `Storage::*`.
- Existing `src/journal/` directory deletes (or becomes a single
  README explaining the move).

### New phase K.5.5 — full convention sweep

A small phase to land the remaining renames that aren't forced by
K.4 / K.5. Mostly mechanical sed.

**Namespace moves:**
- `Sync::QSyncCore::*` → `Conflict::*` (drop the Q prefix; collapse
  the sub-namespace).
- `Sync::SyncEngine`, `SyncEngineWorker`, `SyncEngineFuture`,
  `MappingScheduler`, `CancellationReason`, `EngineDiff`,
  `SyncOperation`, `FetchOperation`, `PushOperation`,
  `DeleteOperation` → `Engine::*`.
- `Sync::ConflictManager`, `Sync::SyncConflictStore` → `Conflict::*`.
- `Sync::IProvider`, `ProviderManager`, `CalDavProvider`,
  `CardDavProvider`, `CalDavConfigWidget`,
  `CardDavCapabilityDiscovery` → `Provider::*`.
- `Sinks::*` → `Universal::*` (directory `src/sinks/` →
  `src/universal/`).
- `Sync::ISyncHost` (`src/calendar/isynchost.h`) → `Host::SyncHost`
  (`src/host/synchost.h`); delete deprecated calendar-typed methods
  — long-overdue Task 67 from G.9.a.
- `Sync::ISyncConfigStore` → `Host::SyncConfigStore`.
- `Sync::ICalendarCollection` (`src/types/icalendarcollection.h`)
  → `Calendar::CalendarRegistry`
  (`src/calendar/calendarregistry.h`).

**`I`-prefix drop (across the codebase):**
- `Shape::IRecordWriter` / `IRecordDiffer` / `IRecordMerger` →
  `Shape::RecordWriter` / `RecordDiffer` / `RecordMerger`.
- `Sync::IProvider` → `Provider::Provider` (handled by namespace
  move above — the new namespace makes `Provider::Provider` the
  destination).
- `Sync::ISyncHost` / `ISyncConfigStore` → `Host::SyncHost` /
  `SyncConfigStore` (handled above).
- Any other `I`-prefixed type found by grep
  (`grep -rn 'class I[A-Z]' src/`) gets renamed to drop the prefix.

**`Abstract`-prefix application** (Qt-style on QObject abstract bases):
- `Engine::SyncOperation` (currently abstract base — read code to
  confirm) → `Engine::AbstractSyncOperation`.
- Audit any other QObject abstract bases for the same treatment.

**Class renames:**
- `Contacts::KalburatorDomainContacts` → `Contacts::ContactsDomainPlugin`.
- `Blob::KalburatorDomainBlob` → `Blob::BlobDomainPlugin`.
- `Calendar::CalendarPluginWriter` → `Calendar::CalendarRecordWriter`
  (suffix matches `Shape::RecordWriter` post-I-drop).

**Spec-name capitalization fixes:**
- `Contacts::vCard3to4Transformation` → `Contacts::VCard3To4Transform`.
- `Contacts::vCardDiffer` / `vCardMerger` / `vCardProperties` →
  `VCardDiffer` / `VCardMerger` / `VCardProperties`.
- `Calendar::iCalRecordDiffer` / `iCalRecordMerger` /
  `iCalProperties` → `ICalRecordDiffer` / `ICalRecordMerger` /
  `ICalProperties`.
- `Todo::iCalVTodoMerger` → `Todo::ICalVTodoMerger`.

This phase is purely vocabulary alignment: no behavior change, no
algorithm change. Mechanical sed, build, verify.

Tag: `v0.34.5-phase-k5.5-naming`.

### K.6 docstring cleanup folds in unchanged

K.6 still does the consumer-named docstring rewrites; semantic
cleansing in K.4/K.5/K.5.5 doesn't subsume that work.

### Updated K verification gates (per-phase grep additions)

These additions to K's existing falsifiable contract:

After K.4:
- `grep -rn '<KCalendarCore' src/backend/` returns empty.
- `grep -rn 'class IBlobBackend\b\|class SyncBackend[^B]' src/ tests/` returns empty.
- `grep -rn 'Kalburator::Sync::IBlobBackend\|Kalburator::Sync::SyncBackend' src/ tests/` returns empty.

After K.5:
- `grep -rn 'class BlobBaselineStore\b\|Kalburator::Sync::BlobBaselineStore' src/ tests/` returns empty.
- `find src/journal -type f` returns empty (or just a README).

After K.5.5:
- `grep -rn 'QSyncCore::' src/ tests/` returns empty.
- `grep -rn 'class I[A-Z]' src/` returns empty (or only platform/Qt-imposed exceptions).
- `grep -rn 'KalburatorDomainContacts\|KalburatorDomainBlob' src/ tests/` returns empty.
- `find src/sinks -type f` returns empty (renamed to `src/universal/`).
- `grep -rn 'src/calendar/isynchost\|src/calendar/syncbackend\|src/types/icalendarcollection' src/ tests/` returns empty (forwarding shims removed).
- `grep -rn 'class vCard\|class iCal' src/ tests/` returns empty (capitalization fixes applied).

---

## 5. Risk

**Risk: namespace move breaks PlanStan + WildPalms includes everywhere.**
Mitigation: forwarding shims for one phase tag (so PlanStan and
WildPalms have a clean cutover window); both worktrees migrate in
the same K.4 / K.5 / K.5.5 commit groups (per the cross-repo
coordination model in CLAUDE.md). `verify-all.sh` green at every
tag.

**Risk: PlanStan's `using namespace Kalburator::Sync;` patterns hide
breakage.** Many PlanStan files do `using namespace Kalburator::Sync;`
with `TODO(phase-c-cleanup)` markers. After the namespace split,
those `using` lines no longer pull in everything they used to
(the engine, backends, conflict types are in different namespaces
now). The cutover replaces the wildcard `using` with specific imports.
This is a forced cleanup of PlanStan's deferred TODOs — net positive.

**Risk: `ISyncBackend` collision with anything WildPalms or PlanStan
already named.** Verified by grep in their src/: neither defines
`ISyncBackend` today. Safe.

**Risk: `BackendRecord` users assume `Kalburator::Sync::BackendRecord`;
moving it to `Backend::` breaks them.** Forwarding `using` declaration
in the old location during the migration window. PlanStan grep
shows BackendRecord references; cutover is mechanical sed.

**Risk: bikeshedding.** The naming choices in §3 are recommendations,
not absolutes. `Storage` could be `Persist`. `Universal` could be
`AnyShape`. `Operation` could fold into `Engine`. The user owns
the final choice.

---

## 6. What this proposal does NOT touch

- The library name `Kalburator`. Etymology: "Kalb" (German for
  calendar) + "urator" (curator). Carries calendar baggage but is
  largely opaque; renaming the library would cost far more than it
  gains.
- The class name `SyncTransaction` (and its `CreateIncidenceItem`
  / `UpdateIncidenceItem` / `DeleteIncidenceItem` items). These are
  calendar-internal — they live in `src/calendar/` and are scoped to
  the calendar plugin. Names are calendar-specific (Incidence) so
  they don't lie. No change.
- `SyncMapping`, `SyncResult`, `SyncStats`, `SyncDiff`,
  `ConflictResolution`, `ConflictType`, `SyncChange`, `SyncMode` —
  the genuine sync data types. Stay in `Kalburator::Sync`.
- `Shape::*`, `Transcoding::*`, `Discovery::*`, `Memo::*`, `Todo::*`
  — already coherent. No change.
- Method names within classes — too granular for this proposal.
  Local renames during K.4/K.5/K.5.5 file edits are case-by-case.

---

## 7. Locked answers (user, 2026-05-09)

The user's directive — "make it all coherent, uniform, and
meaningful" — locked the convention itself. Q-S1–Q-S6 are
scope/granularity questions; user's terse "answering your
questions" reply addressed only the structural design's Q1–Q5.
Per auto-mode, Q-S defaults to my recommendations:

1. **Q-S1 — Sweep scope.** libkalburator-only in K.4/K.5/K.5.5.
   PlanStan and WildPalms internal renames stay out of scope.
2. **Q-S2 — Namespace split granularity.** Full split (~12
   sub-namespaces under `Kalburator::`).
3. **Q-S3 — `Sinks` → `Universal`.**
4. **Q-S4 — Capability interfaces follow convention from day one.**
   K.1 plan spells them `Backend::ChangeDetection`,
   `Backend::ResourceLinearization`, `Backend::RecordRevision`.
5. **Q-S5 — Operations stay in `Kalburator::Engine`.** No
   separate `Operation` namespace.
6. **Q-S6 — Keep `Provider::Provider`.** Mild redundancy beats
   the alternatives (`Service`, `Source`, `Account` all worse).

If user wants to override any of these on review, K.5.5 is the
phase that lands them — single-pass mechanical sed, easy to
adjust before that tag.

---

## 8. Summary — what changes if approved

- One coherent vocabulary that reflects the architecture.
- One naming convention applied uniformly: no `I` prefix; `Abstract*`
  for QObject abstract bases (Qt-idiomatic); spec names in upper
  camel case (`ICal*`, `VCard*`, `CalDav*`, `CardDav*`); no
  `KalburatorDomain*` redundancy; no `QSyncCore` artifact
  sub-namespace.
- 12 namespaces, each with one job (`Engine`, `Backend`, `Conflict`,
  `Storage`, `Provider`, `Host`, `Sync` (data types only),
  `Universal`, `Calendar`, `Contacts`, `Blob`, `Memo`, `Todo`,
  `Shape`, `Transcoding`, `Discovery`).
- 2 directory renames (`journal/` → `storage/`, `sinks/` →
  `universal/`), 1 directory split (`blob/` + `calendar/` →
  `backend/` + `blob/` + `calendar/`), 1 directory creation
  (`host/`).
- `IBlobBackend` and `SyncBackend` collapse into a single
  QObject-derived `Backend::AbstractSyncBackend`. Concrete backends
  inherit it directly (Qt-idiomatic; matches `QAbstractItemModel`).
- `RemoteCalendarBackend` splits cleanly into `AbstractDavBackend`
  (CalDAV/CardDAV transport base) + `CalDavBackend` (calendar
  shape) + `CardDavBackend` (contacts shape).
- ~30 class renames + ~12 namespace moves + ~10 file moves.
  Mechanical via sed in K.5.5.
- Retires the 3-month-old `using namespace Kalburator::Sync; //
  TODO(phase-c-cleanup)` pattern in PlanStan — the post-split
  namespaces are specific enough that targeted `using`
  declarations are practical.
- The library reads as what it is: a multi-domain sync engine,
  with names that describe what each thing actually does, in a
  framework (Qt) whose conventions it follows.
