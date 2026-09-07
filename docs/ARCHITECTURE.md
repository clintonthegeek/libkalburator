# Architecture

**Last verified:** 2026-09-07 baseline snapshot; production consumer cutover under revalidation
**Status:** library/adapter facts followed by the adopted target; no completed
task result certifies PlanStan production ownership before `AUD-003`

## Purpose

Libkalburator reconciles collections of records across heterogeneous storage and transport systems. Native records are promoted through a shape graph to a canonical domain representation, compared against per-side baselines, resolved, demoted, written, and persisted.

The two real consumers exercise different requirements:

- PlanStan uses a long-lived collection runtime centered on calendars and DAV/local backends.
- WildPalms uses short-lived Palm connection sessions, a canonical SQLite hub, four PIM domains, device-specific transformations, and user-selected HotSync/FullSync/mirror operations.

## Current build architecture

| Target | Contents | Current limitation |
|---|---|---|
| `Kalburator::Types` | Value types and base shape type | Build-tree include layout only |
| `Kalburator::Canon` | Headless canon-envelope parsing, serialization, and digest helpers | Build-tree companion target; not installed/exported |
| `Kalburator::Identity` | Cross-domain record/entity link index and `PersonDirectory` | Build-tree companion target; not installed/exported |
| `Kalburator::TypeSupport` | Serialization, locks, and small support services | Not installed or exported |
| `Kalburator::Core` | Headless composition of Types, Canon, Identity, and TypeSupport | Build-tree interface target; not installed/exported |
| `Kalburator::Sync` | Engine, stores, domains, backends, providers, vendors, plugins, and UI | Static monolith with heavy public dependencies |

The configured target graph was regenerated from the current CMake build tree
on 2026-09-04. Its checked target-level dependency list is:

| Target | Direct reusable-target dependencies | Optional/heavy boundary |
|---|---|---|
| `Kalburator::Canon` | — | Qt Core |
| `Kalburator::Identity` | `Kalburator::Canon` | Qt Core, Sql |
| `Kalburator::Types` | — | Qt Core, Gui, CalendarCore |
| `Kalburator::TypeSupport` | `Kalburator::Types` | — |
| `Kalburator::Sync` | `Kalburator::Types`, `Kalburator::TypeSupport`, `Kalburator::Identity` | Qt Sql, Network, Xml, Widgets, Concurrent; KF6 DAV/KIO/Holidays/Contacts/ConfigCore; optional OrgGrove and Akonadi |
| `Kalburator::Widgets` | `Kalburator::Sync` | Qt Widgets (optional top-level target) |

This list is based on generated CMake target edges, then checked against the
declared `target_link_libraries` blocks. Domain families remain classified by
their actual target dependencies; directory placement is not treated as a
split boundary. BLD-002 through BLD-004 perform the resulting extraction and
public-header work.

`Kalburator::Sync` now exposes a generated namespaced build include root and installs headers under `include/kalburator/<domain>`. Qt Widgets remains part of the aggregate Sync target pending a later boundary task. Package export work remains BLD-007. The two production consumers link Sync normally; specialized static-registration tests may still force-link it explicitly.

## Current runtime architecture

### Data path

1. `SyncRequest` selects mappings and optional execution overrides.
2. `SyncEngine` resolves backends and domain definitions from registries.
3. Source and target records are fetched in native encodings.
4. Shape pipelines promote each side to the domain canon.
5. Per-side hashes and persisted baselines produce a three-way diff.
6. Conflict policy and domain merger select or defer changes.
7. Canonical results are demoted and applied to each backend.
8. IDs, aliases, baselines, tokens, conflicts, and statistics are persisted.
9. Additional passes may run to reach a fixpoint after identity-changing writes.

### Main components

