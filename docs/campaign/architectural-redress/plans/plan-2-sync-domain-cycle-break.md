# Plan 2 — Break the sync ↔ calendar/contacts cycle

**Audit refs:** B4 (CRITICAL)
**Depends on:** —
**Branch:** `feature/redress-2-cycle-break`
**State:** Task-level detail.

## Goal

Make `sync/` depend only on `sync/`-owned interfaces and the neutral `synctypes.h`; make
domain dirs (`calendar/`, `contacts/`) consume `sync/`-owned interfaces without `sync/`
ever consuming a domain header (INVARIANTS §1).

## Problem (from AUDIT B4)

Two-way includes today:

- `sync/akonadiprovider.cpp` — `#include "../calendar/akonadibackend.h"` (provider
  instantiates concrete domain backend directly).
- `sync/akonadibackendcontribution.cpp` — `#include "../contacts/akonadicontactsbackend.h"`.
- `sync/caldavprovider.cpp` — `#include "../calendar/remotecalendarbackend.h"`.
- `sync/multiprotocoldavprovider.cpp` — includes both calendar and contacts headers.
- `calendar/akonadibackend.h` — `#include "syncoperation.h"` (from sync/), closing
  the cycle.
- `calendar/syncbackend.h` defines a calendar-typed base class that `sync/` consumes —
  inverted ownership; `sync/syncbackendbase.h` already exists as the neutral base.

The result: any change in calendar/akonadibackend.h's surface forces a sync/ recompile,
and any change to syncoperation.h forces a domain recompile. The cycle also blocks Plan
3 (types/ purification), because domain-typed interfaces have nowhere to land while
types/ owns them and calendar/ depends on sync/.

## Approach

Three movements:

1. **Move `SyncBackend` base into `sync/`.** Calendar-typed specializations inherit it
   from there.
2. **Introduce `IBackendProvider` in `sync/`.** Concrete provider classes implement it
   in *the domain dir*, not in sync/. Sync registers providers; it does not know
   concrete backend types.
3. **Reverse the include direction.** Every `sync/*.cpp` that today `#include`s a
   `../calendar/` or `../contacts/` header is rewritten to construct via the provider
   interface.

After this plan, `grep -rn '#include "../calendar' src/sync/` and `grep -rn '#include
"../contacts' src/sync/` both return empty.

## Tasks

### Task 1 — Move `SyncBackend` from `calendar/` to `sync/`

1. Compare `calendar/syncbackend.h` (319 LOC) and `sync/syncbackendbase.h`. Identify the
   delta: methods only on the calendar-typed variant (the KCalendarCore-aware overloads)
   vs the neutral base.
