# Phase L — Akonadi as first-class provider

**Status:** landed 2026-05-16  
**Tag:** `v0.41-phase-l-akonadi-provider` (pending user `git tag` authorization)  
**Plan:** `~/dev/refactor-engine-merger/2026-05-15-phase-l-akonadi-plan.md`

## What landed

- `AkonadiProvider` (implements `IProvider`) — discovers calendar and addressbook
  collections via `Akonadi::CollectionFetchJob`; `createBackend()` dispatches by
  MIME type (calendar → `AkonadiBackend`, contacts → `AkonadiContactsBackend`).
- `AkonadiContactsBackend` — new; mirrors `AkonadiBackend` for `KContacts::Addressee`,
  uses `KContacts::VCardConverter` v4_0, monitors on `KContacts::Addressee::mimeType()`.
- `AkonadiBackendContribution` + `AkonadiProviderPlugin` — registered in
  `stock_plugins.cpp` under `#ifdef HAVE_AKONADI`; WildPalms CMake changed from
  forced-OFF to `option()` so callers can enable it.
- `AkonadiConfigWidget` — minimal `QWidget` for provider display-name editing,
  returned by `AkonadiProvider::createConfigWidget()`.
- `AccountController::setProviderEnabled(id, bool)` — flips `BackendConfiguration::enabled`
  and fans out to all `SyncMapping` entries referencing that provider (by
  `sourceBackend/targetBackend` prefix match). Load-time re-apply on profile load.
- `AccountController::setMappingEnabled(id, bool)` — flips one mapping's `"enabled"`
  field in the profile JSON array directly.
- `AccountsPage` per-provider `QCheckBox` wired to `setProviderEnabled`.
- `MappingRowDialog` `enabled` checkbox was pre-existing; JSON round-trip verified.
- Pre-existing `emit` → `Q_EMIT` + `connect()` → `QObject::connect()` cleanup in
  `accountcontroller.cpp`, `accountspage.cpp`, `palmruntime.cpp`.

## What deferred

- PlanStan account-add UI (still raw config). See `04w-deferred-work.md`.
- Automatic Akonadi failover / hotswap — rejected as over-abstraction.
- Advanced Akonadi collection filtering (hidden/shared resources).

## Build notes

- Default build: `KALBURATOR_HAVE_AKONADI=OFF` — Phase L code excluded, all existing
  tests unaffected.
- Akonadi build: `cmake -DKALBURATOR_HAVE_AKONADI=ON -DCMAKE_MODULE_PATH=/usr/share/ECM/modules`
  (ECM path required on Arch/Manjaro; `KPim6AkonadiConfig.cmake` calls `include(ECMMarkAsTest)`).
- clangd: `.clangd` `CompilationDatabase` must point at `build-akonadi/` for
  Akonadi-gated code to be visible. The repo `.clangd` was updated accordingly.
- Live Akonadi tests guarded by `KALBURATOR_AKONADI_LIVE_TEST=1` env var (skip in CI).
