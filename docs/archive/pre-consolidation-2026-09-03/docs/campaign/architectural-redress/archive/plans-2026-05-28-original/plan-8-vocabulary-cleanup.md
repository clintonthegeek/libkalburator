# Plan 8 — Vocabulary cleanup: disambiguate Backend, Canon, Store, Registry, Manager

**Audit refs:** U1, U2, U3, U4, U5 (HIGH–MEDIUM confusion)
**Depends on:** Plans 1–7 (rename what survives; renaming through code that's about to
move wastes motion).
**Branch:** `feature/redress-8-vocabulary`
**State:** Architectural plan + first-task detail.

## Goal

Each overloaded term means one thing per scope (INVARIANTS §5). A new contributor
reading a class name can predict its role; a rename pass cannot accidentally move the
wrong concept.

## Problem (from AUDIT U1–U5)

### U1 — "Backend" overloaded across 6 concepts
- Abstract sync interface: `SyncBackend`
- Abstract blob interface: `IBlobBackend`
- Registration object: `BackendContribution`
- Config struct: `BackendConfiguration`
- Capability set: `BackendCapabilities`
- Capability mixin: `ChangeDetection` (named differently but conceptually one of them)

### U2 — "Canon" overloaded
- `CanonEnvelope` — wrapper around a canonical record.
- `CanonicalRecord` — the canonical-form record.
- `*CanonStages` — the staging pipeline that produces canonical form.
- `canonicalShape()` — accessor returning the canonical shape vertex.
- `*CanonProperties` — the property catalogue for the canon.

### U3 — Store / Registry / Manager inconsistent
- "Store" used for both disk persistence (`BaselineStore`) and in-memory maps
  (`ConflictStore`).
- "Manager" used for both stateful lifecycle (`CalendarManager`) and stateless file I/O
  (`CalendarMetadataManager`, pre-Plan 3).
- `TransformationRegistry` is a registry by name but holds the active graph.

### U4 — `BackendRegistry` dual roles
Holds both live instances and factory contributions. Caller can't tell from method
names which kind they get.

### U5 — Misleading method names
- `collectionRevision(id)` — sounds like a fetch; is an in-memory cache read identical
  to `ctag(id)`. (Plan 4 deletes one; this plan ensures the surviving one's name is
  honest.)
- `updateLogicalCalendar()` — sounds like in-memory mutation; persists to disk.
- `storeCalendars(...)` / `loadCalendars(...)` — plural but per-collection.
- `isValid()` — context-dependent meaning across 20+ classes.

## Approach

Establish vocabulary rules; apply renames in a single pass per file batch; honor the
deprecation shims left by earlier plans by deleting them now.

### Vocabulary rules (locked once chosen in Task 1)

These are working proposals; Task 1 ratifies or refines them:

- **Backend** = abstract sync interface (`SyncBackend`). The runtime contract. One
  meaning.
- Other "Backend*" names get renamed:
  - `BackendContribution` → `BackendProvider` (or align with `IBackendProvider` from
    Plan 2 — pick one term, use everywhere).
  - `BackendConfiguration` → `BackendConfig` (shorter, type-flavored).
  - `BackendCapabilities` stays — clearly a capability set.
- **Canon** rules:
  - `CanonEnvelope` stays (the envelope is genuinely an envelope).
  - `CanonicalRecord` → keep; "canonical form" is the concept it carries.
  - `*CanonStages` → `*CanonicalisationStages` (verbose but unambiguous), or move from
    `Canon` prefix to a clearer descriptor. Decide in Task 1.
  - `*CanonProperties` stays — the property catalogue for the canonical form.
- **Store** = persistence to disk or session-durable storage. In-memory maps that are
  cleared on shutdown are not "stores"; they're `Cache`s or `Registry`s.
  - `ConflictStore` → `ConflictTracker` or `ConflictCache` depending on its lifecycle
    (read its code in Task 1).
- **Registry** = lookup-only, registration-time mutation. Not "the one with state".
  - `TransformationRegistry` stays if it's lookup-only; if it carries the graph, the
    name is honest (a registry of edges). Confirm.
