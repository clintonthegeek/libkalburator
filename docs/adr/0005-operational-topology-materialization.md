# ADR 0005: Operational topology materialization

- **Status:** Accepted
- **Date:** 2026-09-04

## Context

`CollectionRuntime` accepts provider identifiers, backend identifiers, and
full mappings, but its `BackendFactory` only validates. It cannot create an
endpoint, bind one to a provider collection or external resource, or make the
topology executable. The PlanStan-like and WildPalms-like contract tests could
therefore construct and reject state, but could not synchronize a record.

## Decision

Desired topology is the single public description of operational endpoint
materialization. It must express, without consumer-native types:

- the configured provider/resource dependencies of each endpoint;
- a factory identity, neutral factory input, and optional external-resource
  binding for each endpoint;
- the collection/materialization selection and the mappings that consume the
  endpoints; and
- removals and declared readiness.

Backend factories create endpoints fallibly rather than merely validate them.
The runtime owns ordinary endpoint executors and lifecycle; an explicitly bound
external-resource lease remains the executor boundary for scarce consumer
resources such as Palm I/O. Applying topology connects providers, creates and
binds endpoints, materializes collections, validates and compiles mappings,
persists state, and publishes one immutable snapshot. It either commits a fully
usable topology or preserves the prior usable topology / returns explicit repair
state.

The runtime facade does not accept consumer-assembled backends, expose mutable
registries or executors, or add Palm/provider-specific types. Exact C++ type
names are deliberately deferred to TOP-001's public-header contract tests.

## Consequences

TOP-001 precedes both consumer vertical contract slices. DES-002 and DES-003
must characterize real local/DAV and Palm-shaped/hub/remote endpoint creation
through this boundary before RUN-001 implements it. TOP-002 owns atomic failure
and rollback behavior once the operational input exists.