| Component | Responsibility | Current owner |
|---|---|---|
| `ShapeRegistries` | Domains, representations, transformations, differs, mergers | Each consumer runtime |
| `PluginManager` | Registers shape and backend contributions | Each consumer runtime |
| `BackendRegistry` | Backend contributions and live backend instances | Each consumer runtime |
| `ProviderManager` | Provider configuration, connection, and backend registration | Each consumer runtime |
| `SyncEngine` | Mapping execution and record reconciliation | Library object assembled by consumer |
| `SyncRunCoordinator` | Higher-level run tracking for some paths | PlanStan; not used for every run |
| `CollectionRuntime` | Runtime ownership facade with provider/factory endpoints, typed provider/discovery snapshots, run intents, bounded convergence, resource leases, and canonical events | Library contracts and preparatory consumers; production ownership is revalidated through AUD-003 and subsequent workflow tasks |
| `BaselineStore` | Per-side hashes, aliases, tokens, collection metadata | Constructed and injected by `CollectionRuntime`; consumer migration pending |
| `SyncConflictStore` / conflict manager | Deferred conflicts and resolutions | Runtime-owned store and deferred manager; presentation remains in consumer UI through facade events/commands |
| Calendar manager, journals, guards | Calendar mutations, recovery, deletion safety | Individually assembled |

`CollectionRuntime` owns the engine-side set in contract tests and on the
connected WildPalms topology/run path; remaining consumer-owned compatibility
and management seams are still being removed.

### Backend contracts

- `IBlobBackend` is the neutral record/collection interface. Collection reads use `RecordLoadResult`, which distinguishes a valid empty collection from a failed load; the legacy list/out-parameter spellings remain migration surfaces.
- `SyncBackendBase` adds QObject identity, shapes, operations, and async application.
- Calendar `SyncBackend` adds a broad calendar-specific surface with several permissive default implementations.

The overlap permits callers to invoke unsupported operations and forces consumers to understand backend affinity and capability details.
Operation-only calendar backends also share `SyncBackendBase` without implementing
blob CRUD, so removing its compatibility methods must follow caller-side
capability acquisition rather than precede it. The migration boundary is recorded
in [ADR-0006](adr/0006-capability-contract-migration-boundary.md).

## Current consumer composition

### PlanStan

PlanStan's `CollectionController` is the effective collection runtime. It
assembles registries, plugins, providers, engine, stores, conflict handling,
journals, guards, and calendar management. Backend objects are now adopted by
libkalburator executors rather than manually relocated, but the controller
still retains their registry views, translates native change callbacks,
recompiles mappings, and provides multiple run paths.

PlanStan installs its KWallet-backed `SecretStore` adapter during startup; the
library owns reference migration, while the host owns secure persistence.

PlanStan supplies its host-owned Org/KalCal dependencies through the optional adapter target, while PlanStanCore links and publishes `Kalburator::Sync` normally. The production path no longer mutates the library target, force-links it, or extracts its include properties.

### WildPalms

WildPalms' `PalmRuntime` assembles device-management state per profile, while
connected topology, provider lifecycle, and runs are submitted to one
`CollectionRuntime`:

- Palm plugin instances and a canonical hub;
- `CollectionRuntime`-owned physical hub, Palm, provider, and filtered-route
  endpoints;
- Palm calendar, contacts, todo, and memo plugins/backends;
- Palm-to-canonical transformation edges;
- generated Palm↔hub and hub↔remote mappings;
- the runtime's bounded coordinator for both connected and injected-backend
  runs; only an explicitly named conflict-store accessor remains for legacy
  fixture coverage.

`PalmDeviceAccess` owns the physical Palm connection and serializes DLP calls on a dedicated link thread. `PalmRuntime` initiates HotSync, FullSync, mirror, clobber, backup, and restore operations. It pauses or resumes the device keepalive according to engine phases and flushes device writes before completing the session.

WildPalms installs the analogous KWallet-backed `SecretStore` adapter during
startup.

PlanStan delegates per-collection sync collaborator construction to a
PlanStan-owned transitional assembly while the public runtime-facade cutover
remains pending.

WildPalms production targets link `Kalburator::Sync` normally and inherit its public include interface. Remaining whole-archive and target-property references are confined to specialized test fixtures and historical planning prose.

## Adopted target architecture

The adopted boundary is:

> Consumers own intent and external-resource sessions. Libkalburator owns synchronization execution and state.

### Library-owned `CollectionRuntime`

The name is provisional; the ownership is not. One runtime owns:

