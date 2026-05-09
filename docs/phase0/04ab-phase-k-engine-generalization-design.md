# Phase K — Engine generalization (design)

**Status:** Design draft — 2026-05-09. Phase J deferred pending K. K.1
not yet started; awaiting user review of this doc.

**Branch:** `refactor/engine-merger` (continued — not a fresh cut).

**Predecessor phases relevant to K:**
- Phase G (shape pipeline) — `04r-phase-g-*`. Introduced
  `Shape::DomainPlugin`, `IRecordWriter`, `BackendRecord`,
  `DomainRegistry`, `TransformationRegistry`, canonical-shape model.
- Phase Ia.5 (engine unification) — `04v-phase-ia.5-status.md`.
  Folded `SyncCoordinator + SyncWorker + BlobSyncEngine` into the
  unified `SyncEngine`. **Did not** retire the calendar-typed
  surface on `SyncBackend`.
- Phase Ib.5 (engine generalization) — `04z-phase-ib.5-status.md`.
  Stripped `<KCalendarCore/*>` from `src/engine/` headers. **Did not**
  retire the engine fast-path's `qobject_cast<RemoteCalendarBackend>` /
  `qobject_cast<LocalBackend>`.

K is the lift those two phases left unfinished, plus the
sync-situation flexibility work that was never explicitly scoped
into any prior phase.

---

## 1. Why Phase K exists

Two parallel oppositional architectural audits run on 2026-05-09
(stored at `2026-05-09-audit-libkalburator-defensive.md` and
`2026-05-09-audit-wildpalms-integrity.md`) converged from opposite
biases on a single root cause:

> The library's "abstraction" is calendar-typed at the bottom of the
> hierarchy. `Sync::SyncBackend` lives at `src/calendar/syncbackend.h`,
> includes `<KCalendarCore/MemoryCalendar>` and `<Incidence>`, declares
> pure virtuals over those types, and is the base class every
> backend (calendar or not) inherits.

The audits also surfaced that this is the *direct* cause of the Phase J
Task 9 stall (`tst_runtime_caldav_e2e` palm→caldav failing): the
calendar-domain writer demands a host-resident `MemoryCalendar` for
the target id, which doesn't exist for the CalDAV side of a Palm sync.

First-hand verification against the code (notes file:
`2026-05-09-phase-k-k0-notes.md`) confirms a richer picture than
the audits described:

- **The domain-plugin scaffolding already exists and is wired into
  the engine's unified path.** `Shape::DomainPlugin`, `IRecordWriter`,
  `BackendRecord`, `Shape::DomainRegistry::findByDomain()` are all
  used by `SyncEngineWorker::unifiedContinueAfterConflicts` (engine.cpp
  lines 2320–2336).
- **A generic record-level baseline store (`BlobBaselineStore` v3,
  G.4) already keys by `(mappingId, recordId) → CanonicalRecord`** —
  the keystone for unifying baselines is already there.
- **`IBlobBackend` is the genuinely-domain-neutral substrate** —
  `loadRecords / createRecord / updateRecord / deleteRecord /
  modifiedSince / deletedSince / batch` over `BackendRecord`. No
  calendar types in its surface.
- **`SyncBackend` inherits both `QObject` and `IBlobBackend`** and
  layers the calendar-typed virtuals on top. The calendar surface is
  a layer of scar tissue; the proper interface is below it.
- **`KalburatorDomainContacts` and `KalburatorDomainBlob` already
  exist** as `DomainPlugin` implementations — the witness substrate
  for K.3 is in place.

So Phase K is **not** a greenfield abstraction effort. It is a
**bottom-up code excision**: lift every calendar-typed virtual off
`SyncBackend` into `Calendar::CalendarDomainPlugin` (or delete
outright), let `IBlobBackend` + the existing `DomainPlugin` system
be the actual public surface, and finish what G/Ia.5/Ib.5 began.

The renaming question is the easy half. The load-bearing half is
making the engine flex along the **sync-situation axes** the user
named: Palm linearity, ETag/CTag affordances, parallelism, baselines,
conflict scenarios. Today the engine fast-path hard-codes those to
`RemoteCalendarBackend` (CTag) and `LocalBackend` (fingerprint) via
`qobject_cast`. That gate must become a capability interface.

---

## 2. End-state contract (falsifiable)

Phase K is done only when **every one** of these is true. Verification
by `grep`/build/test, not by reading docs.

1. **No `<KCalendarCore/*>` include in `src/engine/` or in
   `src/calendar/syncbackend.h`'s replacement** (whether that's a
   slimmed `SyncBackend` or `IBlobBackend` directly).
2. **Backends do not inherit calendar-typed pure virtuals.**
   `RawFilesBackend`, `GenericSqliteBackend`, and any future non-calendar
   backend implement `IBlobBackend` + a small QObject mixin only — no
   no-op stubs for `loadCalendars`, `storeCalendars`, `startSync`,
   `pushItems(QList<Incidence::Ptr>)`.
3. **A non-calendar contacts witness test** runs the full engine
   pipeline (fetch → diff → conflict → write → cancel) end-to-end
   against `KalburatorDomainContacts` + a fake `IBlobBackend` +
   `vCard4` transform, **without linking KCalendarCore** in that test
   target (verified by linker flags or by symbol absence).
4. **Existing calendar tests still pass** via the calendar domain
   plugin, with the calendar-typed methods now living on
   `Calendar::CalendarDomainPlugin` instead of `SyncBackend`.
5. **No `qobject_cast<RemoteCalendarBackend*>` or
   `qobject_cast<LocalBackend*>` in engine code.** Fast-path
   change-detection consults a capability interface
   (`IChangeDetection`, see §4) that backends opt into.
6. **No `dynamic_cast<CalendarPluginWriter*>` in
   `unifiedContinueAfterConflicts`.** The writer's threading contract
   is expressed via a virtual on `IRecordWriter` (e.g.
   `runOnBackendThread() const`), and the engine routes generically.
