---
status: design
date: 2026-04-30
phase: G
companion-to: 04r-phase-g-plan.md (siblings)
supersedes-portion-of: 04k-engine-merger-roadmap.md (Phase G section)
---

# Phase G — Shape pipeline architecture (design)

**Status:** Design. The canonical specification of the post-Phase-G
libkalburator architecture. Formed from the 04r ideation cycle:
`04r-phase-g-shape-pipeline-ideation.md`,
`04r-phase-g-walkthrough.md` (Alice),
`04r-phase-g-walkthrough-wildpalms.md` (Bob), and
`04r-phase-g-walkthrough-migration.md`. This document supersedes
the Phase G framing in `04k-engine-merger-roadmap.md`.

The companion plan doc (`04r-phase-g-plan.md`) takes this design
and expands it into a task-level checklist thorough enough for a
new agent in a new context to implement.

## Goal

Land the **scoped shape-pipeline architecture** in libkalburator:

- The engine deals in **records-with-shape** rather than typed
  domain values. Calendar logic, blob logic, contacts logic, etc.
  become registered domain plugins, not hardcoded engine paths.
- Backends declare their **native shapes** and an optional
  **resource ID** for exclusivity scheduling. The engine routes
  records through a **transformation pipeline** computed by a
  registry of edges between shapes.
- The unified `SyncEngine` from F1+F2 grows a **resource-aware
  scheduler**, a **subset-form `runSyncFuture(QList<MappingId>)`**,
  and a **`SyncEngineFuture` wrapper** with reasoned cancellation.
- The deprecated synchronous I/O surface
  (`loadItems`/`storeItems`/`updateItem`/`writeFinished`) and the
  F1 transitional facade (`runBlobTwoWay`/`runBlobMirror`) are
  deleted.
- WildPalms's `SyncRunner_wp` dissolves into a thin
  `HotSyncCoordinator` that fires `runSyncFuture(palmMappingIds)`
  on device-connect; per-Palm-DB backends become first-class
  `SyncBackend`s.
- `ISyncHost` narrows to a generic event-sink interface;
  calendar-typed methods retire.
- Stock libkalburator ships with property-aware diffs/mergers and
  edge registrations for **calendar, contacts, memo, todo**;
  cross-domain edges are **not** registered in the stock library
  but the architecture supports them as a future extension.

The user-visible result: WildPalms gains all of PlanStan's complex
multi-PIM coordination features as a side effect of consuming the
unified engine. Adding a new PIM type (bookmarks, RSS, …) becomes
a 2-edge registration plus a stock backend or two, not an engine
change.

## Scope

### In v1 (this design)

- Property catalogues per shape
- `Shape` value type with a `Shape::Any` sentinel for universal
  sinks
- `TransformationRegistry` with hub-and-spoke per-domain edge
  topology
- Shape-typed `IRecordDiffer` / `IRecordMerger` interfaces
- `SyncBackend::nativeShapes()` and `resourceId()`
- Mapping-keyed baselines (`BlobBaselineStore` v3 schema)
- `MappingScheduler` with capacity-1 per resource (sequential
  execution within a connected resource component)
- `runSyncFuture(QList<MappingId>)` subset-form
- `SyncEngineFuture` wrapper with `cancelWithReason(...)`
- `CancellationReason` enum
- Universal sinks: `RawFilesBackend`, `GenericSqliteBackend`
- Loss profile reporting (structured `QSet<PropertyId>`) plumbed
  through `ISyncHost::syncStarted`
- `WhenLossWouldOccur` per-mapping field
- First-sync policy field with `RicherSideWins` option
- Intra-domain richness rank declared by domain plugins
- `ISyncHost` narrowed to ~7 generic methods
- `HotSyncCoordinator` reference impl in WildPalms
- Stock new backends: `AkonadiContactsBackend`, `AkonadiNotesBackend`,
  `AkonadiTasksBackend`, `CardDAVRemoteBackend`
- Stock new domain plugins: contacts, memo, todo (calendar already
  exists; gets re-expressed as a plugin)

### Deferred to future phases (post-G)

- **Cross-domain edges in the stock library.** Architecture supports
  them; library doesn't ship any. Adding edges later is a 2-line
  `registry.registerEdge(...)` call per edge.
- **Cross-domain richness rank** — only needed when cross-domain
  edges exist.
- **Mapping groups** — N-way mapping abstraction with shared
  baseline. Architecture-compatible; opt-in field on `SyncMapping`
  stays unused for v1.
- **Capacity-N resource pools** — initial scheduler is capacity-1
  per resource; raising to N is a follow-up.
- **Cross-group concurrent execution** — initial scheduler runs
  sequentially across resource groups; concurrent disjoint-group
  execution is a follow-up capacity bump.
- **Dynamic plugin loading** — initial design is static-init only;
  user-installable `.so` plugins deferred.
- **Auto-derived property catalogues** from Qt MOC introspection
  or KCalendarCore/KContacts type reflection.
- **Schema-evolution UX** for `GenericSqliteBackend` when a shape's
  property catalogue changes between library versions.

## Architectural overview

