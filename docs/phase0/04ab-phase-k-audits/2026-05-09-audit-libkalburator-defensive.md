# libkalburator defensive audit — 2026-05-09

**Stance.** libkalburator was supposed to come out of this refactor as a clean,
domain-neutral sync library: a single `SyncEngine` parameterized over plugin
domains, dispatching opaque `BackendRecord`s through compiled shape pipelines.
Phase G's stated success criterion was "engine deals only in BackendRecord;
calendar diff/merge becomes IRecordDiffer/IRecordMerger registered for type
'calendar'." Phase Ia.5 / Ib.5 doubled down: "delete IDomainAdapter; strip
KCalendarCore from src/engine/." On paper that goal was reached. In practice,
WildPalms' shape — Palm-device cradle semantics, plugin-provided IBlobBackends,
RawFile-as-default-target, no host-side `ICalendarCollection` — has bent the
library back at the seams the refactor was supposed to harden. The core
abstractions read clean; the connective tissue around them is full of
WildPalms-shaped knees. PlanStan, the supposed "ideal" consumer, is small,
boring, and well-served. WildPalms is dictating the failure modes, the type
asymmetries, and the public API extensions. This audit catalogues the
intrusions.

## Top findings, ranked

### Finding 1: `CalendarPluginWriter` requires a host-installed `MemoryCalendar`, but the engine treats the writer interface as generic — and dispatches via `dynamic_cast`

**Severity:** critical
**Where:**
- `libkalburator/src/calendar/calendarplugin_writer.cpp:64-86` (the `apply()`
  null-collection / null-calendar guards)
- `libkalburator/src/engine/syncengine.cpp:2354-2382` (the `applyBatch` lambda
  that `dynamic_cast`s the writer to `CalendarPluginWriter*`)
- `libkalburator/src/types/icalendarcollection.h:28-54` (the host contract
  the writer reaches into)
- `FINDINGS.md` 2727-2769 — the team itself flagged "palm→caldav direction
  is structurally broken under the current guard semantics"

**What it is:** The post-Ia.5 engine speaks `IRecordWriter`, a generic
plugin-supplied apply hook. Its calendar implementation,
`CalendarPluginWriter::apply()`, hard-fails if `m_collection == nullptr` or if
the collection has no registered `MemoryCalendar*` for the target collection
id. PalmRuntime/WildPalms registers `MemoryCalendar`s only for Palm-side
slot ids (`palm:calendar/0`); when the engine writes Palm→CalDAV it asks the
writer to apply to collection id `"Personal"` (the CalDAV collection), the
guard fires, and the sync silently returns `success=false` without even
attempting a network PUT. The engine's `dispatchSync` then has to special-case
the calendar writer:

```cpp
auto *cw = dynamic_cast<Kalburator::Calendar::CalendarPluginWriter*>(writer);
if (cw) {
    cw->setCollection(m_collection);
    cw->setTranscodingPlan(plan);
    ...
}
```

(`syncengine.cpp:2363-2366`)

**Why it's a violation:** The engine is supposed to "deal only in
BackendRecord" and route through plugin-provided writers polymorphically. The
moment the engine has to `dynamic_cast` the writer to a calendar-specific
subclass to inject calendar-specific state (collection + transcoding plan), it
is no longer generic — it has special knowledge of the calendar domain. This
is a pure type-tag pattern, the same anti-pattern Phase G claimed to delete.
The `setCollection()` call is also racy with the writer's own thread
expectations, but that's a separate issue.

The deeper rot: `CalendarPluginWriter` reaches through `ICalendarCollection`
into a host-managed `MemoryCalendar*` to commit changes. That works for
PlanStan (the `Collection` class has a calendar per slot). It works for
WildPalms only because PalmRuntime now does an "eager preload" on connect
(`palmruntime.cpp:313-324`) that builds `MemoryCalendar`s per Palm slot purely
to placate the writer's null check. There is no such eager load for CalDAV
target collections, hence the `palm→caldav` failure documented in FINDINGS.

**What it costs PlanStan / a third consumer:** A third consumer has to
implement `ICalendarCollection` and pre-register a `MemoryCalendar` for every
collection it might ever sync *into*. That includes write-only sinks the host
never reads (CalDAV publishing, web-feed mirrors). For a Palm-style consumer
this is a 50-line eager-load workaround. For a server-side daemon with no
calendar UI, it's a dead-weight requirement that reveals the engine isn't
domain-neutral — it's calendar-shaped with a polymorphism varnish.

PlanStan is unaffected only because PlanStan happens to put a `MemoryCalendar`
behind every `Collection` slot anyway. That's an accident of PlanStan's UI,
not a property of the library.