7. **`CalendarBaselineStore` retired.** Its iCal-text baselines fold
   into `BlobBaselineStore` v3 (the canonical record carries the iCal
   bytes); its property-JSON baselines move to a per-collection
   metadata table on the same store; the per-mapping last-sync
   timestamp moves to that store's metadata table. Calendar-domain
   plugin owns the property-baseline shape; the store itself stays
   domain-neutral.
8. **WildPalms's `BlobBackendAdapter` is deleted.** Its sole reason
   to exist was bridging the calendar-typed `SyncBackend` for blob
   plugins; with calendar virtuals gone, blob plugins become
   `IBlobBackend` directly.
9. **Public API has no Palm-specific docstrings.**
   `ExecutionOverride`, `CancellationReason::ResourceLost`,
   `IDMapping::sourceCategory` retain their semantics (they're
   genuinely cross-consumer concepts) but the docstrings drop the
   "WildPalms's Tools-menu Copy Palm→PC" / "Palm cradle disconnect" /
   "Palm-shaped backends only" framing.
10. **WildPalms's dual `SyncEngine` is collapsed.** The native
    `Sync::SyncEngine` in `KF6MainWindow` (audit finding #1) is
    deleted; settings dialog knobs operate on the library engine.
    This is K.7/K.8 work — design here, code in K.8.

If any of these fails the build or grep verification, the phase
isn't done. **Documentation claims do not satisfy the contract.**

---

## 3. Concrete code delta — what moves where

### 3.1 `Sync::SyncBackend` (src/calendar/syncbackend.h) — bulk lift

The class file ends K relocated from `src/calendar/` to (proposed)
`src/sync/syncbackendbase.{h,cpp}` — naming reflects "minimal Qt
holder + IBlobBackend re-export, not a calendar class." It loses
all calendar-typed members.

| Member (current) | Disposition under K |
|---|---|
| `class SyncBackend : public QObject, public IBlobBackend` | Renamed `SyncBackendBase`; calendar virtuals removed. |
| `loadCalendars(QString)` | **Move** to `Calendar::CalendarDomainPlugin::loadCollections(SyncBackendBase*, QString)`. Calendar plugin proxies `IBlobBackend::availableCollections` and emits its own typed signal. |
| `storeCalendars(QString, QList<MemoryCalendar*>)` | **Delete.** Per first-hand grep, neither PlanStan nor WildPalms calls it externally. Internal callers route through `IRecordWriter::apply`. |
| `startSync(QString, MemoryCalendar*, ..., TranscodingPlan)` | **Delete.** Legacy pre-G API; no external callers. Engine uses `IRecordWriter::apply` via the domain plugin. |
| `pushItems(QString, QList<Incidence::Ptr>, TranscodingPlan)` | **Delete** (calendar-typed overload). Generic `pushItems(QString, QList<BackendRecord>)` already exists implicitly via `IBlobBackend::createRecord` batched in `IRecordWriter::apply`. |
| `removeItem(QString, QString)` | **Replace with** `IBlobBackend::deleteRecord(recordId)` (already there). |
| `RecurrenceCapabilities`, `RecurrenceLossInfo` | **Move** to `src/calendar/recurrencecapabilities.h`. Calendar-only concepts; calendar plugin owns. |
| `analyzeRecurrenceLoss(Incidence::Ptr)` | **Move** to `Calendar::CalendarDomainPlugin::analyzeLoss(SyncBackendBase*, Incidence::Ptr)`. Calendar plugin owns. |
| `discoveredCalendarType / discoveredColor / discoveredWritable / discoveredDisplayName` | **Move** to `Calendar::CalendarDomainPlugin::discoveredProperties(SyncBackendBase*, calendarId)` returning a `CalendarProperties` struct. Domain plugin abstracts the "what does the server know about this collection" query. |
| `calendarColor / calendarDescription` | **Move** to `Calendar::CalendarDomainPlugin::collectionProperties(SyncBackendBase*, calendarId)` (already a virtual on the plugin). |
| `getRawIcs / setRawIcs` | **Move** to `Calendar::CalendarDomainPlugin::rawForm(SyncBackendBase*, calendarId, uid)` / `setRawForm(...)`. Debug-only feature, scoped to calendar. |
| `createCalendar / updateCalendar / renameCalendar / deleteCalendar` | **Move** to `Calendar::CalendarDomainPlugin::createCollection / updateCollection / ...` taking `SyncBackendBase*`. Calls flow back through `IBlobBackend::createCollection / collectionInfo / ...` plus calendar-specific metadata extension. |
| `bindingMetadataKeys / populateBindingMetadata / prepareCreationMetadata` | **Move** to `Calendar::CalendarDomainPlugin`. They're calendar-binding concepts. |
| `sourceFilePath(calendarId)` | **Move** to a new optional capability interface `IFileResident` (some org-mode/local backends are file-resident; not all). Calendar-domain feature — but the *ability* to expose a source file is generic; vCard files exist too. |
| `capabilities() / recurrenceCapabilities()` | **Move** to `Calendar::CalendarDomainPlugin::capabilitiesFor(SyncBackendBase*)` returning a `BackendCapabilities` struct (calendar-shaped). Generic backend capabilities (batch, delete-tracking) stay on `IBlobBackend`. |
| Signals `itemLoaded(MemoryCalendar*, Incidence::Ptr, QString)` | **Move** to `Calendar::CalendarDomainPlugin` as a signal it emits when proxying load events. Plugin connects to backend's generic `recordCreated/Updated/Deleted` signals (declared per-backend per `iblobbackend.h`'s comment). |
| Signals `calendarLoaded(MemoryCalendar*) / itemRemoved / loadCalendarsFinished` | **Move** to plugin. |
| Signals `calendarDiscovered / calendarCreated / calendarUpdated / ...` | **Rename to neutral** `collectionDiscovered / collectionCreated / ...` and lift onto a generic backend signal interface (`IBlobBackend`'s expected-signal list, codified). |
| Signals `transcodingWarning(calId, uid, warnings)` | **Keep** at backend level (generic; non-calendar transforms can warn too). Already used by both calendar and contacts paths. |
| Signals `typeViolationDetected(calId, uid, expected, actual)` | **Move** to `Calendar::CalendarDomainPlugin`. Calendar-only concept (VEVENT-vs-VTODO collection-type mismatch). |
| Signals `fetchStarted/fetchProgressChanged/fetchFinished/itemFetched/writeStarted/writeProgressChanged` | **Lift to `IBlobBackend` expected-signal contract**, with `itemFetched(QString calId, QString recordId)` (id-only) and a separate `Calendar::CalendarDomainPlugin::incidenceFetched` signal proxying the calendar shape. |
| `bool supportsCalendarCreation()` | **Move** to plugin's `supportsCollectionCreation(SyncBackendBase*)`. Generic across domains. |
| Operation API (`fetchItems/pushItems/deleteItems/cancelOperationsFor/...`) | **Stays at backend level**, retyped to BackendRecord. The calendar overloads delete; the plugin's `IRecordWriter` still calls into the backend's BackendRecord-typed methods. Backends already implement these for the blob path. |

After the lift, `SyncBackendBase` looks roughly like:

```cpp
class SyncBackendBase : public QObject, public IBlobBackend {
    Q_OBJECT
public:
    explicit SyncBackendBase(QObject *parent = nullptr);

    virtual QString backendType() const = 0;
    virtual QList<Shape::Shape> nativeShapes() const = 0;
    virtual QString resourceId() const;
    virtual Shape::Shape shapeFor(const QString &collectionId) const;

    // Operation API (BackendRecord-typed, returning Operation pointers
    // for async lifecycle).
    virtual FetchOperation* fetchItems(const QString &collectionId);
    virtual PushOperation*  pushItems (const QString &collectionId,
                                       const QList<BackendRecord> &items,
                                       const TranscodingPlan &plan = {});
    virtual DeleteOperation*deleteItems(const QString &collectionId,
                                       const QStringList &recordIds);

    // Operation tracking (unchanged).
    bool hasPendingOperations() const;
    bool hasPendingOperationsFor(const QString &collectionId) const;
    QList<SyncOperation*> pendingOperations() const;
    QList<SyncOperation*> pendingOperationsFor(const QString &collectionId) const;
    void cancelOperationsFor(const QString &collectionId);
    void cancelAllOperations();

signals:
    // Generic load / change events. (Replaces calendarDiscovered &c.)
    void collectionDiscovered(const QString &collectionId);
    void collectionLoaded(const QString &collectionId);
    void collectionLoadFinished(const QString &collectionId, bool success,
                                const QString &errorMessage);
    void collectionCreated(const QString &collectionId);
    void collectionUpdated(const QString &collectionId);
    void collectionRenamed(const QString &oldId, const QString &newId);
    void collectionDeleted(const QString &collectionId);
    void collectionError(const QString &collectionId, const QString &error);

    // Per-record streaming (BackendRecord-typed).
    void fetchStarted(const QString &collectionId, int totalRecords);
    void recordFetched(const QString &collectionId, const BackendRecord &rec);
    void fetchProgressChanged(const QString &collectionId, int cur, int total);
    void fetchFinished(const QString &collectionId, bool success,
                       const QString &errorMessage);
    void writeStarted(const QString &collectionId, int totalRecords);
    void writeProgressChanged(const QString &collectionId, int cur, int total);

    // Generic warning channel (calendar-domain transcoding, contacts vCard
    // 3↔4 lossy paths, etc.).
    void transcodingWarning(const QString &collectionId, const QString &recordId,
                            const QStringList &warnings);

protected:
    void registerOperation(SyncOperation *op);
    void unregisterOperation(SyncOperation *op);
    QHash<QString, QList<SyncOperation*>> m_pendingOperations;
};
```

About 80 lines instead of 660. No KCalendarCore include. Lives at
`src/sync/syncbackendbase.h`.

### 3.2 `Calendar::CalendarDomainPlugin` — gains the lifted methods

The plugin already exists at `src/calendar/calendardomainplugin.h`.
It absorbs the lifted virtuals as methods that take `SyncBackendBase*`:

```cpp
class CalendarDomainPlugin : public Shape::DomainPlugin {
public:
    // ... existing DomainPlugin overrides ...

    // Calendar-shaped queries against any SyncBackendBase whose
    // nativeShapes() includes calendar/incidence shapes.
    void loadCollections(SyncBackendBase *backend, const QString &collectionId);
    CalendarProperties discoveredProperties(SyncBackendBase *backend,
                                             const QString &calendarId);
    QColor calendarColor(SyncBackendBase *backend, const QString &calendarId);
    QString calendarDescription(SyncBackendBase *backend, const QString &calendarId);
    QString rawForm(SyncBackendBase *backend, const QString &calendarId,
                    const QString &uid);
    bool setRawForm(SyncBackendBase *backend, const QString &calendarId,
                    const QString &uid, const QString &raw);
    BackendCapabilities capabilitiesFor(SyncBackendBase *backend);
    RecurrenceLossInfo analyzeLoss(SyncBackendBase *backend,
                                    const Incidence::Ptr &incidence);

    // Typed signals that proxy generic backend signals.
    Q_OBJECT  // (now needs to be a QObject — see §3.7 below)
signals:
    void incidenceLoaded(SyncBackendBase *backend, MemoryCalendar *cal,
                         Incidence::Ptr incidence, const QString &versionId);
    void calendarLoaded(SyncBackendBase *backend, MemoryCalendar *cal);
    void typeViolationDetected(SyncBackendBase *backend, const QString &calId,
                                const QString &uid, CalendarType expected,
                                CalendarType actual);
};
```

The plugin queries the backend through whatever interface the backend
exposes (`IBlobBackend::loadRecords`, plus a backend-specific
side-channel like `RemoteCalendarBackend::ctag()` if calendar-shaped).
For backends that *aren't* calendar-shaped, the calendar plugin
either returns empty / default or refuses to operate (caller checks
`backend->nativeShapes()` first).

### 3.3 Engine fast-path generalization (the flexibility section)

This is the user-flagged "deserves its own evaluation pass" piece.
Today's `SyncEngine::prepareSyncFastPath` (syncengine.cpp:602–733):

```cpp
if (auto *r = qobject_cast<RemoteCalendarBackend*>(base)) {
    // ctag affordance
} else if (auto *l = qobject_cast<LocalBackend*>(base)) {
    // fingerprint affordance
}
```

K introduces a small set of capability interfaces that backends opt
into. No backend is *required* to implement any of them; the engine
gracefully degrades to "always sync, never skip" for backends without
a relevant capability.

#### `IChangeDetection` — collection-level revision token

```cpp
class IChangeDetection {
public:
    virtual ~IChangeDetection() = default;

    /// Returns a stable token identifying the *current* state of the
    /// collection. May be a CalDAV CTag, a CardDAV CTag, a directory
    /// fingerprint hash, a Palm sync-anchor, etc. Empty string =
    /// "I cannot answer cheaply right now" — engine treats as
    /// changed and proceeds to per-record fetch.
    virtual QString collectionRevision(const QString &collectionId) = 0;

    /// Optional — if backend can batch-fetch revisions for many
    /// collections cheaply (CalDAV PROPFIND with multiple hrefs),
    /// override. Default: loop over collectionRevision().
    virtual QMap<QString, QString>
    collectionRevisions(const QStringList &collectionIds);

    /// True if the backend persists previously-known revisions per
    /// collection (so the engine can write them back on success).
    virtual bool persistsCollectionRevisions() const { return true; }
    virtual void primeRevisionCache(const QMap<QString, QString> &cache) {}
    virtual QString cachedCollectionRevision(const QString &collectionId) const = 0;
};
```

`RemoteCalendarBackend` implements this returning CTag.
`RemoteContactsBackend` implements it returning the CardDAV CTag
(currently only RemoteCalendar has it; this widens it). `LocalBackend`
implements it returning the directory fingerprint. A future Palm
backend can implement it returning the device's last-sync anchor.
Any backend without per-collection cheap revision tokens just doesn't
implement `IChangeDetection` and is never fast-path-skipped.

`prepareSyncFastPath` becomes:

```cpp
auto *src = m_registry->backendInstance(mapping.sourceBackend);
auto *cd = dynamic_cast<IChangeDetection*>(src);
if (cd) {
    fresh.sourceRevision = cd->collectionRevision(mapping.sourceCalendar);
    const QString stored = cd->cachedCollectionRevision(mapping.sourceCalendar);
    sourceUnchanged = !fresh.sourceRevision.isEmpty()
                      && !stored.isEmpty()
                      && fresh.sourceRevision == stored;
}
// ... same shape for target side.
```

`FreshSyncState` collapses from `{sourceCtag, sourceFingerprint,
targetCtag, targetFingerprint}` to `{sourceRevision, targetRevision}`.

#### `IRecordRevision` — per-record revision token (ETag, mtime)

Distinct from collection revision. Many backends have per-record
change tokens (CalDAV ETag, CardDAV ETag, filesystem mtime, etc.):

```cpp
class IRecordRevision {
public:
    virtual ~IRecordRevision() = default;
    /// Token identifying the current revision of one record.
    /// Engine uses this to skip per-record fetch when the cached
    /// baseline matches.
    virtual QString recordRevision(const QString &recordId) = 0;
};
```

Today `BackendRecord::contentHash` partially serves this purpose, but
content-hash requires fetching the bytes; ETag avoids the fetch
entirely. The engine's per-record skip path (Phase D Task 20) currently
hashes; with `IRecordRevision`, backends that can answer cheaply
(without fetching) get a faster skip.

This is a **K.5+ enhancement**, not blocking core K. Reserved as
optional; first cut of K is fine without it. Listed here so the
flexibility model is complete on paper.

#### `IResourceLinearization` — serialize mappings on a shared resource

Palm device, USB-attached store, or any device that can answer one
client at a time. The engine's existing `MappingScheduler` (G.6 Task
44) already supports resource-keyed FIFO, but only for cancellation
("ResourceLost" propagation). K extends it for *scheduling*:

