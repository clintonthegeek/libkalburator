# Consumer contracts

**Current status:** source-tree embedding only; the target contract is being built.

## Compatibility stance

PlanStan, WildPalms, and libkalburator are pre-production and may be refactored together. Preserving existing APIs is less important than arriving at a small correct boundary. Temporary adapters must have a deletion task.

## Current integration constraints

Both consumers currently:

- add libkalburator through CMake source embedding or `FetchContent`;
- link the namespaced target normally and consume its public include interface;
- depend on flat source include directories;
- instantiate registries, plugins, stores, and `SyncEngine` themselves;
- carry integration behavior that belongs in a reusable runtime.

This is supported only as the migration baseline. It is not the target public API.

## PlanStan requirements

PlanStan must retain control of:

- application collection open/close;
- user commands and scheduling policy;
- UI presentation and application-model projection;
- PlanStan-only Org/KalCal behavior that cannot be made generic.

Libkalburator must take ownership of:

- per-collection service construction and destruction;
- backend executors and affinity;
- provider connection state;
- topology compilation and mapping eligibility;
- full, subset, and single-mapping execution;
- native-to-canonical change notification;
- baselines, conflicts, guards, and reconciliation recovery sequencing.

PlanStan's migration gate is removal of backend `moveToThread` logic and direct access to mutable runtime collaborators from `CollectionController`.

Provider observation is facade-only. `RuntimeSnapshot::providers` supplies
typed connection state, credential-free error/warning text, owned backend IDs,
and immutable discovered `CollectionInfo` values. Provider-state runtime events
carry the same typed state and discovery payload, so account and collection UI
does not parse `message` or access `ProviderManager`/`IProvider`. A provider
replaces its discovered collection list before publishing the observation;
disconnect therefore clears stale discovery before reconnect.

Conflict presentation follows the same facade boundary. The runtime publishes a
`RuntimeEvent::Kind::ConflictDetected` event whose `conflict` payload includes
the stable persisted conflict ID and native source/target display data. The
consumer presents that payload and submits the user's choice through
`CollectionRuntime::resolveConflict()`, including the merged native payload for
`CustomMerge`. `ConflictResolved` confirms the accepted command. Consumers do
not access `SyncConflictStore`, `ConflictManager`, or `SyncEngine` directly;
the runtime persists the decision and the next run replays it with the normal
staleness checks.

On startup or event-sink rebind, consumers query
`CollectionRuntime::unresolvedConflicts()` to rehydrate the conflict dock. The
returned payload includes the stable conflict ID and mapping identity; an empty
list is the explicit empty-backlog state.

Run behavior is supplied as `RuntimePolicy` during construction or through
`updatePolicy()`. It carries only concurrency, unchanged-skip, monitored versus
background interaction, and a neutral mass-delete confirmation callback.
Updates are rejected while a run is active and otherwise apply to the next
run; consumers do not configure the engine directly.

The facade kernel is not yet a complete PlanStan contract. `API-007` adds
typed provider state and immutable discovery facts; `API-008` makes persisted
unresolved conflicts visible after reopen/rebind; `API-009` supplies immutable
mapping facts, last-success state, typed mapping/pass telemetry, and committed
collection changes; `RUN-007` carries mass-deletion, tuning, and
monitored/background conflict policy; `RUN-008` resolves the journal boundary;
and `TOP-004`/`TOP-005` bring provider edits, durable desired state, and
capability-checked physical collection mutations into the topology transaction.
Create/adopt/rename/untrack/destroy are submitted as explicit commands, and
the runtime returns repair state when a remote inverse is unavailable. PlanStan must not work
around these gaps by borrowing managers/stores or parsing numeric event
messages.

## PlanEngine requirements

PlanEngine's headless planning core may link `Kalburator::Identity` for
`IdentityStore` and `PersonDirectory`. This target is intentionally separate
from `Kalburator::Sync`: it depends on Qt Core/Sql and the Core-only
`Kalburator::Canon` target, with no Widgets, KCalendarCore, network, or engine
dependency. PlanEngine owns the profile path and the lifetime of its
`IdentityStore`; no identity behavior is duplicated in the consumer.

