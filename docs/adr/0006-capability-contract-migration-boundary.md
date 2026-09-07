# ADR-0006: Preserve operation-only backends during capability migration

**Status:** Accepted

## Context

`API-005` must make callers acquire only the backend capabilities they use and
must stop treating unsupported blob operations as successful empty/no-op work.
The first direct implementation attempt made the blob methods on
`SyncBackendBase` pure. That made the existing Google Calendar, Microsoft
Graph Calendar, and related operation-only backends abstract: they implement
calendar-native operations, but intentionally do not implement blob CRUD or
change tracking.

The current inheritance graph therefore conflates two contracts. `IBlobBackend`
is a composite record/collection/change-tracking/batch contract, while
`SyncBackendBase` is also the common base for operation-only calendar backends.
Removing the compatibility methods from the common base before separating
those lifecycles breaks valid library backends and their tests.

## Decision

Capability migration proceeds in two stages:

1. Keep operation-only backends constructible while the common base is being
   separated. Their compatibility methods remain explicitly transitional and
   must not be treated as evidence that a capability is supported.
2. Introduce the narrow acquisition boundary and migrate engine/runtime and
   consumer callers to it. Only after those callers no longer require the
   composite base may the transitional defaults be removed and the concrete
   capability inheritance be narrowed.

The next implementation slice is consequently caller-side capability
acquisition plus characterization coverage for operation-only and blob-backed
implementations. It must not make `SyncBackendBase`'s blob methods pure as an
isolated change.

## Consequences

This preserves the supported calendar-native backend set while API-005 is
completed. The composite `IBlobBackend` and its permissive defaults remain a
known migration seam, so KAL-015 stays open and API-005 remains in progress.
The final breaking change can be made coherently once the engine and both
consumers have moved to the narrow interfaces.