**Suggested direction:** Move iCal serialize/parse into the writer
end-to-end; have it call `m_backend->pushItems()` (or its post-F2 equivalent)
directly without reaching into a host-owned `MemoryCalendar`. Delete
`CalendarPluginWriter::setCollection`. Delete the engine's `dynamic_cast` and
let the writer be opaque. `ICalendarCollection` becomes a host-side
convenience type, no longer a library contract.

---

### Finding 2: `RawFilesBackend` and `GenericSqliteBackend` live inside libkalburator but exist solely to satisfy WildPalms

**Severity:** major
**Where:**
- `libkalburator/src/sinks/rawfilesbackend.{h,cpp}`
- `libkalburator/src/sinks/genericsqlitebackend.{h,cpp}`
- `WildPalms/src/runtime/palmruntime.cpp:334-374` (the only producer of
  per-slot RawFiles default mappings)
- PlanStan: zero references to either type. Confirmed via
  `grep -r "RawFilesBackend\|GenericSqliteBackend\|Kalburator::Sinks" PlanStan/src` — empty.

**What it is:** The library ships two universal sink backends in `src/sinks/`,
both inheriting from `SyncBackend` — which is a heavy KCalendarCore-typed
abstract base. Both are forced to override `loadCalendars`, `storeCalendars`,
`startSync`, `removeItem` as no-op stubs because they are not calendar
backends. From `rawfilesbackend.h:43-52`:

```cpp
// ---- SyncBackend calendar stubs (not a calendar backend) ----
void loadCalendars(const QString &collectionId) override;
void storeCalendars(const QString &,
                    const QList<KCalendarCore::MemoryCalendar *> &) override {}
void startSync(const QString &, KCalendarCore::MemoryCalendar *,
               const QList<KCalendarCore::Incidence::Ptr> &,
               const QList<KCalendarCore::Incidence::Ptr> &,
               const QMap<QString, QString> &,
               const Kalburator::Sync::TranscodingPlan &) override {}
void removeItem(const QString &, const QString &) override {}
```

The comment is the giveaway: "calendar stubs (not a calendar backend)." A
class designed to be a generic file sink has to know about
`KCalendarCore::Incidence::Ptr` and pretend to handle it.

**Why it's a violation:** Two things wrong here. First, the library is
shipping a backend that exists only because WildPalms's PalmRuntime hard-codes
`std::make_unique<Kalburator::Sinks::RawFilesBackend>(rootPath)` as its
default per-slot target (`palmruntime.cpp:353`). PlanStan never instantiates
either sink. Second, the sinks have to declare ten lines of calendar-typed
no-op overrides because `SyncBackend` (the ABC every backend inherits) is
still saturated with KCalendarCore types. Phase Ib.5 was supposed to "strip
KCalendarCore from src/engine/", and it did — from the *engine*. But
`SyncBackend` itself, the contract every backend inherits from including
non-calendar ones, is still a calendar interface that everything else has to
stub out.

These backends should live in `WildPalms/src/sinks/` if WildPalms is their
only consumer. Or their interface should be `IBlobBackend` (which is genuinely
generic), not `SyncBackend`. Today they pay the calendar tax to be registered
with `BackendRegistry`.

**What it costs PlanStan / a third consumer:** A third consumer (say, a
contacts sync daemon) wanting to add its own backend pays the same tax: it
must inherit from a calendar-typed ABC and stub out four KCalendarCore-typed
methods to participate in the engine. The Phase G promise — "engine deals only
in BackendRecord" — is not delivered at the backend ABC. The library ships
universal-sink primitives optimized for WildPalms's needs; a non-WildPalms
consumer has no use for them yet pulls them into its compile every time.

**Suggested direction:** Either (a) move `RawFilesBackend` and
`GenericSqliteBackend` to `WildPalms/src/sinks/` since WildPalms is their only
consumer, or (b) split `SyncBackend` into a `IRecordBackend` (generic, the
real `IBlobBackend` extension with operation handles) and a
`ICalendarTypedBackend` mixin that calendar-typed implementations inherit on
top. The sinks would inherit only from the generic side. That would also let
the WP `BlobBackendAdapter` (currently 80 lines of forwarding) collapse.

---

### Finding 3: `ExecutionOverride` is a WildPalms-only type sitting in the public API of every consumer

**Severity:** major
**Where:**
- `libkalburator/src/types/synctypes.h:389-400`
- `libkalburator/src/engine/syncengine.h:486-499` (overload
  `runSyncFuture(mappingId, ExecutionOverride, ...)`)
- WildPalms: `src/runtime/palmruntime.cpp:582-585` — only consumer
- PlanStan: `grep -r "ExecutionOverride" PlanStan/src` — zero matches.

**What it is:** A struct in `synctypes.h` whose own docstring says:

```cpp
/// Per-call execution override for runSyncFuture(). Lets callers
/// request mirror-direction semantics for a mapping that's
/// otherwise configured for bidirectional sync. Used by WildPalms's
/// Tools-menu Copy Palm→PC / Copy PC→Palm actions.
struct ExecutionOverride { ... };
```