```
┌──────────────────────────────────────────────────────────────────┐
│                      Consumer (PlanStan / WildPalms)             │
│   ┌─────────────────────────────────────────────────────────┐    │
│   │ Mapping registry (per-profile)                          │    │
│   └────────────────────────┬────────────────────────────────┘    │
│                            │ MappingId list                      │
│                            ▼                                     │
│   ┌─────────────────────────────────────────────────────────┐    │
│   │ HotSyncCoordinator / scheduled trigger / manual button  │    │
│   └────────────────────────┬────────────────────────────────┘    │
└────────────────────────────┼─────────────────────────────────────┘
                             │ runSyncFuture(ids) → SyncEngineFuture
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  SyncEngine                                                      │
│   ┌──────────────────────────────────────────────────────────┐   │
│   │ MappingScheduler (resource graph; capacity-1 per resource)│  │
│   └────────────────┬─────────────────────────────────────────┘   │
│                    │ one mapping at a time per resource group    │
│                    ▼                                             │
│   ┌──────────────────────────────────────────────────────────┐   │
│   │ SyncEngineWorker (private QThread, F1+F2 invariant)      │   │
│   │   1. Fetch source records (native shape)                 │   │
│   │   2. Compile pipeline source-shape → canonical-shape     │   │
│   │   3. Promote to canonical                                │   │
│   │   4. Compute 3-way diff at canonical (against baseline)  │   │
│   │   5. Resolve conflicts per mapping policy                │   │
│   │   6. Compile pipeline canonical-shape → target-shape     │   │
│   │   7. Project & push to target                            │   │
│   │   8. Update baseline                                     │   │
│   └────────────────┬─────────────────────────────────────────┘   │
│                    │                                             │
│                    ├─ TransformationRegistry (compile pipelines) │
│                    ├─ DomainPlugin instances (own edges)         │
│                    ├─ IRecordDiffer / IRecordMerger (per shape)  │
│                    ├─ SyncBackend::pushItems / fetchItems        │
│                    └─ BlobBaselineStore v3 (mapping-keyed)       │
└──────────────────────────────────────────────────────────────────┘
```

The bend from F2 → G is conceptually small at the engine level: the
worker still owns its private QThread; the `pushItems`/`fetchItems`/
`deleteItems` operation handles still drive backend I/O; the F2
cancellation channel still propagates. What changes is **what the
worker does between fetch and push**: it routes records through
*pipelines* instead of through a hardcoded calendar adapter.

## Vocabulary

### Shape

```cpp
namespace Kalburator::Shape {

class DomainId {
    QString m_id;
public:
    DomainId() = default;
    explicit DomainId(QString id) : m_id(std::move(id)) {}
    QString toString() const { return m_id; }
    bool operator==(const DomainId&) const = default;
    // ... QHash support ...
};

class EncodingId {
    QString m_id;
public:
    // same shape as DomainId
};

struct Shape {
    DomainId domain;
    EncodingId encoding;

    static Shape Any();              // sentinel; .isAny() returns true
    bool isAny() const noexcept;

    bool operator==(const Shape&) const = default;
    bool operator!=(const Shape&) const = default;
    QString toString() const;        // "<domain>+<encoding>" or "any"
};

uint qHash(const Shape&, uint seed = 0) noexcept;

}  // namespace
```

`Shape::Any()` is a real value, not a wildcard. Backends that
return `{ Shape::Any() }` from `nativeShapes()` are universal
sinks; the engine treats them with identity-passthrough pipelines
regardless of source shape. Universal sinks must store shape
metadata alongside record bytes (see `RawFilesBackend` and
`GenericSqliteBackend` contracts below).

### PropertyId and PropertyCatalogue

```cpp
namespace Kalburator::Shape {

class PropertyId {
    QString m_id;
public:
    explicit PropertyId(QString id) : m_id(std::move(id)) {}
    QString toString() const { return m_id; }
    bool operator==(const PropertyId&) const = default;
    // ... QHash support ...
};

enum class PropertyKind {
    String,
    Integer,
    Boolean,
    DateTime,
    Duration,
    Bytes,
    StringList,
    Json,            // for nested or composite values (attendees, etc.)
};

struct PropertyDescriptor {
    PropertyId id;
    PropertyKind kind;
    QString displayName;             // for loss-profile UX
    bool optional = true;
};

class PropertyCatalogue {
public:
    const QList<PropertyDescriptor>& properties() const;
    bool hasProperty(const PropertyId&) const;
    const PropertyDescriptor* find(const PropertyId&) const;
    QStringList sqlColumnDdl() const;     // for GenericSqliteBackend
};

}  // namespace
```

Each registered shape has exactly one `PropertyCatalogue`. The
catalogue is hand-written per shape (auto-derivation deferred).
Catalogues drive:

1. **Diff key set.** A shape-typed differ inspects properties named
   in the catalogue; properties absent from the catalogue are
   ignored.
2. **Loss profile reporting.** A `TransformationEdge` whose
   `dropped` set references properties that exist in the source's
   catalogue but not in the target's catalogue.
3. **Schema generation.** `GenericSqliteBackend` reads the catalogue
   to create `<domain>_<encoding>` tables.
4. **Source-shape-unrepresentable diff drop.** When computing diffs
   at canonical shape, any property the source's *native* shape
   cannot represent is dropped from that side's diff (per Bob
   Scene 5).

### LossProfile

```cpp
namespace Kalburator::Shape {

enum class LossLevel {
    Lossless,            // pure encoding round-trip; no information lost
    IntraDomainLossy,    // same domain; capability gap (e.g., RRULE not preserved)
    InterDomainProjection,  // different domain; structural reduction
    Degenerate,          // different domain; only name-like field preserved
};

struct LossProfile {
    LossLevel level = LossLevel::Lossless;
    QSet<PropertyId> dropped;        // properties this pipeline drops
    QString summary() const;         // human-readable for UX

    bool isLossless() const { return level == LossLevel::Lossless; }
    LossProfile compose(const LossProfile& downstream) const;
        // Used when stacking edges into a pipeline; takes max level
        // and unions dropped sets.
};

}  // namespace
```

### TransformationEdge and Pipeline