```cpp
class IResourceLinearization {
public:
    virtual ~IResourceLinearization() = default;
    /// Resource key. Mappings whose source or target backend returns
    /// the same key from this method must run serially. Empty =
    /// no constraint, can run in parallel.
    virtual QString linearizationKey() const { return {}; }
};
```

No backend implements it today. A future `PalmHotSyncBackend` returns
e.g. `"palm:/dev/ttyUSB0"`; if two mappings target the same Palm,
the scheduler runs them in series. The engine's queue driver (`processQueue` /
`advanceQueue`, syncengine.cpp:741+) already iterates mappings; it
gains a "wait until this key is free" hook. **In-scope for K.4** —
this is where the user's "Palm linearity" axis lands concretely.

#### Conflict scenarios — already flexible enough

`Sync::QSyncCore::ConflictHandlerRegistry` (engine.cpp:396) lets
backends register their own per-domain handlers. `ConflictPolicy`
covers SourceWins / TargetWins / AskUser / Newest. `ExecutionOverride`
covers per-call mirror direction. **No K work needed on the
conflict-handling axis itself** — the abstraction is already sound.
What's needed is **clean per-domain overrides**: contacts may want
property-level merge that calendar's `IRecordMerger` doesn't fit. The
existing `DomainPlugin::createCanonicalMerger()` already addresses
this — calendar gets `IcalRecordMerger`, contacts gets `VCardMerger`.