The library publicly exposes a Direction enum (`Default`, `MirrorAToB`,
`MirrorBToA`) and an overloaded `runSyncFuture` that consumes it, then plumbs
`m_pendingOverride` through `SyncEngine` private state, embeds it in
`SyncEngineWorker::Request`, and stores it as `m_unifiedOverride` for
pause/resume cycles (`syncengine.h:275`). Every consumer's compile pulls in
this type. `Q_DECLARE_METATYPE(Kalburator::Sync::ExecutionOverride)` makes it
a Qt-registered type.

**Why it's a violation:** The library has accommodated a single feature of a
single consumer's Tools menu by adding a public type, a public overload, a
worker state field, a pause/resume migration field, and a metatype
registration. The feature itself — "let me run this mapping one-way without
changing its persistent direction" — is reasonable, but the library is the
wrong layer to host it. WildPalms should stage this inline at its own seam:
duplicate the mapping with the desired direction, run it, discard. Or expose
it as a SyncMode mutation. It does not need to be a parallel parameter on the
engine's public API that PlanStan's compiler eats every build.

The naming alone is telling: the library API uses neutral A/B framing, but
the docstring says "Palm→PC / Copy PC→Palm." That slip is honest: the type
exists for Palm semantics and was sanitized for the public header. The lie is
what's wrong, not the docstring.

**What it costs PlanStan / a third consumer:** A consumer reading the API
must understand a feature it does not use. Worse, the existence of
`ExecutionOverride` blocks a future cleaner shape — say, a richer SyncMode
enum or a mapping-clone operator — because both would have to coexist with
the parameter overload to keep WildPalms happy.

**Suggested direction:** Delete `ExecutionOverride` and its overload. Add a
`SyncMode::OneWayUploadEphemeral` or have WildPalms construct an ephemeral
mapping from a real mapping with the desired mode and run *that*. The cost is
~10 lines on WildPalms's side; the win is removing a Palm-named type from the
library's public surface.

---

### Finding 4: The supposedly-generic engine `qobject_cast`s to `RemoteCalendarBackend` and `LocalBackend` to drive its CTag/fingerprint fast-path

**Severity:** major
**Where:**
- `libkalburator/src/engine/syncengine.cpp:611-720` (`prepareSyncFastPath`)
- `libkalburator/src/calendar/remotecalendarbackend.h:56-150` (CTag API on
  the calendar-specific class)

**What it is:** `SyncEngine::prepareSyncFastPath()` is a pre-pass that decides
which mappings can be skipped because their endpoints are demonstrably
unchanged. Implementation (`syncengine.cpp:611-720`):

```cpp
QMap<RemoteCalendarBackend*, QStringList> remoteCalIdsByBackend;
...
if (auto *r = qobject_cast<RemoteCalendarBackend*>(base)) {
    remoteCalIdsByBackend[r].append(calId);
    remoteBackendIds[r] = backendId;
}
...
if (auto *l = qobject_cast<LocalBackend*>(base)) {
    fresh.sourceFingerprint = freshLocalFingerprints.value(...);
    const QString stored = srcLocal->cachedFingerprint(...);
    sourceUnchanged = ... fresh.sourceFingerprint == stored;
}
```