- registries and explicit stock/consumer extension registration;
- providers and backend factories;
- live backend instances and their executors;
- engine and the only run coordinator;
- baselines, conflicts, aliases, and tokens;
- conflict and mass-deletion policies;
- atomic provider/topology/mapping mutations;
- cancellation, bounded teardown, and terminal result delivery;
- convergence passes and progress aggregation.

Committed topology carries full neutral `SyncMapping` definitions; the runtime
validates and installs them in its private engine while exposing only their IDs
through the immutable snapshot. Identifier-only mappings are invalid because
they cannot represent executable work.

The runtime exposes commands, immutable snapshots, capability queries, and typed events. It does not expose mutable registries or concrete stores as routine consumer APIs.

Conflict handling crosses that boundary through typed `ConflictDetected` and
`ConflictResolved` events plus `resolveConflict()`. The runtime owns the
`ConflictManager` in deferred mode, persists detected conflicts, and replays
consumer decisions through the existing engine resolution path. Consumers own
only presentation and merged-payload collection.

Provider configuration changes cross the same boundary through explicit add,
update, and remove commands today. These commands are transitional. Durable
desired-state callers use `TopologyDefinition::replaceProviders` to submit the
complete neutral provider set; staging, connection, endpoint/mapping
validation, persistence acceptance, and snapshot publication share one
transaction. Failure restores the previous provider set and usable snapshot.
Provider mutation remains rejected while a run is active.

### Consumer-owned adapters

Consumers may supply:

- domain/plugin extensions;
- backend factories for consumer-specific systems;
- credential and secret-store adapters;
- presentation hooks for conflicts, warnings, and deletion confirmation;
- external-resource leases and lifecycle hooks;
- projections that update application models after canonical changes.

PlanStan supplies its UI/model projection and any application-only calendar adapters. Backend execution threads are owned by libkalburator executors; PlanStan does not move backend objects between threads.

WildPalms supplies Palm backend factories and a device-session lease. The lease proves that the device is connected, serializes DLP access, controls keepalive around Palm I/O phases, and flushes writes at completion. Backup/restore and connection establishment remain WildPalms operations because they are device management, not replica reconciliation.

### Run model

The public run request must distinguish explicitly between:

- all enabled mappings;
- an exact non-empty mapping set;
- one mapping;
- no work.

It also carries an explicit intent: normal, full re-diff, directional mirror, or destructive rebuild. Empty selection never means “all.” All intents travel through one coordinator and return one typed terminal result.

Runtime construction also accepts a neutral `RuntimePolicy`: mapping
concurrency, unchanged-skip behavior, monitored/background conflict
interaction, and a mass-delete confirmation callback. Policy updates are
rejected during a run and take effect on the next run; the callback receives
only mapping/backend IDs and counts.

Consumer-specific run policy may choose when to request another pass, but normal graph convergence belongs in the library. WildPalms' Palm↔hub↔remote star must be expressible as one runtime run with a bounded convergence policy rather than a consumer-owned future loop.

### Backend execution model

Each backend is bound to a library-owned `BackendExecutor` or to a consumer-supplied external-resource executor. Operations are asynchronous, cancellable where possible, timeout-bounded, and completed exactly once. QObject movement is an implementation detail; consumers never relocate a backend. The registry is non-owning; the executor owns backend creation handoff, affinity, destruction, and thread shutdown.

Synchronous compatibility bridges are temporary migration tools and must not execute network, device, or large filesystem I/O on a caller thread.

### Topology and persistence

A topology transaction changes provider configuration, live backends, mappings, materialized collections, and persisted state as one operation. It either commits a usable snapshot or reports a typed failure with rollback/repair state.

The desired topology is operational rather than an identifier list. Each
endpoint names its factory and factory-specific neutral input, any provider and
collection it depends on, and an optional external-resource binding. Factories
create endpoints fallibly; the runtime owns the resulting endpoint and, for a
mapped endpoint, privately registers its executable backend with the engine.
Provider connection, executor handoff, collection materialization, mapping
compilation, persistence, and publication still need to form one transaction.
The snapshot is published only after every selected endpoint is ready; failure
preserves the previous usable snapshot or reports explicit repair state.