#### Baselines — unify

§3.5 below covers baseline unification end-to-end. The engine surface
already handles two distinct baseline stores; K reduces it to one,
domain-extensible for property baselines.

#### Summary table — sync-situation flexibility under K

| Axis | Today | After K |
|---|---|---|
| Per-collection change detection | qobject_cast to RemoteCalendarBackend (CTag) or LocalBackend (fingerprint) | `IChangeDetection` capability — every backend opts in or doesn't |
| Per-record change detection | `BackendRecord::contentHash` (requires fetch) | `IRecordRevision` capability (optional, K.5+) |
| Linearization (Palm-style) | Not modeled — multiple mappings to same Palm device would race | `IResourceLinearization` capability + scheduler enforcement |
| Parallelism | Engine runs mappings serially via FIFO queue (`MappingScheduler`) | Same; parallelism is gated by linearization keys (default empty = parallelizable) |
| Baselines | Two stores: `CalendarBaselineStore` (calendar-typed) + `BlobBaselineStore` v3 (generic) | One store: `BaselineStore` v3 (renamed). Domain plugin declares property-baseline shape. |
| Conflict handlers | `ConflictHandlerRegistry`, `ConflictPolicy`, `ExecutionOverride`, per-domain `IRecordMerger` | Same — already flexible. Docstrings updated to drop Palm framing. |
| Cancellation reasons | `CancellationReason::{UserRequested, ResourceLost, Timeout, UnrecoverableError}` | Same — semantics fine, drop Palm framing in docstrings. |

