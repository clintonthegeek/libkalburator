# ADR 0003: CollectionRuntime public surface

- **Status:** Accepted
- **Date:** 2026-09-03

## Context

PlanStan's `CollectionController` and WildPalms' `PalmRuntime` both assemble
registries, providers, backends, stores, mappings, and `SyncEngine`. They also
expose those collaborators to application code. This duplicates lifecycle and
run policy, and makes backend affinity and teardown a consumer concern.

The consumers still need different application boundaries: PlanStan owns
collection open/close and model projection; WildPalms owns the physical Palm
session and device management around a sync.

## Decision

Introduce one library-owned `Kalburator::Runtime::CollectionRuntime` per
application collection/profile. Its public prototype is
`src/runtime/collectionruntime.h`.

The facade exposes only:

- `RuntimeDefinition` for storage and generic external-resource leases;
- explicit backend-factory extensions;
- immutable `RuntimeSnapshot` inspection;
- `applyTopology(TopologyDefinition)` returning an atomic-style
  `TopologyResult` with explicit repair state;
- `run(RunRequest)` returning one `QFuture<RunResult>`;
- `cancel()` and a typed `RuntimeEvent` sink.

Run selection is explicit: `AllEnabled`, non-empty `ExactSet`, `One`, or
`None`. Run intent is explicit: `Normal`, `FullRediff`, `Mirror`, or
`DestructiveRebuild`.

The runtime owns registries, plugin/provider activation, backend instances and
executors, stores, mappings, convergence, cancellation, terminal results, and
teardown. It does not return mutable registries, stores, engine pointers,
threads, backend-native records, or mutable mapping objects.

External resources implement a generic lease with prepare, phase execution,
flush, cancel, and exactly-once finish hooks. The lease has no Palm or other
vendor types; WildPalms adapts its device access to it.

## Responsibility map

| Existing owner | Remains consumer-owned | Moves behind CollectionRuntime |
|---|---|---|
| PlanStan `CollectionController` | collection/UI/model lifecycle, user scheduling, Org/KalCal-only adapters | registries, providers, backends, executors, stores, mapping compilation, runs, conflicts, journals, teardown |
| WildPalms `PalmRuntime` | device connect/handshake, category preparation, keepalive, backup/restore, write flush timing | plugin/runtime assembly, hub/topology, engine, convergence, result aggregation, backend readiness |

## Consequences

- DES-002 and DES-003 can compile contract harnesses against one boundary.
- `SAF-002` can add executor ownership without exposing thread mechanics.
- `DES-004` fills out request validation and intent semantics without another
  run API.
- The prototype is deliberately a header-level contract; implementation and
  explicit extension registration belong to RUN-001/RUN-002.
