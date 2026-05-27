# Design: promote sync-topology generation & authority into libkalburator

**Date:** 2026-05-27
**Status:** Approved (design) — ready for implementation plan.
**Repo:** libkalburator (this is Phase 1 of a 3-repo program).
**Target tag:** v0.57 (lands on `main`, tagged after the work is green).
**Parent / coordination spec:** `PlanStan/docs/superpowers/specs/2026-05-27-sync-topology-promotion-coordination-design.md`
**Origin analysis:** `PlanStan/docs/2026-05-27-sync-topology-authority-analysis.md`;
RFC `WildPalms/docs/2026-05-27-libkalburator-topology-authority-proposal.md`.

## 1. Problem

libkalburator ships the policy vocabulary (`LogicalCalendar`, `BackendRole`,
`SyncTopology`, `discoveredWritable()`) but not the verb that animates it. The verb —
translating `(logical calendars + topology)` into the flat `SyncMapping` list the engine
consumes — lives downstream in PlanStan's `CollectionController::generateSyncMappingsFromLogicalCalendars()`
and is unsharable. A second consumer (WildPalms) now needs the same model. This phase
**promotes the verb into libkalburator** as a pure, headless, domain-agnostic helper, and
closes the latent authority gap: `discoveredWritable()` is reported by backends but never
consulted before writing, and `LogicalCalendarBuilder` assigns roles by ordering index,
so a discovered read-only remote becomes a writable `SyncN` target.

The engine's flat `SyncMapping` contract is correct and is **not** touched. The new
generator sits *above* the engine.

## 2. Decisions (resolved during brainstorming, 2026-05-27)

| Decision | Resolution |
|---|---|
| Generator wiring | **Pure function + opt-in persist helper.** The library ships a pure `generateMappings()` and a `regenerateInto(store)` helper that generates + persists; each host still owns its runtime-engine feed (inherently host-specific). |
| Type mechanic | **Keep the name `LogicalCalendar`**; add a `domain` field + `collectionId()` accessor; add `using LogicalCollection = LogicalCalendar;`. No struct rename, no serialization-fn rename. |
| Demotion query | **Neutral fact, not policy.** Ship `hasWritableRemoteSyncTarget()`; the *consumer* applies its policy (WildPalms demotes editing; PlanStan does not — it is not a conduit and syncs two-way). |
| CalendarManager auto-regenerate | **No.** CalendarManager keeps emitting `syncMappingRegenerationRequested()`; it does not take over persistence. |

## 3. Components

### 3.1 Type generalization — `src/types/logicalcalendar.h`

- Add member to `LogicalCalendar` with an in-class default of the calendar domain:
  ```cpp
  Shape::DomainId domain = Shape::DomainId(QStringLiteral("calendar"));
  ```
  This couples `src/types/` → `src/shape/` (include `shape/shape.h`). `Shape` is a
  foundational layer; the dependency is acceptable. (If a layering objection arises in
  review, the fallback is to store `domain` as a `QString` and convert at the boundary;
  default to the pure-function form first.)
- Add accessor `QString collectionId() const { return id; }` (alias of `id`; the field
  itself stays `id` for serialization stability).
- Add `using LogicalCollection = LogicalCalendar;` at namespace scope so domain-agnostic
  code (and WildPalms) can use the neutral name. Both names are permanent.
- Add the demotion fact (see 3.4).

### 3.2 Serialization — `src/types/logicalcalendar.h` (`logicalCalendar{To,From}Json`)

- **Write:** emit `"domain"` **only when non-calendar**:
  ```cpp
  if (cal.domain != Shape::DomainId(QStringLiteral("calendar")))
      obj[QStringLiteral("domain")] = cal.domain.toString();
  ```
- **Read:** default an absent key to calendar:
  ```cpp
  cal.domain = obj.contains(QStringLiteral("domain"))
      ? Shape::DomainId(obj[QStringLiteral("domain")].toString())
      : Shape::DomainId(QStringLiteral("calendar"));
  ```
- **Invariant:** existing calendar collections round-trip byte-for-byte (no new key).
  Covered by a canary test (3.5).

### 3.3 The generator + persist helper — new `src/sync/syncmappinggenerator.{h,cpp}`

Namespace `Kalburator::Sync`. Added to the sync source list in `CMakeLists.txt`.

```cpp
namespace Kalburator::Sync {

// Pure: no engine/runtime state, no side effects.
QList<SyncMapping> generateMappings(const Types::LogicalCalendar &lc,
                                    SyncTopology topology);

// Convenience: per-lc concat, skipping !syncEnabled.
QList<SyncMapping> generateMappings(const QList<Types::LogicalCalendar> &lcs,
                                    SyncTopology topology);

// Opt-in helper: reads logical calendars + topology from the store, generates,
// writes back via store.setSyncMappings(...), and RETURNS the list so the host
// can feed its own runtime engine. (Does NOT touch any runtime coordinator.)
QList<SyncMapping> regenerateInto(ISyncConfigStore &store);

}
```