### 3.4 `qobject_cast<CalendarPluginWriter*>` retirement

`syncengine.cpp:2363`:

```cpp
auto *cw = dynamic_cast<Kalburator::Calendar::CalendarPluginWriter*>(writer);
if (cw) {
    cw->setCollection(m_collection);
    // ... apply on worker thread (writer uses BlockingQueuedConnection)
} else {
    // ... apply on backend thread
}
```

This is a writer-threading dispatch, not a domain leak per se, but
it puts calendar-specific knowledge in engine code. K replaces it
with two virtuals on `IRecordWriter`:

```cpp
class IRecordWriter {
public:
    virtual ~IRecordWriter() = default;

    /// Threading model: where should apply() be called from?
    /// - BackendThread (default): engine marshals via QMetaObject::invokeMethod
    ///   to backend thread, calls apply() there.
    /// - WorkerThread: writer manages its own marshalling internally
    ///   (e.g. CalendarPluginWriter uses BlockingQueuedConnection from
    ///   inside apply()). Engine calls apply() from worker thread.
    enum class Threading { BackendThread, WorkerThread };
    virtual Threading threading() const { return Threading::BackendThread; }

    /// Optional pre-apply hook for writer-specific setup that needs
    /// engine-side context (current collection, transcoding plan, etc.).
    /// Engine calls this once before apply(); default no-op.
    virtual void prepareForApply(const ApplyContext &ctx) {}

    virtual bool apply(...) = 0;
};
```

Engine routes generically based on `writer->threading()`.
`CalendarPluginWriter` returns `WorkerThread` and reads
`ctx.calendarCollection` in `prepareForApply` instead of getting
called via `setCollection` from engine code. `DefaultBlobWriter`
returns `BackendThread`.

This also removes the **Phase J Task 9 root cause**: in `prepareForApply`,
the calendar writer can choose to look up a `MemoryCalendar` from
the collection *only if needed* (e.g. for property-side updates),
not as a hard precondition. For palm→caldav, no MemoryCalendar is
needed because the writer just parses `BackendRecord::data` into an
Incidence and pushes it to the CalDAV backend — the MemoryCalendar
was only required by the *legacy* `CalendarDomainAdapter` path.

### 3.5 Baseline unification

`CalendarBaselineStore` (src/calendar/calendarbaselinestore.h) holds:
- iCal-text baselines per (mappingId, uid) → `QString`
- Property-JSON baselines per (mappingId, calendarId) → `QString`
- Last-sync timestamp per mappingId → `QDateTime`

`BlobBaselineStore` v3 (src/journal/blobbaselinestore.h) holds:
- Canonical-record baselines per (mappingId, recordId) → `CanonicalRecord`

K renames `BlobBaselineStore` to `BaselineStore` (it's already
domain-neutral), moves it from `src/journal/` (the journal directory
is for ID mappings, not baselines) to `src/sync/baselinestore.h`,
and extends:

- iCal-text baselines fold in: per (mappingId, recordId) where
  recordId = the incidence UID. The `CanonicalRecord::data` carries
  the canonical incidence bytes; `CanonicalRecord::shape` distinguishes
  calendar vs blob vs contacts. Calendar plugin's `IRecordDiffer`
  reads it back as iCal text.
- Property baselines fold in via a sibling table `collection_baselines`
  keyed by (mappingId, collectionId) → `QVariantMap`. Domain plugin
  declares the property shape via a virtual (`baselineProperties()`).
- Last-sync timestamp moves to a `mapping_metadata` table on the
  same store.

Migration: a new SQLite version (user_version=5) with idempotent
in-place upgrade. Calendar tests that consume `CalendarBaselineStore`
get a thin compatibility facade *during* the K.4 transition; the
facade goes away by K.5.

### 3.6 Consumer-named types — disposition

