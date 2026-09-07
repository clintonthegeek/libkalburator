# Phase 0 — Conflict engine audit

**Status:** Stable. Supporting detail for `02-inventory-wildpalms.md`
§Conflict-policy audit and `04-merged-interface-sketch.md` §Conflict
framework.

The single load-bearing concern raised during proposal review: Wild
Palms' conflict engine is more advanced than PlanStan's **but is
Palm-fit**. A naive extraction either leaks Palm concepts into the
library or flattens Wild Palms' sophistication away. This document
walks the Palm-vs-generic separation at the class-by-class level and
locks in the subclass pattern.

## The Palm-fit parts, enumerated

From reading `~/dev/WildPalms/src/sync/qsynccore/`:

### `ConnectionBehavior` enum (in `ConflictPolicy`)

```cpp
enum class ConnectionBehavior {
    KeepAlive,          // tickle connection during user prompts
    DisconnectAndDefer, // disconnect immediately, resolve later
    TimeoutThenDefer    // keep alive briefly, then disconnect
};
```

This exists because the **Palm HotSync session has a connection
timeout**. If the user takes too long resolving a conflict, the Palm
device disconnects, and the sync must be aborted mid-stream. No
other backend in libkalburator's scope (CalDAV, local, org, Akonadi,
DecSync) has this concern — their "connection" is stateless or
long-lived.

**Separation decision:** `ConnectionBehavior` does **not** enter
libkalburator. It stays as a field on Wild Palms' own
`PalmConflictHandler` subclass.

### HotSync-tickle-during-prompt logic

The implementation of KeepAlive (and the timeout variant) requires
calling into the Palm connection layer to emit keep-alive packets.
That's Palm-SDK territory.

**Separation decision:** Wild Palms' PalmConflictHandler holds a
reference to the `PalmDeviceConnection` (or equivalent) and issues
tickles from there.

### Archive-bit semantics

Palm records have an "archived" flag distinct from "deleted". When a
record is archived on the Palm and simultaneously modified on the PC
side, the conflict resolution must decide whether the archive wins
(record effectively moves to the archive file) or the modification
wins (record resurrected to active status). No analog exists for
CalDAV / iCal / org.

**Separation decision:** archive-aware conflict behaviour lives in
PalmConflictHandler. The library's `ConflictRecord` has no archive
field.

### Secret-record handling

Palm has a "secret" record flag. Some users rely on this for
privacy. Conflict resolution must preserve the secret flag (never
demote a secret record to non-secret through conflict resolution).

**Separation decision:** again, PalmConflictHandler.

### Category-ID remapping during conflicts

Palm stores categories as 16 fixed slots (0–15) with a name-to-slot
mapping per database. Conflict resolution can involve reconciling
category changes where the PC side has reordered or renamed
categories, leading to Palm-specific ID remapping.

**Separation decision:** PalmConflictHandler carries a
`CategoryMappingStore*` reference.

## What **does** enter libkalburator from the conflict engine

The portable parts, confirmed by cross-referencing `ConflictPolicy`:

| Piece | Portable? | Notes |
|---|---|---|
| `ConflictRecord` struct (id, source, target, fields, timestamps, suggested resolution) | Yes | Generic conflict descriptor |
| `ConflictStore` (persistent deferred-resolution queue) | Yes | Any multi-backend sync needs this |
| `AutoResolveStrategy` enum | Yes | `NewerWins`, `DuplicateAll`, etc. apply universally |
| `PromptStrategy` enum | Yes | When to ask — universal |
| `FallbackBehavior` enum | Yes | What to do if can't ask — universal |
| `promptTimeoutSeconds` | Yes | Numeric; the concept is universal; the default value can differ per-backend |
| `ConflictDecision` enum | Yes | The outcome of resolution — universal |
| `allowBatchReview`, `showPreviewBeforeSync`, `maxAutoResolvePerSync`, `requireConfirmForDeletes` | Yes | Universal safety / UX knobs |
| Abstract `ConflictHandler` base class | Yes | The extension point itself |
| `AutomaticConflictHandler` concrete implementation | Yes | Implements the portable auto-resolve strategies |

## The subclass pattern