```cpp
namespace Kalburator::Shape {

class TransformationStage {
public:
    virtual ~TransformationStage() = default;
    /// Transform record bytes from `from` shape to `to` shape.
    /// Implementation should be pure and free of I/O.
    virtual QByteArray transform(const QByteArray& sourceBytes) const = 0;
};

struct TransformationEdge {
    Shape from;
    Shape to;
    LossProfile loss;
    std::shared_ptr<TransformationStage> stage;

    QString toString() const;  // "calendar+ical → calendar+org [intra-lossy: drops attendees, attachments, …]"
};

class Pipeline {
    Shape m_inputShape;
    Shape m_outputShape;
    QList<TransformationEdge> m_edges;
public:
    Shape inputShape() const { return m_inputShape; }
    Shape outputShape() const { return m_outputShape; }
    LossProfile composedLoss() const;  // composition of edge losses
    QByteArray apply(const QByteArray& input) const;

    /// True iff this pipeline is the trivial identity (Shape::Any sink
    /// or same-shape source/target).
    bool isIdentity() const;

    /// Edges in compose order (source-to-target). Empty for identity.
    const QList<TransformationEdge>& edges() const { return m_edges; }
};

}  // namespace
```

### CanonicalRecord

```cpp
namespace Kalburator::Shape {

/// A BackendRecord plus the shape its bytes are in.
/// Used wherever the engine handles records mid-pipeline.
struct CanonicalRecord {
    Shape shape;
    QByteArray data;
    QString recordId;     // logical id; matches BackendRecord::id
    bool isDeleted = false;
};

}  // namespace
```

The engine's worker holds `CanonicalRecord`s during the merge step;
the `IRecordDiffer` / `IRecordMerger` plugins operate over them.

## Subsystem contracts

### TransformationRegistry

```cpp
namespace Kalburator::Shape {

class TransformationRegistry {
public:
    static TransformationRegistry& instance();

    /// Register a transformation edge. Idempotent on (from, to)
    /// re-registration with identical params; assertion failure on
    /// (from, to) re-registration with conflicting params.
    void registerEdge(TransformationEdge edge);

    /// Register a property catalogue for a shape. Required before
    /// any edge involving that shape can be registered.
    void registerShape(Shape shape, PropertyCatalogue catalogue);

    /// Look up the catalogue for a shape. Returns nullptr if
    /// unregistered.
    const PropertyCatalogue* catalogueFor(const Shape&) const;

    /// Compile a pipeline. Returns std::nullopt if no path exists.
    /// Hub-and-spoke topology means "no path" can mean either
    /// truly unrelated shapes (e.g., calendar to plucker without
    /// cross-domain edges) or that one of the shapes has no
    /// canonical hub registered.
    std::optional<Pipeline> compile(Shape from, Shape to) const;

    /// Inspect a pipeline's loss profile without materialising it.
    LossProfile inspect(Shape from, Shape to) const;

    /// Return all registered shapes (for debugging / UX).
    QList<Shape> registeredShapes() const;

    /// Return all edges out of a shape.
    QList<TransformationEdge> edgesFrom(const Shape&) const;

private:
    TransformationRegistry() = default;
    // ... internal data ...
};

}  // namespace
```

**Topology:** hub-and-spoke per domain. Each domain declares one
**canonical shape** (e.g., `(calendar, ical)` for the calendar
domain). Edges connect:

- Native shapes ↔ canonical shape (intra-domain, possibly lossy)
- Canonical ↔ canonical for the *same* domain only (identity)

In v1, **no cross-domain edges are registered.** A future cross-
domain extension would register canonical-to-canonical edges
between different domains, with `LossLevel::InterDomainProjection`
or `Degenerate`. The architecture is forward-compatible.

**Pipeline compilation algorithm:** for `compile(from, to)`:

1. If `to.isAny()`: return identity pipeline (universal sink).
2. If `from.isAny()`: error (can't compile a pipeline starting from
   an unknown shape).
3. If `from == to`: return identity.
4. Find the canonical hub for `from.domain`: lookup `(from.domain,
   "canonical")` shape in the registry. Likewise for `to.domain`.
5. If `from.domain != to.domain`: cross-domain pipeline. In v1,
   return `nullopt` (no cross-domain edges in stock).
6. Compose: `from → from.domain.canonical → to`. Each leg is one
   registered edge. Compose loss profiles.

The algorithm is intentionally simple. Future cross-domain support
can extend step 5 with a graph search.

### DomainPlugin

```cpp
namespace Kalburator::Shape {

class DomainPlugin {
public:
    virtual ~DomainPlugin() = default;

    /// Domain id this plugin owns (e.g., "calendar", "contacts").
    virtual DomainId domain() const = 0;

    /// The canonical shape for this domain.
    virtual Shape canonicalShape() const = 0;

    /// Other shapes this domain plugin knows about. Edges to/from
    /// the canonical shape are registered for each.
    virtual QList<Shape> peerShapes() const = 0;

    /// Property catalogue for the canonical shape.
    virtual PropertyCatalogue canonicalCatalogue() const = 0;

    /// Property catalogues for the peer shapes (one per peer).
    virtual PropertyCatalogue catalogueFor(const Shape&) const = 0;

    /// Construct the differ for the canonical shape.
    virtual std::unique_ptr<IRecordDiffer> createCanonicalDiffer() const = 0;
    virtual std::unique_ptr<IRecordMerger> createCanonicalMerger() const = 0;

    /// Register all of this plugin's edges with the registry.
    /// Called once at static-init time per process.
    virtual void registerEdges(TransformationRegistry& registry) = 0;

    /// Intra-domain richness rank. Domains with multiple shapes
    /// declare a partial order so first-sync `RicherSideWins` can
    /// pick the more expressive side.
    virtual int richnessRank(const Shape&) const = 0;
};

}  // namespace
```