The unified engine has hardcoded knowledge of two specific calendar-backend
subclasses — `RemoteCalendarBackend` (CalDAV) and `LocalBackend` (file). It
calls into their CTag and fingerprint accessors directly. `CTag` is a
CalDAV-specific concept (RFC 6578 sync-token's older cousin); fingerprint is a
LocalBackend-private detail.

**Why it's a violation:** Phase Ib.5 was titled "engine generalization" and
its named outcome was "stripped KCalendarCore from src/engine/" (per
`04z-phase-ib.5-status.md`). It did. But it left a far worse leak in
`prepareSyncFastPath`: the engine still knows by name about two specific
calendar backends and reaches into their unique APIs. This makes
`SyncEngine` fundamentally calendar-shaped. CardDAV's `RemoteContactsBackend`
also has CTag semantics — but the fast-path doesn't see it, because it casts
specifically to `RemoteCalendarBackend`. The new contacts domain receives
none of this optimization, exposing the path as written-for-calendar-only
even within libkalburator's own scope.

**What it costs PlanStan / a third consumer:** A new backend (say a Caldav
backend implemented out-of-tree) cannot participate in the fast-path skip
optimization without having `qobject_cast<RemoteCalendarBackend*>` succeed —
which means inheriting from a class shipped in libkalburator. That couples
out-of-tree backends to libkalburator's class hierarchy in a way the engine
public surface promises is unnecessary. A third consumer who ships its own
"freshness probe" backend (Akonadi, Google Calendar SDK, anything) gets
treated as "no fast path possible" no matter how cheap a freshness check it
could expose.

**Suggested direction:** Pull the freshness-probe API up into `SyncBackend`:
`virtual std::optional<QString> freshnessToken(collectionId)` returning empty
when not supported. The engine reads tokens via the virtual; CalDAV implements
via CTag; Local via fingerprint hash; CardDAV gets it for free; new backends
opt in by override. Delete the two `qobject_cast`s from
`prepareSyncFastPath`.

---

### Finding 5: `SyncBackend` is still calendar-typed at its abstract base, forcing every backend (calendar or not) to implement KCalendarCore-typed virtuals

**Severity:** major
**Where:**
- `libkalburator/src/calendar/syncbackend.h:117-220` — the ABC every backend
  inherits from
- `libkalburator/src/sinks/rawfilesbackend.h:43-52` — non-calendar backend
  forced to stub calendar virtuals
- `libkalburator/src/sinks/genericsqlitebackend.h:49-58` — same
- `libkalburator/src/contacts/remotecontactsbackend.h` — even contacts has
  to fit into this calendar-typed shape
- `WildPalms/src/runtime/palmruntime.cpp:60-90` — `BlobBackendAdapter`'s
  entire reason for existence: forwarding `IBlobBackend` calls and stubbing
  the calendar API

**What it is:** `SyncBackend` (the abstract base in `src/calendar/syncbackend.h`)
declares pure virtuals over KCalendarCore types:

```cpp
virtual void loadCalendars(const QString &collectionId) = 0;
virtual void storeCalendars(const QString &,
                            const QList<KCalendarCore::MemoryCalendar*>&) = 0;
virtual void startSync(const QString&, KCalendarCore::MemoryCalendar*,
                       const QList<KCalendarCore::Incidence::Ptr>&, ...) = 0;
virtual void removeItem(const QString &calId, const QString &itemUid) = 0;
```

Plus `recurrenceCapabilities()`, `analyzeRecurrenceLoss(Incidence::Ptr)`,
`createCalendar(... CalendarType type ...)`, and so on. These are the
pre-Phase-G API; they were never deleted, only "deprecated" while a parallel
Operation-based API was layered on. The contacts backend, the memo plugin,
the todo plugin, the universal sinks, and WildPalms's `BlobBackendAdapter`
all inherit this — and have to provide implementations or empty stubs of
calendar-typed methods.

The very file lives at `src/calendar/syncbackend.h`, not `src/types/` or
`src/sync/`. The library's own directory layout admits the backend ABC is a
calendar artifact.

**Why it's a violation:** This is the single biggest leaky abstraction in the
post-refactor library. Every claim of "domain neutrality" downstream is built
on a foundation that is calendar-typed at the ABC. The Phase Ib.5 status doc
saying "KCalendarCore stripped from src/engine/" is technically true, but the
engine talks to backends through this ABC, and that ABC wears KCalendarCore
on its sleeve. WildPalms has had to invent an entire `BlobBackendAdapter`
class — 80 lines of forwarding — purely to wrap its non-calendar `IBlobBackend`
plugins into something that satisfies this calendar-typed contract.

The directory location (`src/calendar/syncbackend.h`) is the smoking gun. If
`SyncBackend` were truly the generic backend ABC, it would live in
`src/types/` or `src/sync/`, not under `src/calendar/`. It's there because it
was the calendar engine's backend type pre-merger and never got moved.

**What it costs PlanStan / a third consumer:** A third consumer has two
choices: inherit `SyncBackend` and stub the calendar API (the
`RawFilesBackend` pattern), or inherit `IBlobBackend` and write an adapter
(the `BlobBackendAdapter` pattern). Either way, calendar-typed virtuals
appear in their compile graph. They will never be called. They exist because
the library has not finished what it started.

**Suggested direction:** Hoist `IBlobBackend` (already pure-interface, no
QObject) into the canonical backend ABC. Move the calendar-typed virtuals
into a `ICalendarBackend` interface that `LocalBackend`, `OrgBackend`,
`RemoteCalendarBackend`, `AkonadiBackend`, `DecSyncBackend`,
`SubscriptionBackend`, `MockBackend` inherit alongside `IBlobBackend`. The
engine talks only to `IBlobBackend`. Calendar-aware code paths use the typed
interface only at host-side code (calendar UI, conflict dialogs that show
incidence diffs). Move `syncbackend.h` from `src/calendar/` to either
`src/sync/` or `src/types/`.

---

### Finding 6: `IDMappingStore` carries WildPalms-specific columns directly in its public type

**Severity:** minor
**Where:**
- `libkalburator/src/journal/idmappingstore.h:11-48` (the `IDMapping` struct
  and its docstring)
- `libkalburator/src/journal/idmappingstore.h:90-105` — the
  "WP-contributed category + archive methods" section

**What it is:** The library's identity-mapping store carries fields with
explicit WildPalms provenance, called out in comments:

```cpp
/**
 * Schema evolution: this class creates the sync_id_mappings table and
 * stamps PRAGMA user_version = 3 on fresh DBs.
 * Extends via idempotent ALTER TABLE ADD COLUMN on open for the four
 * WildPalms-specific columns (last_synced, source_category,
 * target_categories, archived).
 */
...
struct IDMapping {
    ...
    QString     sourceCategory;    ///< optional; Palm-shaped backends only
    QStringList targetCategories;  ///< optional
    bool        archived = false;
    ...
};
...
// --- WP-contributed category + archive methods ---
IDMapping getMapping(...);
void updateCategories(...);
void setArchived(...);
```

**Why it's a violation:** The struct's documentation literally states that
some fields apply to "Palm-shaped backends only." The library's identity
table is now a superset of "what every backend needs" plus "what WildPalms
needs" — and nothing structurally separates them. PlanStan's CalDAV mapping
rows have empty `sourceCategory`, empty `targetCategories`, `archived=false`
forever. The columns are stamped at PRAGMA user_version 3 — the schema
evolved past "neutral" to absorb these WildPalms additions.

**What it costs PlanStan / a third consumer:** Their on-disk database carries
columns they will never use. Worse, the `IDMapping` value type carries
fields a third consumer would have to understand even to interact with the
store, encouraging cargo-cult population of unrelated data.

**Suggested direction:** Either (a) split: a base `IDMapping` with
`backendId/sourceUid/recurrenceId/targetId/calendarId/lastSynced`, plus a
WildPalms-side extension table joined on `(backendId, sourceUid,
recurrenceId)` for category/archived state. WildPalms manages that table
itself in its own SQLite store. Or (b) accept these as generic but rename:
`tag` for `sourceCategory`, `tags` for `targetCategories`, `archived` is
fine. The current name `sourceCategory` only makes sense if you know "Palm
device records have a category byte."

---

### Finding 7: `CancellationReason::ResourceLost` and the `cancelWithReason`/`MappingScheduler` machinery exist for a use case (Palm cradle disconnect) only WildPalms has

**Severity:** minor
**Where:**
- `libkalburator/src/engine/syncenginefuture.h:13-18` (the enum)
- `libkalburator/src/engine/syncengine.h:549-560`
  (`cancelWithReason(CancellationReason, resourceId)`)
- `libkalburator/src/engine/syncengine.h:759-760` (G.6 Task 44 the
  `MappingScheduler` for "resource-aware FIFO scheduler")
- PlanStan: `grep -r cancelWithReason PlanStan/src` — empty
- WildPalms: also empty currently, but the tickle/cradle disconnect
  machinery is the documented use case

**What it is:** A multi-mapping queue can be cancelled with a reason. One of
the reasons is `ResourceLost` — explicitly documented as "Palm cradle
disconnect." When `cancelWithReason(ResourceLost, resourceId)` is called, the
engine consults `MappingScheduler` and selectively skips pending mappings
whose source or target backend's `resourceId()` matches the lost resource;
mappings that don't reference that resource continue.

The signature, the per-backend `resourceId()` virtual, the resource set
tracking — all of this is plumbed for a feature that PlanStan does not need
(CalDAV doesn't "lose its cradle" — it has retry / network failure semantics
that fit `UnrecoverableError` or per-mapping failure) and that WildPalms's
current code does not even invoke yet.

**Why it's a violation:** The library has paid for a feature for which there
is no current consumer, designed around a Palm-specific failure mode. The
`resourceId()` virtual on every backend is paying ongoing tax (every backend
must override it; the default is `"backend:<hex-address>"`). The
`MappingScheduler` class adds dispatch indirection for a use case that the
test suite stubs but doesn't drive end-to-end against a real Palm cradle
disconnect (real-device verification is permanently deferred per ROADMAP).

**What it costs PlanStan / a third consumer:** Call-graph clutter. The
`resourceId()` virtual is one more thing every backend implements. A
contributor reading `cancelWithReason` has to understand a use case that
doesn't apply to their consumer. The engine has 100 lines of code keeping
`m_lostResources` and `m_scheduler` consistent for a single not-yet-real
feature.

**Suggested direction:** If real-device verification doesn't actually drive
this, delete it. If it stays, gate it behind a `IResourceAwareCancellation`
mixin so consumers that don't need it don't pay the cost. The neutral
cancellation `QFuture::cancel()` (which already exists and works for
PlanStan) is sufficient for non-Palm consumers.

---

### Finding 8: `ConflictPolicy::ConnectionBehavior` was *stripped* on the way into the library — but the comment admits the asymmetry

**Severity:** minor (admission of guilt rather than active leak)
**Where:** `libkalburator/src/conflict/conflictpolicy.h:64-68`

```cpp
// NOTE: ConnectionBehavior was stripped when lifting qsynccore into
// libkalburator (Phase B, 2026-04-20). HotSync session-keep-alive is
// Palm-specific; Wild Palms re-adds it on a Palm-backend config
// subclass. See docs/phase0/02-inventory-wildpalms.md
// §"Conflict-policy audit".
```

**What it is:** An honest carve-out comment. ConnectionBehavior was a
HotSync-specific concept that didn't belong in a generic conflict policy, so
the lift left it out. WildPalms has to re-add it via subclass.

**Why it's a violation (or rather, why it's a worry):** This is the *correct*
pattern for handling Palm-specific concerns: keep them out of the library,
let the consumer subclass to add what it needs. The audit cite is positive
here — the library held the line. **But** the rest of the audit shows that
this lesson was not internalized for later additions. `ExecutionOverride`,
`ResourceLost`, `IDMappingStore::sourceCategory`, the `palm-calendar`
backendType string (special-cased in `palmruntime.cpp:313` but reached
through library APIs) — all are exactly the kind of thing that "should have
been WildPalms-side subclasses" and weren't. The conflict-policy comment is
evidence that the team knew this pattern; the rest of the audit is evidence
that the discipline slipped.