- Generation logic is lifted verbatim from PlanStan `collectioncontroller.cpp:1862-1926`
  (Star = primary ↔ each spoke; Mirror = full mesh i<j; Chain = sequential), minus the
  persistence/coordinator side-effects. Per-lc it: skips `!syncEnabled`, requires a valid
  `primaryBinding()`, excludes `BackendRole::ReadOnly`, and uses the existing deterministic
  mapping-id scheme (`auto_<lcId>_<role>` / `auto_<lcId>_mirror_<a>_<b>` /
  `auto_<lcId>_chain_<a>_<b>`) so ids are stable across regenerations.
- **Impl note:** verify `ISyncConfigStore` exposes `logicalCalendars()`, `syncTopology()`,
  and `setSyncMappings()`. If `syncTopology()` is absent on the interface, change
  `regenerateInto` to take `SyncTopology` as an explicit parameter rather than widening the
  interface.

### 3.4 Authority

**(a) Role seed — `src/calendar/logicalcalendarbuilder.cpp` (`createBindingFromDiscovery`, ~170-199):**
After the role is assigned by index, if the discovered calendar is not writable:
- for a **sync** binding (`role != Primary`): force `binding.role = BackendRole::ReadOnly`;
- for the **primary** binding: do **not** silently demote (would break exactly-one-Primary);
  instead record a builder warning ("primary backend reports read-only").

**(b) Write-gate — `src/engine/syncengine.cpp`:**
Add a private helper that gates the unified write path and reports without aborting:
```cpp
bool SyncEngine::ensureWritable(IBlobBackend *tgt, const QString &collectionId);
// false ⇒ skip the write, increment a skipped/error counter, log once.
```
Call it before **every** `createRecord`/`updateRecord`/`deleteRecord`. The exploration
located only the first-sync mirror sites (`syncengine.cpp:1709,1714,1723`); the
implementation **must audit all write call sites** (including the regular diff-apply path)
and gate each. This is defense-in-depth — read-only targets are already excluded from the
mapping set by 3.3 + 3.4(a), but a backend can become non-writable at runtime (ACL change).

**(c) Demotion fact — `src/types/logicalcalendar.h`:**
```cpp
// True iff an enabled binding has a Sync* role (a writable remote spoke exists).
// Neutral fact; the consumer decides policy.
bool hasWritableRemoteSyncTarget() const;
```
Pure, config-level, no backend access. Consumers apply their own policy: WildPalms gates
editing on it (conduit demotion); PlanStan ignores it (two-way local editing).

### 3.5 Tests (TDD)

QtTest, under `tests/`, naming `tst_<component>[_<feature>].cpp`, registered in the test
CMake. New files:
- `tests/sync/tst_syncmappinggenerator.cpp` — Star/Mirror/Chain output; ReadOnly excluded;
  `!syncEnabled` skipped; invalid-primary skipped; dedupe; multi-lc concat; empty inputs.
- `tests/calendar/tst_logicalcalendar_domain.cpp` — domain round-trips; absent key ⇒ calendar;
  non-calendar emits the key; calendar collection serializes byte-for-byte unchanged;
  `hasWritableRemoteSyncTarget()` true/false cases; `collectionId() == id`; alias compiles.
- `tests/calendar/tst_logicalcalendarbuilder_readonly_seed.cpp` — non-writable discovered
  sync binding ⇒ `ReadOnly`; non-writable primary ⇒ warning (role stays Primary).
- `tests/engine/tst_engine_write_gate.cpp` — using a stub backend whose `discoveredWritable`
  returns false: create/update/delete are skipped + counted, sync does not abort, writable
  targets are unaffected. (Reuse `tests/calendar/stubs/` patterns.)

## 4. Out of scope (guardrails)

- **No change** to the `SyncMapping` struct or `SyncEngine`'s loop semantics (only the
  pre-write gate is added).
- **No struct rename** — `LogicalCalendar` stays; `LogicalCollection` is the alias.
- **No `CalendarManager` ownership of persistence** — it keeps emitting the hook.
- **No topology editor / widget** — that is PlanStan-side UI, not libkalburator.
- **No policy in the demotion query** — neutral fact only.
- `.kalb` / config files must round-trip unchanged when `domain` is absent or calendar.

## 5. Sequencing within the program

This is Phase 1. It is independent of the program's Phase 0 (PlanStan's
`integration_incidence_crud` fix + v0.56 pin bump) and can be developed in parallel.
PlanStan adoption (Phase 2) consumes this via the v0.57 tag; the WildPalms handoff
(Phase 3) documents consumption. See the coordination spec for the full program.