```
                       ┌────────────────────────────────┐
                       │ Kalburator::Sync::              │
                       │   ConflictHandler (abstract)    │
                       └──────────┬──────────┬───────────┘
                                  │          │
              ┌───────────────────┘          └──────────────┐
              │                                             │
    ┌─────────▼────────────────────────┐           ┌────────▼───────────────┐
    │ Kalburator::Sync::                │           │ WildPalms::              │
    │   AutomaticConflictHandler        │           │   PalmConflictHandler    │
    │                                   │           │                          │
    │ Uses ConflictPolicy fields        │           │ Extends with:            │
    │ (AutoResolveStrategy etc.)        │           │ - ConnectionBehavior     │
    │ No Palm concepts.                 │           │ - archive-bit logic      │
    │                                   │           │ - secret-record handling │
    │ Used by: hosts that want the      │           │ - category-ID remap      │
    │   built-in strategies without     │           │ - PalmDeviceConnection   │
    │   custom UI.                      │           │   ref for keep-alive     │
    └───────────────────────────────────┘           └──────────────────────────┘
                         ▲
                         │
              ┌──────────┴──────────────┐
              │ PlanStan::                │
              │   DialogConflictHandler   │
              │                           │
              │ Extends with:             │
              │ - QDialog-based prompts   │
              │ - integration with        │
              │   CollectionController's  │
              │   undo stack              │
              └───────────────────────────┘
```

Each handler is registered with the coordinator per-backend:

```cpp
// Inside Wild Palms' Full Sync Mode startup:
auto *palmHandler = new PalmConflictHandler(palmConnection, ...);
auto *newerWinsHandler = new AutomaticConflictHandler(
    ConflictPolicy{.autoResolve = AutoResolveStrategy::NewerWins});

coordinator->registerConflictHandler(palmBackend->backendId(), palmHandler);
coordinator->registerConflictHandler(caldavBackend->backendId(), newerWinsHandler);
coordinator->registerConflictHandler(localBackend->backendId(), newerWinsHandler);

// Inside PlanStan's startup:
auto *dialogHandler = new DialogConflictHandler(collectionController);
coordinator->setDefaultConflictHandler(dialogHandler);
```

## Guarantee the library provides

**The library never sees a PalmDeviceConnection.** It never imports a
Palm header. It never knows there is a Palm device in the system.
Wild Palms' PalmConflictHandler hides all Palm details behind
`ConflictHandler`'s virtual interface. The library's only contract
is "you handed me a handler, I will call `handleConflict()` on it
when appropriate."

This is the same guarantee Track 2 gave for `ICalendarHost` — the
calendar-views library never sees PlanStan's `ProjectStore`. Same
pattern, applied to conflict resolution.

## Risk and mitigation

### Risk: `ConflictHandler` interface forces a shape Palm can't fit

If the library's abstract interface requires passing/returning types
that don't have a Palm representation (e.g., assumes no mid-call
state), PalmConflictHandler can't actually implement it faithfully.

**Mitigation:** keep the abstract interface minimal. The current
Wild Palms shape (`handleConflict(const ConflictRecord&) ->
ConflictDecision`, plus signal-based async variants) is a reasonable
starting point. Lift that shape into libkalburator mostly
unchanged, confirm it works with a PalmConflictHandler stub during
Phase 4.

### Risk: Wild Palms' deferred-resolution workflow doesn't fit `ConflictStore`'s abstract shape

`ConflictStore` assumes conflicts are persistable and retrievable
later. If PalmConflictHandler's internal state (open connections,
dirty flags) can't be faithfully serialised for later resolution,
the deferred-resolution workflow breaks for Palm.

**Mitigation:** `ConflictStore` persists `ConflictRecord` (portable
data) only. Handler-internal state is re-established when the
handler is reinstantiated. Wild Palms' deferred conflicts on reload
should pair with connection-reopen logic in PalmConflictHandler's
own setup.

### Risk: namespacing clash between `qsynccore/` and library

Wild Palms already ships `QSyncCore::ConflictRecord` etc. After
library adoption, Wild Palms re-uses `Kalburator::Sync::ConflictRecord`.
Migration is mechanical but non-zero — update includes, update
namespace references, update QML bindings if any.

**Mitigation:** Wild Palms can `using namespace Kalburator::Sync;` or
typedef `QSyncCore = Kalburator::Sync` during a transition window.

## Tests the library needs around this

- Unit test: `AutomaticConflictHandler` resolves every
  `AutoResolveStrategy` variant correctly.
- Unit test: `ConflictStore` round-trips `ConflictRecord` through
  persistence.
- Integration test: a minimal `ConflictHandler` subclass in a test
  file is called for a simulated conflict, returns
  `ConflictDecision::UseTheirs`, and the outcome propagates.
- Smoke (Wild Palms, Phase 4): `PalmConflictHandler` stub (no real
  device) handles a simulated HotSync-timeout scenario without
  tripping any library-level assertions.

## Exit criterion for this audit

✅ Every field / method / class in Wild Palms' `qsynccore/` has a
`[LIFT]` / `[STAY]` tag above or in `02-inventory-wildpalms.md`.
✅ The subclass pattern matches the Track 2 `ICalendarHost` pattern.
✅ Migration path from Wild Palms' `QSyncCore::` namespace to
`Kalburator::Sync::` is identified as mechanical.

Proceed to Phase 1.