**What it costs:** Nothing directly. This finding documents the *correct*
pattern, to argue the others diverged from it.

**Suggested direction:** Not a fix — a yardstick. Apply this same standard
retroactively: each WildPalms-shaped intrusion catalogued above should be
moved to a WildPalms-side subclass, mixin, or sidecar.

---

### Finding 9: The `isAvailable`-counting test posture is calendar-skewed; library tests reflect what calendar needs, not what a non-calendar consumer needs

**Severity:** minor
**Where:**
- `libkalburator/tests/calendar/` — 35 test files, ~10 of which are
  `tst_<backend>_blob_view.cpp` and the other 25 are calendar-flow
  integration tests against `StubSyncHost` + `StubCalendarCollection`
- `libkalburator/tests/contacts/` — 5 files
- `libkalburator/tests/memo/` — 2 files
- `libkalburator/tests/todo/` — 3 files
- `libkalburator/tests/sinks/` — 3 files (the universal sinks)
- `libkalburator/tests/calendar/stubs/` — `StubSyncHost`,
  `StubCalendarCollection`, `StubIncidenceRegistry`,
  `StubSyncConfigStore` — all calendar-shaped, all linked into the
  reusable `kalburator_calendar_test_stubs` static lib

**What it is:** The library's own integration tests are written almost
exclusively against calendar-typed stubs. Phase D.0 ("tests-first") added a
StubSyncHost and a StubCalendarCollection — the latter holds a
`MemoryCalendar` and is the canonical host fixture. Almost every cross-cutting
sync-flow test (`tst_calendar_sync_full`, `tst_calendar_sync_oneway`,
`tst_calendar_conflict`, `tst_calendar_first_sync_via_blob_engine`,
`tst_calendar_subsequent_sync_uses_blob_view`,
`tst_calendar_transcoding_warning`, `tst_calendar_sync_error_recovery`,
`tst_engine_unified_boundary`, `tst_engine_cancellation`) lives in
`tests/calendar/` and uses these calendar stubs.