Plugins are registered via a `DomainRegistry` static singleton at
program start. Each stock plugin (`KalburatorDomainCalendar`,
`KalburatorDomainContacts`, `KalburatorDomainMemo`,
`KalburatorDomainTodo`) calls `DomainRegistry::registerDomain(this)`
in its constructor; the constructor in turn calls `registerEdges()`
to populate the `TransformationRegistry`.

Stock library opinionatedly ships only the four domains above for
v1. Cross-domain edges are not registered. User code that wants
cross-domain or wants additional domains writes its own
`DomainPlugin` subclass and registers it before using the engine.

### IRecordDiffer and IRecordMerger

```cpp
namespace Kalburator::Shape {

class IRecordDiffer {
public:
    virtual ~IRecordDiffer() = default;

    /// Compute a per-property delta between source and baseline.
    /// Returns set of properties that differ.
    virtual QSet<PropertyId> diff(const CanonicalRecord& source,
                                   const CanonicalRecord& baseline) const = 0;

    /// True iff the records are byte-identical OR semantically
    /// equivalent under the differ's notion of equality.
    virtual bool equal(const CanonicalRecord& a,
                        const CanonicalRecord& b) const = 0;
};

class IRecordMerger {
public:
    virtual ~IRecordMerger() = default;

    enum class FieldChoice {
        TakeSource,
        TakeTarget,
        TakeBaseline,
    };

    /// 3-way merge. For each property in the union of source/target/
    /// baseline catalogues, decide TakeSource/TakeTarget/TakeBaseline
    /// per the supplied conflict policy.
    virtual CanonicalRecord merge(const CanonicalRecord& source,
                                   const CanonicalRecord& target,
                                   const CanonicalRecord& baseline,
                                   ConflictPolicy policy) const = 0;
};

}  // namespace
```

Both are shape-typed: each canonical shape has exactly one differ
and one merger, owned by its domain plugin.

### SyncBackend (post-bend)

```cpp
namespace Kalburator::Sync {

class SyncBackend : public QObject {
    Q_OBJECT
public:
    // ===== Identity =====
    virtual QString backendType() const = 0;
    virtual QString id() const = 0;

    /// Shapes this backend natively produces and consumes. May
    /// include `Shape::Any()` for universal sinks. May include
    /// multiple shapes (multi-domain backends).
    virtual QList<Shape> nativeShapes() const = 0;

    /// Resource ID for exclusivity scheduling. Default returns a
    /// per-instance unique value. Backends sharing an underlying
    /// scarce resource (e.g., multiple Palm DBs sharing one device)
    /// override this to return the same value.
    virtual QString resourceId() const;  // default impl provided

    // ===== Discovery =====
    virtual void loadCalendars(const QString& collectionId) = 0;
        // existing signal: `calendarDiscovered(collectionId, calendarId)`
        // existing signal: `loadCalendarsFinished(collectionId, success, err)`

    /// Returns the shape of a given collection on this backend.
    /// Used by the engine to type a SyncMapping.
    virtual Shape shapeFor(const QString& collectionId) const = 0;

    // ===== I/O (operation-handle async API only) =====
    virtual FetchOperation* fetchItems(const QString& calendarId) = 0;
    virtual PushOperation* pushItems(const QString& calendarId,
                                      const QList<KCalendarCore::Incidence::Ptr>& items,
                                      const TranscodingPlan& plan = {}) = 0;
    virtual DeleteOperation* deleteItems(const QString& calendarId,
                                          const QStringList& uids) = 0;

signals:
    // ===== Generic events (replace old per-incidence signals) =====
    void recordChanged(const QString& calendarId, const QString& recordId,
                       ChangeKind kind);
    void errorOccurred(const QString& msg);

public:
    // dataDomain() — DELETED in G.3
    // loadItems() — DELETED in G.9
    // storeItems() — DELETED in G.9
    // updateItem() — DELETED in G.9
    // writeFinished signal — DELETED in G.9
};

}  // namespace
```

Notes:

- The Phase E `TranscodingPlan` parameter on `pushItems` survives.
  In G it becomes vestigial when a backend uses the registry
  directly, but stays for one release as a deprecation overlap
  (backends that haven't migrated to using the registry can still
  receive a plan). Marked for removal in a follow-up cleanup.