2. The calendar-typed methods (taking `KCalendarCore::*`) **stay in calendar/** as a
   subclass: `calendar/calendarsyncbackend.h` inheriting `sync::SyncBackend`. Calendar
   backends derive from `calendar::CalendarSyncBackend`.
3. The neutral surface (operation-based, `pushItems(id, items, TranscodingPlan)`,
   capability queries) moves to `sync/syncbackend.h`, replacing or absorbing the
   existing `syncbackendbase.h`. Pick one name and delete the other; pick `SyncBackend`
   for the neutral base since the rename of any consumer is a single sed-pass.
4. Update all calendar backends (`akonadibackend.h`, `localbackend.h`,
   `remotecalendarbackend.h`, `decsyncbackend.h`, `orgbackend.h`, `mockbackend.h`) to
   inherit `calendar::CalendarSyncBackend` instead of the old `SyncBackend`.
5. Update contacts backends (`akonadicontactsbackend.h`, `remotecontactsbackend.h`)
   analogously — if they currently inherit `SyncBackend` from calendar/, move them
   under a `contacts::ContactsSyncBackend` subclass. **If contacts backends already
   inherit a neutral base, skip this and note in commit message.**
6. Build, run all tests.

### Task 2 — Introduce `IBackendProvider` interface in `sync/`

1. Create `sync/ibackendprovider.h`:
   ```cpp
   namespace Kalburator::Sync {
   class IBackendProvider {
   public:
       virtual ~IBackendProvider() = default;
       virtual QString backendType() const = 0;
       virtual QStringList supportedDomains() const = 0; // "calendar", "contacts", ...
       virtual std::unique_ptr<SyncBackend> create(const BackendConfiguration &config,
                                                   ISyncHost *host) = 0;
   };
   } // namespace
   ```
2. The interface does **not** depend on calendar/, contacts/, or any domain dir.
   Confirm with `clang-tidy --header-filter=ibackendprovider` or by grep.

### Task 3 — Implement provider classes in their domain dirs

For each existing concrete provider:

1. `sync/akonadiprovider.cpp` (calendar) → split: the *interface* stays in sync/ as a
   thin registrar; the *implementation* moves to
   `calendar/akonadicalendarprovider.{h,cpp}` and implements `IBackendProvider`. It
   `#include`s `calendar/akonadibackend.h` (this is fine; it's inside calendar/).
2. `sync/akonadibackendcontribution.cpp` (contacts) → equivalent move to
   `contacts/akonadicontactsprovider.{h,cpp}`.
3. `sync/caldavprovider.cpp` → `calendar/caldavprovider.{h,cpp}`.
4. `sync/multiprotocoldavprovider.cpp` → if it serves both calendar and contacts, it
   splits into `calendar/caldavprovider.cpp` and `contacts/carddavprovider.cpp`; the
   "multi-protocol" abstraction was a workaround for the cycle and disappears.
5. Each provider's `IBackendProvider::create()` is the only place that constructs the
   concrete backend type.

### Task 4 — Register providers via the existing plugin contribution mechanism

The plugin system (`src/plugin/`) already aggregates `BackendContribution` from each
domain plugin. Extend that aggregation to expose `IBackendProvider` instances:

1. Add `virtual QList<std::shared_ptr<IBackendProvider>> providers() const { return {}; }`
   to the base plugin class.
2. Each domain plugin (`calendarplugin.cpp`, `contactsplugin.cpp`) returns its providers.
3. The composition root (host application) calls `plugin->providers()` and registers
   each into the `BackendRegistry` provider map. The `BackendRegistry` gains:
   ```cpp
   void registerProvider(std::shared_ptr<IBackendProvider> provider);
   std::unique_ptr<SyncBackend> createBackend(const QString &backendType, ...);
   ```
4. Every `sync/` call site that today constructs a concrete backend now calls
   `registry->createBackend(type, config, host)` instead.

### Task 5 — Delete the sync/ → domain includes

1. Remove every `#include "../calendar/*"` and `#include "../contacts/*"` from `sync/`.
2. Build. Failures are the call sites that still construct concrete domain types
   directly; rewrite each to go through `BackendRegistry::createBackend()`.
3. Confirm:
   ```
   grep -rn '#include "../calendar' src/sync/
   grep -rn '#include "../contacts' src/sync/
   ```
   Both must return empty.

### Task 6 — Update `calendar/akonadibackend.h` to not include sync/syncoperation.h

The remaining inverted include is calendar/ pulling sync/. Resolve by moving the parts
of `syncoperation.h` that calendar/ depends on into a neutral location:

1. Inspect what calendar/ actually needs from `syncoperation.h`. Likely candidates:
   `FetchOperation`, `PushOperation`, `DeleteOperation` type names.
2. If these are abstract operation classes whose interfaces are domain-neutral, they
   are in the right layer (sync/) and calendar/ correctly depends on them — this is
   *not* a cycle, it's correct one-way dependency (calendar/ depends on sync/, sync/
   does not depend on calendar/ post-Task 5). Confirm no remaining sync/ → calendar/
   includes; then this dependency direction is acceptable and Task 6 closes.
3. If `syncoperation.h` includes any calendar-typed members, those members move to
   `calendar/calendarsyncoperation.h` (a subclass).

### Task 7 — Re-run tests and close

1. Full ctest.
2. PlanStan ctest (reachable headers).
3. WildPalms manual smoke (calendar + contacts sync round-trip if locally runnable).
4. Update FINDINGS: cross out B4-derived entries with the closing commit hash.
5. Open Plan 3.

## Files affected

- `src/sync/syncbackend.h` — **new** (or rename of `syncbackendbase.h`), neutral base.
- `src/sync/ibackendprovider.h` — **new**.
- `src/sync/backendregistry.{h,cpp}` — gains provider registration + createBackend().
- `src/calendar/calendarsyncbackend.h` — **new**, calendar-typed subclass.
- `src/calendar/akonadicalendarprovider.{h,cpp}` — **new**, moved from sync/.
- `src/calendar/caldavprovider.{h,cpp}` — **new**, moved from sync/.
- `src/contacts/contactssyncbackend.h` — **new** if needed.
- `src/contacts/akonadicontactsprovider.{h,cpp}` — **new**, moved from sync/.
- `src/contacts/carddavprovider.{h,cpp}` — **new**, moved from sync/.
- `src/sync/akonadiprovider.cpp` — **deleted**.
- `src/sync/akonadibackendcontribution.cpp` — **deleted**.
- `src/sync/caldavprovider.cpp` — **deleted**.
- `src/sync/multiprotocoldavprovider.{h,cpp}` — **deleted** (abstraction redundant
  post-split).
- `src/calendar/syncbackend.h` — **deleted** (moved to sync/).
- `src/plugin/plugin.h` — gains `providers()` virtual.
- All domain plugin `.cpp` files — register providers.

## Acceptance criteria

- `grep -rn '#include "../calendar' src/sync/` returns empty.
- `grep -rn '#include "../contacts' src/sync/` returns empty.
- Only `sync/`-owned headers appear in `sync/*.cpp`'s domain-facing includes.
- All tests pass; PlanStan ctest baseline holds.
- The `multiprotocoldav*` files are deleted, not just emptied.

## Risks

- **Provider registration order.** If composition root order changes when callers
  migrate, ensure the registration is deterministic. Document in the host integration
  point.
- **WildPalms account form widget** (per memory: `config-widget no bridge`) consumes the
  provider system. Confirm the widget→provider bridge survives the split; this plan does
  not fix the bridge but must not regress it.
- **Multi-protocol provider was a real abstraction, not a workaround.** Verify by
  reading `sync/multiprotocoldavprovider.cpp` in Task 3 step 4 before deleting; if it
  has shared discovery logic both calendar and contacts depend on, extract that logic
  to a neutral location rather than duplicate.

## Estimated effort

4–6 sessions. Task 1 (SyncBackend move) is mechanical but touches many backends; Task 4
(plugin extension) is the conceptually richest piece.