**Why it's a violation:** A neutral library would have neutral integration
tests. A `tst_engine_full_sync` would dispatch a generic mapping with a
generic `BackendRecord`-emitting backend pair and assert ID-mapping
correctness, baseline correctness, conflict detection — none of which is
calendar-specific. Instead, the library's "engine boundary" tests are pinned
through calendar harnesses, which means:

1. The contracts the engine actually preserves are
   `StubCalendarCollection`-shaped contracts.
2. Domain-neutrality regressions go undetected: nothing in the test suite
   exercises "engine sync between two non-calendar backends with a
   non-calendar plugin" except the universal-sink round-trips, which don't
   stress conflict / baseline / cancellation logic.
3. A future change that breaks contacts or memo flows but leaves calendar
   green will pass `verify-all.sh`.

The new contacts tests (`tst_remote_contacts_backend.cpp`,
`tst_vcard_differ.cpp`, `tst_unified_askuser_pause.cpp`) are 5 files,
unit-shaped, and largely about the contacts-domain code, not the engine
through contacts. The engine-cancellation test runs against a calendar mock.

**What it costs PlanStan / a third consumer:** A regression that breaks
non-calendar flows ships green. The library's own claim of "domain neutral
post Ib.5" is not test-pinned at the integration layer — only at the
shape-registry layer (`tst_engine_unified_routing.cpp`, which tests
dispatch logic, not full flow).

**Suggested direction:** Add `tests/engine/tst_engine_full_sync_blob.cpp`
that runs the full sync flow (fetch, diff, merge, write, baseline-update,
conflict-detect, AskUser-pause/resume, cancellation) against two `MockBlobBackend`
instances with a stub blob/text plugin. Mirror the calendar test matrix in
domain-neutral form. Until this exists, "the engine is generic" is faith,
not test-pinned fact.

