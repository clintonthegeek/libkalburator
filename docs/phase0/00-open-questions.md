# Phase 0 — Open questions

Questions the Phase 0 audit surfaced that are worth deciding before
Phase 1 begins. Each question has a proposed default so Phase 1 can
proceed if the user does not weigh in. **The proposed defaults are
not final** — maintainer review explicitly wanted.

---

## 1. `ICalendarSyncCoordinator` vs `BlobSyncEngine`: inherit, compose, or independent?

Options:

- **Compose** — `ICalendarSyncCoordinator` holds a `BlobSyncEngine*`
  internally and delegates lower-layer transport to it.
- **Inherit** — `ICalendarSyncCoordinator : public BlobSyncEngine`,
  adds incidence-typed surface.
- **Independent** — two engines that don't know about each other;
  calendar backends bypass the blob layer entirely.

**Proposed default:** compose. Inheritance leaks lower-layer details
into the calendar-typed public API; independence duplicates transport
logic.

## 2. Namespace

`Kalburator::Sync::*` vs `Kal::Sync::*` vs `Kalburator::*` flat.

**Proposed default:** `Kalburator::Sync::*`, with future modules
using sibling namespaces (`Kalburator::Transport::*`,
`Kalburator::Discovery::*` if they appear). Keeps room for growth
without re-namespacing.

## 3. DecSync layer

DecSync is currently calendar-typed in PlanStan (`DecsyncBackend :
SyncBackend`). DecSync's wire format is blob-shaped (CBOR-encoded
entries keyed by path); it could support contacts / memos / any
data type.

**Proposed default:** keep calendar-typed for Phase 1–3 (don't
re-shape PlanStan's existing code). Phase 4 can reconsider if Wild
Palms asks for DecSync-for-contacts.

## 4. Per-backend vs per-mapping ConflictHandler registration

Wild Palms' current code registers conflict handlers at the
`SyncEngine` level (global). PlanStan registers `IConflictResolver`
at the `ConflictManager` level (also global).

The library needs to support **per-backend** handler registration so
Wild Palms' Palm-specific handler applies only to the `PalmBackend`
and not to CalDAV backends in the same coordinator.

**Proposed default:** per-backend handler registration, with a
global fallback handler for backends that do not register one. API:

```cpp
coordinator->registerConflictHandler(backendId, handler);
coordinator->setDefaultConflictHandler(fallbackHandler);
```

## 5. `ICalendarCollection` surface — exact method list

The sketch listed ~12 methods. Need to audit **every** call to
`Collection::*` from `libs/sync/` sources to confirm the list is
necessary and sufficient.

**Proposed Phase 1 kickoff task:** produce `grep -rn 'collection->'
libs/sync/` report and reconcile against `ICalendarCollection` draft.

## 6. Wild Palms contacts / memos — library or host?

The layered architecture suggests Wild Palms' contacts + memos sync
goes through `IBlobBackend` in the library. But VCardBackend and
MarkdownMemoBackend don't exist yet in either project — they'd be
**new code in the library**.

**Proposed default:** Phase 1 ships the blob layer + calendar
layer; contacts and memos are Wild Palms' responsibility in Phase 4
and either ship as Wild Palms-internal subclasses of `IBlobBackend`
or, if general-interest, get upstreamed to the library as a Phase
5+ addition.

## 7. Syncthing module — same repo or sibling?

PlanStan's `syncthingdiscovery.{h,cpp}` + `syncthingmonitor.{h,cpp}`
could live under the same library root or as a sibling module
`libkalburator-syncthing` that optionally depends on the core.

**Proposed default:** same repo, optional target
`Kalburator::Syncthing` gated by a CMake `-DKALBURATOR_SYNCTHING=ON`
flag. Keeps the monorepo during Phase 1–3.

## 8. License final call

Proposal in `05-repo-strategy.md`: LGPL-3.0-only.

Alternatives: LGPL-2.1-or-later, MPL-2.0, Apache-2.0.

**Needs maintainer explicit decision** — license choice is not
easily reversible.

## 9. Public forge / KDE Invent / Codeberg / GitHub

Deferred to Phase 4 per `05-repo-strategy.md`. Flagged here so we
remember to re-raise it.

## 10. "Kalburator" — is there a spelling / branding preference?

Current doc spells it `libkalburator` (lowercase, no hyphen).
Alternatives: `libKalburator`, `Kalburator` (no "lib"),
`libkalburator-sync`.

**Proposed default:** stay with `libkalburator` (directory +
repo name), `Kalburator` as namespace, `PlanStan::Kalburator` as
... no, strike that — `Kalburator::Sync` as CMake target and
namespace. Per `05-repo-strategy.md`.

---

## Resolution protocol

Each question above, when answered, gets its resolution committed
into this file as a struck-through block. The Phase 0 exit criterion
is: **all 10 questions either answered or consciously deferred**.

At that point the Phase 0 doc set is frozen, and Phase 1 starts from
`04-merged-interface-sketch.md`.