PlanStan submits topology edits as one desired-state commit through its topology data-source adapter. The adapter owns deferred calendar materialization and live mapping publication; wizard assembly uses the controller's equivalent commit boundary. Consumers do not reproduce the materialize-then-compile ordering or construct provider-owned compound identifiers.

Successful record writes publish a generic `RecordChange` event containing
source identity and a typed or explicitly versioned canonical payload.
Consumer callbacks route the event without mapping scans, backend round-trips,
or backend-native parsing.

Provider observations are immutable `ProviderSnapshot` values in the runtime
snapshot. They contain only provider identity, typed state, redacted status
text, owned backend IDs, and discovered collection facts. Provider-state events
carry typed state and the current discovery replacement; no provider manager or
credential-bearing configuration crosses the facade.

PlanStan's Run Plan additionally needs immutable mapping endpoint/collection
facts, per-mapping and convergence-pass events, and last-success timestamps.
Those observations are API-009 work; consumers do not regain store or engine
access to obtain them. `RunProgressKind` identifies overall, mapping-start,
mapping-finish, and convergence-pass observations. `CollectionChanged` carries
the affected `CollectionInfo`, provider identity, mutation kind, and a
committed flag; TOP-005 emits it exactly once after a physical mutation commits
and emits none for rollback or failed preparation.

Conflict UI custom merges are a two-part handoff: the merged native payload is persisted with the conflict resolution and passed to the waiting engine resume command. This keeps restart/recovery data and the in-flight operation aligned.

Stores may remain separate internally, but their construction, schema
migration validation, close ordering, reset, and lifetime belong to the
runtime. Reconciliation recovery is represented by the durable baseline,
conflict, and topology stores; the runtime does not create or replay a
`CalendarJournal`. PlanStan retains that journal for its consumer-owned staged
calendar edits, and only PlanStan's journal recovery coordinator replays it.
Persisted topology atomicity remains TOP-004/TOP-005 work.

Physical collection operations enter the topology transaction through the
neutral `IBackendCollectionMutator` capability. Create, metadata update,
adoption, rename, untracking, and destruction remain distinct commands.
Committed physical create/update/rename/destroy operations publish one
`CollectionChanged` event; adoption and untracking change only desired state.
Remote operations cannot all be literally rolled back, so the runtime uses
prepare/commit/compensation ordering and reports typed repair state whenever a
safe inverse is unavailable; it never describes partial remote destruction as
an atomic rollback success.

## Target build boundaries

| Target | Intended contents |
|---|---|
| `Kalburator::Core` | Types, shape graph, diff, engine contracts, runtime; no Widgets |
| `Kalburator::Canon` | Canon-envelope format helpers; Qt Core only |
| `Kalburator::Identity` | Cross-domain identity links and person directory; Qt Core/Sql plus Canon |
| `Kalburator::Calendar` | Calendar canon and supported calendar backends |
| `Kalburator::Contacts` | Contacts canon and supported contacts backends |
| `Kalburator::Todo` | Todo canon and supported todo backends |
| `Kalburator::Note` | Note canon and supported note backends |
| `Kalburator::ProvidersDav` | DAV/CardDAV/multiprotocol providers and transport |
| `Kalburator::ProvidersGoogle` | Google providers; experimental until consumer-integrated |
| `Kalburator::ProvidersMicrosoft` | Microsoft providers; experimental until consumer-integrated |
| `Kalburator::Storage` | Optional generic and filtered storage backends |
| `Kalburator::Widgets` | Configuration and picker UI only |
| `Kalburator::TestSupport` | Stable fake servers/backends used by consumer contract tests |

The exact split may change after dependency measurement. The required outcomes are a headless core, explicit registration, independently optional feature families, installed namespaced headers, and no whole-archive requirement.

## Architectural fault lines to remove

- Consumer-owned backend thread relocation.
- Consumer assembly of engine internals and store lifetimes.
- Multiple run paths and consumer-owned convergence loops.
- Empty-list sentinel semantics.
- Target mutation and private source include propagation.
- Static-registration linker side effects.
- Public permissive no-op backend methods.
- Native record parsing in host change callbacks.
- Non-atomic provider/topology/mapping workflows.
- UI dependencies in the headless engine target.
