# Plan 9 — Backend-adjacent dir consolidation + discovery placement & DTO

**Audit refs:**
- MAJOR — "`backend/` is a capability bin with no clear principle (B5-adjacent)"
  (`changedetection.h` is a mixin, not a backend; dir should hold "backend base classes
  or nothing").
- MODERATE `B5` — "three backend-adjacent dirs lack documented layer position +
  namespace fragmentation (Backend/Storage/Sinks)".
- MODERATE — "Asymmetric discovery placement" (`CalDavCapabilityDiscovery` in `calendar/`,
  `CardDavCapabilityDiscovery` in `sync/`; `multiprotocoldavprovider.cpp` crosses upward
  into calendar) + "Implicit CMake include paths expose domain headers to `sync/`".
- MODERATE — "7 loose `discovered*` getters leak `RemoteCalendarBackend` discovery state
  … consolidate into a DTO" (now 6 getters post-Plan-7) + "`discoveredCapabilities()`
  bulk getter" + "Discovery URL maps duplicated across discovery/provider/backend" +
  "`addressbookUrls()` loose getter".

**Branch:** `feature/redress-9-backend-dir-discovery-consolidation` off `main` @ `434c7d4`
(tag **v0.72**).
**Baseline at open:** **148/148** (`ctest --test-dir build -j 8`, confirmed 2026-06-11 on
this branch @ `ca06534`, default profile Akonadi/Org OFF). This is the gate for every later
task.
**State:** plan written 2026-06-11 against the landed v0.72 tree. Scope band **Broad**
(user decision 2026-06-11): includes the public `discovered*` → DTO collapse, which carries
a PlanStan consumer wave (invariant 10).

> **Doc-location deviation (INVARIANTS §7 / Scope-and-exceptions):** the brainstorming
> process that produced this plan defaults its design doc to `docs/superpowers/specs/`.
> The campaign convention (plan file in `plans/`, combining rationale + tasks) is an
> explicit project instruction and wins. This file is both the design spec and the
> implementation plan.

## Why the audit's framing is partly stale (re-derived against v0.72, invariant P1)

The AUDIT line-numbers predate v0.63–v0.72. Verified corrections this plan rests on:

- **`backend/` now holds ONE file** — `changedetection.h`. `resourcelinearization.h`
  (the audit's second file) is already deleted. `changedetection.h` is a neutral,
  non-QObject pure-virtual capability mixin (no domain types; namespace
  `Kalburator::Backend`). **0 downstream consumers** (PlanStan + WildPalms grep,
  2026-06-11). Its own doc references a `Backend::AbstractSyncBackend` that never shipped.
- **`CalDavCapabilityDiscovery` is transport-only** — includes only
  `typesupport/backendconfiguration.h` + QtNetwork; no KCalendarCore, no calendar types;
  its output `DiscoveredCapabilities` is neutral (`typesupport/`). **No `calendar/` file
  consumes it** — only `sync/caldavprovider.cpp`, `sync/multiprotocoldavprovider.cpp`,
  and `tests/sync/`. **0 downstream consumers.**
- **The "loose `discovered*` getters" are NOT all RCB-local leaks.** 3 are
  calendar-flavored polymorphic virtuals on `calendar/syncbackend.h`
  (`discoveredCalendarType` :176, `discoveredColor` :181, `discoveredDisplayName` :186,
  overridden by Akonadi/DecSync/RCB); 3 are RCB-local non-virtuals (`discoveredUrl`,
  `discoveredSupportsEvents`, `discoveredSupportsTodos`); and `discoveredWritable` is on
  the **neutral** `sync/syncbackendbase.h:76`, consumed by the **engine write-gate**
  (`syncengine.cpp:1696/:2580`) for all backends and delegated by
  `FilteredCollectionBackend`. The target DTO `Sync::DiscoveredCalendar`
  (`calendar/discoveredcalendar.h`) **already exists** with the right fields, and
  PlanStan's `backenddiscoveryhelper.cpp:66-85` is hand-rebuilding it from the getters.
- **Namespace renames stay OUT.** `Kalburator::Sinks` has **11 WildPalms sites** —
  renaming it is a downstream-breaking vocabulary change = Plan 10. Plan 9 only documents
  the layer position and flags the `Sinks`↔`universal/` dir mismatch for Plan 10.

## Scope (locked by the brainstorming session, 2026-06-11)

| Stream | What | Downstream |
|---|---|---|
| **A** | Delete `backend/`; move `changedetection.h` → `sync/`; ns `Backend`→`Sync` | 0 |
| **B** | Move `caldavcapabilitydiscovery.{h,cpp}` `calendar/` → `sync/` | 0 |
| **C** | `DiscoveredCalendar` DTO accessor; collapse the **6** per-field getters into it. **All 6 become `[[deprecated]]` forwarders** during the PlanStan window — the 3 calendar-base virtuals (`discoveredColor`/`discoveredCalendarType`/`discoveredDisplayName`) as non-virtual base forwarders (overrides deleted); the 3 RCB-local (`discoveredUrl`/`discoveredSupportsEvents`/`discoveredSupportsTodos`) kept on RCB as forwarders (PlanStan consumes all three — `backenddiscoveryhelper.cpp:83-85`). Forwarder deletion → T7.2/Plan 11. Keep neutral `discoveredWritable`; sync-internal URL-map dedup | PlanStan ~10 sites (net-simplifying); WildPalms 0 |
| **D** | Document layer position of `sync/` (neutral contracts), `storage/`, `universal/`; FINDINGS for the deferrals | 0 |

**Explicitly deferred (flagged in FINDINGS, NOT touched — fork decisions 2026-06-11):**
- Namespace renames (`Sinks` etc.) → Plan 10 (11 WildPalms sites).
- The residual `sync/ → calendar/` **concrete-backend** include
  (`caldavprovider.cpp`/`multiprotocoldavprovider.cpp` `#include "../calendar/remotecalendarbackend.h"`)
  — the B4-corrected MAJOR, its own concern (providers producing concrete domain backends),
  NOT a Plan 9 rider.
- `discoveredWritable` stays neutral on `SyncBackendBase` — the engine is neutral
  (post-Plan-3) and must not depend on the calendar-flavored DTO. `DiscoveredCalendar.writable`
  is populated *from* it.

## Gates

- libkalburator `ctest -j 8` stays at the T0-recorded baseline after **every** task
  (one new protective-test executable in T3 raises it by 1).
- `src/backend/` deleted; `grep -rn "backend/changedetection.h\|Kalburator::Backend"
  src/ tests/ examples/` returns **zero** live hits.
- `src/calendar/caldavcapabilitydiscovery.*` gone; `grep -rn "calendar/caldavcapabilitydiscovery"
  src/ tests/` returns zero; the two providers include it same-dir (no `../calendar/`
  for discovery).
- After T4: the 6 collapse-getters survive only as `[[deprecated]]` forwarders (3
  non-virtual base + 3 RCB-local) plus the per-backend `discoveredCalendar()` builder
  bodies and DecSync's extracted private `calendarTypeFor`/`displayNameFor`. **Zero per-field
  overrides** of `discoveredColor`/`CalendarType`/`DisplayName` in concrete backends, and
  **zero in-lib callers** of any deprecated forwarder (the lib build is deprecation-warning
  clean; PlanStan's calls migrate in T7). `discoveredWritable` untouched.
- PlanStan `ctest` baseline green after the consumer wave (T7); WildPalms five invariants
  hold by construction (0 changed-symbol consumers — verified 2026-06-11).
- `compile_commands.json` regenerated; clangd shows no new diagnostics on touched TUs.

## Consumer contract (verified 2026-06-11, one plain pattern per symbol; lib + PlanStan + WildPalms)

| Symbol | Lib callers | PlanStan | WildPalms | Disposition |
|---|---|---|---|---|
| `changedetection.h` / `Kalburator::Backend::ChangeDetection` | 6 includers (5 `../backend/`, 1 unqualified in `syncengine.cpp`) | 0 | 0 | **move to `sync/`**, ns→`Sync`; rewire 5 `../backend/`→`../sync/`; `syncengine.cpp` unqualified needs no edit (sync/ on include path) |
| `CalDavCapabilityDiscovery` (class) / `calendar/caldavcapabilitydiscovery.h` | `sync/caldavprovider.cpp`, `sync/multiprotocoldavprovider.cpp`, `tests/sync/tst_caldav_provider.cpp` | 0 | 0 | **move file to `sync/`**; includes become same-dir |
| `discoveredCalendarType` | `decsyncbackend.cpp` (4 internal), `akonadibackend.cpp`, `remotecalendarbackend.cpp`, `tst_decsyncbackend.cpp` (many) | `backenddiscoverycoordinator.cpp:103`, `collectioncontroller.cpp:1087`, `tst_calendarcrud.cpp:200/836` (4) | 0 | base virtual → **`[[deprecated]]` forwarder** to `discoveredCalendar(id).calendarType()`; overrides deleted; DecSync internal → private helper |
| `discoveredColor` | `akonadibackend.cpp`, `decsyncbackend.cpp`, `remotecalendarbackend.cpp` | `backenddiscoveryhelper.cpp:74`, `tst_calendarcrud.cpp:511` (2) | 0 | base virtual → **`[[deprecated]]` forwarder** to `.color`; overrides deleted |
| `discoveredDisplayName` | `akonadibackend.cpp`, `decsyncbackend.cpp`, `tst_decsyncbackend.cpp` (4) | 0 | 0 | base virtual → **`[[deprecated]]` forwarder** to `.name`; overrides deleted |
| `discoveredUrl` | RCB only + `tst_remotecalendarbackend_writepaths.cpp` (3) | `backenddiscoveryhelper.cpp:83` (1) | 0 | RCB-local → **`[[deprecated]]` forwarder** to `.davUrl()` (PlanStan consumes; deleted T7.2/Plan 11) |
| `discoveredSupportsEvents` | RCB only + writepaths test | `backenddiscoveryhelper.cpp:84` (1) | 0 | RCB-local → **`[[deprecated]]` forwarder** to `.supportsVEvent` (PlanStan consumes; deleted T7.2/Plan 11) |
| `discoveredSupportsTodos` | RCB only + writepaths test | `backenddiscoveryhelper.cpp:85` (1) | 0 | RCB-local → **`[[deprecated]]` forwarder** to `.supportsVTodo` (PlanStan consumes; deleted T7.2/Plan 11) |
| `discoveredWritable` | neutral base; engine `syncengine.cpp:1696/:2580`; `FilteredCollectionBackend`; many backends | `backenddiscoveryhelper.cpp:75` (1) | 0 | **KEEP** (neutral primitive); DTO `.writable` populated from it |
| `discoveredCapabilities()` (CalDav discovery) | `caldavprovider.cpp`, `multiprotocoldavprovider.cpp` | 0 | 0 | sync-internal; tidy in T5 (out of the public-getter wave) |
| `Sinks` namespace / `universal/` | many | 0 | **11 sites** | **DEFER to Plan 10** (document only) |
| `universalstorageplugin.h` | — | `appcontroller.cpp:10` | `palmruntime.cpp:36` | untouched (Plan 9 does not move `universal/`) |

PlanStan total: **10 `discovered*` call sites in 4 files** (6 of them in
`backenddiscoveryhelper.cpp`, which rebuilds a `DiscoveredCalendar` by hand → the wave
*simplifies* it). WildPalms: **0** `discovered*` / discovery / `changedetection`
consumers — its only libkalburator-backend coupling is `Kalburator::Sinks` (deferred).

## Tasks

### T0 — Open the branch; record the baseline

Branch off `main` @ `434c7d4`. `cmake --build build -j 8 && ctest --test-dir build -j 8`;
record the exact pass count in this file's Outcome (it is the gate for every later task).
Regenerate `compile_commands.json`.

### T1 — Stream A: delete `backend/`, move `changedetection.h` → `sync/`

Pure relocation (mechanical; `ChangeDetection` is covered by the existing
`collectionRevision`/`cachedCollectionRevision` tests via every backend's ChangeDetection
suite — no behavior change).

1. `git mv src/backend/changedetection.h src/sync/changedetection.h`.
2. In the moved file: `Kalburator::Backend` → `Kalburator::Sync` (the `namespace`
   open/close at `:8`/`:106`); update the include guard
   `KALBURATOR_BACKEND_CHANGEDETECTION_H` → `KALBURATOR_SYNC_CHANGEDETECTION_H`. Update the
   class doc comment that references `Backend::AbstractSyncBackend` /
   `Backend::ChangeDetection` → `Sync::ChangeDetection`.
3. Rewire the 5 relative includers
   (`src/calendar/remotecalendarbackend.h:8`, `src/contacts/remotecontactsbackend.h:8`,
   `src/calendar/localbackend.h:16`, `src/calendar/akonadibackend.h:10`,
   `src/contacts/akonadicontactsbackend.h:10`): `"../backend/changedetection.h"` →
   `"../sync/changedetection.h"`. `src/engine/syncengine.cpp:30` (`"changedetection.h"`
   unqualified) needs no edit — `src/sync` is already on the build-interface include path
   (CMakeLists `:712`).
4. Each `public Backend::ChangeDetection` base-specifier → `public Sync::ChangeDetection`
   (same 6 files; the backends are already in or `using` `Kalburator::Sync`, so most are
   already-qualified or become unqualified — verify per file).
5. CMakeLists: header-list entry `:288` `src/backend/changedetection.h` →
   `src/sync/changedetection.h` (place it next to `src/sync/syncbackendbase.h`); **delete**
   the build-interface include line `:714`
   `$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/backend>`.
6. `rmdir src/backend`. Build `-j 8`, ctest green, regenerate compile DB.

### T2 — Stream B: move CalDav capability discovery → `sync/`

Pure relocation (transport-only class; consumers all in `sync/` + `tests/sync/`).

1. `git mv src/calendar/caldavcapabilitydiscovery.h src/sync/` and the `.cpp`.
2. Includes: `src/sync/caldavprovider.cpp:6` and the `.cpp`'s own `#include` already
   resolve same-dir (no `../calendar/`); fix `src/sync/multiprotocoldavprovider.cpp:4`
   `"../calendar/caldavcapabilitydiscovery.h"` → `"caldavcapabilitydiscovery.h"`. Update
   `tests/sync/tst_caldav_provider.cpp`'s include if it uses a `calendar/` path.
3. CMakeLists: move the two list entries `:134`/`:179` from the calendar block to the
   CardDAV-transport block (next to `src/sync/carddavcapabilitydiscovery.{h,cpp}`
   `:522`/`:557`). The class is QObject → AUTOMOC already covers it; no target change.
4. Build `-j 8`, ctest green. **Residual flagged (not fixed):**
   `multiprotocoldavprovider.cpp` + `caldavprovider.cpp` still
   `#include "../calendar/remotecalendarbackend.h"` (concrete backend) — the deferred
   B4-corrected MAJOR. FINDINGS entry in T6.

### T3 — Stream C, protective seam first (inv 6): the `DiscoveredCalendar` DTO accessor

The refactor *generalizes existing behaviour*; per inv 6 add the new-seam test and show it
red before the accessor exists, keep all existing getter tests green.

1. `calendar/discoveredcalendar.h`: add a derive-from-fields convenience (no new state):
   ```cpp
   /// Derived from supportsVEvent/supportsVTodo (mirrors the old discoveredCalendarType).
   CalendarType calendarType() const {
       if (supportsVEvent && supportsVTodo) return CalendarType::Hybrid;
       if (supportsVTodo)                   return CalendarType::Todo;
       return CalendarType::Event;
   }
   ```
   (Add `#include "calendartype.h"`.) `davUrl()` already exists.
2. `calendar/syncbackend.h`: add the virtual aggregate accessor, default-built from the
   neutral primitive + empty/invalid for the rest:
   ```cpp
   virtual DiscoveredCalendar discoveredCalendar(const QString &calendarId) const {
       DiscoveredCalendar d;
       d.calendarId = calendarId;
       d.writable   = discoveredWritable(calendarId); // neutral base primitive — KEEP
       return d;
   }
   ```
   (`#include "discoveredcalendar.h"`.)
3. Implement the override in **RCB / Akonadi / DecSync** from internal state (the same
   sources the per-field getters read today — RCB's `m_calendars[id]`/`CalendarFacts`,
   Akonadi's collection, DecSync's computed type). Populate
   `name/color/supportsVEvent/supportsVTodo/writable` + (RCB) `setDavUrl(...)`. The
   per-field getters still exist at this point → suite stays green.
4. Protective test: new slots in `tests/sync/tst_remotecalendarbackend_writepaths.cpp`
   (`discoveredCalendar_reports_url_components_and_type`) and
   `tests/calendar/tst_decsyncbackend.cpp` (`discoveredCalendar_reports_name_and_type`)
   that read every DTO field and `QCOMPARE` against the same fixtures the existing
   per-getter slots use. Falsifiability: stub the override to `return {}` → slots go red;
   restore. Suite baseline → +1 (new executable only if a new file is added; the slots
   land in existing executables → count unchanged, assertions added).

### T4 — Stream C: retire the per-field getters onto the DTO

1. **Calendar-base virtuals** (`syncbackend.h` `discoveredCalendarType` :176,
   `discoveredColor` :181, `discoveredDisplayName` :186): convert each from `virtual` to a
   **`[[deprecated("use discoveredCalendar()")]]` non-virtual** forwarder:
   ```cpp
   [[deprecated("use discoveredCalendar(id).color")]]
   QColor discoveredColor(const QString &id) const { return discoveredCalendar(id).color; }
   ```
   (and `.calendarType()`, `.name`). PlanStan keeps building (deprecation warning) until
   the T7 wave; deletion of the forwarders is T7's tail / Plan 11.
2. **Delete the overrides** of those three in `akonadibackend.{h,cpp}`,
   `decsyncbackend.{h,cpp}`, `remotecalendarbackend.{h,cpp}` (their data now flows through
   `discoveredCalendar()`).
3. **RCB-local trio → `[[deprecated]]` forwarders** (NOT deleted — PlanStan consumes all
   three at `backenddiscoveryhelper.cpp:83-85`): `discoveredUrl` →
   `discoveredCalendar(id).davUrl()`, `discoveredSupportsEvents` → `.supportsVEvent`,
   `discoveredSupportsTodos` → `.supportsVTodo` (mark the header decls `[[deprecated]]`).
   Migrate the in-lib `tst_remotecalendarbackend_writepaths.cpp` call sites to
   `discoveredCalendar()` so the lib build stays deprecation-clean.
4. **DecSync helper extraction + recursion fix.** Extract `discoveredCalendarType`'s body
   into `private CalendarType calendarTypeFor(const QString&) const` and
   `discoveredDisplayName`'s body into `private QString displayNameFor(const QString&) const`;
   delete the three DecSync overrides. Rewire `DecSyncBackend::discoveredCalendar()` (T3) to
   call `calendarTypeFor()`/`displayNameFor()`/`calendarColor()` directly — NOT the
   now-forwarding getters (which would recurse through `discoveredCalendar()`). Point the 4
   internal `discoveredCalendarType` self-calls (`decsyncbackend.cpp` :264/:401/:473/:1034)
   at `calendarTypeFor()`. Likewise fix `AkonadiBackend::calendarColor` (`:642`,
   `return discoveredColor(id)`) → `discoveredCalendar(id).color` (Akonadi gated off —
   compile-verify in build-akonadi).
5. Migrate the in-lib `tst_decsyncbackend.cpp` assertions (many
   `discoveredCalendarType`/`discoveredDisplayName`) to
   `backend.discoveredCalendar(id).calendarType()` / `.name`.
6. `discoveredWritable` untouched everywhere. Build `-j 8`, ctest green.

### T5 — Stream C: sync-internal discovery URL-map dedup

Scope: entirely within `sync/` (after T2). The providers keep a private copy of the
discovery class's URL map:
- `src/sync/caldavprovider.h:70 m_calendarUrls` duplicates
  `CalDavCapabilityDiscovery::calendarUrls()`.
- `src/sync/carddavprovider.h:64 m_addressbookUrls` duplicates
  `CardDavCapabilityDiscovery::addressbookUrls()`.

Each provider holds the discovery object for its connection lifetime; replace the eager
member-copy with a read-through to the discovery getter at the (few) use sites, or store
the map once and drop the discovery-side getter exposure if the provider is the only
reader. Decide per-callsite at implementation (both providers are small); the gate is "one
owner per URL map within `sync/`". The `discoveredCapabilities()` bulk getter
(`caldavcapabilitydiscovery.h`) — both callers reach into `.perCalendarCapabilities`;
narrow to the accessor they actually use if it falls out cleanly, else FINDINGS-note for
Plan 11. Build `-j 8`, ctest green.

### T6 — Stream D: document layer position; FINDINGS for the deferrals

1. Header-comment the layer role at the top of `src/sync/syncbackendbase.h` (and the moved
   `src/sync/changedetection.h`): "`sync/` holds the **neutral backend contracts**
   (`SyncBackendBase`, `ChangeDetection`, `IBlobBackend` lives in `blob/`) that domains
   implement and the engine orchestrates against." One-line role comments at the top of
   `src/storage/baselinestore.h` ("SQLite-persistent sync stores") and
   `src/universal/universalstorageplugin.h` ("concrete sink backends; namespace
   `Kalburator::Sinks` — dir/ns mismatch tracked for Plan 10").
2. `FINDINGS.md` (same commit per inv 9):
   - `sync/{caldav,multiprotocoldav}provider.cpp` still `#include "../calendar/remotecalendarbackend.h"`
     — inv 1 — the deferred B4-corrected concrete-backend layering MAJOR; own concern, not
     a Plan 9 rider (fork decision 2026-06-11).
   - `universal/` dir ↔ `Kalburator::Sinks` namespace mismatch — inv 5 — rename deferred to
     Plan 10 (11 WildPalms `Kalburator::Sinks` sites; downstream-breaking).
   - any `discoveredCapabilities()`/`perCalendarCapabilities` narrowing left for Plan 11
     (if T5 deferred it).

### T7 — Consumer wave (PlanStan) + close-out

1. **RFC + migrate PlanStan** (the only consumer, 10 sites, 4 files):
   - `src/dialogs/backenddiscoveryhelper.cpp:66-85` — collapse the 6 getter calls into
     `auto cal = backend->discoveredCalendar(calendarId);` then set the helper-owned
     `name`/`backendId`/`backendType` fields it adds on top.
   - `src/controllers/backenddiscoverycoordinator.cpp:103`,
     `src/controllers/collectioncontroller.cpp:1087` — `discoveredCalendarType(id)` →
     `discoveredCalendar(id).calendarType()`.
   - `tests/integration/tst_calendarcrud.cpp:200/836` → `.calendarType()`;
     `:511` → `.color`.
   Run PlanStan `ctest` against this branch (`PLANSTAN_LIBKALBURATOR_SOURCE_DIR` override);
   relink any `EXCLUDE_FROM_ALL` fixtures first if the SyncBackend vtable shape changed
   (FINDINGS "Plan 8 step 1" runbook — the `discoveredColor`/etc. vtable slots are
   removed). Expect failed-set = the known Not-Run GUI binaries only.
2. **Delete the `[[deprecated]]` forwarders** (T4.1) once PlanStan is migrated + builds
   green against the branch — or, if coordinating across machines, leave them annotated and
   fold the deletion into Plan 11 (document the choice in the Outcome). WildPalms: no change
   (0 consumers; gate satisfied by construction — re-state the per-symbol verification).
3. Cut the tag; update `STATUS.md` (Plan 9 row → DONE, baseline delta, locked decisions for
   the `discoveredWritable`-stays-neutral and doc-location deviations) and the `AUDIT.md`
   B5 + discovery-MODERATE closing annotations **in the same commit**. Merge `--no-ff`,
   push.

## Risks

- **SyncBackend vtable shape change** (T4 removes 3 virtuals, adds 1) — recompiles every
  backend + SEGFAULTs PlanStan `EXCLUDE_FROM_ALL` fixtures until relinked (FINDINGS Plan 8
  step 1 runbook). Mitigated: T3 adds the new virtual before T4 removes the old ones; the
  PlanStan gate (T7) explicitly relinks first.
- **DecSync internal type logic** — `calendarTypeFor` must be a *pure extract* of the
  existing `discoveredCalendarType` body; T3's protective slot + the existing
  `tst_decsyncbackend` type-violation cases pin it.
- **Deprecated-forwarder window** — the campaign blesses `[[deprecated]]` migration
  scaffolding (AUDIT G8); the risk is forgetting to delete it. T7.2 / Plan 11 owns the
  deletion; the forwarder carries the deprecation message naming `discoveredCalendar()`.
- **CMake move ordering** (T1/T2) — delete the `src/backend` include line only after the
  header list points at `src/sync`; a stale include path masks a missed include rewrite.
- **`tst_caldav_provider.cpp` include path** (T2) — verify it doesn't hard-code
  `calendar/caldavcapabilitydiscovery.h`.

## Outcome

**T0 — baseline (2026-06-11)** — branch `feature/redress-9-backend-dir-discovery-consolidation`
@ `ca06534` (the plan commit). Build `-j 8` (default profile, Akonadi/Org OFF) clean and
incremental (no recompiles needed — source tree unchanged at v0.72).
`ctest --test-dir build -j 8`: **148/148 passed, 0 failed** (total 117.7 s; long pole is
`tst_remotecalendarbackend` at 117.7 s). `compile_commands.json` present + valid (474
entries, symlink → `build/`). **Baseline gate = 148.**

**T1 — Stream A (2026-06-11)** — `git mv src/backend/changedetection.h src/sync/`; ns
`Kalburator::Backend`→`Kalburator::Sync`, guard + doc comment updated (dropped the
never-shipped `AbstractSyncBackend` reference). 6 includers rewired (5 `../backend/`→
`../sync/`; `syncengine.cpp` unqualified unchanged); all `Backend::ChangeDetection`→
`Sync::ChangeDetection` (base specifiers, the 4 engine `dynamic_cast` sites, comments).
CMake: `changedetection.h` moved into the sync header group next to `syncbackendbase.h`;
the empty `KALBURATOR_BACKEND_HEADERS` set + its target reference + the
`$<BUILD_INTERFACE:.../src/backend>` include dir all removed; `src/backend/` dir gone.
Grep-clean (zero `Backend::`/`../backend/` residue). Build clean, **ctest 148/148**.

**T2 — Stream B (2026-06-11)** — `git mv caldavcapabilitydiscovery.{h,cpp}`
`calendar/`→`sync/` (confirmed pure-transport: only Qt + its own header; no calendar
deps). Only `multiprotocoldavprovider.cpp:4` needed an edit (`"../calendar/…"`→same-dir);
`caldavprovider.cpp:6`, the `.cpp`'s own include, and `tst_caldav_provider.cpp:19` are
unqualified and resolve via the `sync/` include path post-move. CMake: both entries moved
from the calendar block to the CardDAV-transport block next to
`carddavcapabilitydiscovery.{h,cpp}`. Zero `calendar/caldavcapabilitydiscovery` residue.
Build clean, **ctest 148/148**. (Residual `sync/→calendar/` *concrete-backend* include —
the deferred B4-corrected MAJOR — logged to FINDINGS in T6.)

**T3 — Stream C seam (2026-06-11)** — added the `DiscoveredCalendar discoveredCalendar()`
aggregate accessor without yet retiring any getter (tree stays green). `DiscoveredCalendar`
gained a faithful `calendarType()` convenience (`#include "calendartype.h"`); `SyncBackend`
got the `virtual discoveredCalendar()` default (DTO with `calendarId` + neutral
`writable=discoveredWritable()`, other fields at defaults that match the old base getter
defaults: invalid color, Hybrid, empty name/url). Overrides reading internal state in **RCB**
(self-contained `m_calendars`/`CalendarFacts` read — independent of the per-field getters so
T4 can delete them without recursion), **DecSync** (color/name/type via its existing
methods — T4 rewires to helpers), **Akonadi** (`m_collections`; gated off — compiles only in
the Akonadi lane, to be verified at close-out). Protective slots pinning the new seam:
`tst_remotecalendarbackend_writepaths::discoveredCalendar_aggregates_url_type_support` +
`tst_decsyncbackend::testDiscoveredCalendarAggregatesType`. **Falsifiability shown:** stubbing
RCB's override to `return {}` reddened the slot (`dc.calendarId` empty), reverted. Slots land
in existing executables, so the count stays **148/148** (the Gates "+1 executable" estimate
didn't apply — no new binary). Caught one self-inflicted bug en route: the base virtual was
initially omitted (only the include was added), surfaced immediately by the `override`
compile error.

**T4 — Stream C getter retirement (2026-06-11)** — all 6 per-field getters are now
`[[deprecated]]` forwarders into `discoveredCalendar()`; **none deleted** (plan corrected
mid-task — PlanStan consumes the RCB-local trio too, so the forwarder window covers all 6;
deletion → T7.2/Plan 11). The 3 calendar-base virtuals (`discoveredColor`/`CalendarType`/
`DisplayName`) became non-virtual base forwarders (polymorphism now flows through the single
overridable `discoveredCalendar()`); their overrides in RCB/DecSync/Akonadi deleted. The 3
RCB-local (`discoveredUrl`/`SupportsEvents`/`SupportsTodos`) stay on RCB as forwarders. DecSync
extracted private `calendarTypeFor`/`displayNameFor` (4 internal self-calls + the DTO builder
repointed — no recursion); Akonadi's `calendarColor` rewired off the deleted `discoveredColor`.
All in-lib callers migrated to `discoveredCalendar()` (tests via scripted rewrite) so the lib
build is **deprecation-warning-clean** (only the pre-existing `recurrenceCapabilities` warning
remains). `discoveredWritable` untouched (neutral; engine write-gate). **Default lane
148/148; Akonadi-lane library compiles clean** (`build-akonadi`, `HAVE_AKONADI=ON` — verifies
the removed/added vtable slots + the T1/T2 moves under Akonadi). Akonadi-lane *tests* deferred
to the D1 periodic manual lane at close-out.

**T5 — Stream C sync-internal discovery tidy (2026-06-11)** — investigation first: the
providers' `m_calendarUrls`/`m_addressbookUrls` are **lifecycle-necessary persistent stores**,
not live duplicates — the `m_discovery` object is transient (`deleteLater()`'d right after
connect, `caldavprovider.cpp:137-139`), so the provider copies the href map out before the
discovery dies. And `PerCalendarCapabilities` carries no href (discovery splits href into
`m_calendarUrls` and caps into `perCalendarCapabilities`, both keyed by calendarId — they're
complementary, not duplicate). So the audit's "URL maps duplicated across discovery/provider/
backend" is a **cross-layer lifecycle handoff** (transient discovery → persistent provider →
backend), and Plan 7 already consolidated the backend-internal half; the "one owner per URL map
within `sync/`" gate already holds. The clean in-scope item — the **`discoveredCapabilities()`
bulk-getter narrowing** — is done: new `perCalendarCapabilities()` accessor on
`CalDavCapabilityDiscovery`; `CalDavProvider` + `MultiProtocolDavProvider` rewired off the
whole-struct fetch (both only ever read `.perCalendarCapabilities`). The full
"fold href into `PerCalendarCapabilities`" consolidation crosses into `typesupport/` + its JSON
codec — out of T5's sync-internal scope; FINDINGS note (T6) for a future pass. ctest 148/148.

**T6 — Stream D docs + FINDINGS (2026-06-11)** — layer-role header comments added:
`sync/syncbackendbase.h` (`sync/` = neutral backend contracts `SyncBackendBase` +
`ChangeDetection`, domains implement downward, `sync/` names no concrete backend),
`storage/baselinestore.h` (`storage/`/`Kalburator::Storage` = SQLite-persistent engine
stores; "Store" persists), `universal/universalstorageplugin.h` (`universal/`/`Kalburator::Sinks`
= concrete sink backends; the dir↔ns mismatch flagged for Plan 10). FINDINGS gained a
"From Plan 9" block (inv 9): the deferred `sync/→calendar/` concrete-backend include
(B4-corrected MAJOR — own concern), the `universal/`↔`Sinks` rename (Plan 10, 11 WildPalms
sites), the href-into-`PerCalendarCapabilities` consolidation (Plan 11, crosses into
typesupport), and the 6 `[[deprecated]]` forwarders' deletion (T7.2/Plan 11). ctest 148/148.

_(T7 filled in as it lands — the PlanStan gate result, the deprecated-forwarder disposition,
and the AUDIT B5 + discovery-MODERATE closing annotations.)_
