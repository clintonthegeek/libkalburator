# Phase 0 follow-ups — discovered during Phase 1 execution

Small corrections and deferrals identified while running the
extraction against the real codebase.

## `ICalendarCollection` surface is 6 methods, not 12

The Phase 0 draft in `04-merged-interface-sketch.md` listed 12
methods. Auditing actual `libs/sync/` usage showed only 6 are
called:

- `id()`, `calendar(id)`, `calendars()`, `addCalendar(cal)`
- `setCalendarColor(id, color)`, `setCalendarVisible(id, visible)`

The other 6 drafted methods (`removeCalendar`, `calendarBackend`,
`setCalendarBackend`, `calendarsForBackend`, `calendarIdForIncidence`,
`workingCalendar`) are used elsewhere in PlanStan but never from
`libs/sync/`. They stay Collection-specific.

This makes the reuse surface for Wild Palms' side narrower — good.

## `ISyncConfigStore` surface is 8 methods, name `save()` chosen

Audited actual `libs/sync/` calls against `KalbConfigManager`.
8 methods used:

- `addLogicalCalendar`, `updateLogicalCalendar`, `removeLogicalCalendar`,
  `logicalCalendar(id)`
- `backendConfig(id)`
- `hasSyncMappings()`, `syncMappings()`
- `save()` (thin wrapper over `saveCollectionConfig()` — renamed to
  decouple from PlanStan's legacy method name)

## `ISyncHost` narrowing landed slightly differently than sketched

The sketch proposed dropping `IIncidenceSource*` and `IIncidenceRegistry*`
from `ISyncHost`. In practice these are already abstract interfaces
(from `libs/core`) that a reuse host can trivially implement over
whatever incidence model it has. Kept them on `ISyncHost` unchanged.

The circular `SyncCoordinator*` getter was indeed dropped.

## Phase 1.3 (relocate types out of `libs/core`) — deferred into Phase 3

The Phase 0 plan said to relocate `BackendConfiguration`,
`LogicalCalendar`, `SyncTypes`, `CalendarType`, `DataDomain` from
`libs/core` to `libs/sync` (and eventually `libkalburator`).

**Deferred.** Reasoning:

1. 65 source files consume these headers. Relocating in-tree (Phase 1)
   means 65 files churn for the intra-repo move, and then churn a
   second time in Phase 3 when the files physically move to
   `~/dev/libkalburator/`. Bundling both relocations into Phase 3 is
   a single consistent churn.
2. Phase 1's end state is functional without the relocation: the
   library interfaces (`ICalendarCollection`, `ISyncHost`,
   `ISyncConfigStore`) are in place and narrow enough to support the
   smoke test in Phase 2.
3. The Phase 1.4 standalone-build check (`PROJECT_IS_TOP_LEVEL` for
   `libs/sync/`) can tolerate the in-tree header locations because
   `libs/core` is a declared PUBLIC dep of `libs/sync` already.

Phase 3's scope is therefore: (a) copy source files to
`~/dev/libkalburator/src/`, (b) move the five type headers from
`libs/core/include/` to `libkalburator/src/types/`, (c) update 65
consumer include paths in PlanStan to the new library's public
headers. All in one pass.

## Phase 2 consumer smoke-test scope confirmed

The smoke test writes a minimal stub host implementing
`ICalendarCollection` + `ISyncHost` + `ISyncConfigStore` using
`MemoryCalendar` as the calendar store, drives a `LocalBackend` and
optionally a `RemoteBackend` against the local Radicale server, and
asserts round-trip X-property preservation.

Whether to include `RemoteBackend` (needs live Radicale) as optional
is TBD during Phase 2 execution — may gate that behind
`-DPLANSTAN_ENABLE_CALDAV_TESTS=ON` as other tests do.