Provider-created backends are registered only after `ProviderManager` adopts them
into a library-owned `BackendExecutor`. The executor owns backend affinity,
operation dispatch, destruction, and bounded thread shutdown. Consumers must not
call `moveToThread()`, retain ownership of a registered backend, or destroy one
while it is registered. WildPalms remains responsible for supplying its external
device-resource lease; that lease is the executor boundary for Palm I/O.

## WildPalms requirements

WildPalms must retain control of:

- discovering and opening a Palm serial/USB/network connection;
- DLP handshake and device ownership;
- Palm database/category preparation before a sync;
- HotSync, FullSync, mirror, and clobber intent selected by the user;
- Palm keepalive/tickle behavior around device I/O;
- device write flush and EndOfSync timing;
- backup and restore, which are device-management operations.

Libkalburator must take ownership of:

- runtime assembly and stores;
- stock plus Palm extension registration;
- hub and mapping topology materialization;
- graph convergence passes;
- engine future/cancellation/result aggregation;
- conflict replay and sync result normalization;
- backend availability validation before run start.

## Responsibility map

The migration destination for each runtime concern is explicit:

| Concern | PlanStan / WildPalms retain | libkalburator owns |
|---|---|---|
| Application commands, scheduling, and model/UI projection | `CollectionController`, `PalmRuntime`, application views | — |
| Device discovery, DLP, categories, backup/restore, and Palm link lifetime | WildPalms device layer | — |
| Provider and plugin selection | consumer-supplied extension values | registry, activation, and contribution validation |
| Backend construction, affinity, cancellation, and destruction | backend factory implementation | `BackendExecutor` / provider executor |
| Stores, baselines, conflicts, and recovery sequencing | profile path selection | runtime-owned stores and lifecycle |
| Desired topology and mapping eligibility | user-facing topology edits | topology compilation and committed snapshot |
| Sync dispatch, convergence, result aggregation, and canonical change events | consumer command intent and event projection | runtime coordinator and engine |
| External scarce resources | WildPalms lease implementation | generic lease contract and run sequencing |

Consumer code must therefore call runtime commands and consume snapshots/events;
it must not retain or mutate registries, stores, engines, worker threads, or
mutable mapping collections. The current PlanStan implementation path is
PS-009 through PS-016 followed by the PS-008 cleanup gate; the WildPalms path
is WP-009 (which consolidates WP-001 through WP-008).

### Required external-resource seam

The runtime needs a host-supplied lease/hook interface for scarce external resources. For Palm it must support:

- `prepare()` before the run, after the device is connected;
- phase notification indicating whether the next operation touches Palm;
- serialized execution on the Palm link thread;
- `flush()` before successful completion;
- `cancel()` and link-loss reporting;
- `finish(result)` exactly once;
- a bounded lifetime that cannot outlive the Palm connection.

The interface must not know Palm types. WildPalms implements it around `PalmDeviceAccess`.

## Target consumption shape

The final API names are not locked. The intended shape is:

```cpp
RuntimeDefinition definition;
definition.storagePath = profileStatePath;
definition.extensions = consumerExtensions;
definition.secrets = secretStore;
definition.resources = externalResourceHooks;

auto runtime = CollectionRuntime::create(definition);
auto providersReady = runtime->connectProviders();
auto topologyResult = runtime->applyTopology(desiredTopology);

RunRequest request = RunRequest::allEnabled(RunIntent::Normal);
QFuture<RunResult> future = runtime->run(request);
```

`TopologyDefinition::mappings` may carry full neutral `SyncMapping` values.
When present, their IDs populate the committed snapshot (unless explicit IDs
are also supplied) and the runtime installs the definitions in its engine.

The topology also names each executable endpoint's factory, neutral creation
input, provider/collection dependency, and optional external-resource binding.
An already connected provider-owned backend is named directly by its snapshot
backend ID with an empty factory ID and its provider ID.
Factories create endpoints fallibly; consumers provide the factory or lease,
while the runtime connects configured providers, owns ordinary endpoint
executors, and performs materialization. A topology that references a provider
is accepted only after `connectProviders()` reports readiness. The public
boundary never accepts a consumer-assembled backend or exposes runtime
registries and executors.