| Type | Where | Disposition |
|---|---|---|
| `ExecutionOverride` (synctypes.h:393) | Public API | **Keep semantics, rewrite docstring.** Per-call mirror direction is generic. Drop the "WildPalms's Tools-menu Copy Palm→PC" framing; just say "per-call mirror-direction override for one-way sync runs." |
| `CancellationReason::ResourceLost` (syncenginefuture.h:13) | Public API | **Keep, rewrite docstring.** Drop "Palm cradle disconnect" — say "a backend resource became unavailable (network drop, server gone, removable device unplugged)." |
| `IDMapping::sourceCategory` + `targetCategories` + `archived` (idmappingstore.h:38–40) | Public API | **Keep, rewrite docstring.** "Palm-shaped backends only" → "optional category metadata; some backends use this to round-trip native categories" (iCal CATEGORIES, vCard CATEGORIES, Palm category bytes — all map). The schema's "WildPalms-specific columns" comment in the file header gets updated to "category-aware columns." |
| `qobject_cast<RemoteCalendarBackend>` / `qobject_cast<LocalBackend>` (syncengine.cpp:611–720) | Internal | **Replace** with `dynamic_cast<IChangeDetection*>` per §3.3. |
| `qobject_cast<RemoteCalendarBackend>` / `qobject_cast<LocalBackend>` elsewhere | Search-and-destroy in K.4 | Audit for any remaining occurrences and convert to capability-based dispatch. |

### 3.7 Q_OBJECT on `DomainPlugin` (a small consequence)

Today `DomainPlugin` is a plain abstract class. K wants it to emit
typed signals (proxying calendar-typed events the backends used to
emit directly). Two options:

- **Option A**: make `DomainPlugin` a `QObject`. Then plugins are
  reference-typed; `DomainRegistry` returns `QObject*`-like pointers;
  signal/slot wiring works.
- **Option B**: keep `DomainPlugin` as POD; have it return a separate
  `DomainPluginSignals` QObject when asked.

