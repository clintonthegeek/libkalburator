# Compatibility and releases

## Current policy

Libkalburator is pre-production. There is no source, binary, package, or schema compatibility promise during the runtime consolidation.

PlanStan, WildPalms, and libkalburator may change together. Clean coordinated breaks are preferred to compatibility overloads, aliases, forwarding headers, or dual execution paths.

## Current consumption

The source-tree build now exposes two additional non-installed targets:
`Kalburator::Canon` (Qt Core-only envelope helpers) and `Kalburator::Identity`
(Qt Core/Sql identity links). They are additive build boundaries; the existing
`Kalburator::Sync` target continues to provide the same identity headers and
links these targets internally. PlanEngine's headless planning core may link
`Kalburator::Identity` without linking `Kalburator::Sync`.

Test-enabled source builds also expose `Kalburator::TestSupport`, which owns
the reusable hermetic CalDAV and CardDAV server fixtures. It is not part of the
production sync target and is not installed yet.

Public source headers use `kalburator/<domain>/<header>` paths, and the build
and install layouts preserve that domain namespace. Export/package metadata is
still deferred to BLD-007.

| Consumer | Source configuration | Current repository pin | Notes |
|---|---|---|---|
| PlanStan | `FetchContent` or local `add_subdirectory` | v1.05 in current CMake | Mutates/link-wraps library target |
| WildPalms | `FetchContent` or local `add_subdirectory` | v1.01 in current CMake | Existing build cache used a local working tree |

The consumers' source CMake files, not copied status prose, are authoritative for pins.

## Current version mismatch

- CMake project version: `0.2.4`.
- Repository release tags: through `v1.05`.
- No installed package version or compatibility file exists.

This mismatch must be resolved before a distributable release.

## Schema policy during consolidation

There are no production users. A task may replace or reset a persisted schema when that is safer than maintaining migration complexity. The task must state which local data is discarded and update both consumers' development fixtures.

Any schema intended to survive the consolidation must have:

- one owner;
- an explicit schema version;
- transactional migration;
- forward and failure tests;
- documented recovery or reset behavior.

## Target release policy

The first reusable-library release begins only after roadmap Gate G5. It will provide:

- one version source used by CMake and tags;
- installed namespaced headers and exported CMake targets;
- declared Qt/KF dependency ranges;
- an external package-consumer test;
- schema compatibility rules;
- a chosen license and license file;
- release notes generated from completed tasks and remaining known issues.

Semantic versioning starts from that boundary. Earlier tags describe internal integration snapshots and do not imply API stability.

The current consolidation adds `RecordLoadResult` to the blob backend read
contract. Existing `loadRecords()` and `loadRecordsOrError()` entry points are
retained temporarily for direct consumer extensions while they migrate; new
library orchestration uses the typed result.

The sync-conflict schema now persists CustomMerge payloads in `merged_ical`.
The migration is additive for existing SQLite stores; non-CustomMerge
resolutions clear the column.

`TopologyDefinition` requires full `SyncMapping` definitions for every mapping
ID. Identifier-only topology was removed after it was found to report
successful work without creating endpoints or dispatching the engine.
Connected provider-owned backends may now be declared as endpoints by using
their snapshot backend ID, provider ID, and an empty factory ID.

The runtime facade now exposes `RuntimeSnapshot::providers` and typed provider
observation event fields. These contain provider identity/state, redacted
status text, owned backend IDs, and discovered collection facts; provider
configuration and credentials remain outside the facade.

Runtime telemetry now exposes immutable mapping endpoint/collection facts,
success-only last-sync timestamps, and `RunProgressKind` values for overall,
mapping, and convergence-pass observations. `CollectionChanged` has a typed
collection payload and is reserved for committed physical topology mutations;
TOP-005 owns its emission rules.

Physical collection mutation support is an explicit
`IBackendCollectionMutator` capability. Consumers submit create, adopt,
metadata update, rename, untrack, and destroy through `TopologyDefinition`;
unsupported operations fail instead of falling back to domain-specific
managers. This is part of the consolidation contract and is not a stable
external API before the first packaged release.

When `replaceProviders` is true, omitted providers are removals. The optional
`providerRemovalPolicies` map carries the consumer's explicit cascade choice
without exposing consumer controller enums to the runtime contract.

`RuntimePolicy` is the facade contract for concurrency, unchanged skipping,
conflict interaction, and mass-delete confirmation. Policy changes during an
active run are rejected; this replaces direct consumer calls into engine
configuration methods.

`MappingRunResult` now carries source- and target-side `SyncStats`, preserving
per-mapping result counts through the public runtime boundary. This is an
internal consolidation contract and may change before the first packaged
release.

`RunRequest` includes explicit mirror direction. Engine execution overrides now
apply direction as well as clobber to subset/all-enabled requests, using each
mapping's source/target orientation. This intentionally replaces the former
single-mapping-only direction behavior.

`ExternalResourceLease` has a loss-handler installation hook so the runtime can
cancel only work bound to a resource that disappears. Runtime record events now
carry explicitly versioned canonical bytes and canonical shape identity.

`SyncResult` now carries the originating `mappingId` so aggregate consumers can
project per-mapping outcomes without depending on completion order.

DAV provider configurations now persist opaque `passwordRef` values instead of
plaintext passwords. Hosts must install a persistent secure `SecretStore`
through `SecretStoreRegistry` before loading provider profiles; the built-in
store is process-local and is intended for tests or explicitly ephemeral use.