---

### Finding 10: PlanStan's clean integration is a feature of PlanStan, not of the library

**Severity:** structural observation, not a bug
**Where:** `PlanStan/src/controllers/collectioncontroller.cpp:1657-1714`,
PlanStan's use of `Kalburator::Sync::ProviderManager`,
`Kalburator::Sync::CalDavProvider`, `runSyncFuture(...)`. PlanStan never
touches `ExecutionOverride`, `cancelWithReason`, `RawFilesBackend`,
`BlobBackendAdapter`, or any of the Palm-shaped APIs.

**What it is:** PlanStan uses ~5 library types: `ProviderManager`,
`CalDavProvider`, `BackendConfiguration`, `SyncResult`, and
`runSyncFuture(SyncBehavior)`. Its CollectionController owns a `Collection`
that happens to satisfy `ICalendarCollection` because `Collection` already
held `MemoryCalendar`s for its own UI reasons. PlanStan never had to invent
an adapter, never had to pre-register any host objects to placate library
guards, and never reached into a deprecated overload.

**Why it's not a violation:** PlanStan is the consumer the library was
extracted from. Of course the fit is clean — large parts of the library are
literally code lifted from PlanStan (`CalendarManager`, the SyncStore split,
the calendar baseline machinery). Phase H.5 audit
(`2026-05-07-phase-h5-task1-audit.md`) explicitly noted that
`CalendarManager` was already CalDAV-clean before Phase H lifted it.

**What it shows:** The library is a clean library *for PlanStan*. The Phase
H/H.5/Ia/Ia.5/Ib/Ib.5 audits all show the library serving PlanStan well, and
PlanStan migrating with minimal pain. Where the seams have been tested
against a *different* consumer (WildPalms), they've cracked: Phase J's stall
(documented in FINDINGS 2654-2769) is exactly the seam where the
PlanStan-shaped library stops fitting WildPalms's shape.

The defensive read: when the library's PlanStan-shaped seams meet
WildPalms's shape, the team's response has been to bend the *library* (add
`ExecutionOverride`, ship `RawFilesBackend` in `src/sinks/`, document
WildPalms columns in `IDMappingStore`, special-case calendar in
`CalendarPluginWriter` because WildPalms can't satisfy
`ICalendarCollection`'s contract) rather than to bend WildPalms (subclass,
sidecar, adapter on *its* side). That's the choice that matters. Each
individual concession was small and locally justifiable. The aggregate is a
library that has accumulated a Palm-shaped knee at every seam.

**Suggested direction:** Stop conceding. The next time WildPalms can't fit a
library API, the fix should be on WildPalms's side first. Only if multiple
consumers (PlanStan + a hypothetical third) want the same accommodation does
the library accept it.

---

## What I'd argue at the dialectic

In priority order:

1. **`CalendarPluginWriter`'s `ICalendarCollection` requirement is a
   documented load-bearing bug.** This is the Phase J Task 9 blocker
   (FINDINGS 2727-2769). It exists because `CalendarPluginWriter` reaches
   through a host-managed `MemoryCalendar*` to commit changes, which forces
   *every* consumer of the engine to pre-register a `MemoryCalendar` for
   *every* collection it writes into — even purely-output sinks. PlanStan
   gets away with it because its `Collection` class already holds
   `MemoryCalendar`s. WildPalms eagerly preloads to placate the guard
   (`palmruntime.cpp:313-324`) on the source side, but cannot for the
   target side, hence palm→caldav is structurally broken. **The fix is
   library-side**, not WildPalms-side: the writer should not need an
   `ICalendarCollection`. It should serialize iCal from the
   `BackendRecord::data` it already receives and call
   `pushItems()` directly. The fact that the team is currently considering
   how to "give PalmRuntime a way to register MemoryCalendars for CalDAV
   collections" (per CURRENT-STATUS) is the wrong direction — it bends
   WildPalms further to serve a library guard that should not exist.

2. **`SyncBackend` belongs in `src/types/` and should not declare KCalendarCore
   virtuals.** The Phase Ib.5 status doc claims KCalendarCore was stripped
   from `src/engine/`. True. But every backend the engine talks to inherits
   `SyncBackend`, and `SyncBackend` lives in `src/calendar/syncbackend.h`
   and declares `loadCalendars`, `storeCalendars`, `startSync`, `removeItem`
   over KCalendarCore types. Two universal sinks (`RawFilesBackend`,
   `GenericSqliteBackend`) and one WildPalms-side adapter
   (`BlobBackendAdapter`) all have to stub out four calendar virtuals. This
   is the single highest-leverage cleanup: split `SyncBackend` into a
   generic operation-based backend (`IRecordBackend`, an evolution of
   `IBlobBackend`) and a calendar-typed mixin (`ICalendarBackend`), and
   move the file out of `src/calendar/`. Most consumers and the engine talk
   only to the generic side. The `BlobBackendAdapter` collapses to zero.
   The universal sinks lose the calendar-stub pollution.