Option A is simpler and matches Qt conventions. Cost: any external
code that constructs `DomainPlugin` instances (currently just
`DomainRegistry`'s static initializers) needs `QObject` parents.
**Recommend Option A.**

---

## 4. Migration sequence — with verification gates

Per-task tagging on `refactor/engine-merger`. Each task small enough
to verify by reading the diff. If a gate fails, the task isn't done;
no advancing.

### K.1 — Introduce capability interfaces (no behavior change)

**Files added:**
- `src/sync/ichangedetection.h`
- `src/sync/iresourcelinearization.h`
- (Optional `src/sync/irecordrevision.h` deferred to K.5+.)

**Files changed:**
- `RemoteCalendarBackend` and `LocalBackend` implement
  `IChangeDetection` returning their existing CTag/fingerprint
  values. No engine changes yet.
- `RemoteContactsBackend` implements `IChangeDetection` (CardDAV
  CTag — the contacts side of CalDAV's CTag mechanism is currently
  unused; trivial to wire).

**Gate:** verify-all green; no behavior change. The new interfaces
are present but unused by the engine.

**Tag:** `v0.30-phase-k1-capabilities` (replaces the old reserved
`v0.30-phase-j-wildpalms-providers` slot, which is freed by Phase J
deferral).

### K.2 — Engine fast-path consumes `IChangeDetection`

**Files changed:**
- `syncengine.cpp:prepareSyncFastPath` — replace
  `qobject_cast<RemoteCalendarBackend>` / `qobject_cast<LocalBackend>`
  with `dynamic_cast<IChangeDetection*>`. `FreshSyncState` collapses
  to `{sourceRevision, targetRevision}`.
- Engine writes the new revision back via `IChangeDetection::primeRevisionCache`
  on success (today: `RemoteCalendarBackend::primeCtagCache`).

**Gate:**
- All existing tests pass.
- `grep -n 'qobject_cast<RemoteCalendarBackend\|qobject_cast<LocalBackend' src/engine/` returns empty.

**Tag:** `v0.31-phase-k2-fastpath`

### K.3 — Contacts witness lights up

**Files added:**
- `tests/contacts/tst_contacts_engine_witness.cpp` — full pipeline
  test using `KalburatorDomainContacts` + a fake `IBlobBackend` +
  vCard4 transform + `DefaultBlobWriter`. Fetches, diffs, conflicts,
  applies, cancels.
- Possibly `tests/contacts/fakecontactsbackend.{h,cpp}` if
  `MockBlobBackend` doesn't suffice as fake.

**Files changed:**
- `tests/contacts/CMakeLists.txt` — define a test target that
  **does not** link `KF6CalendarCore`. The CMake guard is the witness:
  if KCalendarCore is needed transitively, the link fails.

**Gate:**
- Witness test passes.
- `nm` (or equivalent) on the test binary shows zero `KCalendarCore::*`
  symbols. Documented via a small CMake test that greps the link
  command.

**Tag:** `v0.32-phase-k3-contacts-witness`

### K.4 — Lift calendar virtuals off `SyncBackend`

**Files renamed/moved:**
- `src/calendar/syncbackend.{h,cpp}` → `src/sync/syncbackendbase.{h,cpp}`
  with the slim contract from §3.1.

**Files changed:**
- `src/calendar/calendardomainplugin.{h,cpp}` — gains all the lifted
  methods. Becomes a `QObject` (Option A from §3.7).
- All non-calendar backends (`RawFilesBackend`, `GenericSqliteBackend`,
  `MockBackend`, `LocalBlobBackend`, `MockBlobBackend`) — drop their
  no-op calendar-stub overrides.
- All calendar backends (`LocalBackend`, `RemoteCalendarBackend`,
  `OrgBackend`, `AkonadiBackend`, `HolidaySubscriptionBackend`,
  `DecSyncBackend`, `IcsFeedFetcher`, `SubscriptionBackend`,
  `MockBackend`) — calendar-typed methods become methods on the
  backend that the calendar plugin's wrapper calls into. This is
  the largest mechanical change in K; one backend at a time.
- `IRecordWriter` gains `threading()` and `prepareForApply(ApplyContext)`.
  `CalendarPluginWriter` overrides `threading()` to `WorkerThread`
  and absorbs `setCollection`/`setTranscodingPlan` into
  `prepareForApply`.
- `syncengine.cpp` — `dynamic_cast<CalendarPluginWriter*>` deleted;
  generic threading routing.

**Gate:**
- `verify-all.sh` green (libkalburator + PlanStan + WildPalms).
- `grep -nE '<KCalendarCore' src/sync/syncbackendbase.h` returns empty.
- `grep -nE 'loadCalendars|storeCalendars|startSync.*MemoryCalendar|pushItems.*Incidence::Ptr|getRawIcs|setRawIcs|analyzeRecurrenceLoss|discoveredCalendarType|calendarColor|calendarDescription' src/sinks/ src/blob/ src/contacts/` returns empty.
- `dynamic_cast<CalendarPluginWriter*>` no longer in syncengine.cpp.
- Phase J Task 9 test (palm→caldav direction) passes — root cause
  fixed by writer not requiring host MemoryCalendar.

**Tag:** `v0.33-phase-k4-syncbackend-lift`

### K.5 — Baseline unification

**Files renamed:**
- `src/journal/blobbaselinestore.{h,cpp}` → `src/sync/baselinestore.{h,cpp}`,
  class `BlobBaselineStore` → `BaselineStore`.

**Files changed:**
- New SQLite schema version 5 with `collection_baselines` and
  `mapping_metadata` tables. v4→v5 idempotent migration on open.
- `CalendarBaselineStore` becomes a thin compatibility facade over
  `BaselineStore` (constant-time mechanical translation), then —
  in the same task — its callers migrate to `BaselineStore` directly,
  and the facade is deleted.
- Calendar property-baseline reads/writes route through
  `CalendarDomainPlugin::baselineProperties()`.

**Gate:**
- `verify-all.sh` green.
- `grep -rn 'class CalendarBaselineStore\|CalendarBaselineStore *\*' src/ tests/` returns empty.
- All calendar baseline tests pass against the unified store.

**Tag:** `v0.34-phase-k5-unified-baseline`

### K.6 — Documentation cleanup + consumer-named-type docstring rewrite

**Files changed:**
- `synctypes.h:393` — `ExecutionOverride` docstring rewritten.
- `syncenginefuture.h:13` — `CancellationReason::ResourceLost`
  comment rewritten.
- `idmappingstore.h:5–22` + lines 38–40 — file-level + member
  docstrings rewritten.
- All updated phase status docs reflect K landing.
- `04w-deferred-work.md` revisited; any items closed by K.

**Gate:**
- `grep -rn -i 'WildPalms\|Palm cradle\|Palm-shaped' libkalburator/src/ libkalburator/include/` returns only items in audit-finding files (which by then are moved out of the active tree, see K.6.1 below) or empty. Comments referencing WildPalms in libkalburator code = bug per the audit's strongest finding.

**Sub-task K.6.1**: move the two audit reports
(`2026-05-09-audit-libkalburator-defensive.md`,
`2026-05-09-audit-wildpalms-integrity.md`) and the K.0 notes file
into `libkalburator/docs/phase0/04ab-phase-k-audits/` (so they
survive in the phase doc tree), drop them from the coordination
folder root.

**Tag:** `v0.35-phase-k6-docs`

### K.7 — Ideal-WildPalms architectural design (design only)

Design doc only — no code. Specifies the WildPalms application's
*ideal* architecture against the now-clean libkalburator:

- Single `Sync::SyncEngine`. Native `Sync::SyncEngine` deleted from
  KF6MainWindow. SettingsDialog knobs operate on the library engine
  directly (or are deleted if redundant with library settings).
- Native plugin contract `IConduit` speaks WildPalms application
  vocabulary (Palm record blocks, conduit lifecycle hooks, slot
  semantics). **Plugins do not import libkalburator types.**
  WildPalms's app layer adapts each plugin into a `SyncBackendBase`
  + appropriate `DomainPlugin` for engine consumption.
- Single account/profile/persistence model. `.wildpalms.providers`
  parallel persistence retired.
- `BlobBackendAdapter` deleted (no longer needed once SyncBackend
  has no calendar virtuals).
- `CalendarCollection_WP` deleted (existed only as null-guard
  workaround).
- Dead V1 `BackendPluginManager` deleted.
- Memory-corruption guard in `~KF6MainWindow` investigated and
  resolved at root cause.

Design lives at `libkalburator/docs/phase0/04ac-phase-k7-ideal-wildpalms-design.md`.
Reviewed by user before K.8 begins.

**Tag:** `v0.35.5-phase-k7-design`

### K.8 — WildPalms migration to ideal architecture

Code lands per K.7 design. Largest single set of changes; many
deletions (per the audit's "what we throw away" list). User-driven
breakage of compat with existing WildPalms profiles / plugin
binaries / on-disk format is acceptable per the user's "WildPalms
can be sacrificed" stance.

**Gate:**
- `verify-all.sh` green.
- WildPalms's Phase J Task 9 (`tst_runtime_caldav_e2e`) passes both
  directions (palm→caldav, caldav→palm).
- Stress test (Phase J Task 10) passes.
- CardDAV E2E (Phase J Task 11) passes.
- WildPalms's settings dialog knobs all observably affect sync
  behavior (audit finding #1 closed).

**Tag:** `v0.36-phase-k8-wildpalms-rewrite`

### Closing tag

`v0.40-phase-k-engine-generalized` after K.8 lands clean across all
three repos and `verify-all.sh` is green.

---

## 5. Contacts witness — concrete scope

The K.3 witness must exercise **the full pipeline without
KCalendarCore**. Concretely:

- Two `MockBlobBackend` instances declaring `nativeShapes()` of
  contacts/vcard4. Linked into the test target without
  `KF6CalendarCore`.
- `KalburatorDomainContacts` registered with `DomainRegistry`.
- `vCard3to4Transformation` registered with `TransformationRegistry`
  (already exists at `src/contacts/vcard3to4transformation.h`).
- Five test cases covering:
  1. **Bidirectional add**: A creates contact, B creates different
     contact, sync merges both.
  2. **Update + content-hash skip**: A updates, B sees same contentHash
     baseline, no re-fetch needed.
  3. **Conflict** (AskUser policy): both sides modify same FN/UID,
     test resolves via mocked conflict handler.
  4. **Cancel mid-fetch**: cancel observed, sync teardown clean,
     no half-applied state.
  5. **vCard 3 → vCard 4 transform path**: source backend declares
     vcard3 shape, target declares vcard4, engine routes through
     transformation registry.

CMake target name: `tst_contacts_engine_witness`. Exclusion of
`KF6CalendarCore` is enforced via:

```cmake
add_executable(tst_contacts_engine_witness ...)
target_link_libraries(tst_contacts_engine_witness PRIVATE
    kalburator_contacts kalburator_engine kalburator_shape kalburator_blob
    Qt6::Test KF6::Contacts)
# Deliberately not linked: KF6::CalendarCore.

# Witness gate: link command must not pull KCalendarCore.
add_test(NAME contacts_witness_no_kcal COMMAND
    sh -c "! ldd $<TARGET_FILE:tst_contacts_engine_witness> | grep -q KF6CalendarCore")
```

---

## 6. What this design does NOT solve

To keep the scope honest:

- **Domain extensibility for new domains** beyond calendar/contacts/blob
  is enabled by K but not exercised. A real "memo domain" or "todo
  domain" would benefit from K but is not in scope here. (`04w-deferred-work.md`
  catalogs these.)
- **Plugin-loadable domain plugins** at runtime. Today
  `DomainRegistry` is populated at static-init. K does not change
  this. Out-of-process plugins, `.so` loading, version negotiation —
  all deferred.
- **Streaming-fetch real-time backpressure**. The engine fetches a
  full collection then diffs; K does not introduce per-record
  pipelining beyond what's already there.
- **Multi-source N-way merge**. Engine still operates on pairs.
- **PlanStan's library-decomposition story** (PlanStan is splitting
  *itself* into 11 sub-libraries per its CLAUDE.md). K assumes
  PlanStan stays monolithic during this work; PlanStan-internal
  decomposition is orthogonal.

---

## 7. Risk and mitigation

**Risk: K.4 is the largest change and touches ~10 backends.**
Mitigation: per-backend commits within K.4. Each backend's
calendar-virtual lift is its own small commit with verify-all green
between commits. Can pause mid-K.4 if anything reveals a deeper
issue.

**Risk: Renaming `SyncBackend` → `SyncBackendBase` and moving the
file breaks PlanStan and WildPalms includes.**
Mitigation: leave `src/calendar/syncbackend.h` as a deprecated
forwarding header that includes the new location and `using`-aliases
the new name. Remove in K.6 after PlanStan/WildPalms migrate their
includes. (PlanStan grep showed `Kalburator::Sync::SyncBackend`
forward-declarations everywhere; mechanical sed is fine.)

**Risk: Baseline migration corrupts user data.**
Mitigation: SQLite v4→v5 migration is idempotent + leaves v4 tables
in place during K.5. Verified by a dedicated migration test against
fixtures of v3 and v4 databases. If migration fails, engine falls
back to "no baseline available" (forces full sync, no data loss —
just performance).

**Risk: WildPalms's "ideal architecture" design (K.7) takes longer
than expected and blocks K.8.**
Mitigation: K.1–K.6 land independently of K.7/K.8. The library is
generalized at the K.6 tag regardless. K.8 can slip without holding
up the library.

---

## 8. Locked answers (user, 2026-05-09)

1. **Q1 — Capability interfaces.** ✓ Locked. K introduces
   `Backend::ChangeDetection`, `Backend::ResourceLinearization`,
   and (deferred to K.5+) `Backend::RecordRevision` as side-interfaces;
   backends opt in via inheritance, engine consumes via `dynamic_cast`.
   Naming follows the semantic cleansing convention (no `I` prefix).
2. **Q2 — `DomainPlugin` becomes a `QObject`.** ✓ Locked. Plugins
   gain typed signals (proxying generic backend signals into
   domain-shaped events). Per the convention, the abstract plugin
   class becomes `Kalburator::Shape::AbstractDomainPlugin` (Qt-style)
   if any concrete subclasses end up in the public API as
   `DomainPlugin*`-typed; otherwise just `DomainPlugin` is fine.
   Audit during K.4.
3. **Q3 — `BaselineStore` (drop the Blob).** ✓ Locked. `BlobBaselineStore`
   → `Storage::BaselineStore` per the semantic cleansing proposal.
4. **Q4 — No migration safety tool.** ✓ Locked. SQLite v4→v5
   migration is idempotent; "no baseline" fallback is full-sync.
   No export/import recovery tool unless field corruption is
   actually observed.
5. **Q5 — K.7 in parallel with K.4–K.6.** ✓ Locked. The library
   lift in K.4 doesn't block WildPalms-side design work; K.7's
   ideal-WP design proceeds concurrently and is ready when K.6
   tags.

---

## 9. After this doc

K.0 design is **landed**. K.1 plan doc:
`libkalburator/docs/phase0/04ab-phase-k1-plan.md` (written
2026-05-09 alongside the start of K.1 execution). Code lands on
`refactor/engine-merger`.

Coordination doc updates landed simultaneous with K.0:
- `CURRENT-STATUS.md` — Phase J deferred, Phase K active. ✓
- `FINDINGS.md` — K.0 dialectic outcome appended. ✓
- `2026-05-09-phase-j-replan-plan.md` — DEFERRED banner at top. ✓