Consumers should not receive registry, engine, store, worker-thread, or mutable mapping pointers from this facade.

Provider account changes use `addProvider()`, `updateProvider()`, and
`removeProvider()` only as a transitional surface. Durable callers submit a
complete provider set with `TopologyDefinition::replaceProviders`; provider
additions, edits, removals, reconnection, dependent endpoint/mapping
replacement, and durable desired state then share one commit/rollback
boundary. A failed staged edit restores the prior provider set and usable
snapshot. The typed event and credential-free discovery snapshot needed by
real consumers are API-007 work; the current event encodes state as a decimal
string and must not become a consumer contract.

PlanStan's monitored runs are also not expressible yet. The current facade
dispatches background conflict behavior and `resolveConflict()` records a
decision for the next run. RUN-007 adds a neutral monitored/background choice
and an in-flight resume path; PS-013 retains Immediate, Deferred, Hybrid, and
AutoResolve as presentation/decision policy without exposing the engine.

DES-001's prototype is now the implemented surface in
`src/runtime/collectionruntime.h`: `CollectionRuntime`, explicit
`RunRequest` selection/intent, immutable snapshots, topology results, and the
generic external-resource lease.

## Contract tests

Two library-owned contract executables define the intended boundary:

1. **PlanStan-like long-lived collection:** local hub plus DAV fake, provider/topology mutation, subset/full runs, model-change events, cancellation, and destruction during failure.
2. **WildPalms-like leased device:** Palm-shaped backend plus canonical hub plus remote fake, bounded multi-hop convergence, phase hooks, link loss, mirror/full intent, flush-on-success, and teardown.

Each real consumer also keeps a small integration test against the working libkalburator tree. These tests must use exported targets and public headers only by the packaging milestone.

The reproducible local integration lane is `python3 tools/run_consumer_integration.py` from the libkalburator checkout. It configures PlanStan with `PLANSTAN_LIBKALBURATOR_SOURCE_DIR` and WildPalms with `WILDPALMS_LIBKALBURATOR_SOURCE_DIR`, records the source-tree identity and working diff hash, then builds both consumers and runs their selected runtime-contract CTest groups. Use `--artifacts DIR` to retain the configure, build, and test logs.

The supported library lane is `python3 tools/run_library_ci.py`; it configures a test-enabled, example-disabled build, applies a per-test timeout, runs CTest, and retains the machine-readable baseline report and command logs under the build's `ci-artifacts` directory.

KRN-001 completes both vertical proofs. The PlanStan-like executable covers
provider readiness, usable endpoints, record transfer, canonical model events,
cancellation, and active teardown. The WildPalms-like executable covers real
Palm-shaped/hub/remote fakes, reverse-ordered convergence, directional mirror,
resource scoping, link loss, and exactly-once hook order. Identifier-only
topology remains rejected rather than treated as successful work.

## Migration rules

- Change the library and consumers in coordinated branches when a boundary changes.
- Prefer one flag day over long-lived compatibility layers.
- Preserve user data only where a migration is cheaper and safer than rebuilding it; no production users currently constrain schema changes.
- Do not preserve source layout, private includes, whole-archive behavior, or host target mutation.
- Keep Palm wire/device code in WildPalms unless it is genuinely reusable outside Palm devices.
## Secrets

Before loading persisted provider profiles, a consumer must install its
persistent secure implementation of `Kalburator::Sync::SecretStore` with
`SecretStoreRegistry::setDefaultStore()`. Provider `save()` emits only
`passwordRef`; legacy plaintext `password` entries are accepted on load and
migrated into the active store. The built-in store is process-local and is
intended only for tests or explicitly ephemeral profiles.

### Consumer handoff

PlanStan and WildPalms should provide their secure, persistent secret-store
adapter during application startup:

```cpp
Kalburator::Sync::SecretStoreRegistry::setDefaultStore(hostSecretStore);
```

The adapter owns persistence, OS/keychain integration, rotation, and reset.
The library owns only the reference contract and provider migration. Consumers
must not write `password` back to `BackendConfiguration`; they may continue to
accept legacy plaintext input long enough for the provider to migrate it.