- `KCalendarCore::Incidence::Ptr` in the I/O signatures is itself
  a temporary calendar-flavoured holdover; full G includes a
  migration to `BackendRecord` everywhere (see "Backend record vs
  Incidence::Ptr" below). For v1 the calendar-flavoured signatures
  are preserved on `pushItems` overloads; new shapes use
  `BackendRecord` directly via additional virtual methods.

### Backend record vs Incidence::Ptr — staged migration

Today's `pushItems` takes `QList<Incidence::Ptr>`. New domains
(contacts, memo, todo) don't have a KCalendarCore equivalent.

**Stage 1 (G.5+G.6):** add a `pushRecords(calendarId, records, plan)`
overload that takes `QList<CanonicalRecord>`. Existing calendar-
flavoured `pushItems` continues to work. New domain backends
implement `pushRecords` only.

**Stage 2 (G.10 cleanup):** all calendar backends migrate to
`pushRecords`; the calendar-flavoured `pushItems(Incidence::Ptr)`
overload is deleted.

This pattern preserves verify-all-green during the migration —
calendar tests using the existing API keep working until the
calendar backends migrate. The migration is mechanical (rewrite
each backend's `pushItems(Incidence::Ptr)` body to serialize to
canonical bytes and call `pushRecords`).

### SyncMapping (post-bend)

```cpp
struct SyncMapping {
    // Existing F2 fields
    MappingId id;
    QString sourceBackendId;
    QString sourceCalendarId;
    QString targetBackendId;
    QString targetCalendarId;
    SyncDirection direction;
    bool enabled = true;

    // New in G
    ConflictPolicy conflictPolicy = ConflictPolicy::AskUser;

    enum class FirstSyncPolicy {
        SourceWins,
        TargetWins,
        RicherSideWins,    // new in G; default for new mappings
        AskUser,
    };
    FirstSyncPolicy firstSyncPolicy = FirstSyncPolicy::RicherSideWins;

    enum class WhenLossWouldOccur {
        Abort,             // refuse to sync if pipeline is lossy
        Warn,              // sync but emit a warning (default)
        Proceed,           // sync silently
    };
    WhenLossWouldOccur whenLossWouldOccur = WhenLossWouldOccur::Warn;

    enum class CorruptRecordPolicy {
        AbortSync,
        SkipRecord,
        TreatAsDeleted,
    };
    CorruptRecordPolicy corruptRecordPolicy = CorruptRecordPolicy::SkipRecord;

    // Optional grouping for future "mapping groups" feature.
    // Mappings sharing a non-empty group id are intended to share a
    // baseline keyspace and N-way merge logic. Unused in v1.
    QString mappingGroupId;
};
```

### BlobBaselineStore v3 schema

```sql
-- v3 schema (G.4 introduces; v2 is post-F1; v1 is pre-F1)
CREATE TABLE IF NOT EXISTS blob_baselines_v3 (
    mapping_id TEXT NOT NULL,
    record_id  TEXT NOT NULL,
    canonical_shape_domain    TEXT NOT NULL,
    canonical_shape_encoding  TEXT NOT NULL,
    canonical_bytes BLOB NOT NULL,
    updated_at INTEGER NOT NULL,
    PRIMARY KEY (mapping_id, record_id)
);

CREATE INDEX IF NOT EXISTS idx_baselines_v3_mapping
    ON blob_baselines_v3 (mapping_id);

PRAGMA user_version = 3;
```

Migration from v2 (post-F1, `blob_baselines_triple` keyed on
`(backend_id, collection_id, record_id)`):

1. Read `PRAGMA user_version`. If < 3, perform migration.
2. For each row in `blob_baselines_triple`:
   - Look up which mapping(s) reference `(backend_id,
     collection_id)` as either source or target.
   - For each such mapping, INSERT a v3 row with the same record
     bytes and `mapping_id` filled in.
   - If no mapping references the pair, the v2 row is orphaned;
     skip and log.
3. Set `PRAGMA user_version = 3`.
4. Optionally drop `blob_baselines_triple` after a release; keep
   for one cycle to allow rollback.

**Migration discipline (per FINDINGS):**

- Use `PRAGMA user_version` as the gate, not `IF EXISTS` against
  a name that exists in both states.
- Verify by re-opening the DB after migration and asserting
  `user_version == 3`. The F1 lesson: per-open migrators must be
  idempotent.
- Document the migration path in a comment block in
  `BlobBaselineStore::ensureSchemaAndVersion`.

### MappingScheduler

```cpp
namespace Kalburator::Sync {

class MappingScheduler {
public:
    /// Schedule a list of mappings for execution. Returns immediately;
    /// the caller observes results via the SyncEngineFuture.
    void schedule(const QList<MappingId>& ids,
                  SyncEngineFuture* future);

    /// Cancel any queued mappings touching the given resource.
    /// Used when a resource becomes unavailable (e.g., Palm cradle
    /// disconnect).
    void cancelMappingsTouchingResource(const QString& resourceId,
                                         CancellationReason reason);

private:
    /// Map mapping id → set of resource ids the mapping uses.
    QHash<MappingId, QSet<QString>> m_mappingResources;

    /// Map resource id → currently-active mapping (if any).
    /// Capacity 1 per resource in v1.
    QHash<QString, MappingId> m_activeOnResource;

    /// FIFO queue of mappings waiting for resources.
    QQueue<QPair<MappingId, SyncEngineFuture*>> m_queue;
};

}  // namespace
```

**Scheduling semantics in v1:**

- For each mapping, compute the *resource set* = the union of
  `resourceId()` from source backend and target backend.
- A mapping is *runnable* iff none of its resources are currently
  active.
- The scheduler runs at most one mapping per resource at a time.
- Across disjoint resource components, mappings still run
  sequentially in v1 (no cross-component concurrency). A future
  bump enables concurrent disjoint-component execution.

**Cancellation rules:**

- `QFuture::cancel()` → `CancellationReason::UserRequested`. Cancels
  the in-flight mapping; lets queued mappings continue (the user
  may want to allow remaining work).
- Internal `cancelWithReason(ResourceLost, resourceId)` cancels the
  in-flight mapping AND short-circuits queued mappings whose
  resource set includes `resourceId`. Their results report
  `cancelled=true, errorMessage="<resource> lost"`.
- Internal `cancelWithReason(UnrecoverableError)` aborts the whole
  queue.

### SyncEngine public API (post-bend)

```cpp
namespace Kalburator::Sync {

class SyncEngine : public QObject {
    Q_OBJECT
public:
    // ===== Existing F2 API (preserved) =====
    SyncEngineFuture runSyncFuture(SyncBehavior behavior = {});
    SyncEngineFuture runSyncFuture(MappingId, SyncBehavior = {});

    // ===== New in G =====
    /// Run a subset of registered mappings, scheduled by the
    /// MappingScheduler. Returns a future that resolves when all
    /// mappings complete.
    SyncEngineFuture runSyncFuture(QList<MappingId> ids,
                                    SyncBehavior behavior = {});

    // Existing streaming signals (preserved):
    //   progressChanged(MappingId, int current, int total, QString msg)
    //   phaseChanged(MappingId, Phase)
    //   conflictDetected(...)

    // DELETED:
    //   void runSync(...)              — F2 already deleted void runSync
    //   cancelSync()                   — F2 already deleted
    //   syncCompleted signal           — F2 already deleted
    //   allSyncsCompleted signal       — F2 already deleted
    //   runBlobTwoWay(...)             — DELETED in G.8
    //   runBlobMirror(...)             — DELETED in G.8
};

}  // namespace
```

### SyncEngineFuture and CancellationReason

```cpp
namespace Kalburator::Sync {

enum class CancellationReason {
    UserRequested,         // QFuture::cancel() default
    ResourceLost,          // e.g., Palm cradle disconnect
    Timeout,               // overall sync took too long
    UnrecoverableError,    // engine-internal failure
};

class SyncEngineFuture {
    QFuture<QList<SyncResult>> m_future;
    QFutureWatcher<QList<SyncResult>>* m_watcher = nullptr;
    SyncEngine* m_engine = nullptr;
public:
    // Convertibility to/from raw QFuture for compatibility.
    operator QFuture<QList<SyncResult>>() const { return m_future; }
    QFuture<QList<SyncResult>> future() const { return m_future; }

    // Forwarding accessors
    bool isFinished() const { return m_future.isFinished(); }
    bool isCanceled() const { return m_future.isCanceled(); }
    QList<SyncResult> result() const;     // see Qt6 quirk in FINDINGS

    // Cancellation with reason.
    void cancel();                         // == cancelWithReason(UserRequested)
    void cancelWithReason(CancellationReason);
    CancellationReason cancellationReason() const;

    // Watcher hookups
    void onFinished(std::function<void(const QList<SyncResult>&)>);
    void onCanceled(std::function<void(CancellationReason)>);
    void onProgress(std::function<void(MappingId, int, int)>);
};

}  // namespace
```

The wrapper is small. The bulk of state stays in the underlying
`QFuture`. The wrapper exists to attach the cancellation reason
side-channel (Qt6 doesn't natively support reasoned cancellation)
and to provide ergonomic onFinished/onCanceled hooks.

### ISyncHost (post-bend)

```cpp
namespace Kalburator::Sync {

class ISyncHost {
public:
    virtual ~ISyncHost() = default;

    // Registry access
    virtual SyncBackend* backendById(const QString& id) = 0;
    virtual QHash<QString, SyncBackend*> backends() = 0;
    virtual ISyncConfigStore* configStore() = 0;

    // Lifecycle events
    virtual void syncStarted(MappingId, const Shape::LossProfile& pipelineLoss);
    virtual void syncFinished(MappingId, const SyncResult&);

    // Per-record events (replace incidence-typed methods)
    enum class ChangeKind { Created, Updated, Deleted };
    virtual void recordChanged(MappingId, const QString& recordId,
                               ChangeKind kind);

    // Conflict resolution (only fired for AskUser policies)
    virtual ConflictResolution resolveConflict(MappingId,
                                                const QString& recordId,
                                                const Shape::CanonicalRecord& source,
                                                const Shape::CanonicalRecord& target,
                                                const Shape::CanonicalRecord& baseline);

    // Progress
    virtual void progressChanged(MappingId, int current, int total,
                                  const QString& msg);
    virtual void phaseChanged(MappingId, Phase);

    // Errors
    virtual void errorOccurred(MappingId, const QString& msg);

    // DELETED methods (was calendar-typed, now consumer's
    // responsibility on receiving recordChanged):
    //   applyIncidenceAddition(...)
    //   applyIncidenceRemoval(...)
    //   applyIncidenceUpdate(...)
    //   collection()
    //   incidenceSource()
    //   incidenceRegistry()
    //   unloadCalendar(...)
    //   generateSyncMappingsFromLogicalCalendars()
};

}  // namespace
```

The shape narrows from 12 to 11 methods (two of the removed ones
are absorbed into `recordChanged`; the rest move into consumers'
own per-domain handling).

### HotSyncCoordinator (consumer-side reference impl)

```cpp
// In WildPalms/src/runtime/hotsynccoordinator.{h,cpp}

namespace WildPalms::Runtime {

class HotSyncCoordinator : public QObject {
    Q_OBJECT
public:
    HotSyncCoordinator(Kalburator::Sync::SyncEngine* engine,
                       Kalburator::Sync::BackendRegistry* registry,
                       PalmDeviceConnection* device,
                       QObject* parent = nullptr);

private slots:
    void onDeviceConnected(const QString& serial);
    void onDeviceDisconnected();

private:
    Kalburator::Sync::SyncEngine* m_engine;
    Kalburator::Sync::BackendRegistry* m_registry;
    std::optional<Kalburator::Sync::SyncEngineFuture> m_currentFuture;
};

}  // namespace
```

The body is ~50 lines. `onDeviceConnected` queries the registry
for mappings touching `palm-device:<serial>` and fires
`runSyncFuture(ids)`. `onDeviceDisconnected` calls
`m_currentFuture->cancelWithReason(CancellationReason::ResourceLost)`.
This *replaces* the current `SyncRunner_wp` (~700 lines).

## Universal sinks

### RawFilesBackend

```cpp
namespace Kalburator::Sinks {

class RawFilesBackend : public Kalburator::Sync::SyncBackend {
public:
    RawFilesBackend(QString rootPath, QObject* parent = nullptr);

    QString backendType() const override { return "raw-files"; }
    QList<Kalburator::Shape::Shape> nativeShapes() const override
        { return { Kalburator::Shape::Shape::Any() }; }
    QString resourceId() const override
        { return "raw-files:" + m_rootPath; }

    // I/O implementation:
    //   Each record is stored at <rootPath>/<recordId>.<encoding>.<domain>
    //   plus a manifest at <rootPath>/_shapes.json for fast re-discovery.
    //   When used as a *source*, parses filenames to recover shape.
    //   When parsing fails (filename mismatch or shape registry doesn't
    //   know the encoding), the corruptRecordPolicy of the mapping
    //   determines behavior.

    // ... fetchItems, pushItems, deleteItems, etc.
};

}  // namespace
```

### GenericSqliteBackend

```cpp
namespace Kalburator::Sinks {

class GenericSqliteBackend : public Kalburator::Sync::SyncBackend {
public:
    GenericSqliteBackend(QString dbPath, QObject* parent = nullptr);

    QString backendType() const override { return "generic-sqlite"; }
    QList<Kalburator::Shape::Shape> nativeShapes() const override
        { return { Kalburator::Shape::Shape::Any() }; }
    QString resourceId() const override
        { return "generic-sqlite:" + m_dbPath; }

    // Schema model:
    //   _shapes (shape_domain, shape_encoding, properties_json,
    //             created_at) — registry of shapes seen.
    //   <domain>_<encoding> tables — one per shape, columns derived
    //     from the shape's PropertyCatalogue.
    //
    // First push of a new shape triggers `CREATE TABLE` and a row
    // in _shapes. Subsequent pushes go to the existing table.
    //
    // Schema evolution (catalogue changes between library versions)
    // is deferred — for v1, attempting to push a shape whose existing
    // schema differs from the current catalogue fails with a clear
    // error pointing at the deferred feature.
};

}  // namespace
```

## Stock domain & shape inventory

### Calendar domain

- **Canonical:** `(calendar, ical)` — VEVENT/VTODO/VJOURNAL via
  KCalendarCore
- **Peers:**
  - `(calendar, palm-datebook)` — Palm DateBook native binary
  - `(calendar, org)` — org-mode (deferred to G.5 if no immediate
    org-todo backend; actually org-todo backend itself uses
    `(todo, org)`, see todo domain; org-calendar deferred to
    post-G if no demand)
- **Differ/merger:** `IRecordDifferICal`, `IRecordMergerICal` —
  KCalendarCore property-aware
- **Richness rank:** `ical (10) > palm-datebook (3)` (the
  10/3 are illustrative — declares a partial order)

### Contacts domain

- **Canonical:** `(contacts, vcard)` — vCard 3.0 via KContacts
- **Peers:**
  - `(contacts, palm-address)` — Palm AddressBook native binary
- **Differ/merger:** `IRecordDifferVCard`, `IRecordMergerVCard` —
  KContacts property-aware
- **Richness rank:** `vcard (10) > palm-address (3)`

### Memo domain

- **Canonical:** `(memo, plaintext)` — plain text body + category list
- **Peers:**
  - `(memo, palm-memo)` — Palm MemoPad native binary (essentially
    plaintext + category slot, lossless to/from canonical)
- **Differ/merger:** `IRecordDifferText`, `IRecordMergerText` —
  text equality + category set
- **Richness rank:** `plaintext (10) == palm-memo (10)` (lossless;
  same rank)

### Todo domain

- **Canonical:** `(todo, ical-vtodo)` — VTODO via KCalendarCore
- **Peers:**
  - `(todo, palm-todo)` — Palm ToDoList native binary
  - `(todo, org)` — org-mode TODO entries
  - `(todo, todotxt)` — todo.txt format
- **Differ/merger:** `IRecordDifferVTodo`, `IRecordMergerVTodo` —
  KCalendarCore VTODO property-aware
- **Richness rank:** `ical-vtodo (10) > org (5) > todotxt (2) >
  palm-todo (2)`

### Sui generis shapes

- `(plucker, palm-plucker)` — Palm Plucker offline-web format. No
  canonical shape; only valid sink is `Shape::Any` (raw-files or
  generic-sqlite backup).
- `(webcal, ical)` — read-only iCal subscription. Effectively a
  `(calendar, ical)` source-only variant; modeled as a separate
  shape for clarity.

These do not register canonical-hub edges. Mappings using these
shapes only compile to/from `Shape::Any` sinks.

## Stock backend inventory

### Calendar-domain backends (existing; migrate in G.3)

- `LocalBackend`, `RemoteBackend` (CalDAV), `OrgBackend`,
  `AkonadiBackend`, `DecSyncBackend`, `HolidaySubscriptionBackend`,
  `SubscriptionBackend`, `MockBackend` — each declares
  `nativeShapes() = { (calendar, ical) }` (or
  `(calendar, palm-datebook)` for `PalmCalendarBackend`).

### Contacts-domain backends (new in G.10)

- `AkonadiContactsBackend` — `nativeShapes() = { (contacts, vcard) }`
- `CardDAVRemoteBackend` — `nativeShapes() = { (contacts, vcard) }`
- `PalmAddressBackend` (WildPalms-side) —
  `nativeShapes() = { (contacts, palm-address) }`

### Memo-domain backends (new in G.10)

- `AkonadiNotesBackend` — `nativeShapes() = { (memo, plaintext) }`
- `PalmMemoBackend` (WildPalms-side) —
  `nativeShapes() = { (memo, palm-memo) }`

### Todo-domain backends (new in G.10)

- `AkonadiTasksBackend` — `nativeShapes() = { (todo, ical-vtodo) }`
- `PalmToDoBackend` (WildPalms-side) —
  `nativeShapes() = { (todo, palm-todo) }`

### Universal sinks (new in G.8)

- `RawFilesBackend`, `GenericSqliteBackend` —
  `nativeShapes() = { Shape::Any() }`

## Backwards-compatibility / deprecation contract

Each major API change follows the deprecation-with-overlap pattern
from `OPERATIONS.md`. The order of deletions is gated by consumer
migrations:

| Surface | Deprecation lands | Deletion lands | Gated by |
|---|---|---|---|
| `dataDomain()` | G.3 (mark) | G.3 (delete after migrating 13 callsites) | All callsites in same phase |
| `runBlobTwoWay`/`runBlobMirror` | G.6 (mark) | G.8 (delete) | WildPalms transformation in G.7 |
| `loadItems`/`storeItems`/`updateItem`/`writeFinished` | already deprecated pre-G | G.9 (delete) | Test moves G.9.b |
| Calendar-typed `ISyncHost` methods | G.9.a (mark) | G.9.a (delete after consumers migrate) | Same phase |
| `pushItems(Incidence::Ptr)` (calendar-flavoured) | G.5 (mark when `pushRecords` lands) | G.10 (delete) | All calendar backends migrate |

After each deletion, `verify-all.sh` must be green. Tests that
rely on the deprecated API are migrated in the same phase as the
deletion.

## Testing approach

Tests fall into five categories post-G:

1. **Per-shape property-catalogue tests** — verify each catalogue
   matches the real type's property set. Lives in
   `libkalburator/tests/<domain>/plugin/`.
2. **Per-edge transformation tests** — verify each registered edge
   transforms correctly with documented loss profile. Lives in
   `libkalburator/tests/<domain>/plugin/`.
3. **Per-shape differ tests** — verify each `IRecordDiffer` and
   `IRecordMerger`. Lives in `libkalburator/tests/<domain>/differs/`.
4. **Per-backend operation-handle tests** — verify each backend's
   `fetchItems`/`pushItems`/`deleteItems` contract. Lives in
   `libkalburator/tests/<domain>/backends/` for stock backends;
   in consumer repos for consumer-provided backends.
5. **Engine integration tests** — full-pipeline tests exercising
   the engine + scheduler + multiple mappings. Lives in
   `libkalburator/tests/engine/`.

Layout per `04r-phase-g-walkthrough-migration.md` § "Test
organization in libkalburator post-migration."

The ~8000 lines of PlanStan backend tests migrate per the four-
bucket fate map in the migration walkthrough.

## Explicit deferrals (out of scope; documented for posterity)

- Cross-domain edges in stock library
- Mapping groups (N-way mapping abstraction)
- Capacity-N resource pools
- Cross-component concurrent execution
- Dynamic plugin loading
- Auto-derived property catalogues
- Schema-evolution UX in `GenericSqliteBackend`
- "Smart" pipeline-cost-based path selection (only matters if
  cross-domain edges exist with multiple paths)

## Genuine open questions remaining for the design pass

These are questions the walkthroughs did not fully settle and that
the plan doc must resolve before implementation begins:

1. **`libkalburator-qtwidgets` sibling library decision.** The
   `LossProfileDetailView` Qt widget is consumer-shared but
   couples libkalburator to QtWidgets if it lives in core. Options:
   (a) sibling library; (b) keep core widget-free, ship the widget
   as a header-only utility in `tools/`; (c) accept QtWidgets
   dependency in core. **Recommendation:** (a) — create the sibling
   library now to keep core dependency-clean.
2. **`PropertyId` namespace strategy.** Options: per-shape namespaces
   (`PropertyId{"calendar.ical.summary"}`), shared global namespace
   (`PropertyId{"summary"}` shared across shapes), or per-domain
   namespaces (`PropertyId{"calendar.summary"}`). **Recommendation:**
   per-domain — strikes a balance between sharing common properties
   like "summary" and avoiding cross-domain collisions.
3. **`KCalendarCore::Incidence::Ptr` retention timeline.** Calendar
   backends today use `Incidence::Ptr` for performance (parse-once,
   pass-around). Migrating fully to `BackendRecord` byte-shaped is
   correct but loses that optimization. **Recommendation:** keep
   `Incidence::Ptr` overloads internally to calendar plugin and
   calendar backends; engine stays in `BackendRecord`/`CanonicalRecord`.
   Calendar-flavoured `pushItems(Incidence::Ptr)` lives behind the
   plugin's encapsulation, not on the public `SyncBackend` interface.

## Cross-references

- `04r-phase-g-shape-pipeline-ideation.md` — origin of this
  architecture
- `04r-phase-g-walkthrough.md` — Alice's todos walk
- `04r-phase-g-walkthrough-wildpalms.md` — Bob's HotSync walk
- `04r-phase-g-walkthrough-migration.md` — implementation slicing
  this design's plan doc expands
- `04r-phase-g-plan.md` — sibling plan doc (forthcoming)
- `04q-phase-f2-threading-design.md` — F2 threading model;
  preserved invariants
- `04p-phase-f1-unify-design.md` — F1 unification; preserved
  invariants
- `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- `~/dev/refactor-engine-merger/ROADMAP.md`
- `~/dev/refactor-engine-merger/OPERATIONS.md` —
  deprecation-with-overlap pattern
- `~/dev/refactor-engine-merger/FINDINGS.md` — schema migration
  lessons (G.4), dispatch decoupling (G.6), threading model
  (G.6, G.7)