- **Manager** is banned for new code; existing offenders renamed:
  - `CalendarManager` → `CalendarOperations` (per AUDIT U3).
  - `CalendarMetadataManager` (in services/ post-Plan 3) → `CalendarMetadataStore` or
    `VDirMetadataWriter`. Decide in Task 1.

### `BackendRegistry` dual-role split (U4)

Decision options:

- **A:** Split into `BackendInstanceMap` (live instances) + `BackendProviderRegistry`
  (factory contributions). Two classes, clear names.
- **B:** Keep one class, rename to `BackendServiceBus` or `BackendRegistry`, but give
  every method a prefix: `instanceFor(id)`, `providerFor(type)`, `registerProvider(...)`,
  `registerInstance(...)`. Clear by method name.

Decision deferred to Task 1.

## Tasks

### Task 1 — Ratify vocabulary rules

1. Spend one focused session reading the candidates: the existing `Backend*`, `Canon*`,
   `*Store`, `*Manager`, `*Registry` classes.
2. For each rule above, confirm or revise. Record final vocabulary in STATUS as a
   Locked decision (one entry per renamed type).
3. Decide `BackendRegistry` split (A or B).

### Task 2 — Rename pass (mechanical, one batch per concept)

(Detail written after Task 1 ratifies the names. Sketch: scripted `git mv` plus
`sed -i` rewrite across `src/`, `tests/`, and downstream consumers if reachable.
One commit per concept group so a revert is surgical. After each batch, run ctest.)

### Task 3 — Delete the deprecated shims from Plans 1, 4, 7

- `runSyncFuture` overloads (Plan 1) → delete; `runSync(SyncRequest)` is the only
  entry.
- Six `discoveredX(id)` getters (Plan 4) → delete; `discovered(id)` returning the DTO
  is the only entry.
- Four `setCalendar{Color,DisplayName,Description,Order}` setters (Plan 4) → delete;
  `updateCalendarMetadata(patch)` is the only entry.
- `deleteCalendar(id, DeleteMode)` (Plan 7) → delete; the four named methods are the
  entry.

Each deletion requires confirming the downstream migration completed; coordinate
through PlanStan / WildPalms before deleting their callers' surface.

### Task 4 — Fix misleading method names (U5)

1. `updateLogicalCalendar()` → `persistLogicalCalendar()` (or `saveLogicalCalendar()`).
   The verb names what the method *does*, not what it conceptually represents.
2. `storeCalendars(...)` / `loadCalendars(...)` → `storeCollectionCalendars(...)` /
   `loadCollectionCalendars(...)` (or pick a less plural form).
3. `isValid()` audit: spot-check the larger consumers. Where the meaning is "fields
   present and well-formed", `isWellFormed()` is honest. Where it's "represents a real
   live entity", `isLive()` or `exists()`. **Do not do a sweeping `isValid` rename**;
   pick the worst 3–5 offenders flagged in AUDIT and fix only those. The rest is too
   risky for the value.

### Task 5 — Re-run tests and close

1. Full ctest.
2. PlanStan ctest.
3. WildPalms smoke if locally runnable.
4. Update FINDINGS: cross out U1–U5 entries with closing commit hash.
5. Open Plan 9.

## Files affected

Many. The rename is wide but mechanical. Use `git grep -l` to enumerate each batch
before executing.

## Acceptance criteria

- Every renamed type has one meaning per scope (confirm by greppning the new name and
  finding it only where intended).
- Deprecated shims from Plans 1, 4, 7 are deleted, not just hidden.
- All tests pass; PlanStan ctest baseline holds; WildPalms invariants preserved.

## Risks

- **Downstream consumer breakage.** PlanStan and WildPalms call into many of these
  names. Coordinate with their teams (i.e., yourself in another session) before
  deleting deprecated shims. The `[[deprecated]]` warnings in Plans 1/4/7 are the
  notice period.
- **Reviewer fatigue.** A vocabulary-cleanup PR with 200 renamed identifiers is hard to
  review. Split per concept group (one PR per locked decision) so each is reviewable in
  isolation.
- **Merge conflicts with parallel work.** If any other Plan branch is in flight, this
  Plan should be the merge-base — it goes last so its rebase cost is borne once.

## Estimated effort

3–4 sessions, mostly mechanical. Reviewer fatigue and downstream coordination dominate
the wall-clock.