3. **WildPalms-named types should not be in the library's public API.**
   `ExecutionOverride` (with its docstring naming WildPalms's Tools menu),
   `CancellationReason::ResourceLost` (commented "Palm cradle disconnect"),
   `IDMapping::sourceCategory` (documented "Palm-shaped backends only") —
   these are public types every consumer's compile sees, and they exist for
   one consumer's specific feature. `ConnectionBehavior` was correctly
   *stripped* during the qsynccore lift; the team knew this pattern. The
   library's discipline lapsed for these later additions. They should each
   move to a WildPalms-side mixin/subclass/sidecar, on the same precedent.
   Yes, that means WildPalms re-implements a few hundred lines. That is
   correct — the library is the asset that needs to stay clean across
   multiple consumers. The consumer is the right place to absorb
   consumer-specific cost.

## What I am NOT claiming

These are places where the library actually held the line, and I want my
audit's credibility on record:

- **Phase Ib.5 succeeded at its stated narrow goal.** `IDomainAdapter`,
  `BlobDomainAdapter`, `CalendarDomainAdapter` are gone. `dispatchCalendarLegacy`
  is gone. `SyncEngine::dispatchSync` no longer has the `if (domain != "calendar")`
  router that motivated the phase. KCalendarCore is genuinely absent from
  `src/engine/`. These are real wins; the team did the technical work.

- **The shape system is well-designed and not Palm-shaped.** `Shape`,
  `DomainId`, `EncodingId`, `TransformationRegistry`, `Pipeline`,
  `PropertyCatalogue`, `LossProfile` — these are clean, generic
  abstractions. They are the load-bearing infrastructure for a future
  third domain, and they would absorb one without distortion. The
  `(blob, blob)` vs `(blob, raw)` issue documented in FINDINGS
  2281-2313 was an adapter-default bug, not a shape-system flaw.

- **`IProvider` is genuinely generic.** It models "one auth/connection,
  N collections." `CalDavProvider` and `CardDavProvider` both fit.
  Nextcloud (one server, two protocols) fits. Akonadi will fit. The
  comment in `iprovider.h:40` mentions WildPalms's `PalmDeviceAccess`
  pattern as inspiration, but the abstraction itself doesn't carry
  Palm baggage. `ProviderManager`'s
  `dynamic_cast<SyncBackend*>(backend.get())` (line 212) is the only
  smell, and it's there because `BackendRegistry` historically stores
  `SyncBackend*`, not `IBlobBackend*` — that's a registry-side issue,
  not a provider-side issue.

- **The conflict/baseline/journal layer is mostly neutral.**
  `ConflictInfo`, `SyncConflictStore`, `BlobBaselineStore`,
  `CalendarBaselineStore`, `ConflictManager` — these are domain-aware
  but not consumer-aware. The "source/target" terminology lift
  (`synctypes.h:27-35`) is a positive — local/remote framing was
  PlanStan-shaped and was correctly genericized. The baseline split
  (Phase D — `BlobBaselineStore` from `CalendarBaselineStore`)
  did the right thing.

- **The transcoding layer (transcoding-into-backends + TranscodingPlan +
  TranscodingRouter) is well-bounded.** Phase E moved transcoding decisions
  to backend boundaries successfully. The router stays small. This part
  of the architecture is not where the Palm-isms accumulated.

- **Test coverage on what the team chose to test is conscientious.** The
  per-backend `tst_<backend>_blob_view.cpp` files, the
  `tst_engine_unified_routing.cpp` boundary, `tst_remote_contacts_backend`,
  the FakeCalDavServer extension for Phase J — these are all real engineering
  effort and they catch real regressions. My finding 9 critique is about
  what's *not* tested (engine flow against non-calendar backends), not
  about the quality of what's there.

- **PlanStan's adoption was clean.** Phase H.5 absorbed the provider model
  with one PR per concern, the davUrl-replay paths were correctly deleted,
  and PlanStan's controllers integrate the library through five types and
  a couple dozen lines. That part of the refactor is what the rest of it
  should look like.

- **`CTagStore` correctly lives inside `RemoteCalendarBackend` private
  state** (per `04a-followups.md` Audit 1's recommendation). The
  finding-4 critique is about `prepareSyncFastPath` *reaching into* that
  store via `qobject_cast`, not about its location.

The library is recoverable. The architectural skeleton is sound. The wins
listed above are real. What's lacking is the discipline that the
ConnectionBehavior strip-out demonstrated — applied to every consumer
intrusion since.
